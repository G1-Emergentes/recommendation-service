#pragma once
#include "recommendation/domain/OutfitPolicy.h"
#include <functional>
#include <utility>
namespace Json { class Value; }

namespace mirage::recommendation
{
using ToolCompletion = std::function<void(Json::Value)>;
using ToolExecutor = std::function<void(const std::string &, const Json::Value &, ToolCompletion)>;
struct ToolEvidence
{
    std::string name;
    std::vector<Garment> garments;
    unsigned matchedCount = 0;
    std::string outfitId;
    std::string explanation;
    std::vector<StyleReference> styleReferences;
    std::vector<FashionWebReference> webReferences;
};
enum class AgentStatus
{
    Generated,
    NotConfigured,
    Unavailable,
    InvalidResponse,
    RateLimited,
    ToolLimitExceeded,
    ToolUnavailable,
    Unauthorized
};
struct AgentResult
{
    AgentStatus status;
    std::vector<Outfit> outfits;
    std::string assistantMessage;
    // Internal model decision: never inferred by parsing the response prose.
    std::string action;
    std::vector<std::string> excludedColors;
    bool darkOnly = false;
    unsigned referenceOutfit = 0;
    std::optional<Category> changeCategory;
    std::vector<std::string> allowedColors;
    bool relaxDark = false;
    std::optional<std::string> styleUpdate;
    std::optional<std::string> occasionUpdate;
    std::string preferenceOperation = "NONE";
    std::string preferenceField = "NONE";
    std::vector<std::string> preferenceValues;
    std::vector<ToolEvidence> toolEvidence;
};

class RecommenderAgent
{
  public:
    virtual ~RecommenderAgent() = default;
    virtual std::string model() const = 0;
    virtual bool configured() const = 0;
    virtual void generate(const std::vector<Garment> &eligible,
                          const RecommendationParameters &parameters,
                          const RecommendationPreferences &preferences,
                          std::function<void(AgentResult)> done) const = 0;
    virtual void generateWithTools(const std::vector<Garment> &eligible,
        const RecommendationParameters &parameters,const RecommendationPreferences &preferences,
        ToolExecutor, std::function<void(AgentResult)> done) const
    {generate(eligible,parameters,preferences,std::move(done));}
};
} // namespace mirage::recommendation
