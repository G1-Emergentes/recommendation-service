#pragma once
#include "ProfileClient.h"
#include <functional>

namespace mirage::recommendation
{
enum class WardrobeStatus { Found, Unauthorized, Unavailable, InvalidResponse, Changed, LimitExceeded };

struct WardrobeResult
{
    WardrobeStatus status;
    std::vector<Garment> garments;
};

using WardrobeCompletion = std::function<void(WardrobeResult)>;

class WardrobeClient
{
  public:
    virtual ~WardrobeClient() = default;
    virtual void listForUser(const UserAccess &access, WardrobeCompletion done) const = 0;
    virtual void resolve(const UserAccess &access, const std::vector<std::string> &ids,
                         WardrobeCompletion done) const = 0;
};
} // namespace mirage::recommendation
