#pragma once

#include "trip_http_server_detail.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace trip::detail
{
    struct RequestContext
    {
        TripService &service;
        const PublishEventFn &publish_latest_event;
        const http::request<http::string_body> &req;
        std::unordered_map<std::string, std::string> query;
        std::unordered_map<std::string, std::string> body;

        [[nodiscard]] std::string param(const std::string &key) const
        {
            const auto in_body = body.find(key);
            if (in_body != body.end())
            {
                return in_body->second;
            }
            const auto in_query = query.find(key);
            if (in_query != query.end())
            {
                return in_query->second;
            }
            if (key == "token")
            {
                return authorizationBearerToken(req);
            }
            return {};
        }
    };

    StringResponse makeStatusResponse(
        const std::string &status,
        const std::string &message,
        unsigned version,
        bool keep_alive);

    template <typename T>
    StringResponse makeStatusOrResponse(
        const StatusOr<T> &result,
        std::function<std::string(const T &)> on_ok_payload,
        unsigned version,
        bool keep_alive)
    {
        if (!result.ok())
        {
            return makeStatusResponse(statusToString(result.status), result.message, version, keep_alive);
        }

        std::string body = "{\"status\":\"Ok\"";
        const std::string payload = on_ok_payload(result.value);
        if (!payload.empty())
        {
            body += "," + payload;
        }
        body += "}";
        return jsonResponse(http::status::ok, std::move(body), version, keep_alive);
    }

    StringResponse makeStatusResultResponse(
        const StatusResult &result,
        unsigned version,
        bool keep_alive);

    bool handleAuthRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response);
    bool handleTripCollabRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response);
    bool handleItineraryRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response);
    bool handleTaskChatRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response);
    bool handleBudgetSearchSyncRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response);
}
