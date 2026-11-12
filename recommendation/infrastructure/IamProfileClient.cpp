#include "IamProfileClient.h"
#include <algorithm>
#include <cctype>
#include <drogon/HttpClient.h>
#include <regex>
#include <set>
#include <stdexcept>
#include <utility>

namespace mirage::recommendation
{
namespace
{
bool readList(const Json::Value &body, const char *name, std::vector<std::string> &output)
{
    const auto &list = body[name];
    if (!list.isArray() || list.size() > 50)
        return false;
    std::set<std::string> seen;
    for (const auto &item : list)
    {
        if (!item.isString())
            return false;
        auto value = item.asString();
        if (value.empty() || value.size() > 80 || value.find('\0') != std::string::npos)
            return false;
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;
        value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return c < 128 ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
        });
        if (seen.insert(value).second)
            output.push_back(std::move(value));
    }
    return true;
}
} // namespace

IamProfileClient::IamProfileClient(std::string baseUrl, double timeoutSeconds)
    : baseUrl_(std::move(baseUrl)), timeoutSeconds_(timeoutSeconds)
{
    static const std::regex origin(
        R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$|^http://(127\.0\.0\.1|localhost):[0-9]{1,5}$)");
    if (!std::regex_match(baseUrl_, origin) || !(timeoutSeconds_ >= 1 && timeoutSeconds_ <= 30))
        throw std::invalid_argument("Invalid IAM origin or timeout for Profile");
}

void IamProfileClient::getPreferences(const UserAccess &access,
                                      std::function<void(ProfileResult)> done) const
{ preferencesRequest(access,nullptr,std::move(done)); }

void IamProfileClient::updatePreferences(const UserAccess &access,
    const RecommendationPreferences &preferences,std::function<void(ProfileResult)> done) const
{ preferencesRequest(access,&preferences,std::move(done)); }

void IamProfileClient::preferencesRequest(const UserAccess &access,
    const RecommendationPreferences *update,std::function<void(ProfileResult)> done) const
{
    static const std::regex uuid(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    if (!std::regex_match(access.userId, uuid) || access.accessToken.empty())
    {
        done({ProfileStatus::Unauthorized, {}});
        return;
    }
    auto client = drogon::HttpClient::newHttpClient(baseUrl_, nullptr, false, true);
    auto request = drogon::HttpRequest::newHttpRequest();
    if(update)
    {
        Json::Value body;
        const auto list=[&](const char *key,const std::vector<std::string> &values) {
            body[key]=Json::Value(Json::arrayValue);
            for(const auto &value:values) body[key].append(value);
        };
        list("preferredStyles",update->preferredStyles); list("preferredColors",update->preferredColors);
        list("avoidedColors",update->avoidedColors); list("preferredCategories",update->preferredCategories);
        request=drogon::HttpRequest::newHttpJsonRequest(body);
    }
    request->setMethod(update?drogon::Put:drogon::Get);
    request->setPath("/api/v1/profiles/" + access.userId + "/preferences");
    request->addHeader("Authorization", "Bearer " + access.accessToken);
    request->addHeader("Accept", "application/json");
    client->sendRequest(
        request,
        [client, done = std::move(done)](drogon::ReqResult result,
                                         const drogon::HttpResponsePtr &response) {
            if (result != drogon::ReqResult::Ok || !response)
            {
                done({ProfileStatus::Unavailable, {}});
                return;
            }
            const auto status = response->statusCode();
            if (status == drogon::k401Unauthorized || status == drogon::k403Forbidden)
            {
                done({ProfileStatus::Unauthorized, {}});
                return;
            }
            if (status == drogon::k404NotFound)
            {
                done({ProfileStatus::Missing, {}});
                return;
            }
            if (status != drogon::k200OK)
            {
                done({ProfileStatus::Unavailable, {}});
                return;
            }
            const auto body = response->getJsonObject();
            RecommendationPreferences preferences;
            if (!body || !body->isObject() ||
                !readList(*body, "preferredStyles", preferences.preferredStyles) ||
                !readList(*body, "preferredColors", preferences.preferredColors) ||
                !readList(*body, "avoidedColors", preferences.avoidedColors) ||
                !readList(*body, "preferredCategories", preferences.preferredCategories))
            {
                done({ProfileStatus::Unavailable, {}});
                return;
            }
            for (const auto &color : preferences.preferredColors)
                if (std::find(preferences.avoidedColors.begin(), preferences.avoidedColors.end(),
                              color) != preferences.avoidedColors.end())
                {
                    done({ProfileStatus::Unavailable, {}});
                    return;
                }
            done({ProfileStatus::Found, std::move(preferences)});
        },
        timeoutSeconds_);
}
} // namespace mirage::recommendation
