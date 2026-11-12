#include "RecommendationHttp.h"
#include "recommendation/infrastructure/FashionWebSearch.h"
#include "recommendation/infrastructure/GarmentJson.h"
#include "recommendation/infrastructure/StyleKnowledgeJson.h"
#include "recommendation/infrastructure/OutfitExampleJson.h"
#include "recommendation/infrastructure/WardrobeIndex.h"
#include <algorithm>
#include <cctype>
#include <drogon/drogon.h>
#include <regex>
#include <set>

namespace mirage::recommendation
{
namespace
{
using Reply = std::function<void(const drogon::HttpResponsePtr &)>;

void send(const Reply &reply, drogon::HttpStatusCode status, const Json::Value &body)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    if (status == drogon::k401Unauthorized)
        response->addHeader("WWW-Authenticate", "Bearer");
    reply(response);
}

void error(const Reply &reply, drogon::HttpStatusCode status, const std::string &code,
           const std::string &message)
{
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    send(reply, status, body);
}

using Authorized = std::function<void(UserAccess, Reply)>;

void authorize(const IdentityVerifier &identity, const drogon::HttpRequestPtr &request, Reply reply,
               Authorized done)
{
    const auto authorization = request->getHeader("authorization");
    static const std::regex bearer(R"(^Bearer ([A-Za-z0-9._~+/-]+=*)$)", std::regex::icase);
    std::smatch match;
    if (authorization.size() > 8192 || !std::regex_match(authorization, match, bearer))
    {
        error(reply, drogon::k401Unauthorized, "UNAUTHENTICATED",
              "A valid Bearer access token is required.");
        return;
    }
    const auto token = match[1].str();
    identity.verify(token, [reply = std::move(reply), done = std::move(done),
                            token](IdentityResult verified) mutable {
        if (verified.status == IdentityStatus::Rejected)
        {
            error(reply, drogon::k401Unauthorized, "UNAUTHENTICATED",
                  "The access token or session is invalid.");
            return;
        }
        if (verified.status != IdentityStatus::Verified || verified.userId.empty())
        {
            error(reply, drogon::k503ServiceUnavailable, "IAM_UNAVAILABLE",
                  "Could not validate the session with IAM. Try again later.");
            return;
        }
        done({verified.userId, token}, std::move(reply));
    });
}

HistoryCompletion historyReply(Reply reply)
{
    return [reply = std::move(reply)](HistoryResult result) {
        send(reply, static_cast<drogon::HttpStatusCode>(result.status), result.body);
    };
}

bool validId(const std::string &id)
{
    static const std::regex uuid(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(id, uuid);
}

bool feedbackBody(const drogon::HttpRequestPtr &request, Json::Value &body, const Reply &reply)
{
    body = Json::Value(Json::objectValue);
    if (request->body().empty())
        return true;
    const auto contentType = request->getHeader("content-type");
    if (contentType.substr(0, contentType.find(';')) != "application/json")
    {
        error(reply, drogon::k415UnsupportedMediaType, "UNSUPPORTED_MEDIA_TYPE",
              "Use Content-Type: application/json.");
        return false;
    }
    const auto value = request->getJsonObject();
    if (!value || !value->isObject())
    {
        error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "Feedback must be a JSON object.");
        return false;
    }
    body = *value;
    return true;
}

bool parse(const Json::Value &body, RecommendationParameters &parameters)
{
    if (!body.isObject())
        return false;
    const std::set<std::string> fields{"occasion", "style", "excluded_garments", "max_results"};
    for (const auto &key : body.getMemberNames())
        if (!fields.count(key))
            return false;
    if (!body["occasion"].isString())
        return false;
    parameters.occasion = body["occasion"].asString();
    if (parameters.occasion.empty() || parameters.occasion.size() > 120 ||
        std::all_of(parameters.occasion.begin(), parameters.occasion.end(),
                    [](unsigned char c) { return std::isspace(c); }))
        return false;
    if (body.isMember("style"))
    {
        if (!body["style"].isString())
            return false;
        parameters.style = body["style"].asString();
        const std::set<std::string> styles{"CASUAL",     "FORMAL",  "SPORT",   "STREETWEAR",
                                           "MINIMALIST", "CLASSIC", "BOHEMIAN"};
        if (!styles.count(parameters.style))
            return false;
    }
    if (body.isMember("max_results"))
    {
        if (!body["max_results"].isUInt() || body["max_results"].asUInt() < 1 ||
            body["max_results"].asUInt() > 3)
            return false;
        parameters.maxResults = body["max_results"].asUInt();
    }
    if (body.isMember("excluded_garments"))
    {
        const auto &ids = body["excluded_garments"];
        if (!ids.isArray() || ids.size() > 100)
            return false;
        static const std::regex uuid(
            "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
        std::set<std::string> unique;
        for (const auto &id : ids)
        {
            if (!id.isString() || !std::regex_match(id.asString(), uuid))
                return false;
            auto normalized = id.asString();
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!unique.insert(normalized).second)
                return false;
            parameters.excludedGarments.push_back(std::move(normalized));
        }
    }
    return true;
}
bool wardrobeError(const Reply &reply, RecommendationStatus status)
{
    if (status == RecommendationStatus::WardrobeChanged)
    {
        error(reply,drogon::k409Conflict,"WARDROBE_CHANGED",
              "The wardrobe changed during generation. Nothing was saved; request a new proposal.");
        return true;
    }
    if (status == RecommendationStatus::WardrobeLimitExceeded)
    {
        error(reply,drogon::k422UnprocessableEntity,"WARDROBE_LIMIT_EXCEEDED",
              "This version supports up to 100 eligible garments and 1000 listed garments. Nothing was saved.");
        return true;
    }
    return false;
}

bool textField(const Json::Value &v, size_t limit)
{
    if (!v.isString()) return false;
    const auto text = v.asString();
    return !text.empty() && text.size() <= limit && text.find('\0') == std::string::npos &&
           text.find_first_not_of(" \t\r\n") != std::string::npos;
}

Json::Value chatSnapshot(const UserAccess &access, const RecommendationResult &result, const std::string &mode)
{
    Json::Value output;
    output["user_id"] = access.userId;
    output["mode"] = mode;
    output["garments"] = garmentsJson(result.garments);
    output["style_references"] = styleReferencesJson(result.styleReferences);
    output["outfit_examples"] = outfitExamplesJson(result.outfitExamples);
    output["web_references"] = fashionWebReferencesJson(result.webReferences);
    output["engine"] = result.engine;
    output["model"] = result.model;
    output["llm_used"] = result.llmUsed;
    output["occasion"] = "Conversacion";
    output["parameters"]["occasion"] = "Conversacion";
    output["status"] = "GENERATED";
    output["persisted"] = false;
    const auto array = [](const std::vector<std::string> &values) {
        Json::Value list(Json::arrayValue);
        for (const auto &value : values) list.append(value);
        return list;
    };
    output["profile_preferences"]["preferred_styles"] = array(result.preferences.preferredStyles);
    output["profile_preferences"]["preferred_colors"] = array(result.preferences.preferredColors);
    output["profile_preferences"]["avoided_colors"] = array(result.preferences.avoidedColors);
    output["profile_preferences"]["preferred_categories"] = array(result.preferences.preferredCategories);
    output["outfits"] = Json::Value(Json::arrayValue);
    for (const auto &outfit : result.outfits)
    {
        Json::Value item;
        item["garment_ids"] = array(outfit.garmentIds);
        item["explanation"] = outfit.explanation;
        output["outfits"].append(item);
    }
    return output;
}

void registerConversationHttp(const std::string &mode, const RecommendationService *service,
                              const IdentityVerifier &identity, const RecommendationRepository &history,
                              const RecommenderAgent *agent)
{
    drogon::app().registerHandler("/api/v1/conversations",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply) {
            authorize(identity, request, std::move(reply), [request, &history](UserAccess access, Reply reply) {
                Json::Value body;
                if (!feedbackBody(request, body, reply)) return;
                if (body.size() > 1 || (body.size() && !body.isMember("title")) ||
                    (body.isMember("title") && !textField(body["title"], 120)))
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "Expected optional title (1-120 bytes)."); return; }
                Json::Value payload;
                payload["p_title"] = body.get("title", "Nueva conversacion");
                history.chat(access, "create", payload, [reply](HistoryResult result) {
                    send(reply, static_cast<drogon::HttpStatusCode>(result.status == 200 ? 201 : result.status), result.body);
                });
            });
        }, {drogon::Post});
    drogon::app().registerHandler("/api/v1/conversations",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply) {
            authorize(identity, request, std::move(reply), [request, &history](UserAccess access, Reply reply) {
                for (const auto &q : request->getParameters())
                    if (q.first != "limit" && q.first != "cursor")
                    { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "Unknown query parameter."); return; }
                const auto limit = request->getParameter("limit");
                const auto cursor = request->getParameter("cursor");
                const bool hasLimit = request->getParameters().count("limit") != 0;
                const bool hasCursor = request->getParameters().count("cursor") != 0;
                if ((hasLimit && (limit.empty() || limit.size()>3 || limit.find_first_not_of("0123456789")!=std::string::npos)) ||
                    (hasCursor && !validId(cursor)))
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "Invalid limit or cursor."); return; }
                const unsigned count = hasLimit ? static_cast<unsigned>(std::stoul(limit)) : 20;
                if (count<1 || count>100)
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "limit must be 1-100."); return; }
                Json::Value payload;
                payload["p_limit"] = count;
                payload["p_cursor"] = hasCursor ? Json::Value(cursor) : Json::Value();
                history.chat(access, "list", payload, historyReply(reply));
            });
        }, {drogon::Get});
    drogon::app().registerHandler("/api/v1/conversations/{1}",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply, std::string id) {
            authorize(identity, request, std::move(reply), [id, &history](UserAccess access, Reply reply) {
                if (!validId(id))
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "id must be a UUID."); return; }
                Json::Value payload; payload["p_id"] = id;
                history.chat(access, "get", payload, historyReply(reply));
            });
        }, {drogon::Get});
    drogon::app().registerHandler("/api/v1/conversations/{1}/messages",
        [mode, service, agent, &identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply, std::string id) {
            authorize(identity, request, std::move(reply), [=, &history](UserAccess access, Reply reply) {
                if (!validId(id))
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "id must be a UUID."); return; }
                Json::Value input;
                if (!feedbackBody(request, input, reply)) return;
                if (input.size()!=2 || !textField(input["message"], 2000) || !input["client_message_id"].isString() ||
                    !validId(input["client_message_id"].asString()))
                { error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT", "Expected message (1-2000 bytes) and client_message_id UUID."); return; }
                Json::Value payload; payload["p_id"] = id;
                history.chat(access, "get", payload, [=, &history](HistoryResult chat) mutable {
                    if (chat.status!=200) { send(reply, static_cast<drogon::HttpStatusCode>(chat.status), chat.body); return; }
                    auto messageId = input["client_message_id"].asString();
                    std::transform(messageId.begin(), messageId.end(), messageId.begin(), [](unsigned char c) {return static_cast<char>(std::tolower(c));});
                    for (const auto &turn : chat.body["turns"])
                        if (turn["client_message_id"] == messageId)
                        {
                            if (turn["message"] != input["message"])
                                error(reply, drogon::k409Conflict, "MESSAGE_ID_REUSED", "Use a new client_message_id for a different message.");
                            else send(reply, drogon::k200OK, turn);
                            return;
                        }
                    if (chat.body["version"].asUInt() >= 20)
                    { error(reply, drogon::k409Conflict, "CONVERSATION_FULL", "Create another conversation after 20 turns."); return; }
                    if (mode == "disabled" || !service || !agent || !agent->configured())
                    { error(reply, drogon::k503ServiceUnavailable, "CHAT_NOT_CONFIGURED", "Chat requires an enabled wardrobe and a configured Nemotron engine."); return; }
                    Json::Value context(Json::arrayValue);
                    for (const auto &turn : chat.body["turns"])
                    {
                        Json::Value t;
                        t["user"] = turn["message"];
                        t["assistant"] = turn["assistant_message"];
                        t["outfits"] = Json::Value(Json::arrayValue);
                        for (const auto &outfit : turn["recommendation"]["outfits"])
                        {
                            Json::Value o;
                            o["garment_ids"] = outfit["garment_ids"];
                            o["id"] = outfit["id"];
                            o["explanation"] = outfit["explanation"];
                            t["outfits"].append(o);
                        }
                        context.append(t);
                    }
                    RecommendationParameters params;
                    params.occasion = "Conversacion";
                    params.message = input["message"].asString();
                    if(!chat.body["turns"].empty())
                    {
                        const auto &memory=chat.body["turns"][chat.body["turns"].size()-1]["memory"];
                        if(memory.isObject())
                        {
                            for(const auto &color:memory["excluded_colors"]) params.excludedColors.push_back(color.asString());
                            params.darkOnly=memory.get("dark_only",false).asBool();
                            params.conversationStyle=memory.get("style","").asString();
                            params.conversationOccasion=memory.get("occasion","").asString();
                        }
                    }
                    for (const auto &turn : chat.body["turns"])
                    {
                        const auto &previous = turn["recommendation"]["outfits"];
                        if (!previous.isArray() || previous.empty()) continue;
                        params.previousOutfits.clear();
                        for (const auto &value : previous)
                        {
                            Outfit outfit;
                            outfit.explanation = value["explanation"].asString();
                            for (const auto &garmentId : value["garment_ids"]) outfit.garmentIds.push_back(garmentId.asString());
                            params.previousOutfits.push_back(std::move(outfit));
                        }
                    }
                    Json::StreamWriterBuilder writer; writer["indentation"] = "";
                    params.conversationContext = Json::writeString(writer, context);
                    service->generate(access, params, [=, &history](RecommendationResult result) {
                        if (wardrobeError(reply,result.status)) return;
                        if (result.status != RecommendationStatus::Generated)
                        {
                            if(result.status==RecommendationStatus::ToolLimitExceeded || result.status==RecommendationStatus::ToolUnavailable)
                            {error(reply,result.status==RecommendationStatus::ToolLimitExceeded?drogon::k502BadGateway:drogon::k503ServiceUnavailable,
                                   result.status==RecommendationStatus::ToolLimitExceeded?"TOOL_LIMIT_EXCEEDED":"TOOL_UNAVAILABLE",
                                   "Could not complete the guarded tool workflow. No turn was saved.");return;}
                            if(result.status==RecommendationStatus::ProfileUpdateUncertain)
                            {error(reply,drogon::k503ServiceUnavailable,"PROFILE_UPDATE_UNCONFIRMED",
                                   "Could not confirm the Profile update. No turn was saved. Read your preferences before retrying.");return;}
                            if (result.status == RecommendationStatus::Unauthorized)
                                error(reply, drogon::k401Unauthorized, "UNAUTHENTICATED", "Session expired.");
                            else if (result.status == RecommendationStatus::ProfileRequired)
                                error(reply, drogon::k409Conflict, "PROFILE_REQUIRED", "Create your IAM profile first.");
                            else error(reply, result.status == RecommendationStatus::ModelInvalidResponse ? drogon::k502BadGateway : drogon::k503ServiceUnavailable,
                                       "CHAT_GENERATION_FAILED", "Could not generate the reply. No turn was saved. Retry with the same client_message_id.");
                            return;
                        }
                        Json::Value save;
                        save["p_id"] = id;
                        save["p_client_message_id"] = messageId;
                        save["p_version"] = chat.body["version"];
                        save["p_message"] = input["message"];
                        save["p_assistant_message"] = result.assistantMessage;
                        save["p_snapshot"] = result.outfits.empty() ? Json::Value() : chatSnapshot(access, result, mode);
                        auto &memory=save["p_memory"];
                        memory["excluded_colors"]=Json::Value(Json::arrayValue);
                        for(const auto &color:result.excludedColors) memory["excluded_colors"].append(color);
                        memory["dark_only"]=result.darkOnly;
                        memory["style"]=result.conversationStyle;
                        memory["occasion"]=result.conversationOccasion;
                        memory["profile_updated"]=result.preferencesUpdated;
                        history.chat(access, "append", save, [reply,updated=result.preferencesUpdated](HistoryResult saved) {
                            if(saved.status!=200 && updated)
                            {error(reply,static_cast<drogon::HttpStatusCode>(saved.status),"PROFILE_UPDATED_CHAT_NOT_SAVED",
                                   "Profile was updated, but the chat turn could not be saved. Read your preferences before retrying.");return;}
                            send(reply,static_cast<drogon::HttpStatusCode>(saved.status),saved.body);
                        });
                    });
                });
            });
        }, {drogon::Post});
}
} // namespace

