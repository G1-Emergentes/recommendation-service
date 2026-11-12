#include "GarmentJson.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace mirage::recommendation
{
namespace
{
const std::map<std::string,Category> categories{{"TOP",Category::Top},{"BOTTOM",Category::Bottom},
    {"ONE_PIECE",Category::OnePiece},{"OUTERWEAR",Category::Outerwear},
    {"FOOTWEAR",Category::Footwear},{"ACCESSORY",Category::Accessory}};
std::string text(const Json::Value &v, size_t limit)
{
    if (!v.isString()) throw std::invalid_argument("Invalid garment text");
    const auto s=v.asString();
    // Contract limits are characters; UTF-8 can use up to four bytes per character.
    const auto characters=std::count_if(s.begin(),s.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
    if (s.empty() || s.size()>limit*4 || static_cast<size_t>(characters)>limit || s.find('\0')!=std::string::npos ||
        s.find_first_not_of(" \t\r\n")==std::string::npos)
        throw std::invalid_argument("Invalid garment text");
    return s;
}
std::string id(const Json::Value &v)
{
    static const std::regex uuid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    auto s=text(v,36);
    if (!std::regex_match(s,uuid)) throw std::invalid_argument("Invalid garment ID");
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}
std::vector<std::string> codes(const Json::Value &v, const std::set<std::string> &allowed)
{
    if (!v.isArray() || v.size()>50) throw std::invalid_argument("Invalid garment codes");
    std::set<std::string> seen;
    std::vector<std::string> result;
    for (const auto &code:v)
    {
        const auto s=text(code,80);
        if (!allowed.count(s) || !seen.insert(s).second) throw std::invalid_argument("Invalid garment codes");
        result.push_back(s);
    }
    return result;
}
Json::Value strings(const std::vector<std::string> &values)
{
    Json::Value a(Json::arrayValue);
    for (const auto &v:values) a.append(v);
    return a;
}
}
Garment readGarment(const Json::Value &v)
{
    if (!v.isObject()) throw std::invalid_argument("Invalid garment");
    for (const auto *key:{"id","name","category","subcategory","description","colors","styles","availability","revision","updated_at","image"})
        if (!v.isMember(key)) throw std::invalid_argument("Missing garment field");
    Garment g;
    g.id=id(v["id"]);
    const auto category=categories.find(text(v["category"],30));
    if (category==categories.end()) throw std::invalid_argument("Invalid category");
    g.category=category->second;
    const auto availability=text(v["availability"],20);
    if (availability!="AVAILABLE" && availability!="UNAVAILABLE") throw std::invalid_argument("Invalid availability");
    g.available=availability=="AVAILABLE";
    g.name=text(v["name"],120);
    g.description=text(v["description"],1000);
    g.styles=codes(v["styles"],{"CASUAL","FORMAL","SPORT","STREETWEAR","MINIMALIST","CLASSIC","BOHEMIAN"});
    g.colors=codes(v["colors"],{"BLACK","WHITE","GRAY","BLUE","RED","GREEN","YELLOW","ORANGE","PURPLE","PINK","BROWN","BEIGE","MULTICOLOR"});
    if (!v["subcategory"].isNull())
    {
        const auto sub=text(v["subcategory"],60);
        static const std::regex code("^[A-Z0-9_]{1,60}$");
        if (!std::regex_match(sub,code)) throw std::invalid_argument("Invalid subcategory");
        g.subcategory=sub;
    }
    if (!v["revision"].isUInt() || v["revision"].asUInt()==0) throw std::invalid_argument("Invalid revision");
    g.revision=v["revision"].asUInt();
    g.updatedAt=text(v["updated_at"],40);
    static const std::regex stamp(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,6})?(Z|\+00:00)$)");
    if (!std::regex_match(g.updatedAt,stamp)) throw std::invalid_argument("Invalid timestamp");
    const auto &image=v["image"];
    if (!image.isNull())
    {
        if (!image.isObject() || !image["version"].isUInt() || image["version"].asUInt()==0)
            throw std::invalid_argument("Invalid image version");
        g.image=GarmentImage{id(image["id"]),image["version"].asUInt()};
    }
    return g;
}
Json::Value garmentJson(const Garment &g)
{
    Json::Value v;
    v["id"]=g.id;
    v["name"]=g.name;
    for (const auto &c:categories) if(c.second==g.category) v["category"]=c.first;
    v["description"]=g.description;
    v["subcategory"]=g.subcategory?Json::Value(*g.subcategory):Json::Value();
    v["availability"]=g.available?"AVAILABLE":"UNAVAILABLE";
    v["colors"]=strings(g.colors);
    v["styles"]=strings(g.styles);
    v["revision"]=g.revision;
    v["updated_at"]=g.updatedAt;
    v["image"]=Json::Value();
    if(g.image){v["image"]["id"]=g.image->id;v["image"]["version"]=g.image->version;}
    return v;
}
Json::Value garmentsJson(const std::vector<Garment> &garments)
{
    Json::Value a(Json::arrayValue);
    for (const auto &g:garments) a.append(garmentJson(g));
    return a;
}
}
