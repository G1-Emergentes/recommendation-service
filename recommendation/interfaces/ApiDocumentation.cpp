#include "ApiDocumentation.h"
#include <drogon/drogon.h>

namespace mirage::recommendation
{
void registerApiDocumentation(const std::filesystem::path &directory)
{
    // Only explicit documentation assets are served, never the project directory.
    const auto serve = [&directory](const std::string &route, const char *file) {
        const auto path = (directory / file).string();
        drogon::app().registerHandler(route,
            [path](const drogon::HttpRequestPtr &request,
                   std::function<void(const drogon::HttpResponsePtr &)> &&reply) {
                auto response = drogon::HttpResponse::newFileResponse(
                    path, "", drogon::CT_NONE, "", request);
                response->addHeader("Cache-Control", "no-store");
                response->addHeader("X-Content-Type-Options", "nosniff");
                response->addHeader("Referrer-Policy", "no-referrer");
                response->addHeader("Content-Security-Policy",
                    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                    "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'");
                reply(response);
            }, {drogon::Get});
    };
    for (const auto *route : {"/docs", "/docs/", "/swagger", "/swagger/"})
        serve(route, "index.html");
    serve("/openapi.json", "openapi.json");
    serve("/docs/swagger-ui.css", "swagger-ui.css");
    serve("/docs/swagger-ui-bundle.js", "swagger-ui-bundle.js");
    serve("/docs/swagger-initializer.js", "swagger-initializer.js");
}
}
