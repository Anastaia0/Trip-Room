#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    bool handleBudgetSearchSyncRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response)
    {
        if (path == "/budget/settings" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            double total_limit = -1.0;
            try
            {
                expected_revision = parseUint64(ctx.param("expected_revision"));
                total_limit = parseDouble(ctx.param("total_limit"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid expected_revision or total_limit", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            BudgetSettings settings;
            settings.currency = ctx.param("currency");
            settings.total_limit = total_limit;
            const auto result = ctx.service.setBudgetSettings(ctx.param("token"), ctx.param("trip_id"), expected_revision, settings);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/budget/add_expense" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            double amount = 0.0;
            try
            {
                expected_revision = parseUint64(ctx.param("expected_revision"));
                amount = parseDouble(ctx.param("amount"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid expected_revision or amount", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            Expense expense;
            expense.amount = amount;
            expense.category = ctx.param("category");
            expense.paid_by_user_id = ctx.param("paid_by_user_id");
            expense.comment = ctx.param("comment");
            expense.date = ctx.param("date");
            expense.day_id = ctx.param("day_id");

            const auto result = ctx.service.addExpense(ctx.param("token"), ctx.param("trip_id"), expected_revision, expense);
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &expense_id)
                { return "\"expense_id\":\"" + escapeJson(expense_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            return true;
        }

        if (path == "/budget/summary" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.getBudgetSummary(ctx.param("token"), ctx.param("trip_id"));
            response = makeStatusOrResponse<BudgetSummary>(
                result,
                [](const BudgetSummary &summary)
                { return "\"summary\":" + budgetSummaryToJson(summary); },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/search" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.searchInTrip(ctx.param("token"), ctx.param("trip_id"), ctx.param("query"));
            response = makeStatusOrResponse<std::vector<SearchHit>>(
                result,
                [](const std::vector<SearchHit> &hits)
                {
                    return "\"hits\":" + searchHitsToJson(hits) + ",\"hits_count\":" + std::to_string(hits.size());
                },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/export_json" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.exportTripJson(ctx.param("token"), ctx.param("trip_id"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &json)
                { return "\"trip_json\":\"" + escapeJson(json) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/import_json" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.importTripJson(ctx.param("token"), ctx.param("trip_json"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &trip_id)
                { return "\"trip_id\":\"" + escapeJson(trip_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), result.value);
            }
            return true;
        }

        if (path == "/events/since" && ctx.req.method() == http::verb::get)
        {
            uint64_t since_revision = 0;
            try
            {
                since_revision = parseUint64(ctx.param("since_revision"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid since_revision", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            const auto result = ctx.service.getEventsSince(ctx.param("token"), ctx.param("trip_id"), since_revision);
            response = makeStatusOrResponse<std::vector<Event>>(
                result,
                [](const std::vector<Event> &events)
                {
                    uint64_t latest_revision = events.empty() ? 0 : events.back().revision;
                    return "\"events\":" + eventsArrayToJson(events) + ",\"latest_revision\":" + std::to_string(latest_revision);
                },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/snapshot" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.getTripSnapshot(ctx.param("token"), ctx.param("trip_id"));
            response = makeStatusOrResponse<Trip>(
                result,
                [](const Trip &trip)
                {
                    return "\"trip_id\":\"" + escapeJson(trip.id) + "\",\"revision\":" + std::to_string(trip.revision) +
                           ",\"tasks_count\":" + std::to_string(trip.tasks.size());
                },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/health" && ctx.req.method() == http::verb::get)
        {
            response = jsonResponse(http::status::ok, "{\"status\":\"Ok\"}", ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        return false;
    }
}
