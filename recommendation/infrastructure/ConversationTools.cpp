#include "ConversationTools.h"
#include "GarmentJson.h"
#include "StyleKnowledgeJson.h"
#include "WardrobeIndex.h"
#include "FashionWebSearch.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace mirage::recommendation
{
namespace
{
Json::Value values(std::initializer_list<const char *> strings)
{Json::Value a(Json::arrayValue);for(auto s:strings)a.append(s);return a;}
Json::Value failure(const char *code)
{Json::Value v;v["ok"]=false;v["error"]["code"]=code;return v;}
std::string lower(std::string s)
{for(auto &c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
bool id(const Json::Value &v)
{
    static const std::regex uuid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return v.isString() && std::regex_match(v.asString(),uuid);
}
}
Json::Value conversationToolDefinitions()
{
    Json::Value tools(Json::arrayValue),search,get;
    search["type"]="function";
    auto &s=search["function"];
    s["name"]="search_wardrobe";
    s["description"]="Busca prendas DISPONIBLES del usuario autenticado. Filtros exactos de categoría/color/estilo. search_mode literal busca subcadena; semantic recupera por significado mediante RAG y sincroniza el índice privado. No modifica prendas.";
    s["strict"]=false;
    auto &p=s["parameters"];
    p["type"]="object";p["additionalProperties"]=false;
    p["required"]=Json::Value(Json::arrayValue);
    for(auto key:{"query","category","color","style"})
    {p["properties"][key]["type"]="string";p["properties"][key]["default"]="";}
    p["properties"]["query"]["description"]="Con filtros exactos puede quedar vacío en modo literal; para semantic debe contener una descripción no vacía por significado. Nunca repitas prendas negras como query literal si ya filtras color BLACK.";
    p["properties"]["query"]["maxLength"]=120;
    p["properties"]["category"]["enum"]=values({"","TOP","BOTTOM","ONE_PIECE","FOOTWEAR","OUTERWEAR","ACCESSORY"});
    p["properties"]["color"]["enum"]=values({"","BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"});
    p["properties"]["style"]["enum"]=values({"","CASUAL","FORMAL","SPORT","STREETWEAR","MINIMALIST","CLASSIC","BOHEMIAN"});
    p["properties"]["limit"]["type"]="integer";
    p["properties"]["limit"]["minimum"]=1;p["properties"]["limit"]["maximum"]=10;
    p["properties"]["limit"]["default"]=10;
    p["properties"]["search_mode"]["type"]="string";
    p["properties"]["search_mode"]["enum"]=values({"literal","semantic"});
    p["properties"]["search_mode"]["default"]="literal";
    get["type"]="function";auto &g=get["function"];
    g["name"]="get_outfit";g["strict"]=true;
    g["description"]="Consulta un outfit de Recommendation guardado por el usuario autenticado, mediante su ID exacto conocido. Devuelve una captura histórica, no disponibilidad actual. No inventes IDs. No consulta outfits de otros usuarios ni los de Digital Closet.";
    auto &q=g["parameters"];q["type"]="object";q["additionalProperties"]=false;
    q["required"]=values({"outfit_id"});q["properties"]["outfit_id"]["type"]="string";
    q["properties"]["outfit_id"]["pattern"]="^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$";
    Json::Value knowledge;knowledge["type"]="function";auto &k=knowledge["function"];
    k["name"]="search_style_knowledge";k["strict"]=false;
    k["description"]="Consulta referencias de moda revisadas y compartidas. Devuelve criterios orientativos con fuente y licencia, nunca prendas del usuario. No navega por Internet ni acepta URLs.";
    auto &kp=k["parameters"];kp["type"]="object";kp["additionalProperties"]=false;kp["required"]=values({"query"});
    kp["properties"]["query"]["type"]="string";kp["properties"]["query"]["minLength"]=1;kp["properties"]["query"]["maxLength"]=512;
    kp["properties"]["style"]["type"]="string";kp["properties"]["style"]["enum"]=values({"","CASUAL","FORMAL","SPORT","STREETWEAR","VINTAGE","MINIMALIST","BOHEMIAN","CLASSIC","ELEGANT"});
    kp["properties"]["limit"]["type"]="integer";kp["properties"]["limit"]["minimum"]=1;kp["properties"]["limit"]["maximum"]=3;
    Json::Value web;web["type"]="function";auto &w=web["function"];
    w["name"]="search_fashion_web";w["strict"]=false;
    w["description"]="Busca inspiración externa bajo demanda en FashionStylist, repositorio publicado en abril de 2026. Úsala si necesitas estilos no cubiertos por las referencias locales, ideas adicionales o si el usuario pide fuentes web. No es un feed de tendencias actuales. Solo consulta descripciones públicas, no imágenes ni tiendas. No son prendas del usuario. Licencia CC-BY-NC-4.0, uso académico/no comercial.";
    auto &wp=w["parameters"];wp["type"]="object";wp["additionalProperties"]=false;wp["required"]=values({"query"});
    wp["properties"]["query"]["type"]="string";wp["properties"]["query"]["minLength"]=2;wp["properties"]["query"]["maxLength"]=200;
    wp["properties"]["query"]["pattern"]="^[A-Za-z][A-Za-z -]{1,199}$";
    wp["properties"]["query"]["description"]="Traduce el estilo a 2-8 palabras clave EN INGLÉS: quiet luxury, old money, dark street, techwear cargo, summer linen. Búsqueda léxica, no semántica. Omite nombres personales, correos, instrucciones, URLs y fechas.";
    wp["properties"]["audience"]["type"]="string";wp["properties"]["audience"]["enum"]=values({"ANY","MALE","FEMALE"});
    wp["properties"]["season"]["type"]="string";wp["properties"]["season"]["enum"]=values({"ANY","SPRING","SUMMER","AUTUMN","WINTER"});
    wp["properties"]["limit"]["type"]="integer";wp["properties"]["limit"]["minimum"]=1;wp["properties"]["limit"]["maximum"]=3;
    tools.append(search);tools.append(get);tools.append(knowledge);tools.append(web);return tools;
}
ToolExecutor conversationToolExecutor(UserAccess access,const WardrobeClient &wardrobe,const RecommendationRepository &history,const WardrobeIndex *index,FashionWebSearch *web)
{
    return [access,&wardrobe,&history,index,web](const std::string &name,const Json::Value &input,ToolCompletion done) {
        auto args=input;
        if(access.userId.empty() || access.accessToken.empty()){done(failure("UNAUTHENTICATED"));return;}
        if(name=="search_fashion_web")
        {
            if(!FashionWebSearch::validFilters(args)){done(failure("INVALID_ARGUMENTS"));return;}
            if(!web){done(failure("EXTERNAL_SOURCE_UNAVAILABLE"));return;}
            web->search(args,std::move(done));return;
        }
        if(name=="search_wardrobe")
        {
            const auto definition=conversationToolDefinitions()[0]["function"]["parameters"];
            if(!args.isObject() || args.size()>6){done(failure("INVALID_ARGUMENTS"));return;}
            for(const auto &key:args.getMemberNames())
                if(!definition["properties"].isMember(key)){done(failure("INVALID_ARGUMENTS"));return;}
            for(auto key:{"query","category","color","style"})if(!args.isMember(key))args[key]="";
            if(!args.isMember("limit"))args["limit"]=10;
            if(!args["query"].isString() ||
               args["query"].asString().size()>120 || args["query"].asString().find('\0')!=std::string::npos ||
               !args["limit"].isUInt() || args["limit"].asUInt()<1 || args["limit"].asUInt()>10)
            {done(failure("INVALID_ARGUMENTS"));return;}
            for(auto key:{"category","color","style"})
            {
                const auto &allowed=definition["properties"][key]["enum"];
                if(!args[key].isString() || std::find(allowed.begin(),allowed.end(),args[key])==allowed.end())
                {done(failure("INVALID_ARGUMENTS"));return;}
            }
            const auto mode=args.get("search_mode","literal");
            if(!mode.isString() || (mode!="literal" && mode!="semantic")){done(failure("INVALID_ARGUMENTS"));return;}
            args.removeMember("search_mode");
            if(mode=="semantic")
            {
                if(!index){done(failure("TOOL_UNAVAILABLE"));return;}
                if(!WardrobeIndex::validFilters(args,true)){done(failure("INVALID_ARGUMENTS"));return;}
                index->search(access,args,[done](HistoryResult r){
                    if(r.status!=200){done(failure(r.status==401?"UNAUTHENTICATED":"TOOL_UNAVAILABLE"));return;}
                    auto body=r.body;body["ok"]=true;done(std::move(body));
                });return;
            }
            wardrobe.listForUser(access,[args,done](WardrobeResult found) {
                if(found.status!=WardrobeStatus::Found)
                {done(failure(found.status==WardrobeStatus::Unauthorized?"UNAUTHENTICATED":"TOOL_UNAVAILABLE"));return;}
                Json::Value result;result["ok"]=true;result["items"]=Json::Value(Json::arrayValue);
                const auto query=lower(args["query"].asString());unsigned count=0;
                for(const auto &g:found.garments)
                {
                    if(!g.available)continue;
                    const auto item=garmentJson(g);
                    if(!args["category"].asString().empty() && item["category"]!=args["category"])continue;
                    const auto contains=[&](const std::vector<std::string> &v,const char *key) {
                        return args[key].asString().empty() || std::find(v.begin(),v.end(),args[key].asString())!=v.end();
                    };
                    if(!contains(g.colors,"color") || !contains(g.styles,"style"))continue;
                    if(!query.empty() && lower(g.name+" "+g.description).find(query)==std::string::npos)continue;
                    ++count;
                    if(result["items"].size()<args["limit"].asUInt())result["items"].append(item);
                }
                result["matched_count"]=count;result["has_more"]=count>result["items"].size();done(std::move(result));
            });return;
        }
        if(name=="search_style_knowledge")
        {
            if(!WardrobeIndex::validStyleFilters(args)){done(failure("INVALID_ARGUMENTS"));return;}
            if(!index){done(failure("TOOL_UNAVAILABLE"));return;}
            index->searchStyleKnowledge(access,args,[done](HistoryResult result){
                if(result.status!=200){done(failure(result.status==401?"UNAUTHENTICATED":"TOOL_UNAVAILABLE"));return;}
                auto body=result.body;body["ok"]=true;done(std::move(body));
            });return;
        }
        if(name=="get_outfit")
        {
            if(!args.isObject() || args.size()!=1 || !id(args["outfit_id"]))
            {done(failure("INVALID_ARGUMENTS"));return;}
            const auto outfitId=lower(args["outfit_id"].asString());
            history.find(access,outfitId,[access,outfitId,done](HistoryResult found) {
                if(found.status!=200)
                {done(failure(found.status==404?"NOT_FOUND":"TOOL_UNAVAILABLE"));return;}
                const auto &record=found.body;
                if(record["user_id"]!=access.userId || record["id"]!=outfitId ||
                   !record["garment_ids"].isArray() || record["garment_ids"].size()<2 || record["garment_ids"].size()>3 ||
                   !record["explanation"].isString() || record["explanation"].asString().size()>1000)
                {done(failure("TOOL_UNAVAILABLE"));return;}
                if(!record.isMember("garments") || record["garments"].isNull())
                {done(failure("SNAPSHOT_UNAVAILABLE"));return;}
                if(!record["garments"].isArray() || record["garments"].size()>100)
                {done(failure("TOOL_UNAVAILABLE"));return;}
                Json::Value result;result["ok"]=true;result["snapshot_only"]=true;
                result["outfit"]["id"]=outfitId;result["outfit"]["explanation"]=record["explanation"];
                result["outfit"]["status"]=record["status"];result["outfit"]["is_favorite"]=record["is_favorite"];
                result["outfit"]["created_at"]=record["created_at"];
                result["garments"]=Json::Value(Json::arrayValue);std::set<std::string> seen;
                try
                {
                    for(const auto &value:record["garment_ids"])
                    {
                        if(!id(value) || !seen.insert(value.asString()).second)throw std::invalid_argument("Invalid outfit snapshot");
                        const auto &list=record["garments"];
                        const auto item=std::find_if(list.begin(),list.end(),[&](const auto &g){return g.isObject() && g["id"]==value;});
                        if(item==list.end()) {done(failure("SNAPSHOT_UNAVAILABLE"));return;}
                        result["garments"].append(garmentJson(readGarment(*item)));
                    }
                }
                catch(const std::invalid_argument &) {done(failure("TOOL_UNAVAILABLE"));return;}
                done(std::move(result));
            });return;
        }
        done(failure("UNKNOWN_TOOL"));
    };
}
}
