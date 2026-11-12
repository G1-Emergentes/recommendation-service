#include "FashionWebSearch.h"
#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <stdexcept>

namespace mirage::recommendation
{
namespace
{
const std::string repo="https://github.com/recsys-benchmark/FashionStylist";
std::string sourcePath(const std::string &group)
{return "/recsys-benchmark/FashionStylist/"+std::string(fashionWebRevision)+"/Dataset/"+group+"/look_en.csv";}
Json::Value error(const char *code)
{Json::Value v;v["ok"]=false;v["error"]["code"]=code;return v;}
std::string clean(std::string s,size_t maximum)
{
    if(s.empty() || s.size()>maximum || s.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid source text");
    s=std::regex_replace(s,std::regex("<[^>]*>"),"");
    for(auto &c:s)if(static_cast<unsigned char>(c)<32 || c==127)c=' ';
    if(s.find_first_not_of(' ')==std::string::npos)throw std::invalid_argument("Empty source text");
    return s;
}
std::set<std::string> words(std::string text)
{
    for(auto &c:text)c=std::isalpha(static_cast<unsigned char>(c))?static_cast<char>(std::tolower(static_cast<unsigned char>(c))):' ';
    const std::set<std::string> stop{"a","an","the","and","or","with","for","of","to","in","my","me","style","outfit","look","fashion","clothes","clothing","wear","wearing"};
    std::set<std::string> result;std::string word;
    for(size_t i=0;i<=text.size();++i) {
        const char c=i<text.size()?text[i]:' ';
        if(c!=' ')word+=c;
        else {if(word.size()>1 && !stop.count(word))result.insert(word);word.clear();}
    }
    return result;
}
}
FashionWebReference readFashionWebReference(const Json::Value &v)
{
    auto field=[&](const char *key,size_t n){if(!v[key].isString())throw std::invalid_argument("Missing source field");return clean(v[key].asString(),n);};
    FashionWebReference r{field("id",64),field("title",120),field("content",1200),field("source_url",220),field("audience",6),field("season",30),field("occasion",80)};
    const auto group=r.audience=="MALE"?"Male":r.audience=="FEMALE"?"Female":"";
    const auto prefix=repo+"/blob/"+fashionWebRevision+"/Dataset/"+group+"/look_en.csv#L";
    if(std::string(group).empty() || !std::regex_match(r.id,std::regex("^fashionstylist-(male|female)-b[1-9][0-9]{0,2}$")) ||
       r.id.find(r.audience=="MALE"?"-male-":"-female-")==std::string::npos ||
       r.sourceUrl.compare(0,prefix.size(),prefix)!=0 ||
       !std::regex_match(r.sourceUrl.substr(prefix.size()),std::regex("[1-9][0-9]{0,3}")) ||
       v["source_revision"]!=fashionWebRevision || v["license"]!="CC-BY-NC-4.0")
        throw std::invalid_argument("Unapproved web reference");
    return r;
}
Json::Value fashionWebReferenceJson(const FashionWebReference &r)
{
    Json::Value v;v["id"]=r.id;v["title"]=r.title;v["content"]=r.content;v["source_url"]=r.sourceUrl;
    v["audience"]=r.audience;v["season"]=r.season;v["occasion"]=r.occasion;
    v["source_revision"]=fashionWebRevision;v["dataset_release"]="2026-04-08";
    v["license"]="CC-BY-NC-4.0";v["license_url"]="https://creativecommons.org/licenses/by-nc/4.0/";
    v["attribution"]="FashionStylist Authors (2026). Mirage: selección textual y búsqueda léxica; sin imágenes.";
    return v;
}
Json::Value fashionWebReferencesJson(const std::vector<FashionWebReference> &refs)
{Json::Value a(Json::arrayValue);for(const auto &r:refs)a.append(fashionWebReferenceJson(r));return a;}

FashionWebSearch::FashionWebSearch(std::string origin,double timeout):origin_(std::move(origin)),timeout_(timeout)
{
    if(!std::regex_match(origin_,std::regex(R"(^https://raw\.githubusercontent\.com$|^http://127\.0\.0\.1:[0-9]{1,5}$)")) || !(timeout>=1 && timeout<=10))
        throw std::invalid_argument("Invalid fashion web source configuration");
}
bool FashionWebSearch::validFilters(const Json::Value &args)
{
    if(!args.isObject() || args.size()>4 || !args["query"].isString())return false;
    for(const auto &key:args.getMemberNames())if(key!="query" && key!="limit" && key!="audience" && key!="season")return false;
    if(!std::regex_match(args["query"].asString(),std::regex("[A-Za-z][A-Za-z -]{1,199}")))return false;
    if(args.isMember("limit") && (!args["limit"].isUInt() || args["limit"].asUInt()<1 || args["limit"].asUInt()>3))return false;
    for(const auto &entry:std::vector<std::pair<const char *,std::set<std::string>>>{{"audience",{"ANY","MALE","FEMALE"}},{"season",{"ANY","SPRING","SUMMER","AUTUMN","WINTER"}}})
        if(args.isMember(entry.first) && (!args[entry.first].isString() || !entry.second.count(args[entry.first].asString())))return false;
    return true;
}

std::vector<FashionWebReference> FashionWebSearch::parseCsv(const std::string &body,const std::string &audience)
{
    if(body.size()>256*1024 || (audience!="MALE" && audience!="FEMALE"))throw std::invalid_argument("Invalid fashion CSV");
    std::vector<FashionWebReference> records;std::vector<std::string> cells;std::string cell;
    bool quoted=false,closed=false,header=false;unsigned line=1,start=1;
    size_t offset=body.compare(0,3,"\xef\xbb\xbf")==0?3:0;
    auto row=[&]{
        cells.push_back(cell);cell.clear();closed=false;
        if(cells.size()!=5)throw std::invalid_argument("Unexpected CSV columns");
        if(!header) {
            if(cells!=std::vector<std::string>{"outfitID","items","look","season","occasion"})throw std::invalid_argument("Changed source schema");
            header=true;
        } else {
            if(!std::regex_match(cells[0],std::regex("b[1-9][0-9]{0,2}")) || cells[1].size()>400)throw std::invalid_argument("Invalid outfit row");
            const auto group=audience=="MALE"?"Male":"Female";
            FashionWebReference r{"fashionstylist-"+std::string(audience=="MALE"?"male":"female")+"-"+cells[0],
              "FashionStylist · "+std::string(group)+" · "+cells[0],clean(cells[2],1200),
              repo+"/blob/"+fashionWebRevision+"/Dataset/"+group+"/look_en.csv#L"+std::to_string(start),
              audience,clean(cells[3],30),clean(cells[4],80)};
            records.push_back(readFashionWebReference(fashionWebReferenceJson(r)));
            if(records.size()>500)throw std::invalid_argument("Unexpected source size");
        }
        cells.clear();
    };
    for(size_t i=offset;i<body.size();++i) {
        const char c=body[i];
        if(c=='\0')throw std::invalid_argument("NUL in CSV");
        if(quoted) {
            if(c=='"') {if(i+1<body.size() && body[i+1]=='"'){cell+='"';++i;}else{quoted=false;closed=true;}}
            else {cell+=c;if(c=='\n')++line;}
        } else if(c=='"' && cell.empty() && !closed)quoted=true;
        else if(c==','){cells.push_back(cell);cell.clear();closed=false;}
        else if(c=='\n'){row();++line;start=line;}
        else if(c=='\r') {if(i+1>=body.size() || body[i+1]!='\n')throw std::invalid_argument("Invalid CSV newline");}
        else {if(closed || c=='"')throw std::invalid_argument("Invalid CSV quote");cell+=c;}
        if(cell.size()>1400 || cells.size()>4)throw std::invalid_argument("Oversized source field");
    }
    if(quoted)throw std::invalid_argument("Unclosed CSV quote");
    if(!cells.empty() || !cell.empty())row();
    std::set<std::string> ids;for(const auto &r:records)if(!ids.insert(r.id).second)throw std::invalid_argument("Duplicate source outfit");
    if(records.empty())throw std::invalid_argument("Empty fashion source");
    return records;
}
void FashionWebSearch::search(const Json::Value &args,ToolCompletion done)
{
    auto *loop=drogon::app().getLoop();
    if(!loop->isInLoopThread()){loop->queueInLoop([this,args,done]{search(args,done);});return;}
    if(!validFilters(args)){done(error("INVALID_ARGUMENTS"));return;}
    if(retryAfter_>std::chrono::steady_clock::now()){done(error("EXTERNAL_SOURCE_UNAVAILABLE"));return;}
    if(cache_.expires>std::chrono::steady_clock::now()){results(args,std::move(done));return;}
    if(waiters_.size()>=16){done(error("EXTERNAL_SOURCE_UNAVAILABLE"));return;}
    waiters_.push_back([this,args,done](bool ok){if(ok)results(args,done);else done(error("EXTERNAL_SOURCE_UNAVAILABLE"));});
    if(loading_)return;
    loading_=true;load(0,std::make_shared<std::vector<FashionWebReference>>());
}
void FashionWebSearch::load(unsigned group,std::shared_ptr<std::vector<FashionWebReference>> records)
{
    if(group==2){loaded(true,std::move(*records));return;}
    const auto name=group==0?"Female":"Male";
    auto client=drogon::HttpClient::newHttpClient(origin_,drogon::app().getLoop(),false,true);
    auto request=drogon::HttpRequest::newHttpRequest();request->setMethod(drogon::Get);request->setPath(sourcePath(name));
    request->addHeader("User-Agent","Mirage-Recommendation/0.1 (academic fashion assistant)");
    request->addHeader("Accept","text/plain");
    client->sendRequest(request,[this,client,group,records](drogon::ReqResult status,const drogon::HttpResponsePtr &response){
        if(status!=drogon::ReqResult::Ok || !response || response->statusCode()!=drogon::k200OK || response->body().size()>256*1024)
        {loaded(false);return;}
        try {
            auto rows=parseCsv(std::string(response->body()),group==0?"FEMALE":"MALE");
            records->insert(records->end(),rows.begin(),rows.end());
        } catch(const std::exception &) {loaded(false);return;}
        load(group+1,records);
    },timeout_);
}
void FashionWebSearch::loaded(bool ok,std::vector<FashionWebReference> records)
{
    if(ok){cache_.items=std::move(records);cache_.expires=std::chrono::steady_clock::now()+std::chrono::minutes(30);}
    else retryAfter_=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    loading_=false;auto waiters=std::move(waiters_);waiters_.clear();for(auto &done:waiters)done(ok);
}
void FashionWebSearch::results(const Json::Value &args,ToolCompletion done) const
{
    const auto query=words(args["query"].asString());
    std::vector<std::pair<unsigned,const FashionWebReference *>> ranked;
    for(const auto &r:cache_.items) {
        if(args.get("audience","ANY")!="ANY" && args["audience"]!=r.audience)continue;
        auto season=words(r.season);auto wanted=words(args.get("season","ANY").asString());
        if(args.get("season","ANY")!="ANY" && (wanted.empty() || !season.count(*wanted.begin())))continue;
        const auto doc=words(r.content+" "+r.occasion);unsigned matched=0;
        for(const auto &term:query)if(doc.count(term))++matched;
        if(matched)ranked.emplace_back(matched,&r);
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    Json::Value output;output["ok"]=true;output["items"]=Json::Value(Json::arrayValue);
    const auto count=(std::min)(ranked.size(),static_cast<size_t>(args.get("limit",3).asUInt()));
    for(size_t i=0;i<count;++i)output["items"].append(fashionWebReferenceJson(*ranked[i].second));
    output["source_repository"]=repo;output["source_revision"]=fashionWebRevision;
    output["search_method"]="lexical_overlap";output["cache_ttl_seconds"]=1800;
    output["scope"]="external inspiration only; dataset release 2026-04-08; not live trends or owned inventory";
    done(std::move(output));
}
}
