#pragma once
#include "ProfileClient.h"
#include "RecommenderAgent.h"
#include "WardrobeClient.h"

namespace mirage::recommendation
{
enum class RecommendationStatus
{
    Generated,
    InsufficientGarments,
    WardrobeUnavailable,
    WardrobeChanged,
    WardrobeLimitExceeded,
    ProfileRequired,
    Unauthorized,
    ProfileUnavailable,
    ProfileUpdateUncertain,
    ModelNotConfigured,
    ModelUnavailable,
    ModelInvalidResponse,
    ModelRateLimited,
    ToolLimitExceeded,
    ToolUnavailable
};

struct RecommendationResult
{
    RecommendationStatus status;
    std::vector<Outfit> outfits;
    RecommendationPreferences preferences;
    std::string engine = "category_rules";
    std::string model;
    bool llmUsed = false;
    std::string assistantMessage;
    std::vector<Garment> garments;
    std::vector<std::string> excludedColors;
    bool darkOnly = false;
    std::string conversationStyle;
    std::string conversationOccasion;
    bool preferencesUpdated = false;
    std::vector<StyleReference> styleReferences;
    std::vector<OutfitExample> outfitExamples;
    std::vector<FashionWebReference> webReferences;
};

class RecommendationService
{
  public:
    using CandidateLoader = std::function<void(UserAccess, RecommendationParameters, RecommendationPreferences, WardrobeCompletion)>;
    using KnowledgeCompletion = std::function<void(bool,std::vector<StyleReference>)>;
    using KnowledgeLoader = std::function<void(UserAccess,RecommendationParameters,KnowledgeCompletion)>;
    using ExampleCompletion = std::function<void(bool,std::vector<OutfitExample>)>;
    using ExampleLoader = std::function<void(UserAccess,RecommendationParameters,std::vector<Garment>,ExampleCompletion)>;
    RecommendationService(const WardrobeClient &wardrobe, const ProfileClient &profile,
                          const RecommenderAgent *agent = nullptr,
                          std::function<ToolExecutor(const UserAccess &)> tools = {},
                          CandidateLoader candidates = {}, KnowledgeLoader knowledge = {}, ExampleLoader examples = {})
        : wardrobe_(wardrobe), profile_(profile), agent_(agent), tools_(std::move(tools)), candidates_(std::move(candidates)), knowledge_(std::move(knowledge)), examples_(std::move(examples))
    {
    }
    void generate(UserAccess access, RecommendationParameters parameters,
                  std::function<void(RecommendationResult)> done) const;

  private:
    void generateFromWardrobe(UserAccess,RecommendationParameters,RecommendationPreferences,std::function<void(RecommendationResult)>) const;
    const WardrobeClient &wardrobe_;
    const ProfileClient &profile_;
    const RecommenderAgent *agent_;
    std::function<ToolExecutor(const UserAccess &)> tools_;
    CandidateLoader candidates_;
    KnowledgeLoader knowledge_;
    ExampleLoader examples_;
};
} // namespace mirage::recommendation
