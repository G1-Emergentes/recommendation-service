#include "OutfitExampleJson.h"
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
namespace mirage::recommendation
{
OutfitExample readOutfitExample(const Json::Value &v)
{
    const auto text=[&](const Json::Value &object,const char *key,size_t max){
        if(!object[key].isString())throw std::invalid_argument("Invalid outfit reference");
        const auto s=object[key].asString();
        if(s.empty() || s.size()>max || s.find('\0')!=std::string::npos || s.find_first_not_of(" \t\r\n")==std::string::npos)throw std::invalid_argument("Invalid outfit reference");
        return s;
    };
    OutfitExample r;
    r.id=text(v,"id",62);r.sourceOutfitId=text(v,"source_outfit_id",20);r.title=text(v,"title",120);
    r.structure=text(v,"structure",12);r.content=text(v,"content",1400);r.sourceCommit=text(v,"source_commit",40);
    r.sourceArchiveHash=text(v,"source_archive_sha256",64);r.attribution=text(v,"attribution",240);
    static const std::regex sha("^[0-9a-f]{40}$"),hash("^[0-9a-f]{64}$"),number("^[1-9][0-9]{0,19}$");
    if(!std::regex_match(r.sourceCommit,sha) || !std::regex_match(r.sourceArchiveHash,hash) || !std::regex_match(r.sourceOutfitId,number) ||
       r.id!="polyvore-"+r.sourceCommit.substr(0,12)+"-"+r.sourceOutfitId ||
       v["source_url"]!="https://github.com/xthan/polyvore-dataset/blob/"+r.sourceCommit+"/polyvore.tar.gz" ||
       v["source_split"]!="train" || v["license"]!="Apache-2.0" || v["license_scope"]!="repository_metadata_only_no_images" ||
       !v["items"].isArray() || v["items"].size()<2 || v["items"].size()>3 ||
       !v["source_items_count"].isUInt() || !v["omitted_items_count"].isUInt())throw std::invalid_argument("Unapproved outfit reference");
    r.sourceItemsCount=v["source_items_count"].asUInt();r.omittedItemsCount=v["omitted_items_count"].asUInt();
    if(r.sourceItemsCount<v["items"].size() || r.sourceItemsCount>8 || r.omittedItemsCount>6 || r.omittedItemsCount+v["items"].size()!=r.sourceItemsCount)throw std::invalid_argument("Invalid projection count");
    std::map<std::string,unsigned> categories;std::set<unsigned> indices;
    for(const auto &item:v["items"])
    {
        if(!item.isObject() || item.size()!=3 || !item["source_item_index"].isUInt())throw std::invalid_argument("Invalid source item");
        const auto index=item["source_item_index"].asUInt();
        if(index<1 || index>99 || !indices.insert(index).second)throw std::invalid_argument("Invalid source item index");
        ExampleItem i{text(item,"category",12),text(item,"name",140),index};++categories[i.category];r.items.push_back(std::move(i));
    }
    if(!((r.structure=="SEPARATES" && categories==std::map<std::string,unsigned>{{"TOP",1},{"BOTTOM",1},{"FOOTWEAR",1}}) ||
         (r.structure=="ONE_PIECE" && categories==std::map<std::string,unsigned>{{"ONE_PIECE",1},{"FOOTWEAR",1}})))throw std::invalid_argument("Incomplete source projection");
    return r;
}
Json::Value outfitExampleJson(const OutfitExample &r)
{
    Json::Value v;v["id"]=r.id;v["source_outfit_id"]=r.sourceOutfitId;v["title"]=r.title;v["structure"]=r.structure;v["content"]=r.content;
    v["source_commit"]=r.sourceCommit;v["source_archive_sha256"]=r.sourceArchiveHash;v["source_split"]="train";
    v["source_url"]="https://github.com/xthan/polyvore-dataset/blob/"+r.sourceCommit+"/polyvore.tar.gz";
    v["license"]="Apache-2.0";v["license_scope"]="repository_metadata_only_no_images";v["license_url"]="https://github.com/xthan/polyvore-dataset/blob/"+r.sourceCommit+"/LICENSE";
    v["attribution"]=r.attribution;v["content_kind"]="polyvore_core_projection";v["source_items_count"]=r.sourceItemsCount;v["omitted_items_count"]=r.omittedItemsCount;
    v["items"]=Json::Value(Json::arrayValue);
    for(const auto &item:r.items){Json::Value i;i["category"]=item.category;i["name"]=item.name;i["source_item_index"]=item.sourceItemIndex;v["items"].append(i);}
    return v;
}
Json::Value outfitExamplesJson(const std::vector<OutfitExample> &refs)
{Json::Value a(Json::arrayValue);for(const auto &r:refs)a.append(outfitExampleJson(r));return a;}
}