void registerRecommendationHttp(const std::string &mode, const RecommendationService *service,
                                const IdentityVerifier &identity,
                                const RecommendationRepository &history,
                                const RecommenderAgent *agent,const WardrobeIndex *index)
{
    registerConversationHttp(mode, service, identity, history, agent);
    for(const auto &operation:{std::string("sync"),std::string("search")})
    drogon::app().registerHandler("/api/v1/wardrobe-index/"+operation,
        [&identity,index,operation](const drogon::HttpRequestPtr &request,Reply &&reply){
            authorize(identity,request,std::move(reply),[request,index,operation](UserAccess access,Reply reply){
                Json::Value body;if(!feedbackBody(request,body,reply))return;
                if((operation=="sync" && (!body.isObject() || !body.empty())) ||
                   (operation=="search" && !WardrobeIndex::validFilters(body,true)))
                {error(reply,drogon::k400BadRequest,"INVALID_ARGUMENT","Sync expects {}; search expects query and optional category, color, style, limit. No user_id.");return;}
                if(!index){error(reply,drogon::k503ServiceUnavailable,"RAG_NOT_CONFIGURED","Vector search is disabled.");return;}
                auto done=[reply](HistoryResult r){send(reply,static_cast<drogon::HttpStatusCode>(r.status),r.body);};
                if(operation=="sync")index->sync(access,std::move(done));else index->search(access,body,std::move(done));
            });
        },{drogon::Post});
    drogon::app().registerHandler("/api/v1/style-knowledge/search",
        [&identity,index](const drogon::HttpRequestPtr &request,Reply &&reply){
            authorize(identity,request,std::move(reply),[request,index](UserAccess access,Reply reply){
                Json::Value body;if(!feedbackBody(request,body,reply))return;
                if(!WardrobeIndex::validStyleFilters(body)){error(reply,drogon::k400BadRequest,"INVALID_ARGUMENT","Expected query, optional style and limit from 1 to 3. No user_id or URLs.");return;}
                if(!index){error(reply,drogon::k503ServiceUnavailable,"RAG_NOT_CONFIGURED","Style knowledge retrieval is disabled.");return;}
                index->searchStyleKnowledge(access,body,[reply](HistoryResult r){send(reply,static_cast<drogon::HttpStatusCode>(r.status),r.body);});
            });
        },{drogon::Post});
    drogon::app().registerHandler("/api/v1/outfit-examples/search",
        [&identity,index](const drogon::HttpRequestPtr &request,Reply &&reply){
            authorize(identity,request,std::move(reply),[request,index](UserAccess access,Reply reply){
                Json::Value body;if(!feedbackBody(request,body,reply))return;
                if(!WardrobeIndex::validExampleFilters(body)){error(reply,drogon::k400BadRequest,"INVALID_ARGUMENT","Expected query, optional structure and limit from 1 to 3. No user_id, source IDs, URLs or split.");return;}
                if(!index){error(reply,drogon::k503ServiceUnavailable,"RAG_NOT_CONFIGURED","Example retrieval is disabled.");return;}
                index->searchOutfitExamples(access,body,[reply](HistoryResult r){send(reply,static_cast<drogon::HttpStatusCode>(r.status),r.body);});
            });
        },{drogon::Post});
    drogon::app().registerHandler("/health",
                                  [mode, agent,index](const drogon::HttpRequestPtr &, Reply &&reply) {
                                      Json::Value body;
                                      body["status"] = "ok";
                                      body["service"] = "recommendation-service";
                                      body["mode"] = mode;
                                      body["wardrobe_source"] = mode == "live" ? "digital_closet" : mode == "demo" ? "fixture" : "disabled";
                                      body["recommendations_ready"] = mode != "disabled";
                                      body["engine"] = agent ? "nemotron" : "category_rules";
                                      body["rag_enabled"] = index!=nullptr;
                                      body["conversations_enabled"] = mode != "disabled" && agent && agent->configured();
                                      body["llm_enabled"] = agent && agent->configured();
                                      if (agent)
                                          body["model"] = agent->model();
                                      body["authentication"] = "iam";
                                      send(reply, drogon::k200OK, body);
                                  },
                                  {drogon::Get});

    drogon::app().registerHandler(
        "/api/v1/recommendations",
        [mode, service, &identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply) {
            authorize(
                identity, request, std::move(reply),
                [mode, service, request, &history](UserAccess access, Reply reply) mutable {
                    if (mode == "disabled" || service == nullptr)
                    {
                        error(reply, drogon::k503ServiceUnavailable, "NOT_CONFIGURED",
                              "Enable generation with RECOMMENDATION_MODE=demo or live.");
                        return;
                    }
                    const auto contentType = request->getHeader("content-type");
                    const auto separator = contentType.find(';');
                    if (contentType.substr(0, separator) != "application/json")
                    {
                        error(reply, drogon::k415UnsupportedMediaType, "UNSUPPORTED_MEDIA_TYPE",
                              "Use Content-Type: application/json.");
                        return;
                    }
                    const auto body = request->getJsonObject();
                    RecommendationParameters parameters;
                    if (!body || !parse(*body, parameters))
                    {
                        error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                              "Expected occasion (1-120 bytes), optional style, up to 100 unique "
                              "UUID "
                              "exclusions and max_results (1-3).");
                        return;
                    }
                    const auto occasion = parameters.occasion;
                    const auto userId = access.userId;
                    service->generate(
                        access, std::move(parameters),
                        [reply = std::move(reply), occasion, userId, access, mode,
                         parametersSnapshot = *body, &history](RecommendationResult result) {
                            if (wardrobeError(reply,result.status)) return;
                            if(result.status==RecommendationStatus::ToolUnavailable)
                            {error(reply,drogon::k503ServiceUnavailable,"RAG_CONTEXT_UNAVAILABLE","Could not retrieve reference context. No recommendation was saved.");return;}
                            if (result.status == RecommendationStatus::Unauthorized)
                            {
                                error(reply, drogon::k401Unauthorized, "UNAUTHENTICATED",
                                      "The access token or session is invalid.");
                                return;
                            }
                            if (result.status == RecommendationStatus::ProfileRequired)
                            {
                                error(reply, drogon::k409Conflict, "PROFILE_REQUIRED",
                                      "Create your profile in IAM before requesting "
                                      "recommendations.");
                                return;
                            }
                            if (result.status == RecommendationStatus::ProfileUnavailable)
                            {
                                error(reply, drogon::k503ServiceUnavailable, "PROFILE_UNAVAILABLE",
                                      "Could not retrieve preferences from Profile. Try again "
                                      "later.");
                                return;
                            }
                            if (result.status == RecommendationStatus::WardrobeUnavailable)
                            {
                                error(reply, drogon::k503ServiceUnavailable, "WARDROBE_UNAVAILABLE",
                                      "Could not read the wardrobe.");
                                return;
                            }
                            if (result.status == RecommendationStatus::ModelNotConfigured ||
                                result.status == RecommendationStatus::ModelUnavailable ||
                                result.status == RecommendationStatus::ModelInvalidResponse ||
                                result.status == RecommendationStatus::ModelRateLimited)
                            {
                                const auto code =
                                    result.status == RecommendationStatus::ModelNotConfigured
                                        ? "MODEL_NOT_CONFIGURED"
                                    : result.status == RecommendationStatus::ModelInvalidResponse
                                        ? "MODEL_INVALID_RESPONSE"
                                    : result.status == RecommendationStatus::ModelRateLimited
                                        ? "MODEL_RATE_LIMITED"
                                        : "MODEL_UNAVAILABLE";
                                error(
                                    reply,
                                    result.status == RecommendationStatus::ModelInvalidResponse
                                        ? drogon::k502BadGateway
                                        : drogon::k503ServiceUnavailable,
                                    code,
                                    "Could not generate a valid outfit with the configured model.");
                                return;
                            }
                            Json::Value output;
                            output["mode"] = mode;
    output["garments"] = garmentsJson(result.garments);
    output["style_references"] = styleReferencesJson(result.styleReferences);
    output["outfit_examples"] = outfitExamplesJson(result.outfitExamples);
                            output["engine"] = result.engine;
                            output["llm_used"] = result.llmUsed;
                            if (!result.model.empty())
                                output["model"] = result.model;
                            output["persisted"] = false;
                            output["occasion"] = occasion;
                            output["user_id"] = userId;
                            output["parameters"] = parametersSnapshot;
                            const auto array = [](const std::vector<std::string> &values) {
                                Json::Value list(Json::arrayValue);
                                for (const auto &value : values)
                                    list.append(value);
                                return list;
                            };
                            output["profile_preferences"]["preferred_styles"] =
                                array(result.preferences.preferredStyles);
                            output["profile_preferences"]["preferred_colors"] =
                                array(result.preferences.preferredColors);
                            output["profile_preferences"]["avoided_colors"] =
                                array(result.preferences.avoidedColors);
                            output["profile_preferences"]["preferred_categories"] =
                                array(result.preferences.preferredCategories);
                            output["status"] = result.status == RecommendationStatus::Generated
                                                   ? "GENERATED"
                                                   : "INSUFFICIENT_GARMENTS";
                            output["outfits"] = Json::Value(Json::arrayValue);
                            for (const auto &outfit : result.outfits)
                            {
                                Json::Value item;
                                item["garment_ids"] = Json::Value(Json::arrayValue);
                                for (const auto &id : outfit.garmentIds)
                                    item["garment_ids"].append(id);
                                item["explanation"] =
                                    outfit.explanation.empty()
                                        ? "Combinacion base con prendas disponibles y "
                                          "preferencias de Profile; "
                                          "no evalua la ocasion ni compatibilidad visual."
                                        : outfit.explanation;
                                output["outfits"].append(std::move(item));
                            }
                            history.create(access, std::move(output),
                                           historyReply(std::move(reply)));
                        });
                });
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/v1/recommendations",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply) {
            authorize(identity, request, std::move(reply),
                      [request, &history](UserAccess access, Reply reply) {
                          const auto &query = request->getParameters();
                          for (const auto &entry : query)
                              if (entry.first != "limit" && entry.first != "cursor" &&
                                  entry.first != "favorite")
                              {
                                  error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                        "Unknown history parameter.");
                                  return;
                              }
                          const auto text = request->getParameter("limit");
                          unsigned limit = 20;
                          if (query.count("limit"))
                          {
                              if (text.empty() || text.size() > 3 ||
                                  text.find_first_not_of("0123456789") != std::string::npos)
                              {
                                  error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                        "limit must be between 1 and 100.");
                                  return;
                              }
                              limit = static_cast<unsigned>(std::stoul(text));
                              if (limit < 1 || limit > 100)
                              {
                                  error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                        "limit must be between 1 and 100.");
                                  return;
                              }
                          }
                          const auto cursor = request->getParameter("cursor");
                          if (query.count("cursor") && !validId(cursor))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "cursor must be a recommendation UUID.");
                              return;
                          }
                          std::optional<bool> favorite;
                          if (query.count("favorite"))
                          {
                              const auto value = request->getParameter("favorite");
                              if (value != "true" && value != "false")
                              {
                                  error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                        "favorite must be true or false.");
                                  return;
                              }
                              favorite = value == "true";
                          }
                          history.list(access, limit, cursor, favorite,
                                       historyReply(std::move(reply)));
                      });
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/v1/recommendations/{1}",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply,
                              std::string id) {
            authorize(identity, request, std::move(reply),
                      [id, &history](UserAccess access, Reply reply) {
                          if (!validId(id))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "id must be a UUID.");
                              return;
                          }
                          history.find(access, id, historyReply(std::move(reply)));
                      });
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/v1/recommendations/{1}/favorite",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply,
                              std::string id) {
            authorize(identity, request, std::move(reply),
                      [request, id, &history](UserAccess access, Reply reply) {
                          if (!validId(id))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "id must be a UUID.");
                              return;
                          }
                          Json::Value body;
                          if (!feedbackBody(request, body, reply))
                              return;
                          if (body.size() > 1 || (body.size() == 1 && !body.isMember("favorite")) ||
                              (body.isMember("favorite") && !body["favorite"].isBool()))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "Expected only an optional favorite boolean.");
                              return;
                          }
                          history.favorite(access, id, body.get("favorite", true).asBool(),
                                           historyReply(std::move(reply)));
                      });
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/v1/recommendations/{1}/reject",
        [&identity, &history](const drogon::HttpRequestPtr &request, Reply &&reply,
                              std::string id) {
            authorize(identity, request, std::move(reply),
                      [request, id, &history](UserAccess access, Reply reply) {
                          if (!validId(id))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "id must be a UUID.");
                              return;
                          }
                          Json::Value body;
                          if (!feedbackBody(request, body, reply))
                              return;
                          std::string reason;
                          if (body.size() > 1 || (body.size() == 1 && !body.isMember("reason")) ||
                              (body.isMember("reason") && !body["reason"].isString()))
                          {
                              error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                    "Expected only an optional rejection reason.");
                              return;
                          }
                          if (body.isMember("reason"))
                          {
                              reason = body["reason"].asString();
                              const auto first = reason.find_first_not_of(" \t\r\n");
                              if (first == std::string::npos || reason.size() > 500 ||
                                  reason.find('\0') != std::string::npos)
                              {
                                  error(reply, drogon::k400BadRequest, "INVALID_ARGUMENT",
                                        "reason must contain 1-500 UTF-8 bytes.");
                                  return;
                              }
                              reason = reason.substr(first, reason.find_last_not_of(" \t\r\n") -
                                                                first + 1);
                          }
                          history.reject(access, id, reason, historyReply(std::move(reply)));
                      });
        },
        {drogon::Post});
}
} // namespace mirage::recommendation
