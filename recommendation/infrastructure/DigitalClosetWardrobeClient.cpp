#include "DigitalClosetWardrobeClient.h"
#include "GarmentJson.h"
#include <drogon/HttpClient.h>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>

namespace mirage::recommendation
{
namespace
{
using Response = std::function<void(WardrobeStatus, const Json::Value &)>;
void call(const std::string &origin, double timeout, const UserAccess &access,
          const std::string &path, const Json::Value &body, const std::string &cursor, Response done)
{
    auto client=drogon::HttpClient::newHttpClient(origin,nullptr,false,true);
    auto req=body.isNull()?drogon::HttpRequest::newHttpRequest():drogon::HttpRequest::newHttpJsonRequest(body);
    req->setMethod(body.isNull()?drogon::Get:drogon::Post);
    req->setPath(path);
    if(body.isNull())
    {
        req->setQueryParameter("limit","100");
        if(!cursor.empty()) req->setQueryParameter("cursor",cursor);
    }
    req->addHeader("Authorization","Bearer "+access.accessToken);
    req->addHeader("Accept","application/json");
    client->sendRequest(req,[client,done=std::move(done)](drogon::ReqResult state,const drogon::HttpResponsePtr &response){
        if(state!=drogon::ReqResult::Ok || !response){done(WardrobeStatus::Unavailable,{});return;}
        const auto status=response->statusCode();
        if(status==drogon::k401Unauthorized || status==drogon::k403Forbidden)
        {done(WardrobeStatus::Unauthorized,{});return;}
        if(status!=drogon::k200OK){done(WardrobeStatus::Unavailable,{});return;}
        if(response->body().size()>2*1024*1024){done(WardrobeStatus::InvalidResponse,{});return;}
        const auto value=response->getJsonObject();
        if(!value || !value->isObject()){done(WardrobeStatus::InvalidResponse,{});return;}
        done(WardrobeStatus::Found,*value);
    },timeout);
}
struct Pages
{
    std::vector<Garment> garments;
    std::set<std::string> ids,cursors;
    unsigned count=0;
};
void page(const std::string &origin, double timeout, UserAccess access, std::string cursor,
          std::shared_ptr<Pages> pages, WardrobeCompletion done)
{
    call(origin,timeout,access,"/api/v1/garments",{},cursor,
         [=](WardrobeStatus status,const Json::Value &body) mutable {
        if(status!=WardrobeStatus::Found){done({status,{}});return;}
        std::string next;
        try
        {
            if(!body["items"].isArray() || body["items"].size()>100 || !body.isMember("next_cursor") ||
               (!body["next_cursor"].isNull() && !body["next_cursor"].isString()))
                throw std::invalid_argument("Invalid wardrobe page");
            for(const auto &item:body["items"])
            {
                auto g=readGarment(item);
                if(!pages->ids.insert(g.id).second) throw std::invalid_argument("Duplicate garment");
                pages->garments.push_back(std::move(g));
            }
            if(!body["next_cursor"].isNull())
            {
                next=body["next_cursor"].asString();
                if(next.empty() || next.size()>2048 || next.find_first_of("\r\n\0",0,3)!=std::string::npos ||
                   body["items"].empty() || !pages->cursors.insert(next).second)
                    throw std::invalid_argument("Invalid or repeated cursor");
            }
        }
        catch(const std::exception &){done({WardrobeStatus::InvalidResponse,{}});return;}
        ++pages->count;
        // Explicit bound until semantic candidate retrieval can handle larger closets.
        if(pages->garments.size()>1000 || (!next.empty() && pages->count>=100))
        {done({WardrobeStatus::LimitExceeded,{}});return;}
        if(next.empty()){done({WardrobeStatus::Found,std::move(pages->garments)});return;}
        page(origin,timeout,std::move(access),next,pages,std::move(done));
    });
}
}
DigitalClosetWardrobeClient::DigitalClosetWardrobeClient(std::string origin,double timeoutSeconds)
    :origin_(std::move(origin)),timeoutSeconds_(timeoutSeconds)
{
    static const std::regex originPattern(R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$|^http://(127\.0\.0\.1|localhost):[0-9]{1,5}$)");
    if(!std::regex_match(origin_,originPattern) || !(timeoutSeconds_>=1 && timeoutSeconds_<=30))
        throw std::invalid_argument("Invalid Digital Closet origin or timeout");
}
void DigitalClosetWardrobeClient::listForUser(const UserAccess &access,WardrobeCompletion done) const
{
    page(origin_,timeoutSeconds_,access,"",std::make_shared<Pages>(),std::move(done));
}
void DigitalClosetWardrobeClient::resolve(const UserAccess &access,const std::vector<std::string> &ids,
                                        WardrobeCompletion done) const
{
    if(ids.empty() || ids.size()>100) throw std::invalid_argument("Resolve requires 1-100 garment IDs");
    Json::Value body;body["ids"]=Json::Value(Json::arrayValue);
    for(const auto &id:ids) body["ids"].append(id);
    call(origin_,timeoutSeconds_,access,"/api/v1/garments/resolve",body,"",
         [ids,done=std::move(done)](WardrobeStatus status,const Json::Value &body){
        if(status!=WardrobeStatus::Found){done({status,{}});return;}
        std::vector<Garment> garments;
        bool changed=false;
        try
        {
            if(!body["results"].isArray() || body["results"].size()!=ids.size())
                throw std::invalid_argument("Incomplete resolve");
            for(Json::ArrayIndex i=0;i<body["results"].size();++i)
            {
                const auto &entry=body["results"][i];
                if(!entry.isObject() || !entry["id"].isString() || entry["id"].asString()!=ids[i] ||
                   !entry["status"].isString() || !entry.isMember("garment"))
                    throw std::invalid_argument("Mismatched resolve result");
                const auto state=entry["status"].asString();
                if(state=="AVAILABLE")
                {
                    auto g=readGarment(entry["garment"]);
                    if(g.id!=ids[i] || !g.available) throw std::invalid_argument("Inconsistent resolve");
                    garments.push_back(std::move(g));
                }
                else if((state=="UNAVAILABLE" || state=="NOT_FOUND") && entry["garment"].isNull()) changed=true;
                else throw std::invalid_argument("Unknown resolve status");
            }
        }
        catch(const std::exception &){done({WardrobeStatus::InvalidResponse,{}});return;}
        done({changed?WardrobeStatus::Changed:WardrobeStatus::Found,std::move(garments)});
    });
}
}
