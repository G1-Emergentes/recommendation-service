#include "StyleKnowledgeJson.h"
#include <regex>
#include <stdexcept>
namespace mirage::recommendation
{
StyleReference readStyleReference(const Json::Value &v)
{
    const auto text=[&](const char *key,size_t max){
        if(!v[key].isString())throw std::invalid_argument("Invalid style reference");
        const auto s=v[key].asString();
        if(s.empty() || s.size()>max || s.find('\0')!=std::string::npos || s.find_first_not_of(" \t\r\n")==std::string::npos)
            throw std::invalid_argument("Invalid style reference");
        return s;
    };
    StyleReference r{text("id",77),text("title",160),text("content",1400),text("source_url",240),text("license",20),text("attribution",240)};
    static const std::regex id("^mirage-[a-z0-9-]{1,70}$"),url("^https://en[.]wikipedia[.]org/wiki/[A-Za-z0-9_()-]+$");
    if(!std::regex_match(r.id,id) || !std::regex_match(r.sourceUrl,url) || r.license!="CC-BY-SA-4.0")throw std::invalid_argument("Unapproved style reference");
    return r;
}
Json::Value styleReferenceJson(const StyleReference &r)
{
    Json::Value v;v["id"]=r.id;v["title"]=r.title;v["content"]=r.content;v["source_url"]=r.sourceUrl;
    v["license"]=r.license;v["license_url"]="https://creativecommons.org/licenses/by-sa/4.0/";
    v["attribution"]=r.attribution;v["content_kind"]="adaptation_with_editorial_guidance";return v;
}
Json::Value styleReferencesJson(const std::vector<StyleReference> &refs)
{Json::Value a(Json::arrayValue);for(const auto &r:refs)a.append(styleReferenceJson(r));return a;}
}
