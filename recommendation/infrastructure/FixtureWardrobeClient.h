#pragma once
#include "recommendation/application/WardrobeClient.h"

namespace mirage::recommendation
{
class FixtureWardrobeClient final : public WardrobeClient
{
  public:
    explicit FixtureWardrobeClient(const std::string &path);
    void listForUser(const UserAccess &, WardrobeCompletion done) const override;
    void resolve(const UserAccess &, const std::vector<std::string> &, WardrobeCompletion) const override;

  private:
    std::vector<Garment> garments_;
};
} // namespace mirage::recommendation
