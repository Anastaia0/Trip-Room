#include "trip_http_server_detail.hpp"

namespace trip::detail
{
    namespace
    {
        std::string roleMapToJson(const std::unordered_map<std::string, Role> &members)
        {
            std::string body = "{";
            bool first = true;
            for (const auto &[user_id, role] : members)
            {
                if (!first)
                {
                    body += ",";
                }
                first = false;
                body += "\"" + escapeJson(user_id) + "\":\"" + escapeJson(roleToString(role)) + "\"";
            }
            body += "}";
            return body;
        }

        std::string tripInfoToJson(const TripInfo &info)
        {
            return "{\"title\":\"" + escapeJson(info.title) +
                   "\",\"start_date\":\"" + escapeJson(info.start_date) +
                   "\",\"end_date\":\"" + escapeJson(info.end_date) +
                   "\",\"description\":\"" + escapeJson(info.description) + "\"}";
        }

        std::string planItemToJson(const PlanItem &item)
        {
            return "{\"id\":\"" + escapeJson(item.id) +
                   "\",\"name\":\"" + escapeJson(item.name) +
                   "\",\"time\":\"" + escapeJson(item.time) +
                   "\",\"notes\":\"" + escapeJson(item.notes) +
                   "\",\"category\":\"" + escapeJson(item.category) +
                   "\",\"link\":\"" + escapeJson(item.link) + "\"}";
        }

        std::string planItemsArrayToJson(const std::vector<PlanItem> &items)
        {
            std::string body = "[";
            bool first = true;
            for (const auto &item : items)
            {
                if (!first)
                {
                    body += ",";
                }
                first = false;
                body += planItemToJson(item);
            }
            body += "]";
            return body;
        }

        std::string dayToJson(const Day &day)
        {
            return "{\"id\":\"" + escapeJson(day.id) +
                   "\",\"name\":\"" + escapeJson(day.name) +
                   "\",\"items\":" + planItemsArrayToJson(day.items) +
                   ",\"items_count\":" + std::to_string(day.items.size()) + "}";
        }

        std::string daysArrayToJson(const std::vector<Day> &days)
        {
            std::string body = "[";
            bool first = true;
            for (const auto &day : days)
            {
                if (!first)
                {
                    body += ",";
                }
                first = false;
                body += dayToJson(day);
            }
            body += "]";
            return body;
        }

        std::string taskToJson(const Task &task)
        {
            return "{\"id\":\"" + escapeJson(task.id) +
                   "\",\"text\":\"" + escapeJson(task.text) +
                   "\",\"done\":" + std::string(task.done ? "true" : "false") +
                   ",\"assignee_user_id\":\"" + escapeJson(task.assignee_user_id) +
                   "\",\"deadline\":\"" + escapeJson(task.deadline) + "\"}";
        }

        std::string tasksArrayToJson(const std::vector<Task> &tasks)
        {
            std::string body = "[";
            bool first = true;
            for (const auto &task : tasks)
            {
                if (!first)
                {
                    body += ",";
                }
                first = false;
                body += taskToJson(task);
            }
            body += "]";
            return body;
        }

        std::string expenseToJson(const Expense &expense)
        {
            return "{\"id\":\"" + escapeJson(expense.id) +
                   "\",\"amount\":" + std::to_string(expense.amount) +
                   ",\"category\":\"" + escapeJson(expense.category) +
                   "\",\"paid_by_user_id\":\"" + escapeJson(expense.paid_by_user_id) +
                   "\",\"comment\":\"" + escapeJson(expense.comment) +
                   "\",\"date\":\"" + escapeJson(expense.date) +
                   "\",\"day_id\":\"" + escapeJson(expense.day_id) + "\"}";
        }

        std::string expensesArrayToJson(const std::vector<Expense> &expenses)
        {
            std::string body = "[";
            bool first = true;
            for (const auto &expense : expenses)
            {
                if (!first)
                {
                    body += ",";
                }
                first = false;
                body += expenseToJson(expense);
            }
            body += "]";
            return body;
        }

        std::string tripSummaryToJson(const TripSummary &summary)
        {
            return "{\"id\":\"" + escapeJson(summary.id) +
                   "\",\"info\":" + tripInfoToJson(summary.info) +
                   ",\"my_role\":\"" + escapeJson(roleToString(summary.my_role)) +
                   "\",\"revision\":" + std::to_string(summary.revision) +
                   ",\"members_count\":" + std::to_string(summary.members_count) +
                   ",\"days_count\":" + std::to_string(summary.days_count) +
                   ",\"tasks_count\":" + std::to_string(summary.tasks_count) +
                   ",\"expenses_count\":" + std::to_string(summary.expenses_count) + "}";
        }
    }

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

    std::string roleToString(Role role)
    {
        switch (role)
        {
        case Role::Owner:
            return "Owner";
        case Role::Editor:
            return "Editor";
        case Role::Viewer:
            return "Viewer";
        }
        return "Viewer";
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

    std::string tripToJson(const Trip &trip)
    {
        return "{\"id\":\"" + escapeJson(trip.id) +
               "\",\"info\":" + tripInfoToJson(trip.info) +
               ",\"members\":" + roleMapToJson(trip.members) +
               ",\"days\":" + daysArrayToJson(trip.days) +
               ",\"tasks\":" + tasksArrayToJson(trip.tasks) +
               ",\"budget\":{\"currency\":\"" + escapeJson(trip.budget.currency) +
               "\",\"total_limit\":" + std::to_string(trip.budget.total_limit) + "}" +
               ",\"expenses\":" + expensesArrayToJson(trip.expenses) +
               ",\"chat\":" + messagesArrayToJson(trip.chat) +
               ",\"events\":" + eventsArrayToJson(trip.events) +
               ",\"revision\":" + std::to_string(trip.revision) +
               ",\"members_count\":" + std::to_string(trip.members.size()) +
               ",\"days_count\":" + std::to_string(trip.days.size()) +
               ",\"tasks_count\":" + std::to_string(trip.tasks.size()) +
               ",\"expenses_count\":" + std::to_string(trip.expenses.size()) +
               ",\"messages_count\":" + std::to_string(trip.chat.size()) +
               ",\"events_count\":" + std::to_string(trip.events.size()) + "}";
    }

    std::string tripSummariesToJson(const std::vector<TripSummary> &summaries)
    {
        std::string body = "[";
        bool first = true;
        for (const auto &summary : summaries)
        {
            if (!first)
            {
                body += ",";
            }
            first = false;
            body += tripSummaryToJson(summary);
        }
        body += "]";
        return body;
    }
}
