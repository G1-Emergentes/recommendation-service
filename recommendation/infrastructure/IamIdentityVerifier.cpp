#include "IamIdentityVerifier.h"
#include <algorithm>
#include <cctype>
#include <drogon/HttpClient.h>
#include <regex>
#include <stdexcept>
#include <utility>

namespace mirage::recommendation
{
IamIdentityVerifier::IamIdentityVerifier(std::string baseUrl, double timeoutSeconds)
    : baseUrl_(std::move(baseUrl)), timeoutSeconds_(timeoutSeconds)
{
    // Access tokens may travel over plain HTTP only to a local development IAM.
    static const std::regex origin(
        R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$|^http://(127\.0\.0\.1|localhost):[0-9]{1,5}$)");
    if (!std::regex_match(baseUrl_, origin))
        throw std::invalid_argument("IAM_BASE_URL must be an HTTPS origin or local HTTP origin "
                                    "with port; no path or trailing slash");
    if (!(timeoutSeconds_ >= 1.0 && timeoutSeconds_ <= 30.0))
        throw std::invalid_argument("IAM timeout must be between 1 and 30 seconds");
}

void IamIdentityVerifier::verify(const std::string &accessToken,
                                 std::function<void(IdentityResult)> done) const
{
    auto client = drogon::HttpClient::newHttpClient(baseUrl_, nullptr, false, true);
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);
    request->setPath("/api/v1/auth/me");
    request->addHeader("Authorization", "Bearer " + accessToken);
    request->addHeader("Accept", "application/json");
    // Never cache success: IAM checks active sessions on every request.
    client->sendRequest(
        request,
        [client, done = std::move(done)](drogon::ReqResult result,
                                         const drogon::HttpResponsePtr &response) {
            if (result != drogon::ReqResult::Ok || !response)
            {
                done({IdentityStatus::Unavailable, {}});
                return;
            }
            const auto status = response->statusCode();
            if (status == drogon::k400BadRequest || status == drogon::k401Unauthorized ||
                status == drogon::k403Forbidden)
            {
                done({IdentityStatus::Rejected, {}});
                return;
            }
            if (status != drogon::k200OK)
            {
                done({IdentityStatus::Unavailable, {}});
                return;
            }
            const auto body = response->getJsonObject();
            if (!body || !body->isObject() || !(*body)["id"].isString())
            {
                done({IdentityStatus::Unavailable, {}});
                return;
            }
            auto userId = (*body)["id"].asString();
            static const std::regex uuid(
                "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
            if (!std::regex_match(userId, uuid))
            {
                done({IdentityStatus::Unavailable, {}});
                return;
            }
            std::transform(userId.begin(), userId.end(), userId.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            done({IdentityStatus::Verified, std::move(userId)});
        },
        timeoutSeconds_);
}
} // namespace mirage::recommendation
