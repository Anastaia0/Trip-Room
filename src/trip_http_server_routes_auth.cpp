#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    bool handleAuthRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response)
    {
        if (path == "/register" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.registerUser(ctx.param("login"), ctx.param("password"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &user_id)
                { return "\"user_id\":\"" + escapeJson(user_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/login" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.login(ctx.param("login"), ctx.param("password"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &token)
                { return "\"token\":\"" + escapeJson(token) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        return false;
    }
}
