#include "RecommendationService.h"
#include "ConversationPolicy.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace mirage::recommendation
{
namespace
{
RecommendationStatus wardrobeFailure(WardrobeStatus status)
{
    if(status==WardrobeStatus::Unauthorized) return RecommendationStatus::Unauthorized;
    if(status==WardrobeStatus::Changed) return RecommendationStatus::WardrobeChanged;
    if(status==WardrobeStatus::LimitExceeded) return RecommendationStatus::WardrobeLimitExceeded;
    return RecommendationStatus::WardrobeUnavailable;
}
bool sameGarment(const Garment &a,const Garment &b)
{
    return a.id==b.id && a.revision==b.revision && a.updatedAt==b.updatedAt &&
        a.name==b.name && a.description==b.description && a.subcategory==b.subcategory &&
        a.category==b.category && a.available==b.available && a.colors==b.colors && a.styles==b.styles &&
        a.image.has_value()==b.image.has_value() &&
        (!a.image || (a.image->id==b.image->id && a.image->version==b.image->version));
}
bool sameSnapshot(const std::vector<Garment> &before,const std::vector<Garment> &after)
{
    if(before.size()!=after.size()) return false;
    for(size_t i=0;i<before.size();++i) if(!sameGarment(before[i],after[i])) return false;
    return true;
}
std::vector<std::string> garmentIds(const std::vector<Garment> &garments)
{
    std::vector<std::string> result;
    for(const auto &g:garments) result.push_back(g.id);
    return result;
}
}

void RecommendationService::generate(UserAccess access, RecommendationParameters parameters,
                                     std::function<void(RecommendationResult)> done) const
{
    if (access.userId.empty() || access.accessToken.empty() || parameters.maxResults < 1 ||
        parameters.maxResults > 3)
        throw std::invalid_argument("Invalid recommendation request");
    if(requestsNewOutfit(parameters.message))
        for(const auto &color:explicitExcludedColors(parameters.message))
            if(std::find(parameters.excludedColors.begin(),parameters.excludedColors.end(),color)==parameters.excludedColors.end())parameters.excludedColors.push_back(color);
    if(explicitDarkRequest(parameters.message)) parameters.darkOnly=true;
    if(explicitRelaxDarkRequest(parameters.message)) parameters.darkOnly=false;
    profile_.getPreferences(access, [this, access,
                                     parameters = std::move(parameters),
                                     done = std::move(done)](ProfileResult profile) mutable {
        if (profile.status != ProfileStatus::Found)
        {
            const auto status = profile.status == ProfileStatus::Missing
                                    ? RecommendationStatus::ProfileRequired
                                : profile.status == ProfileStatus::Unauthorized
                                    ? RecommendationStatus::Unauthorized
                                    : RecommendationStatus::ProfileUnavailable;
            done({status, {}, {}});
            return;
        }
        auto prepare=[this,access,parameters,preferences=profile.preferences,done=std::move(done)](bool ok,std::vector<StyleReference> refs) mutable {
            if(!ok){done({RecommendationStatus::ToolUnavailable,{},{}});return;}
            parameters.styleKnowledge=std::move(refs);
            generateFromWardrobe(access,std::move(parameters),std::move(preferences),std::move(done));
        };
        if(knowledge_)knowledge_(access,parameters,std::move(prepare));else prepare(true,{});
    });
}
void RecommendationService::generateFromWardrobe(UserAccess access,RecommendationParameters parameters,
    RecommendationPreferences preferences,std::function<void(RecommendationResult)> done) const
{
        auto receive = [this, access, parameters,
                                       preferences,
                                       done = std::move(done)](WardrobeResult result) {
            if (result.status != WardrobeStatus::Found)
            {
                done({wardrobeFailure(result.status), {}, {}});
                return;
            }
            std::vector<Outfit> outfits;
            std::vector<Garment> eligible;
            try
            {
                eligible = eligibleGarments(result.garments, parameters, preferences);
                if(candidates_ && requestsAlternative(parameters.message) && !mayRelaxConstraints(parameters.message))
                {
                    // Constrain decoding to a genuinely new complete candidate, not a prompt-only promise.
                    const auto alternative=findOutfitCandidate(eligible,parameters);
                    if(alternative)eligible.erase(std::remove_if(eligible.begin(),eligible.end(),[&](const auto &g){
                        return std::find(alternative->garmentIds.begin(),alternative->garmentIds.end(),g.id)==alternative->garmentIds.end();
                    }),eligible.end());
                }
                outfits = suggestOutfits(eligible, parameters, preferences);
            }
            catch (const std::invalid_argument &)
            {
                done({RecommendationStatus::WardrobeUnavailable, {}, {}});
                return;
            }
            if (eligible.size()>100)
            { done({RecommendationStatus::WardrobeLimitExceeded,{},{}}); return; }
            auto prepare=[this,access,parameters,preferences,done,eligible,outfits=std::move(outfits)](bool ok,std::vector<OutfitExample> examples) mutable {
                if(!ok){done({RecommendationStatus::ToolUnavailable,{},{}});return;}
                parameters.outfitExamples=std::move(examples);
            auto generate = [this,access,eligible,outfits=std::move(outfits),parameters,preferences,done](WardrobeResult checked) mutable {
                if (checked.status!=WardrobeStatus::Found)
                { done({wardrobeFailure(checked.status),{},{}}); return; }
                if (!sameSnapshot(eligible,checked.garments))
                { done({RecommendationStatus::WardrobeChanged,{},{}}); return; }
                auto finish = [this,access,eligible,refs=parameters.styleKnowledge,examples=parameters.outfitExamples,done](RecommendationResult result) {
                    result.styleReferences=refs;result.outfitExamples=examples;
                    if (result.outfits.empty()) { done(std::move(result)); return; }
                    std::set<std::string> used;
                    for (const auto &outfit:result.outfits) for (const auto &id:outfit.garmentIds) used.insert(id);
                    std::vector<Garment> selected;
                    for (const auto &g:eligible) if (used.count(g.id)) selected.push_back(g);
                    wardrobe_.resolve(access,garmentIds(selected),[result,selected,done](WardrobeResult current) mutable {
                        if (current.status!=WardrobeStatus::Found)
                        { done({wardrobeFailure(current.status),{},{}}); return; }
                        if (!sameSnapshot(selected,current.garments))
                        { done({RecommendationStatus::WardrobeChanged,{},{}}); return; }
                        result.garments=std::move(current.garments);
                        done(std::move(result));
                    });
                };
            if(candidates_ && requestsNewOutfit(parameters.message) && !mayRelaxConstraints(parameters.message))
            {
                if(!hasOutfitCandidate(eligible,parameters))
                {
                    AgentResult clarification{AgentStatus::Generated,{}};
                    clarification.action="CLARIFY";clarification.darkOnly=parameters.darkOnly;clarification.excludedColors=parameters.excludedColors;
                    clarification.assistantMessage="No encuentro otra combinación completa entre los candidatos disponibles que respete tus restricciones. ¿Prefieres relajar alguna restricción o agregar prendas?";
                    RecommendationResult response{RecommendationStatus::Generated,{},preferences};
                    response.assistantMessage=conversationReply(clarification,parameters,eligible);
                    response.excludedColors=parameters.excludedColors;response.darkOnly=parameters.darkOnly;
                    response.conversationStyle=parameters.conversationStyle;response.conversationOccasion=parameters.conversationOccasion;
                    finish(std::move(response));return;
                }
            }
            const auto status = outfits.empty() ? RecommendationStatus::InsufficientGarments
                                                : RecommendationStatus::Generated;
            if (agent_ == nullptr || (outfits.empty() && parameters.message.empty()))
            {
                finish({status, std::move(outfits), preferences});
                return;
            }
            const auto model = agent_->model();
            agent_->generateWithTools(
                eligible, parameters, preferences,
                !parameters.message.empty() && tools_ ? tools_(access) : ToolExecutor{},
                [this, access, eligible, parameters, preferences, model, finish,
                 done = std::move(done)](AgentResult result) mutable {
                    if (result.status != AgentStatus::Generated)
                    {
                        if(result.status==AgentStatus::ToolLimitExceeded || result.status==AgentStatus::ToolUnavailable || result.status==AgentStatus::Unauthorized)
                        {done({result.status==AgentStatus::Unauthorized?RecommendationStatus::Unauthorized:
                               result.status==AgentStatus::ToolLimitExceeded?RecommendationStatus::ToolLimitExceeded:RecommendationStatus::ToolUnavailable,{},{}});return;}
                        const auto status = result.status == AgentStatus::NotConfigured
                                                ? RecommendationStatus::ModelNotConfigured
                                            : result.status == AgentStatus::InvalidResponse
                                                ? RecommendationStatus::ModelInvalidResponse
                                            : result.status == AgentStatus::RateLimited
                                                ? RecommendationStatus::ModelRateLimited
                                                : RecommendationStatus::ModelUnavailable;
                        done({status, {}, preferences, "nemotron", model, false});
                        return;
                    }
                    bool valid = result.outfits.size() <= parameters.maxResults &&
                        (parameters.message.empty() ? !result.outfits.empty() :
                         !result.assistantMessage.empty() && result.assistantMessage.size() <= 4000 &&
                         result.assistantMessage.find('\0') == std::string::npos &&
                         result.assistantMessage.find_first_not_of(" \t\r\n") != std::string::npos);
                    auto updatedPreferences=preferences;
                    if (!parameters.message.empty())
                        valid = valid && applyConversationMemory(result,parameters) &&
                            applyPreferenceChange(result,parameters.message,updatedPreferences) &&
                            validConversationDecision(result,parameters,eligible);
                    std::set<std::vector<std::string>> seen;
                    for (const auto &outfit : result.outfits)
                    {
                        auto ids = outfit.garmentIds;
                        std::sort(ids.begin(), ids.end());
                        valid =
                            valid && validOutfit(outfit, eligible) && seen.insert(ids).second &&
                            !outfit.explanation.empty() && outfit.explanation.size() <= 1000 &&
                            outfit.explanation.find('\0') == std::string::npos &&
                            outfit.explanation.find_first_not_of(" \t\r\n") != std::string::npos;
                    }
                    if (!valid)
                    {
                        done({RecommendationStatus::ModelInvalidResponse,
                              {},
                              preferences,
                              "nemotron",
                              model,
                              false});
                        return;
                    }
                    if (!parameters.message.empty())
                    {
                        result.assistantMessage = conversationReply(result,parameters,eligible);
                        if (result.assistantMessage.empty() || result.assistantMessage.size()>4000)
                        { done({RecommendationStatus::ModelInvalidResponse,{},preferences,"nemotron",model,false}); return; }
                    }
                    RecommendationResult response{RecommendationStatus::Generated, std::move(result.outfits), preferences,
                          "nemotron", model, true, std::move(result.assistantMessage)};
                    response.excludedColors=parameters.excludedColors;
                    response.darkOnly=parameters.darkOnly;
                    response.conversationStyle=parameters.conversationStyle;
                    response.conversationOccasion=parameters.conversationOccasion;
                    for(const auto &e:result.toolEvidence)if(e.name=="search_fashion_web")
                        response.webReferences.insert(response.webReferences.end(),e.webReferences.begin(),e.webReferences.end());
                    if(response.outfits.empty() && !response.webReferences.empty() && result.action!="SHOW_FASHION_WEB")
                    {
                        // Keep source provenance in durable text turns that have no outfit snapshot.
                        response.assistantMessage+="\nReferencias externas: FashionStylist Authors (2026), CC-BY-NC-4.0 (uso no comercial).";
                        for(const auto &r:response.webReferences)response.assistantMessage+="\n"+r.title+": "+r.sourceUrl;
                    }
                    if(result.action=="PREFERENCES_READ") response.assistantMessage=preferencesReply(preferences);
                    if(result.action=="PREFERENCES_UPDATE")
                    {
                        profile_.updatePreferences(access,updatedPreferences,
                            [response,updatedPreferences,finish,done](ProfileResult saved) mutable {
                                if(saved.status!=ProfileStatus::Found ||
                                   saved.preferences.preferredStyles!=updatedPreferences.preferredStyles ||
                                   saved.preferences.preferredColors!=updatedPreferences.preferredColors ||
                                   saved.preferences.avoidedColors!=updatedPreferences.avoidedColors ||
                                   saved.preferences.preferredCategories!=updatedPreferences.preferredCategories)
                                {done({saved.status==ProfileStatus::Unauthorized?RecommendationStatus::Unauthorized:
                                       RecommendationStatus::ProfileUpdateUncertain,{},{}});return;}
                                response.preferences=saved.preferences;
                                response.preferencesUpdated=true;
                                response.assistantMessage="Actualicé tus gustos para futuras conversaciones. "+preferencesReply(saved.preferences);
                                if(response.assistantMessage.size()>4000)
                                    response.assistantMessage="Actualicé tus gustos en Profile para futuras conversaciones.";
                                finish(std::move(response));
                            });
                        return;
                    }
                    if(response.assistantMessage.size()>4000)
                    {done({RecommendationStatus::ModelInvalidResponse,{},{}});return;}
                    finish(std::move(response));
                });
            };
            if (eligible.empty()) generate({WardrobeStatus::Found,{}});
            else wardrobe_.resolve(access,garmentIds(eligible),std::move(generate));
            };
            if(examples_)examples_(access,parameters,eligible,std::move(prepare));else prepare(true,{});
        };
        if(candidates_) candidates_(access,parameters,preferences,std::move(receive));
        else wardrobe_.listForUser(access,std::move(receive));
}

} // namespace mirage::recommendation
