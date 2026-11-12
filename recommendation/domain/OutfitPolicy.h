#pragma once
#include <string>
#include <optional>
#include <vector>

namespace mirage::recommendation
{
enum class Category
{
    Top,
    Bottom,
    OnePiece,
    Outerwear,
    Footwear,
    Accessory
};

struct GarmentImage
{
    std::string id;
    unsigned version;
};

struct Garment
{
    std::string id;
    Category category;
    bool available;
    std::vector<std::string> styles;
    std::vector<std::string> colors;
    std::string name;
    std::string description;
    std::optional<std::string> subcategory;
    unsigned revision = 0;
    std::string updatedAt;
    std::optional<GarmentImage> image;
};

struct RecommendationPreferences
{
    std::vector<std::string> preferredStyles;
    std::vector<std::string> preferredColors;
    std::vector<std::string> avoidedColors;
    std::vector<std::string> preferredCategories;
};

struct Outfit
{
    std::vector<std::string> garmentIds;
    std::string explanation;
};

struct StyleReference
{
    std::string id,title,content,sourceUrl,license,attribution;
};

struct FashionWebReference
{
    std::string id, title, content, sourceUrl, audience, season, occasion;
};

struct ExampleItem
{
    std::string category,name;
    unsigned sourceItemIndex=0;
};
struct OutfitExample
{
    std::string id,sourceOutfitId,title,structure,content,sourceCommit,sourceArchiveHash,attribution;
    unsigned sourceItemsCount=0,omittedItemsCount=0;
    std::vector<ExampleItem> items;
};

struct RecommendationParameters
{
    std::string occasion;
    std::string style;
    std::vector<std::string> excludedGarments;
    unsigned maxResults = 3;
    std::string message;
    std::string conversationContext;
    std::vector<Outfit> previousOutfits;
    std::vector<std::string> excludedColors;
    bool darkOnly = false;
    std::string conversationStyle;
    std::string conversationOccasion;
    std::vector<StyleReference> styleKnowledge;
    std::vector<OutfitExample> outfitExamples;
};

std::vector<Garment> eligibleGarments(const std::vector<Garment> &garments,
                                      const RecommendationParameters &parameters,
                                      const RecommendationPreferences &preferences);
bool validOutfit(const Outfit &outfit, const std::vector<Garment> &eligible);

// Baseline: complete category combinations, not a learned compatibility score.
std::vector<Outfit> suggestOutfits(const std::vector<Garment> &garments,
                                   const RecommendationParameters &parameters,
                                   const RecommendationPreferences &preferences = {});
} // namespace mirage::recommendation
