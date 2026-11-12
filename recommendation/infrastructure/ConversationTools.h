#pragma once
#include "recommendation/application/RecommenderAgent.h"
#include "recommendation/application/RecommendationRepository.h"
#include "recommendation/application/WardrobeClient.h"

namespace mirage::recommendation
{
class WardrobeIndex;
class FashionWebSearch;
Json::Value conversationToolDefinitions();
ToolExecutor conversationToolExecutor(UserAccess, const WardrobeClient &, const RecommendationRepository &, const WardrobeIndex *index=nullptr, FashionWebSearch *web=nullptr);
}
