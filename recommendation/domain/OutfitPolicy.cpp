#include "OutfitPolicy.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace mirage::recommendation
{
namespace
{
bool overlaps(const std::vector<std::string> &values, const std::vector<std::string> &preferred)
{
    return std::any_of(values.begin(), values.end(), [&](const auto &value) {
        return std::find(preferred.begin(), preferred.end(), value) != preferred.end();
    });
}
} // namespace

std::vector<Garment> eligibleGarments(const std::vector<Garment> &garments,
                                      const RecommendationParameters &parameters,
                                      const RecommendationPreferences &preferences)
{
    if (parameters.maxResults < 1 || parameters.maxResults > 3)
        throw std::invalid_argument("max_results must be between 1 and 3");

    std::vector<Garment> eligible;
    std::unordered_set<std::string> seen;
    const std::unordered_set<std::string> excluded(parameters.excludedGarments.begin(),
                                                   parameters.excludedGarments.end());
    auto rankedGarments = garments;
    const auto preferenceScore = [&](const Garment &garment) {
        return static_cast<int>(parameters.style.empty() &&
                                overlaps(garment.styles, preferences.preferredStyles)) +
               static_cast<int>(overlaps(garment.colors, preferences.preferredColors));
    };
    std::stable_sort(
        rankedGarments.begin(), rankedGarments.end(),
        [&](const auto &a, const auto &b) { return preferenceScore(a) > preferenceScore(b); });
    for (const auto &garment : rankedGarments)
    {
        if (garment.id.empty() || !seen.insert(garment.id).second)
            throw std::invalid_argument("Wardrobe contains an empty or duplicate ID");
        if (!garment.available || excluded.count(garment.id))
            continue;
        if (overlaps(garment.colors, preferences.avoidedColors))
            continue;
        if (!parameters.style.empty() && !garment.styles.empty() &&
            std::find(garment.styles.begin(), garment.styles.end(), parameters.style) ==
                garment.styles.end())
            continue;
        eligible.push_back(garment);
    }
    return eligible;
}

bool validOutfit(const Outfit &outfit, const std::vector<Garment> &eligible)
{
    if (outfit.garmentIds.size() < 2 || outfit.garmentIds.size() > 3)
        return false;
    std::unordered_set<std::string> seen;
    unsigned tops = 0, bottoms = 0, pieces = 0, shoes = 0;
    for (const auto &id : outfit.garmentIds)
    {
        if (!seen.insert(id).second)
            return false;
        const auto found = std::find_if(eligible.begin(), eligible.end(),
                                        [&](const auto &garment) { return garment.id == id; });
        if (found == eligible.end() || !found->available)
            return false;
        switch (found->category)
        {
        case Category::Top:
            ++tops;
            break;
        case Category::Bottom:
            ++bottoms;
            break;
        case Category::OnePiece:
            ++pieces;
            break;
        case Category::Footwear:
            ++shoes;
            break;
        default:
            return false;
        }
    }
    return (tops == 1 && bottoms == 1 && shoes == 1 && pieces == 0) ||
           (pieces == 1 && shoes == 1 && tops == 0 && bottoms == 0);
}

std::vector<Outfit> suggestOutfits(const std::vector<Garment> &garments,
                                   const RecommendationParameters &parameters,
                                   const RecommendationPreferences &preferences)
{
    std::vector<std::string> tops, bottoms, pieces, shoes;
    for (const auto &garment : eligibleGarments(garments, parameters, preferences))
    {
        switch (garment.category)
        {
        case Category::Top:
            tops.push_back(garment.id);
            break;
        case Category::Bottom:
            bottoms.push_back(garment.id);
            break;
        case Category::OnePiece:
            pieces.push_back(garment.id);
            break;
        case Category::Footwear:
            shoes.push_back(garment.id);
            break;
        default:
            break;
        }
    }

    std::vector<Outfit> results;
    // Baseline order is deterministic; semantic ranking is added separately.
    const auto slots = std::max(tops.size(), pieces.size());
    for (std::size_t i = 0; i < slots; ++i)
    {
        if (i < tops.size())
            for (const auto &bottom : bottoms)
                for (const auto &shoe : shoes)
                {
                    results.push_back({{tops[i], bottom, shoe}});
                    if (results.size() == parameters.maxResults)
                        return results;
                }
        if (i < pieces.size())
            for (const auto &shoe : shoes)
            {
                results.push_back({{pieces[i], shoe}});
                if (results.size() == parameters.maxResults)
                    return results;
            }
    }
    return results;
}
} // namespace mirage::recommendation
