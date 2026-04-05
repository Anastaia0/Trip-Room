#include "trip_http_server_detail.hpp"

namespace trip::detail
{
    std::string escapeJson(const std::string &text)
    {
        std::string out;
        out.reserve(text.size() + 8);
        for (char ch : text)
        {
            if (ch == '\\')
            {
                out += "\\\\";
            }
            else if (ch == '"')
            {
                out += "\\\"";
            }
            else if (ch == '\n')
            {
                out += "\\n";
            }
            else
            {
                out.push_back(ch);
            }
        }
        return out;
    }

    std::string statusToString(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "Ok";
        case Status::Unauthorized:
            return "Unauthorized";
        case Status::Forbidden:
            return "Forbidden";
        case Status::NotFound:
            return "NotFound";
        case Status::Conflict:
            return "Conflict";
        case Status::InvalidArgument:
            return "InvalidArgument";
        }
        return "InvalidArgument";
    }

    StringResponse jsonResponse(http::status code, std::string body, unsigned version, bool keep_alive)
    {
        StringResponse res{code, version};
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.keep_alive(keep_alive);
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }

    std::string eventToJson(const Event &event)
    {
        return "{\"revision\":" + std::to_string(event.revision) +
               ",\"timestamp_ms\":" + std::to_string(event.timestamp_ms) +
               ",\"actor_user_id\":\"" + escapeJson(event.actor_user_id) +
               "\",\"action\":\"" + escapeJson(event.action) +
               "\",\"entity\":\"" + escapeJson(event.entity) +
               "\",\"entity_id\":\"" + escapeJson(event.entity_id) +
               "\",\"details\":\"" + escapeJson(event.details) + "\"}";
    }

    std::string eventsArrayToJson(const std::vector<Event> &events)
    {
        std::string body = "[";
        bool first = true;
        for (const auto &event : events)
        {
            if (!first)
            {
                body += ",";
            }
            first = false;
            body += eventToJson(event);
        }
        body += "]";
        return body;
    }

    std::string messagesArrayToJson(const std::vector<ChatMessage> &messages)
    {
        std::string body = "[";
        bool first = true;
        for (const auto &message : messages)
        {
            if (!first)
            {
                body += ",";
            }
            first = false;
            body += "{\"id\":\"" + escapeJson(message.id) +
                    "\",\"user_id\":\"" + escapeJson(message.user_id) +
                    "\",\"text\":\"" + escapeJson(message.text) +
                    "\",\"timestamp_ms\":" + std::to_string(message.timestamp_ms) + "}";
        }
        body += "]";
        return body;
    }

    std::string mapToJsonObject(const std::unordered_map<std::string, double> &values)
    {
        std::string body = "{";
        bool first = true;
        for (const auto &[key, value] : values)
        {
            if (!first)
            {
                body += ",";
            }
            first = false;
            body += "\"" + escapeJson(key) + "\":" + std::to_string(value);
        }
        body += "}";
        return body;
    }

    std::string searchHitsToJson(const std::vector<SearchHit> &hits)
    {
        std::string body = "[";
        bool first = true;
        for (const auto &hit : hits)
        {
            if (!first)
            {
                body += ",";
            }
            first = false;
            body += "{\"kind\":\"" + escapeJson(hit.kind) +
                    "\",\"id\":\"" + escapeJson(hit.id) +
                    "\",\"text\":\"" + escapeJson(hit.text) + "\"}";
        }
        body += "]";
        return body;
    }

    std::string budgetSummaryToJson(const BudgetSummary &summary)
    {
        return "{\"total_expenses\":" + std::to_string(summary.total_expenses) +
               ",\"by_category\":" + mapToJsonObject(summary.by_category) +
               ",\"paid_by_user\":" + mapToJsonObject(summary.paid_by_user) +
               ",\"balance_by_user\":" + mapToJsonObject(summary.balance_by_user) + "}";
    }
}
