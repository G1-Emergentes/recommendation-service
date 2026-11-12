#pragma once
#include "recommendation/application/RecommenderAgent.h"
#include <json/json.h>
#include <chrono>
#include <memory>

namespace mirage::recommendation
{
inline constexpr const char *fashionWebRevision="f834484cecaed20d08fea97be7781426063a7275";
FashionWebReference readFashionWebReference(const Json::Value &);
Json::Value fashionWebReferenceJson(const FashionWebReference &);
Json::Value fashionWebReferencesJson(const std::vector<FashionWebReference> &);

// Public text only. Neither the query nor the caller's credentials leave this service.
class FashionWebSearch
{
  public:
    explicit FashionWebSearch(std::string origin="https://raw.githubusercontent.com", double timeout=5);
    static bool validFilters(const Json::Value &);
    static std::vector<FashionWebReference> parseCsv(const std::string &,const std::string &audience);
    void search(const Json::Value &,ToolCompletion);
  private:
    struct Cache {std::vector<FashionWebReference> items;std::chrono::steady_clock::time_point expires{};};
    std::string origin_;
    double timeout_;
    Cache cache_;
    bool loading_=false;
    std::chrono::steady_clock::time_point retryAfter_{};
    std::vector<std::function<void(bool)>> waiters_;
    void load(unsigned group,std::shared_ptr<std::vector<FashionWebReference>>);
    void loaded(bool,std::vector<FashionWebReference> = {});
    void results(const Json::Value &,ToolCompletion) const;
};
}
