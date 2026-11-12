#include "SupabaseRecommendationRepository.h"
#include <drogon/HttpClient.h>
#include <regex>
#include <stdexcept>
#include <utility>

namespace mirage::recommendation
{
namespace
{
HistoryResult failure(int status, const char *code, const char *message)
{
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    return {status, std::move(body)};
}
bool uuid(const Json::Value &value)
{
    static const std::regex pattern(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return value.isString() && std::regex_match(value.asString(), pattern);
}
bool outfit(const Json::Value &body)
{
    if (!body.isObject() || !uuid(body["id"]) || !uuid(body["request_id"]) ||
        !body["created_at"].isString() || !body["garment_ids"].isArray() ||
        body["garment_ids"].size() < 2 || body["garment_ids"].size() > 3 ||
        !body["explanation"].isString() || !body["is_favorite"].isBool() ||
        !body["status"].isString() ||
        (body["status"].asString() != "GENERATED" && body["status"].asString() != "REJECTED"))
        return false;
    for (const auto &id : body["garment_ids"])
        if (!uuid(id))
            return false;
    return true;
}
bool ownedContext(const Json::Value &body, const std::string &userId)
{
    return body.isObject() && body["user_id"].isString() && body["user_id"].asString() == userId &&
           uuid(body["request_id"]) && body["created_at"].isString() &&
           body["profile_preferences"].isObject() && body["parameters"].isObject() &&
           body["persisted"].isBool() && body["persisted"].asBool();
}
bool ownedRecord(const Json::Value &body, const std::string &userId)
{
    return ownedContext(body, userId) && outfit(body);
}
} // namespace

SupabaseRecommendationRepository::SupabaseRecommendationRepository(std::string url, std::string key,
                                                                   std::string authUrl)
    : url_(std::move(url)), key_(std::move(key))
{
    static const std::regex origin(
        R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$|^http://(127\.0\.0\.1|localhost):[0-9]{1,5}$)");
    if ((!url_.empty() && !std::regex_match(url_, origin)) ||
        (!authUrl.empty() && !std::regex_match(authUrl, origin)) ||
        key_.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Invalid Supabase connection configuration");
    if (!url_.empty() && (authUrl.empty() || url_ == authUrl))
        throw std::invalid_argument(
            "Recommendation requires a database separate from IAM_SUPABASE_URL");
    if (!key_.empty() && key_.rfind("sb_secret_", 0) != 0)
        throw std::invalid_argument(
            "Recommendation storage requires a backend-only Supabase secret key");
}

void SupabaseRecommendationRepository::rpc(const UserAccess &access, const std::string &name,
                                           Json::Value body, bool nullable,
                                           HistoryCompletion done) const
{
    if (url_.empty() || key_.empty())
    {
        done(failure(503, "HISTORY_NOT_CONFIGURED",
                     "Configure Supabase history before using recommendations."));
        return;
    }
    auto client = drogon::HttpClient::newHttpClient(url_, nullptr, false, true);
    // The actor is derived by the server from IAM, never from the HTTP client's body.
    body["p_user_id"] = access.userId;
    auto request = drogon::HttpRequest::newHttpJsonRequest(body);
    request->setMethod(drogon::Post);
    request->setPath("/rest/v1/rpc/" + name);
    request->addHeader("apikey", key_);
    // The IAM user's JWT belongs to another project and must not be sent here.
    client->sendRequest(
        request,
        [client, done = std::move(done), userId = access.userId, name,
         nullable](drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
            if (result != drogon::ReqResult::Ok || !response)
            {
                done(failure(503, "HISTORY_UNAVAILABLE",
                             "Could not access recommendation history."));
                return;
            }
            const auto status = response->statusCode();
            const bool chat = name.rfind("mirage_chat_", 0) == 0;
            if (chat && (status == drogon::k400BadRequest || status == drogon::k409Conflict))
            {
                done(failure(static_cast<int>(status), status == drogon::k409Conflict ? "CONVERSATION_CONFLICT" : "INVALID_ARGUMENT",
                             "Conversation changed, is full, or request parameters are invalid. Reload before retrying."));
                return;
            }
            if (status == drogon::k401Unauthorized || status == drogon::k403Forbidden)
            {
                done(failure(503, "HISTORY_UNAVAILABLE",
                             "History backend credentials were rejected."));
                return;
            }
            if (status == drogon::k409Conflict && name == "mirage_recommendation_feedback")
            {
                done(failure(409, "RECOMMENDATION_REJECTED",
                             "A rejected recommendation cannot be marked as favorite."));
                return;
            }
            if (status == drogon::k400BadRequest && name == "mirage_list_recommendations")
            {
                done(failure(400, "INVALID_ARGUMENT",
                             "The history cursor or parameters are invalid."));
                return;
            }
            const auto value = response->getJsonObject();
            if (status != drogon::k200OK || !value)
            {
                done(failure(503, "HISTORY_UNAVAILABLE",
                             "Could not access recommendation history."));
                return;
            }
            if (nullable && value->isNull())
            {
                done(failure(404, chat ? "CONVERSATION_NOT_FOUND" : "RECOMMENDATION_NOT_FOUND", "Resource not found."));
                return;
            }
            bool valid = false;
            if (chat)
            {
                const auto owned = [&userId](const Json::Value &v) {
                    return v.isObject() && uuid(v["id"]) && v["user_id"] == userId &&
                           v["version"].isUInt() && v["version"].asUInt() <= 20;
                };
                if (name == "mirage_chat_list")
                {
                    valid = value->isObject() && (*value)["items"].isArray() && (*value)["items"].size() <= 100 &&
                            value->isMember("next_cursor") && ((*value)["next_cursor"].isNull() || uuid((*value)["next_cursor"]));
                    if (valid) for (const auto &v : (*value)["items"]) valid = valid && owned(v);
                }
                else if (name == "mirage_chat_append")
                    valid = value->isObject() && uuid((*value)["conversation_id"]) && uuid((*value)["client_message_id"]) &&
                            (*value)["message"].isString() && (*value)["assistant_message"].isString();
                else
                    valid = owned(*value) && (name != "mirage_chat_get" ||
                            ((*value)["turns"].isArray() && (*value)["turns"].size() <= 20));
            }
            else if (name == "mirage_list_recommendations")
            {
                valid = value->isObject() && (*value)["items"].isArray() &&
                        value->isMember("next_cursor") &&
                        ((*value)["next_cursor"].isNull() || uuid((*value)["next_cursor"])) &&
                        (*value)["items"].size() <= 100;
                if (valid)
                    for (const auto &item : (*value)["items"])
                        valid = valid && ownedRecord(item, userId);
            }
            else if (name == "mirage_create_recommendations")
            {
                valid = ownedContext(*value, userId) && (*value)["outfits"].isArray() &&
                        (*value)["outfits"].size() <= 3;
                if (valid)
                    for (const auto &item : (*value)["outfits"])
                        valid =
                            valid && outfit(item) && item["request_id"] == (*value)["request_id"];
            }
            else
                valid = ownedRecord(*value, userId);
            if (!valid)
            {
                done(failure(503, "HISTORY_UNAVAILABLE", "History returned an invalid response."));
                return;
            }
            done({200, *value});
        },
        10.0);
}

void SupabaseRecommendationRepository::chat(const UserAccess &access, const std::string &operation,
                                           Json::Value body, HistoryCompletion done) const
{
    if (operation != "create" && operation != "list" && operation != "get" && operation != "append")
        throw std::invalid_argument("Unknown conversation operation");
    rpc(access, "mirage_chat_" + operation, std::move(body), operation == "get" || operation == "append", std::move(done));
}

void SupabaseRecommendationRepository::create(const UserAccess &access, Json::Value snapshot,
                                              HistoryCompletion done) const
{
    Json::Value body;
    body["p_snapshot"] = std::move(snapshot);
    rpc(access, "mirage_create_recommendations", std::move(body), false, std::move(done));
}
void SupabaseRecommendationRepository::list(const UserAccess &access, unsigned limit,
                                            const std::string &cursor, std::optional<bool> favorite,
                                            HistoryCompletion done) const
{
    Json::Value body;
    body["p_limit"] = limit;
    body["p_cursor"] = cursor.empty() ? Json::Value() : Json::Value(cursor);
    body["p_favorite"] = favorite ? Json::Value(*favorite) : Json::Value();
    rpc(access, "mirage_list_recommendations", std::move(body), false, std::move(done));
}
void SupabaseRecommendationRepository::find(const UserAccess &access, const std::string &id,
                                            HistoryCompletion done) const
{
    Json::Value body;
    body["p_id"] = id;
    rpc(access, "mirage_get_recommendation", std::move(body), true, std::move(done));
}
void SupabaseRecommendationRepository::favorite(const UserAccess &access, const std::string &id,
                                                bool favorite, HistoryCompletion done) const
{
    Json::Value body;
    body["p_id"] = id;
    body["p_action"] = "favorite";
    body["p_favorite"] = favorite;
    rpc(access, "mirage_recommendation_feedback", std::move(body), true, std::move(done));
}
void SupabaseRecommendationRepository::reject(const UserAccess &access, const std::string &id,
                                              const std::string &reason,
                                              HistoryCompletion done) const
{
    Json::Value body;
    body["p_id"] = id;
    body["p_action"] = "reject";
    body["p_reason"] = reason.empty() ? Json::Value() : Json::Value(reason);
    rpc(access, "mirage_recommendation_feedback", std::move(body), true, std::move(done));
}
} // namespace mirage::recommendation
