#include "recommendation/infrastructure/OutfitExampleJson.h"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace mirage::recommendation;
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
int main()
{
    OutfitExample r;r.id="polyvore-aaaaaaaaaaaa-123";r.sourceOutfitId="123";r.sourceCommit=std::string(40,'a');r.sourceArchiveHash=std::string(64,'b');
    r.title="Example";r.content="A historical core projection.";r.structure="SEPARATES";r.sourceItemsCount=4;r.omittedItemsCount=1;r.attribution="Xintong Han et al.; Mirage projection.";
    r.items={{"TOP","Tee",1},{"BOTTOM","Jeans",2},{"FOOTWEAR","Boots",3}};
    const auto v=outfitExampleJson(r);const auto parsed=readOutfitExample(v);
    check(parsed.items.size()==3 && parsed.omittedItemsCount==1,"Read complete attributed core projection");
    const std::vector<std::function<void(Json::Value &)>> mutations{
        [](auto &v){v["id"]="10000000-0000-4000-8000-000000000001";},
        [](auto &v){v["source_split"]="valid";},[](auto &v){v["source_split"]="test";},
        [](auto &v){v["source_url"]="https://attacker.example/data";},
        [](auto &v){v["items"][0]["image"]="https://attacker/image";},
        [](auto &v){v["items"][1]["source_item_index"]=1;},
        [](auto &v){v["items"][0]["category"]="ACCESSORY";},
        [](auto &v){v["structure"]="ONE_PIECE";},
        [](auto &v){v["omitted_items_count"]=0;},
        [](auto &v){v["source_commit"]="not-a-commit";},
        [](auto &v){v["license"]="unknown";}};
    for(const auto &change:mutations){auto bad=v;change(bad);bool rejected=false;try{readOutfitExample(bad);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Reject untrusted or inconsistent external example");}
    std::cout<<"Polyvore JSON guards passed: external IDs, source/split/license allowlist, complete categories, indices, projection counts.\n";
}
