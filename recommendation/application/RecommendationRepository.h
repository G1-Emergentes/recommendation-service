#pragma once
#include "ProfileClient.h"
#include <json/json.h>
#include <optional>

namespace mirage::recommendation
{
struct HistoryResult
{
    int status;
    Json::Value body;
};
using HistoryCompletion = std::function<void(HistoryResult)>;

class RecommendationRepository
{
  public:
    virtual ~RecommendationRepository() = default;
    virtual void chat(const UserAccess &, const std::string &operation, Json::Value,
                      HistoryCompletion) const = 0;
    virtual void create(const UserAccess &, Json::Value snapshot, HistoryCompletion) const = 0;
    virtual void list(const UserAccess &, unsigned limit, const std::string &cursor,
                      std::optional<bool> favorite, HistoryCompletion) const = 0;
    virtual void find(const UserAccess &, const std::string &id, HistoryCompletion) const = 0;
    virtual void favorite(const UserAccess &, const std::string &id, bool favorite,
                          HistoryCompletion) const = 0;
    virtual void reject(const UserAccess &, const std::string &id, const std::string &reason,
                        HistoryCompletion) const = 0;
};
} // namespace mirage::recommendation
