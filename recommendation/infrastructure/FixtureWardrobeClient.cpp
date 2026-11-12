#include "FixtureWardrobeClient.h"
#include "GarmentJson.h"
#include <algorithm>
#include <json/json.h>
#include <fstream>
#include <map>
#include <stdexcept>

namespace mirage::recommendation
{
FixtureWardrobeClient::FixtureWardrobeClient(const std::string &path)
{
    std::ifstream input(path);
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;
    if (!input || !Json::parseFromStream(reader, input, &root, &errors) ||
        !root["items"].isArray() || !root.isMember("next_cursor") ||
        !root["next_cursor"].isNull())
        throw std::runtime_error("Invalid or incomplete local wardrobe fixture");

    for (const auto &item : root["items"]) garments_.push_back(readGarment(item));
    suggestOutfits(garments_, {});
}

void FixtureWardrobeClient::listForUser(const UserAccess &access, WardrobeCompletion done) const
{
    if (access.userId.empty() || access.accessToken.empty())
    { done({WardrobeStatus::Unauthorized, {}}); return; }
    done({WardrobeStatus::Found, garments_});
}
void FixtureWardrobeClient::resolve(const UserAccess &access, const std::vector<std::string> &ids,
                                  WardrobeCompletion done) const
{
    if (access.userId.empty() || access.accessToken.empty())
    { done({WardrobeStatus::Unauthorized, {}}); return; }
    std::vector<Garment> garments;
    for (const auto &id : ids)
    {
        const auto found=std::find_if(garments_.begin(),garments_.end(),[&](const auto &g){return g.id==id;});
        if(found==garments_.end() || !found->available)
        {done({WardrobeStatus::Changed,{}});return;}
        garments.push_back(*found);
    }
    done({WardrobeStatus::Found,std::move(garments)});
}
} // namespace mirage::recommendation
