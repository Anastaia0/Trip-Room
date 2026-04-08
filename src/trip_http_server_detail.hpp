#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/beast/http.hpp>

#include "trip/trip_service.hpp"

namespace trip::detail
{
    namespace http = boost::beast::http;

    using StringResponse = http::response<http::string_body>;
    using PublishEventFn = std::function<void(const std::string &token, const std::string &trip_id)>;

    std::unordered_map<std::string, std::string> parseForm(std::string_view payload);
    std::string escapeJson(const std::string &text);
    std::string statusToString(Status status);
    std::string roleToString(Role role);

    StringResponse jsonResponse(http::status code, std::string body, unsigned version, bool keep_alive);
    std::pair<std::string, std::unordered_map<std::string, std::string>> splitTarget(const std::string &target);

    uint64_t parseUint64(const std::string &text);
    double parseDouble(const std::string &text);
    bool parseBool(const std::string &text);
    std::vector<std::string> splitCsv(const std::string &text);
    Role parseRole(const std::string &text);

    std::string eventToJson(const Event &event);
    std::string eventsArrayToJson(const std::vector<Event> &events);
    std::string messagesArrayToJson(const std::vector<ChatMessage> &messages);
    std::string mapToJsonObject(const std::unordered_map<std::string, double> &values);
    std::string searchHitsToJson(const std::vector<SearchHit> &hits);
    std::string budgetSummaryToJson(const BudgetSummary &summary);
    std::string tripToJson(const Trip &trip);
    std::string tripSummariesToJson(const std::vector<TripSummary> &summaries);

    StringResponse handleApiRequest(
        TripService &service,
        const PublishEventFn &publish_latest_event,
        const http::request<http::string_body> &req);
}
