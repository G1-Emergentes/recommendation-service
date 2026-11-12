#pragma once
#include "recommendation/application/IdentityVerifier.h"
#include "recommendation/application/RecommendationRepository.h"
#include "recommendation/application/RecommendationService.h"

namespace mirage::recommendation
{
class WardrobeIndex;
void registerRecommendationHttp(const std::string &mode, const RecommendationService *service,
                                const IdentityVerifier &identity,
                                const RecommendationRepository &history,
                                const RecommenderAgent *agent = nullptr, const WardrobeIndex *index=nullptr);
}
