#pragma once
#include "recommendation/application/RecommendationRepository.h"

namespace mirage::recommendation
{
class SupabaseRecommendationRepository final : public RecommendationRepository
{
  public:
    SupabaseRecommendationRepository(std::string url, std::string secretKey, std::string authUrl);
    void chat(const UserAccess &, const std::string &, Json::Value, HistoryCompletion) const override;
    void create(const UserAccess &, Json::Value, HistoryCompletion) const override;
    void list(const UserAccess &, unsigned, const std::string &, std::optional<bool>,
              HistoryCompletion) const override;
    void find(const UserAccess &, const std::string &, HistoryCompletion) const override;
    void favorite(const UserAccess &, const std::string &, bool, HistoryCompletion) const override;
    void reject(const UserAccess &, const std::string &, const std::string &,
                HistoryCompletion) const override;

  private:
    void rpc(const UserAccess &, const std::string &name, Json::Value body, bool nullable,
             HistoryCompletion) const;
    std::string url_;
    std::string key_;
};
} // namespace mirage::recommendation
