#pragma once
#include "recommendation/domain/OutfitPolicy.h"
#include <json/json.h>
namespace mirage::recommendation
{
StyleReference readStyleReference(const Json::Value &);
Json::Value styleReferenceJson(const StyleReference &);
Json::Value styleReferencesJson(const std::vector<StyleReference> &);
}
