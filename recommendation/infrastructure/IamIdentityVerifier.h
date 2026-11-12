#pragma once
#include "recommendation/application/IdentityVerifier.h"

namespace mirage::recommendation
{
class IamIdentityVerifier final : public IdentityVerifier
{
  public:
    explicit IamIdentityVerifier(std::string baseUrl, double timeoutSeconds = 25.0);
    void verify(const std::string &accessToken,
                std::function<void(IdentityResult)> done) const override;

  private:
    std::string baseUrl_;
    double timeoutSeconds_;
};
} // namespace mirage::recommendation
