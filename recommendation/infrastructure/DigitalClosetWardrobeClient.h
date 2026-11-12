#pragma once
#include "recommendation/application/WardrobeClient.h"

namespace mirage::recommendation
{
class DigitalClosetWardrobeClient final : public WardrobeClient
{
  public:
    explicit DigitalClosetWardrobeClient(std::string origin, double timeoutSeconds = 10);
    void listForUser(const UserAccess &, WardrobeCompletion) const override;
    void resolve(const UserAccess &, const std::vector<std::string> &, WardrobeCompletion) const override;
  private:
    std::string origin_;
    double timeoutSeconds_;
};
}
