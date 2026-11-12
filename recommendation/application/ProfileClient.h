#pragma once
#include "recommendation/domain/OutfitPolicy.h"
#include <functional>

namespace mirage::recommendation
{
struct UserAccess
{
    std::string userId;
    std::string accessToken;
};

enum class ProfileStatus
{
    Found,
    Missing,
    Unauthorized,
    Unavailable
};

struct ProfileResult
{
    ProfileStatus status;
    RecommendationPreferences preferences;
};

class ProfileClient
{
  public:
    virtual ~ProfileClient() = default;
    virtual void getPreferences(const UserAccess &access,
                                std::function<void(ProfileResult)> done) const = 0;
    virtual void updatePreferences(const UserAccess &, const RecommendationPreferences &,
                                   std::function<void(ProfileResult)> done) const
    { done({ProfileStatus::Unavailable,{}}); }
};
} // namespace mirage::recommendation
