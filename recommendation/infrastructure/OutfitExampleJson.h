#pragma once
#include "recommendation/domain/OutfitPolicy.h"
#include <json/json.h>
namespace mirage::recommendation
{
OutfitExample readOutfitExample(const Json::Value &);
Json::Value outfitExampleJson(const OutfitExample &);
Json::Value outfitExamplesJson(const std::vector<OutfitExample> &);
}
