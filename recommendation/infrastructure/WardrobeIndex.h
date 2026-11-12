#pragma once
#include "recommendation/application/RecommendationRepository.h"
#include "recommendation/application/WardrobeClient.h"
#include <drogon/orm/DbClient.h>
#include <chrono>
#include <mutex>
#include <set>

namespace mirage::recommendation
{
// Backend-only: no connection information crosses the HTTP/model boundary.
class WardrobeIndex
{
  public:
    WardrobeIndex(const WardrobeClient &,std::string databaseUrl,std::string embeddingKey,
                  std::string model,std::string origin,double timeout);
    void sync(UserAccess,HistoryCompletion) const;
    void search(UserAccess,Json::Value filters,HistoryCompletion) const;
    void candidates(UserAccess,RecommendationParameters,RecommendationPreferences,WardrobeCompletion) const;
    void searchStyleKnowledge(UserAccess,Json::Value,HistoryCompletion) const;
    void searchOutfitExamples(UserAccess,Json::Value,HistoryCompletion) const;
    static bool validExampleFilters(const Json::Value &);
    static bool validStyleFilters(const Json::Value &);
    static bool validFilters(const Json::Value &,bool requireQuery);
  private:
    void retrieve(UserAccess,Json::Value,Json::Value,HistoryCompletion) const;
    struct SyncRun;
    using Vectors=std::vector<std::vector<double>>;
    using Embedded=std::function<void(bool,Vectors)>;
    void embed(std::vector<std::string>,std::string,Embedded,double timeout) const;
    void read(const UserAccess &,HistoryCompletion) const;
    void write(std::shared_ptr<SyncRun>) const;
    void nextBatch(std::shared_ptr<SyncRun>) const;
    void actor(const UserAccess &,std::function<void(std::shared_ptr<drogon::orm::Transaction>)>) const;
    const WardrobeClient &wardrobe_;
    std::shared_ptr<drogon::orm::DbClient> db_;
    std::string key_,model_,origin_;
    double timeout_;
    mutable std::mutex mutex_;
    mutable std::set<std::string> busy_;
};
}
