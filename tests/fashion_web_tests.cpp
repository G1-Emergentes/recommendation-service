#include "recommendation/infrastructure/FashionWebSearch.h"
#include <iostream>
#include <stdexcept>
using namespace mirage::recommendation;
void check(bool ok){if(!ok)throw std::runtime_error("Fashion web guard failed");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected);}
int main()
{
    const std::string header="outfitID,items,look,season,occasion\r\n";
    const auto csv=header+"b1,\"p1,p2\",\"Quiet luxury, \"\"knit\"\" layered\nwith trousers\",Autumn/Winter,School\r\n";
    auto rows=FashionWebSearch::parseCsv("\xef\xbb\xbf"+csv,"MALE");
    check(rows.size()==1 && rows[0].content.find("\"knit\"")!=std::string::npos);
    check(rows[0].sourceUrl.find("#L2")!=std::string::npos);
    auto record=fashionWebReferenceJson(rows[0]);check(readFashionWebReference(record).id==rows[0].id);
    for(const auto &key:{"source_url","license","source_revision","id"}) {
        auto bad=record;bad[key]="https://attacker.example";rejects([&]{readFashionWebReference(bad);});
    }
    rejects([&]{FashionWebSearch::parseCsv(header+"b1,x,\"unclosed,Spring,Daily","MALE");});
    rejects([&]{FashionWebSearch::parseCsv(header+"b1,x,valid,Spring,Daily\nb1,x,duplicate,Spring,Daily","MALE");});
    rejects([&]{FashionWebSearch::parseCsv(header+"b1,x,content,Spring,Daily,extra","MALE");});
    rejects([&]{FashionWebSearch::parseCsv("<html>changed</html>","MALE");});
    rejects([&]{FashionWebSearch::parseCsv(std::string(262145,'x'),"MALE");});
    rejects([&]{FashionWebSearch bad("https://attacker.example");});
    rejects([&]{FashionWebSearch bad("http://localhost:80/path");});
    Json::Value args;args["query"]="quiet luxury";check(FashionWebSearch::validFilters(args));
    for(const auto &query:{"x"," ","https://attacker.example","correo@example.com","quiet luxury 2026"}) {
        auto bad=args;bad["query"]=query;check(!FashionWebSearch::validFilters(bad));
    }
    auto bad=args;bad["user_id"]="foreign";check(!FashionWebSearch::validFilters(bad));
    bad=args;bad["limit"]=4;check(!FashionWebSearch::validFilters(bad));
    bad=args;bad["audience"]="CHILD";check(!FashionWebSearch::validFilters(bad));
    std::cout<<"PASS: CSV quoting, multiline/BOM, source provenance, bounds, schemas and URL guards\n";
}
