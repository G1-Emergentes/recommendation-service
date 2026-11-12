#pragma once
#include <functional>
#include <string>

namespace mirage::recommendation
{
enum class IdentityStatus
{
    Verified,
    Rejected,
    Unavailable
};

struct IdentityResult
{
    IdentityStatus status;
    std::string userId;
};

class IdentityVerifier
{
  public:
    virtual ~IdentityVerifier() = default;
    virtual void verify(const std::string &accessToken,
                        std::function<void(IdentityResult)> done) const = 0;
};
} // namespace mirage::recommendation
