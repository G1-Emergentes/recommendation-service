#pragma once
#include "recommendation/application/RecommenderAgent.h"
#include <memory>

namespace mirage::recommendation
{
class NvidiaRecommenderAgent final : public RecommenderAgent
{
  public:
    explicit NvidiaRecommenderAgent(std::string apiKey,
                                    std::string model = "nvidia/nemotron-3.5-lightning-30b-a3b",
                                    std::string origin = "https://integrate.api.nvidia.com",
                                    double timeoutSeconds = 30);
    std::string model() const override
    {
        return model_;
    }
    bool configured() const override
    {
        return !key_.empty();
    }
    void generate(const std::vector<Garment> &, const RecommendationParameters &,
                  const RecommendationPreferences &,
                  std::function<void(AgentResult)>) const override;
    void generateWithTools(const std::vector<Garment> &, const RecommendationParameters &,
        const RecommendationPreferences &, ToolExecutor, std::function<void(AgentResult)>) const override;

  private:
    struct ToolRun;
    void generateImpl(const std::vector<Garment> &, const RecommendationParameters &,
        const RecommendationPreferences &, ToolExecutor, std::function<void(AgentResult)>) const;
    void complete(std::shared_ptr<ToolRun>) const;
    std::string key_;
    std::string model_;
    std::string origin_;
    double timeoutSeconds_;
};
} // namespace mirage::recommendation
