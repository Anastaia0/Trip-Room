#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    StringResponse handleApiRequest(
        TripService &service,
        const PublishEventFn &publish_latest_event,
        const http::request<http::string_body> &req)
    {
        if (req.method() != http::verb::get && req.method() != http::verb::post)
        {
            return jsonResponse(
                http::status::method_not_allowed,
                "{\"status\":\"InvalidArgument\",\"message\":\"Only GET and POST are supported\"}",
                req.version(),
                req.keep_alive());
        }

        const auto [path, query] = splitTarget(std::string(req.target()));
        RequestContext ctx{service, publish_latest_event, req, query, parseForm(req.body())};

        StringResponse response;
        if (handleAuthRoutes(ctx, path, response) ||
            handleTripCollabRoutes(ctx, path, response) ||
            handleItineraryRoutes(ctx, path, response) ||
            handleTaskChatRoutes(ctx, path, response) ||
            handleBudgetSearchSyncRoutes(ctx, path, response))
        {
            return response;
        }

        return jsonResponse(
            http::status::not_found,
            "{\"status\":\"NotFound\",\"message\":\"Route not found\"}",
            req.version(),
            req.keep_alive());
    }
}
