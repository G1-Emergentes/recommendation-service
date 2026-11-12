#include "ConversationPolicy.h"
#include <algorithm>
#include <map>
#include <set>
#include <regex>
namespace mirage::recommendation
{
namespace
{
const Garment *find(const std::vector<Garment> &garments,const std::string &id)
{
    const auto g=std::find_if(garments.begin(),garments.end(),[&](const auto &g){return g.id==id;});
    return g==garments.end()?nullptr:&*g;
}

bool referenceExists(const AgentResult &decision,const RecommendationParameters &params)
{
    return decision.referenceOutfit>0 && decision.referenceOutfit<=params.previousOutfits.size();
}

std::string names(const Outfit &outfit,const std::vector<Garment> &garments)
{
    std::string result;
    for(const auto &id:outfit.garmentIds)
    {
        const auto g=find(garments,id);
        if(!g) return {};
        if(!result.empty()) result+=" + ";
        result+=g->name;
    }
    return result;
}
const ToolEvidence *evidenceFor(const AgentResult &d,const std::string &name)
{
    for(auto it=d.toolEvidence.rbegin();it!=d.toolEvidence.rend();++it)
        if(it->name==name)return &*it;
    return nullptr;
}
}

std::vector<std::string> explicitExcludedColors(const std::string &message)
{
    static const std::vector<std::pair<std::string,std::string>> vocabulary{
        {"WHITE","blanc[oa]s?"},{"BLACK","negr[oa]s?"},{"GRAY","gris(es)?"},
        {"BLUE","azul(es)?"},{"RED","roj[oa]s?"},{"GREEN","verdes?"},
        {"YELLOW","amarill[oa]s?"},{"ORANGE","naranjas?"},{"PURPLE","morad[oa]s?"},
        {"PINK","rosad[oa]s?"},{"BROWN","marron(es)?|marrón|cafe|café"},
        {"BEIGE","beige"},{"MULTICOLOR","multicolor"}};
    static const std::regex clause([]{
        std::string colors;
        for(const auto &entry:vocabulary){if(!colors.empty())colors+="|";colors+=entry.second;}
        return "\\bsin\\s+((?:"+colors+")(?:\\s*(?:,|ni|y)\\s*(?:"+colors+"))*)\\b";
    }(),std::regex::icase);
    std::set<std::string> found;
    for(std::sregex_iterator it(message.begin(),message.end(),clause),end;it!=end;++it)
        for(const auto &entry:vocabulary)
            if(std::regex_search((*it)[1].str(),std::regex("\\b(?:"+entry.second+")\\b",std::regex::icase)))found.insert(entry.first);
    return {found.begin(),found.end()};
}

bool requestsAlternative(const std::string &message)
{
    static const std::regex pattern(R"(otra (opcion|opción|combinacion|combinación|alternativa)|otro (outfit|conjunto)|algo (distinto|diferente)|^otra[.! ]*$)",std::regex::icase);
    return std::regex_search(message,pattern);
}

bool requestsNewOutfit(const std::string &message)
{
    static const std::regex pattern(R"((quiero|necesito|dame|genera|arma|recomienda).{0,35}(outfit|conjunto|combinacion|combinación)|quiero algo|^oscuro pero llamativo|acepto usar|puedes relajar)",std::regex::icase);
    return requestsAlternative(message) || std::regex_search(message,pattern);
}

bool mayRelaxConstraints(const std::string &message)
{
    static const std::regex pattern(R"(acepto|permito|permite|puedes|relaja|quita|elimina|olvida|sin restricciones|cualquier color)",std::regex::icase);
    return std::regex_search(message,pattern);
}

bool explicitRelaxDarkRequest(const std::string &message)
{
    static const std::regex permission(R"(puedes relajar.{0,30}oscuro|acepto (una aproximacion|una aproximación|usar (el )?jean azul))",std::regex::icase);
    static const std::regex denied(R"(no (puedes|quiero|acepto|permito).{0,40}(relaj|aproxim|azul))",std::regex::icase);
    return std::regex_search(message,permission) && !std::regex_search(message,denied);
}

bool explicitDarkRequest(const std::string &message)
{
    static const std::regex dark(R"(oscuro|todo negro)",std::regex::icase);
    static const std::regex negative(R"(no (quiero |lo quiero |sea |todo |tan |algo ){0,3}oscuro|sin (tonos |colores )?oscuros?)",std::regex::icase);
    return requestsNewOutfit(message) && !mayRelaxConstraints(message) &&
        std::regex_search(message,dark) && !std::regex_search(message,negative);
}

bool confirmedDark(const Garment &g)
{
    // The current contract has color families, not reliable shade metadata.
    // Until shade is explicit, only BLACK is a confirmed dark color.
    return !g.colors.empty() && std::all_of(g.colors.begin(),g.colors.end(),[](const auto &c){return c=="BLACK";});
}

bool applyConversationMemory(AgentResult &d,RecommendationParameters &p)
{
    if(d.action=="SHOW_WARDROBE" || d.action=="SHOW_OUTFIT" || d.action=="SHOW_STYLE_KNOWLEDGE" || d.action=="SHOW_FASHION_WEB")
    {
        // A verified read must never create another outfit from copied model data.
        d.outfits.clear();d.referenceOutfit=0;d.changeCategory.reset();
    }
    if(d.action=="REDIRECT" || d.action=="PREFERENCES_READ" || d.action=="PREFERENCES_UPDATE" ||
       d.action=="SHOW_WARDROBE" || d.action=="SHOW_OUTFIT" || d.action=="SHOW_STYLE_KNOWLEDGE" || d.action=="SHOW_FASHION_WEB")
    {d.excludedColors=p.excludedColors;d.darkOnly=p.darkOnly;return true;}
    const std::set<std::string> colors{"BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"};
    std::set<std::string> excluded(p.excludedColors.begin(),p.excludedColors.end()), added, removed;
    static const std::regex permission(
        R"(\b(acepto|acepta|permito|permite|permitir|puedes|puede|usa|usar|incluye|incluir|quita|elimina|olvida)\b|sin restricciones|cualquier color)",std::regex::icase);
    if((!d.allowedColors.empty() || d.relaxDark) && !std::regex_search(p.message,permission)) return false;
    for(const auto &c:d.excludedColors)
        if(!colors.count(c) || !added.insert(c).second) return false;
    for(const auto &c:d.allowedColors)
        if(!colors.count(c) || !removed.insert(c).second || added.count(c)) return false;
    // Explicit user permission is authoritative even if the model repeats an old flag.
    if(explicitRelaxDarkRequest(p.message)){d.relaxDark=true;d.darkOnly=false;}
    if(d.relaxDark && d.darkOnly) return false;
    for(const auto &c:removed) excluded.erase(c);
    excluded.insert(added.begin(),added.end());
    p.excludedColors.assign(excluded.begin(),excluded.end());
    p.darkOnly=(p.darkOnly || d.darkOnly) && !d.relaxDark;
    if(d.styleUpdate)
    {
        const std::set<std::string> styles{"","CASUAL","FORMAL","SPORT","STREETWEAR","VINTAGE","MINIMALIST","BOHEMIAN","CLASSIC","ELEGANT"};
        if(!styles.count(*d.styleUpdate)) return false;
        static const std::regex clearStyle(R"((olvida|quita|elimina|borra|sin|cualquier|no importa).*estilo)",std::regex::icase);
        if(!d.styleUpdate->empty() || std::regex_search(p.message,clearStyle)) p.conversationStyle=*d.styleUpdate;
    }
    if(d.occasionUpdate)
    {
        const auto &s=*d.occasionUpdate;
        if(s.size()>120 || s.find('\0')!=std::string::npos ||
           (!s.empty() && s.find_first_not_of(" \t\r\n")==std::string::npos)) return false;
        static const std::regex clearOccasion(R"((olvida|quita|elimina|borra|sin|cualquier|no importa).*(ocasion|ocasión))",std::regex::icase);
        if(!s.empty() || std::regex_search(p.message,clearOccasion)) p.conversationOccasion=s;
    }
    d.excludedColors=p.excludedColors; d.darkOnly=p.darkOnly;
    return true;
}

bool applyPreferenceChange(const AgentResult &d,const std::string &message,RecommendationPreferences &p)
{
    if(d.action!="PREFERENCES_UPDATE")
        return d.preferenceOperation=="NONE" && d.preferenceField=="NONE" && d.preferenceValues.empty();
    // An ordinary outfit request must never silently become a permanent preference.
    static const std::regex explicitWrite(
        R"(\b(recuerda|guarda|olvida|memoriza)\b|\b(actualiza|cambia|borra|elimina)\b.*\b(gustos|preferencias)\b)",std::regex::icase);
    if(!std::regex_search(message,explicitWrite)) return false;
    std::vector<std::string> *target=nullptr;
    std::set<std::string> allowed;
    const std::set<std::string> colors{"BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"};
    if(d.preferenceField=="preferred_styles")
    {target=&p.preferredStyles;allowed={"CASUAL","FORMAL","SPORT","STREETWEAR","VINTAGE","MINIMALIST","BOHEMIAN","CLASSIC","ELEGANT"};}
    else if(d.preferenceField=="preferred_colors") {target=&p.preferredColors;allowed=colors;}
    else if(d.preferenceField=="avoided_colors") {target=&p.avoidedColors;allowed=colors;}
    else if(d.preferenceField=="preferred_categories")
    {target=&p.preferredCategories;allowed={"TOP","BOTTOM","ONE_PIECE","FOOTWEAR","OUTERWEAR","ACCESSORY"};}
    else return false;
    std::set<std::string> values;
    for(const auto &v:d.preferenceValues) if(!allowed.count(v) || !values.insert(v).second) return false;
    if(d.preferenceOperation=="CLEAR") {if(!values.empty()) return false; target->clear();}
    else
    {
        if(values.empty()) return false;
        if(d.preferenceOperation=="REPLACE") target->assign(values.begin(),values.end());
        else if(d.preferenceOperation=="ADD")
        {for(const auto &v:values) if(std::find(target->begin(),target->end(),v)==target->end()) target->push_back(v);}
        else if(d.preferenceOperation=="REMOVE")
        {target->erase(std::remove_if(target->begin(),target->end(),[&](const auto &v){return values.count(v);}),target->end());}
        else return false;
    }
    if(target->size()>50) return false;
    for(const auto &v:p.preferredColors)
        if(std::find(p.avoidedColors.begin(),p.avoidedColors.end(),v)!=p.avoidedColors.end()) return false;
    return true;
}

std::string preferencesReply(const RecommendationPreferences &p)
{
    const auto list=[](const std::vector<std::string> &values) {
        std::string text;
        for(const auto &v:values) {if(!text.empty()) text+=", ";text+=v;}
        return text.empty()?std::string("ninguno"):text;
    };
    return "Tus gustos guardados en Profile:\nEstilos: "+list(p.preferredStyles)+
        ".\nColores preferidos: "+list(p.preferredColors)+".\nColores que evitas: "+list(p.avoidedColors)+
        ".\nCategorías preferidas: "+list(p.preferredCategories)+".";
}

bool validConversationDecision(const AgentResult &d,const RecommendationParameters &p,
                               const std::vector<Garment> &garments)
{
    const bool generates=d.action=="RECOMMEND" || d.action=="MODIFY";
    if(!generates && d.action!="CHAT" && d.action!="CLARIFY" && d.action!="EXPLAIN" &&
       d.action!="REDIRECT" && d.action!="PREFERENCES_READ" && d.action!="PREFERENCES_UPDATE" &&
       d.action!="SHOW_WARDROBE" && d.action!="SHOW_OUTFIT" && d.action!="SHOW_STYLE_KNOWLEDGE" && d.action!="SHOW_FASHION_WEB") return false;
    if(d.action=="SHOW_WARDROBE" && !evidenceFor(d,"search_wardrobe"))return false;
    if(d.action=="SHOW_STYLE_KNOWLEDGE" && !evidenceFor(d,"search_style_knowledge"))return false;
    if(d.action=="SHOW_FASHION_WEB" && !evidenceFor(d,"search_fashion_web"))return false;
    if(d.action=="SHOW_OUTFIT")
    {
        const auto evidence=evidenceFor(d,"get_outfit");
        if(!evidence || evidence->outfitId.empty() || evidence->garments.empty())return false;
    }
    if(generates!=!d.outfits.empty()) return false;
    if(d.action=="MODIFY" || d.action=="EXPLAIN")
    {
        if(!referenceExists(d,p)) return false;
    }
    else if(d.referenceOutfit!=0) return false;
    if((d.action=="MODIFY")!=d.changeCategory.has_value()) return false;
    const std::set<std::string> colors{"BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"};
    std::set<std::string> seen;
    for(const auto &c:d.excludedColors) if(!colors.count(c) || !seen.insert(c).second) return false;
    for(const auto &outfit:d.outfits)
        for(const auto &id:outfit.garmentIds)
        {
            const auto g=find(garments,id);
            if(!g || (d.darkOnly && !confirmedDark(*g))) return false;
            if(!p.conversationStyle.empty() && !g->styles.empty() &&
               std::find(g->styles.begin(),g->styles.end(),p.conversationStyle)==g->styles.end()) return false;
            if(!d.excludedColors.empty() && g->colors.empty()) return false;
            for(const auto &c:g->colors) if(seen.count(c)) return false;
        }
    if(d.action=="RECOMMEND")
    {
        static const std::regex alternative(R"(otra (opcion|opción|combinacion|combinación|alternativa)|otro (outfit|conjunto)|algo (distinto|diferente)|^otra[.! ]*$)",std::regex::icase);
        if(std::regex_search(p.message,alternative))
            for(const auto &outfit:d.outfits)for(const auto &previous:p.previousOutfits)
            {
                auto a=outfit.garmentIds,b=previous.garmentIds;
                std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
                if(a==b)return false;
            }
    }
    if(d.action=="EXPLAIN")
        return !names(p.previousOutfits[d.referenceOutfit-1],garments).empty();
    if(d.action=="MODIFY")
    {
        if(d.outfits.size()!=1) return false;
        const auto &old=p.previousOutfits[d.referenceOutfit-1];
        std::map<Category,std::string> before,after;
        for(const auto &id:old.garmentIds)
        {
            const auto g=find(garments,id);
            if(!g || !before.emplace(g->category,id).second) return false;
        }
        for(const auto &id:d.outfits[0].garmentIds)
        {
            const auto g=find(garments,id);
            if(!g || !after.emplace(g->category,id).second) return false;
        }
        if(before.size()!=after.size() || !before.count(*d.changeCategory)) return false;
        for(const auto &entry:before)
        {
            const auto next=after.find(entry.first);
            if(next==after.end()) return false;
            if(entry.first==*d.changeCategory)
            {if(entry.second==next->second) return false;}
            else if(entry.second!=next->second) return false;
        }
    }
    return true;
}

std::optional<Outfit> findOutfitCandidate(const std::vector<Garment> &garments,const RecommendationParameters &p)
{
    const auto allowed=[&](std::vector<std::string> ids){
        AgentResult d{AgentStatus::Generated,{{std::move(ids),""}}};
        d.action="RECOMMEND";d.darkOnly=p.darkOnly;d.excludedColors=p.excludedColors;
        return validOutfit(d.outfits[0],garments) && validConversationDecision(d,p,garments);
    };
    for(const auto &g:garments)for(const auto &shoe:garments)
    {
        if(shoe.category!=Category::Footwear)continue;
        if(g.category==Category::OnePiece && allowed({g.id,shoe.id}))return Outfit{{g.id,shoe.id},""};
        if(g.category==Category::Top)
            for(const auto &bottom:garments)
                if(bottom.category==Category::Bottom && allowed({g.id,bottom.id,shoe.id}))return Outfit{{g.id,bottom.id,shoe.id},""};
    }
    return std::nullopt;
}

bool hasOutfitCandidate(const std::vector<Garment> &garments,const RecommendationParameters &p)
{return findOutfitCandidate(garments,p).has_value();}

std::string conversationReply(const AgentResult &d,const RecommendationParameters &p,
                              const std::vector<Garment> &garments)
{
    if(d.action=="REDIRECT")
        return "Puedo ayudarte con moda, tus prendas y combinaciones. ¿Para qué ocasión quieres vestirte?";
    if(d.action=="SHOW_FASHION_WEB")
    {
        const auto &refs=evidenceFor(d,"search_fashion_web")->webReferences;
        if(refs.empty())return "No encontré ejemplos en FashionStylist con esa búsqueda. Podemos precisar el estilo; esto no descarta otras fuentes de moda.";
        std::string text="Encontré estos ejemplos de inspiración en FashionStylist (publicación abril de 2026; no son prendas de tu clóset ni tendencias verificadas de hoy):";
        for(const auto &r:refs) {
            auto content=r.content;
            if(content.size()>350){size_t n=350;while(n && (static_cast<unsigned char>(content[n])&0xc0)==0x80)--n;content.resize(n);content+="...";}
            text+="\n- "+r.title+": "+content+"\nFuente: "+r.sourceUrl;
        }
        return text+"\nFashionStylist Authors (2026). CC-BY-NC-4.0: https://creativecommons.org/licenses/by-nc/4.0/ . Selección textual de Mirage; uso no comercial.";
    }
    if(d.action=="SHOW_STYLE_KNOWLEDGE")
    {
        const auto &refs=evidenceFor(d,"search_style_knowledge")->styleReferences;
        if(refs.empty())return "No encontré referencias publicadas pertinentes con esos filtros. Puedo ayudarte a precisar el estilo.";
        std::string text="Estas referencias orientativas pueden ayudarte:";
        for(const auto &r:refs)
        {
            auto excerpt=r.content;
            if(excerpt.size()>500){size_t end=500;while(end && (static_cast<unsigned char>(excerpt[end])&0xc0)==0x80)--end;excerpt.resize(end);excerpt+="...";}
            text+="\n- "+r.title+": "+excerpt+"\nFuente: "+r.sourceUrl+" ("+r.license+"). "+r.attribution;
        }
        return text;
    }
    if(d.action=="SHOW_WARDROBE")
    {
        const auto &e=*evidenceFor(d,"search_wardrobe");
        if(e.garments.empty())return "No encontré prendas disponibles con esos filtros.";
        std::string text="Encontré "+std::to_string(e.matchedCount)+" prendas disponibles. ";
        text+="Te muestro "+std::to_string(e.garments.size())+":";
        for(const auto &g:e.garments)text+="\n- "+g.name;
        return text;
    }
    if(d.action=="SHOW_OUTFIT")
    {
        const auto &e=*evidenceFor(d,"get_outfit");
        std::string text="El outfit guardado incluye ";
        for(size_t i=0;i<e.garments.size();++i){if(i)text+=" + ";text+=e.garments[i].name;}
        return text+". "+e.explanation+" Es una propuesta histórica; la disponibilidad actual puede haber cambiado.";
    }
    if(d.action=="CLARIFY" && d.darkOnly)
    {
        std::set<Category> darkCategories;
        for(const auto &g:garments)
            if(confirmedDark(g) && std::none_of(g.colors.begin(),g.colors.end(),[&](const auto &c){
                return std::find(d.excludedColors.begin(),d.excludedColors.end(),c)!=d.excludedColors.end();
            })) darkCategories.insert(g.category);
        const bool complete=darkCategories.count(Category::Footwear) &&
            (darkCategories.count(Category::OnePiece) ||
             (darkCategories.count(Category::Top) && darkCategories.count(Category::Bottom)));
        if(!complete)
            return "No tengo prendas de tono oscuro confirmado suficientes para armar un outfit completo. "
                   "El color azul por sí solo no confirma un tono oscuro. "
                   "¿Aceptas una aproximación con otros colores disponibles, respetando tus exclusiones?";
    }
    if(d.action=="RECOMMEND" || d.action=="MODIFY")
    {
        std::string text=d.action=="MODIFY"?"La opción modificada es:":
            d.outfits.size()==1?"Te propongo una opción:":"Te propongo "+std::to_string(d.outfits.size())+" opciones:";
        for(size_t i=0;i<d.outfits.size();++i)
            text+="\n"+std::to_string(i+1)+". "+names(d.outfits[i],garments)+".";
        return text;
    }
    if(d.action=="EXPLAIN")
    {
        const auto &outfit=p.previousOutfits[d.referenceOutfit-1];
        return "La opción "+std::to_string(d.referenceOutfit)+" incluye "+names(outfit,garments)+". "+outfit.explanation;
    }
    return d.assistantMessage;
}
}
