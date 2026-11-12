#pragma once
#include "recommendation/domain/OutfitPolicy.h"
#include <json/json.h>

namespace mirage::recommendation
{
// Source read-model only. Never carries a signed URL, token or account details.
Garment readGarment(const Json::Value &);
Json::Value garmentJson(const Garment &);
Json::Value garmentsJson(const std::vector<Garment> &);
}
