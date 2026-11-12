#pragma once
#include "recommendation/application/ProfileClient.h"

namespace mirage::recommendation
{
class IamProfileClient final : public ProfileClient
{
  public:
    explicit IamProfileClient(std::string baseUrl, double timeoutSeconds = 25.0);
    void getPreferences(const UserAccess &access,
                        std::function<void(ProfileResult)> done) const override;
    void updatePreferences(const UserAccess &access, const RecommendationPreferences &preferences,
                           std::function<void(ProfileResult)> done) const override;

  private:
    void preferencesRequest(const UserAccess &, const RecommendationPreferences *,
                            std::function<void(ProfileResult)>) const;
    std::string baseUrl_;
    double timeoutSeconds_;
};
} // namespace mirage::recommendation
