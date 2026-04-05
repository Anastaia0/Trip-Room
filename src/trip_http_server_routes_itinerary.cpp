#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    namespace
    {
        bool parseExpectedRevision(const RequestContext &ctx, StringResponse &response, uint64_t &expected_revision)
        {
            try
            {
                expected_revision = parseUint64(ctx.param("expected_revision"));
                return true;
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid expected_revision", ctx.req.version(), ctx.req.keep_alive());
                return false;
            }
        }
    }

    bool handleItineraryRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response)
    {
        if (path == "/days/add" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.addDay(ctx.param("token"), ctx.param("trip_id"), expected_revision, ctx.param("day_name"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &day_id)
                { return "\"day_id\":\"" + escapeJson(day_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            return true;
        }

        if (path == "/days/rename" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.renameDay(ctx.param("token"), ctx.param("trip_id"), expected_revision, ctx.param("day_id"), ctx.param("new_name"));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/days/remove" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.removeDay(ctx.param("token"), ctx.param("trip_id"), expected_revision, ctx.param("day_id"));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/days/reorder" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.reorderDays(ctx.param("token"), ctx.param("trip_id"), expected_revision, splitCsv(ctx.param("day_ids_order")));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/plan/add" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            PlanItem item;
            item.name = ctx.param("name");
            item.time = ctx.param("time");
            item.notes = ctx.param("notes");
            item.category = ctx.param("category");
            item.link = ctx.param("link");

            const auto result = ctx.service.addPlanItem(ctx.param("token"), ctx.param("trip_id"), ctx.param("day_id"), expected_revision, item);
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &item_id)
                { return "\"item_id\":\"" + escapeJson(item_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            return true;
        }

        if (path == "/plan/update" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            PlanItem item;
            item.id = ctx.param("item_id");
            item.name = ctx.param("name");
            item.time = ctx.param("time");
            item.notes = ctx.param("notes");
            item.category = ctx.param("category");
            item.link = ctx.param("link");

            const auto result = ctx.service.updatePlanItem(ctx.param("token"), ctx.param("trip_id"), ctx.param("day_id"), expected_revision, item);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/plan/remove" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.removePlanItem(ctx.param("token"), ctx.param("trip_id"), ctx.param("day_id"), expected_revision, ctx.param("item_id"));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/plan/reorder" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            if (!parseExpectedRevision(ctx, response, expected_revision))
            {
                return true;
            }

            const auto result = ctx.service.reorderPlanItems(ctx.param("token"), ctx.param("trip_id"), ctx.param("day_id"), expected_revision, splitCsv(ctx.param("item_ids_order")));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        return false;
    }
}
