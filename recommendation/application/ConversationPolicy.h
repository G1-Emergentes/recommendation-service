#pragma once
#include "RecommenderAgent.h"

namespace mirage::recommendation
{
std::vector<std::string> explicitExcludedColors(const std::string &);
bool requestsAlternative(const std::string &);
bool requestsNewOutfit(const std::string &);
bool explicitRelaxDarkRequest(const std::string &);
bool explicitDarkRequest(const std::string &);
bool mayRelaxConstraints(const std::string &);
std::optional<Outfit> findOutfitCandidate(const std::vector<Garment> &,const RecommendationParameters &);
bool hasOutfitCandidate(const std::vector<Garment> &,const RecommendationParameters &);
bool confirmedDark(const Garment &);
bool applyConversationMemory(AgentResult &, RecommendationParameters &);
bool applyPreferenceChange(const AgentResult &, const std::string &, RecommendationPreferences &);
std::string preferencesReply(const RecommendationPreferences &);
bool validConversationDecision(const AgentResult &, const RecommendationParameters &,
                               const std::vector<Garment> &);
std::string conversationReply(const AgentResult &, const RecommendationParameters &,
                              const std::vector<Garment> &);
}
