#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    StringResponse makeStatusResponse(
        const std::string &status,
        const std::string &message,
        unsigned version,
        bool keep_alive)
    {
        std::string body = "{\"status\":\"" + escapeJson(status) + "\"";
        if (!message.empty())
        {
            body += ",\"message\":\"" + escapeJson(message) + "\"";
        }
        body += "}";
        return jsonResponse(http::status::ok, std::move(body), version, keep_alive);
    }

    StringResponse makeStatusResultResponse(
        const StatusResult &result,
        unsigned version,
        bool keep_alive)
    {
        return makeStatusResponse(statusToString(result.status), result.message, version, keep_alive);
    }
}
