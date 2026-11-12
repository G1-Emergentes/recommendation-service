#include "recommendation/application/RecommendationService.h"
#include <iostream>
#include <stdexcept>

using namespace mirage::recommendation;

void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

class Wardrobe final : public WardrobeClient
{
  public:
    bool success = true;
    std::vector<Garment> garments;
    mutable std::string requestedUser;
    mutable std::string requestedToken;
    mutable unsigned resolves=0;
    unsigned changeAt=0;
    WardrobeStatus resolveStatus=WardrobeStatus::Found;
    void listForUser(const UserAccess &access, WardrobeCompletion done) const override
    {
        requestedUser = access.userId;
        requestedToken = access.accessToken;
        done({success?WardrobeStatus::Found:WardrobeStatus::Unavailable, garments});
    }
    void resolve(const UserAccess &access,const std::vector<std::string> &ids,WardrobeCompletion done) const override
    {
        require(access.accessToken=="access-a", "Forward authorized access to resolve");
        ++resolves;
        if (resolveStatus!=WardrobeStatus::Found) {done({resolveStatus,{}});return;}
        std::vector<Garment> result;
        for(const auto &id:ids) for(const auto &g:garments) if(g.id==id) result.push_back(g);
        if(changeAt==resolves && !result.empty()) ++result.front().revision;
        done({WardrobeStatus::Found,result});
    }

};

class Profile final : public ProfileClient
{
  public:
    ProfileResult result{ProfileStatus::Found, {}};
    mutable UserAccess requestedAccess;
    void getPreferences(const UserAccess &access,
                        std::function<void(ProfileResult)> done) const override
    {
        requestedAccess = access;
        done(result);
    }
};

class ReadingAgent final : public RecommenderAgent
{
 public:
    mutable unsigned calls=0;
    bool expectExamples=false;
    std::string model() const override{return "test-model";}
    bool configured() const override{return true;}
    void generate(const std::vector<Garment> &g,const RecommendationParameters &p,const RecommendationPreferences &prefs,std::function<void(AgentResult)> done) const override
    {
        ++calls;require(p.outfitExamples.size()==(expectExamples?1u:0u),"Prepared examples reach generation, separate from wardrobe");
        require(p.styleKnowledge.size()==1 && p.styleKnowledge[0].id=="reference","Retrieved references reach the model");
        auto outfits=suggestOutfits(g,p,prefs);for(auto &o:outfits)o.explanation="Verified combination.";
        done({AgentStatus::Generated,std::move(outfits)});
    }
};

int main()
{
    try
    {
        Wardrobe wardrobe;
        wardrobe.garments = {
            {"top", Category::Top, true, {"CASUAL"}},
            {"bottom", Category::Bottom, true, {}},
            {"shoe", Category::Footwear, true, {}},
            {"dress", Category::OnePiece, true, {"CASUAL"}},
            {"unavailable", Category::Top, false, {}},
            {"formal", Category::Top, true, {"FORMAL"}}};
        RecommendationParameters request{"university", "CASUAL", {}, 3};
        auto result = suggestOutfits(wardrobe.garments, request);
        require(result.size() == 2, "Expected two casual outfit templates");
        require(result[0].garmentIds == std::vector<std::string>{"top", "bottom", "shoe"},
                "Expected a complete separates outfit");
        require(result[1].garmentIds == std::vector<std::string>{"dress", "shoe"},
                "Expected a one-piece outfit without top/bottom");

        request.excludedGarments = {"top"};
        result = suggestOutfits(wardrobe.garments, request);
        require(result.size() == 1 && result[0].garmentIds[0] == "dress",
                "Exclusions must remove the top");
        request.excludedGarments = {"shoe"};
        require(suggestOutfits(wardrobe.garments, request).empty(),
                "Must not return incomplete outfits without footwear");
        require(suggestOutfits({}, request).empty(), "Empty closet must remain empty");
        request.excludedGarments.clear();
        request.maxResults = 1;
        require(suggestOutfits(wardrobe.garments, request).size() == 1,
                "Respect the requested result limit");

        Profile profile;
        RecommendationService service(wardrobe, profile);
        bool called = false;
        service.generate({"user-a", "access-a"}, request, [&](RecommendationResult response) {
            called = true;
            require(response.status == RecommendationStatus::Generated, "Expected generated");
        });
        require(called && wardrobe.requestedUser == "user-a" && wardrobe.requestedToken=="access-a", "Forward authorized context to wardrobe");
        require(wardrobe.resolves==2,"Resolve before and after generation");
        wardrobe.changeAt=wardrobe.resolves+2;
        service.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::WardrobeChanged && r.outfits.empty(),"Reject final revision change");
        });
        wardrobe.changeAt=0;
        wardrobe.resolveStatus=WardrobeStatus::Unauthorized;
        service.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::Unauthorized,"Propagate Closet session rejection");
        });
        wardrobe.resolveStatus=WardrobeStatus::Found;
        require(profile.requestedAccess.userId == "user-a" &&
                profile.requestedAccess.accessToken == "access-a", "Forward authorized access to Profile");
        wardrobe.success = false;
        called = false;
        service.generate({"user-a", "access-a"}, request, [&](RecommendationResult response) {
            called = true;
            require(response.status == RecommendationStatus::WardrobeUnavailable,
                    "Dependency failure must not become an empty wardrobe");
        });
        require(called, "Complete failures");
        wardrobe.requestedUser.clear();
        for (const auto status : {ProfileStatus::Missing, ProfileStatus::Unauthorized, ProfileStatus::Unavailable})
        {
            profile.result.status = status;
            called = false;
            service.generate({"user-a", "access-a"}, request, [&](RecommendationResult response) {
                called = true;
                const auto expected = status == ProfileStatus::Missing ? RecommendationStatus::ProfileRequired
                    : status == ProfileStatus::Unauthorized ? RecommendationStatus::Unauthorized
                    : RecommendationStatus::ProfileUnavailable;
                require(response.status == expected && response.outfits.empty(), "Propagate Profile failures");
            });
            require(called && wardrobe.requestedUser.empty(), "Stop before reading wardrobe on Profile failure");
        }
        profile.result.status = ProfileStatus::Found;
        wardrobe.success = true;
        wardrobe.garments.push_back(wardrobe.garments.front());
        called = false;
        service.generate({"user-a", "access-a"}, request, [&](RecommendationResult response) {
            called = true;
            require(response.status == RecommendationStatus::WardrobeUnavailable,
                    "Reject duplicate IDs from wardrobe");
        });
        require(called, "Complete invalid dependency responses");
        std::vector<Garment> colored{
            {"white-top", Category::Top, true, {"CASUAL"}, {"WHITE"}},
            {"green-top", Category::Top, true, {"CASUAL"}, {"GREEN"}},
            {"formal-top", Category::Top, true, {"FORMAL"}, {"BLACK"}},
            {"bottom", Category::Bottom, true, {}, {"BLUE"}},
            {"shoe", Category::Footwear, true, {}, {"WHITE"}}};
        RecommendationPreferences preferences{{}, {"GREEN"}, {}, {"TOP"}};
        request = {"university", "", {}, 1};
        result = suggestOutfits(colored, request, preferences);
        require(result.size() == 1 && result[0].garmentIds[0] == "green-top", "Prioritize preferred colors");
        require(result[0].garmentIds.size() == 3, "Category preferences must not remove required categories");
        preferences = {{"FORMAL"}, {}, {}, {}};
        result = suggestOutfits(colored, request, preferences);
        require(result[0].garmentIds[0] == "formal-top", "Prioritize saved styles when no explicit style");
        request.style = "CASUAL";
        result = suggestOutfits(colored, request, preferences);
        require(result[0].garmentIds[0] == "white-top", "Explicit style takes precedence over saved styles");
        preferences.avoidedColors = {"WHITE"};
        require(suggestOutfits(colored, request, preferences).empty(), "Avoided colors must remove footwear too");
        // The retrieval boundary replaces listForUser; it never augments/falls back to the closet.
        wardrobe.garments = colored;
        request = {"university", "", {}, 1};
        unsigned retrievals=0;
        RecommendationService retrieved(wardrobe,profile,nullptr,{},
            [&](UserAccess access,RecommendationParameters params,RecommendationPreferences, WardrobeCompletion done){
                require(access.accessToken=="access-a" && params.occasion=="university","Authorized retrieval context");
                ++retrievals;
                done({WardrobeStatus::Found,{colored[1],colored[3],colored[4]}});
            });
        wardrobe.requestedUser.clear();
        retrieved.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::Generated && r.outfits[0].garmentIds[0]=="green-top","Generate only from retrieved candidates");
        });
        require(retrievals==1 && wardrobe.requestedUser.empty(),"No complete inventory load after retrieval");
        RecommendationService failedRetrieval(wardrobe,profile,nullptr,{},
            [](UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion done){done({WardrobeStatus::Unavailable,{}});});
        failedRetrieval.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::WardrobeUnavailable && r.outfits.empty(),"No inventory fallback after vector failure");
        });
        RecommendationService incompleteRetrieval(wardrobe,profile,nullptr,{},
            [](UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion done){done({WardrobeStatus::Found,{}});});
        incompleteRetrieval.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::InsufficientGarments && r.outfits.empty(),"No invented outfit without retrieved candidates");
        });
        ReadingAgent readingAgent;unsigned phase=0;
        RecommendationService knowledgeService(wardrobe,profile,&readingAgent,{},
            [&](UserAccess,RecommendationParameters p,RecommendationPreferences,WardrobeCompletion complete){
                require(phase==1 && p.styleKnowledge.size()==1,"Knowledge is retrieved before wardrobe candidates");phase=2;
                complete({WardrobeStatus::Found,{colored[1],colored[3],colored[4]}});
            },
            [&](UserAccess access,RecommendationParameters,RecommendationService::KnowledgeCompletion complete){
                require(access.accessToken=="access-a" && phase==0,"Knowledge uses authenticated backend identity");phase=1;
                complete(true,{{"reference","Guidance","Only confirmed colors.","https://en.wikipedia.org/wiki/Monochrome","CC-BY-SA-4.0","Wikipedia contributors; Mirage adaptation."}});
            });
        knowledgeService.generate({"user-a","access-a"},request,[&](RecommendationResult r){
            require(r.status==RecommendationStatus::Generated && r.styleReferences.size()==1 && phase==2,"Keep retrieved source provenance in generation result");
        });
        RecommendationService missingKnowledge(wardrobe,profile,&readingAgent,{},
            [&](UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion){throw std::runtime_error("Must not read wardrobe after knowledge failure");},
            [](UserAccess,RecommendationParameters,RecommendationService::KnowledgeCompletion complete){complete(false,{});});
        missingKnowledge.generate({"user-a","access-a"},request,[&](RecommendationResult r){require(r.status==RecommendationStatus::ToolUnavailable,"Knowledge failure is explicit, not an empty successful retrieval");});
        require(readingAgent.calls==1,"Failed knowledge retrieval never reaches generation");
        readingAgent.expectExamples=true;phase=0;
        OutfitExample example;example.id="polyvore-reference";
        RecommendationService exampleService(wardrobe,profile,&readingAgent,{},
            [&](UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion complete){phase=1;complete({WardrobeStatus::Found,{colored[1],colored[3],colored[4]}});},
            [](UserAccess,RecommendationParameters,RecommendationService::KnowledgeCompletion complete){complete(true,{{"reference","Guide","Content","url","license","author"}});},
            [&](UserAccess access,RecommendationParameters p,std::vector<Garment> garments,RecommendationService::ExampleCompletion complete){
                require(access.accessToken=="access-a" && phase==1 && p.styleKnowledge.size()==1 && garments.size()==3,"Examples follow private candidate retrieval with authorized context");phase=2;complete(true,{example});
            });
        exampleService.generate({"user-a","access-a"},request,[&](RecommendationResult r){require(phase==2 && r.outfitExamples.size()==1 && r.status==RecommendationStatus::Generated,"Keep external examples in result provenance");});
        const auto modelCalls=readingAgent.calls;
        RecommendationService failedExamples(wardrobe,profile,&readingAgent,{},
            [&](UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion complete){complete({WardrobeStatus::Found,{colored[1],colored[3],colored[4]}});},{},
            [](UserAccess,RecommendationParameters,std::vector<Garment>,RecommendationService::ExampleCompletion complete){complete(false,{});});
        failedExamples.generate({"user-a","access-a"},request,[&](RecommendationResult r){require(r.status==RecommendationStatus::ToolUnavailable && r.outfits.empty(),"Fail closed if example retrieval fails");});
        require(readingAgent.calls==modelCalls,"No model call after example failure");
        bool rejected = false;
        request.maxResults = 0;
        try { suggestOutfits({}, request); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "Reject invalid limits");
        std::cout << "Recommendation rules and application checks passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
