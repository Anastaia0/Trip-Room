#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    bool handleTripCollabRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response)
    {
        if (path == "/trips/list" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.listTrips(ctx.param("token"));
            response = makeStatusOrResponse<std::vector<TripSummary>>(
                result,
                [](const std::vector<TripSummary> &trips)
                {
                    return "\"trips\":" + tripSummariesToJson(trips) + ",\"trips_count\":" + std::to_string(trips.size());
                },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/create" && ctx.req.method() == http::verb::post)
        {
            TripInfo info;
            info.title = ctx.param("title");
            info.start_date = ctx.param("start_date");
            info.end_date = ctx.param("end_date");
            info.description = ctx.param("description");

            const auto result = ctx.service.createTrip(ctx.param("token"), info);
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

        if (path == "/trips/delete" && ctx.req.method() == http::verb::post)
        {
            response = makeStatusResultResponse(
                ctx.service.deleteTrip(ctx.param("token"), ctx.param("trip_id")),
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/update_info" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            try
            {
                expected_revision = parseUint64(ctx.param("expected_revision"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid expected_revision", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            TripInfo info;
            info.title = ctx.param("title");
            info.start_date = ctx.param("start_date");
            info.end_date = ctx.param("end_date");
            info.description = ctx.param("description");

            const auto result = ctx.service.updateTripInfo(ctx.param("token"), ctx.param("trip_id"), expected_revision, info);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/trips/revision" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.getTripRevision(ctx.param("token"), ctx.param("trip_id"));
            response = makeStatusOrResponse<uint64_t>(
                result,
                [](uint64_t revision)
                { return "\"revision\":" + std::to_string(revision); },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/invites/create" && ctx.req.method() == http::verb::post)
        {
            Role role = Role::Viewer;
            try
            {
                role = parseRole(ctx.param("role"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid role", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            const auto result = ctx.service.createInvite(ctx.param("token"), ctx.param("trip_id"), role);
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &invite_code)
                { return "\"invite_code\":\"" + escapeJson(invite_code) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        if (path == "/invites/accept" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.acceptInvite(ctx.param("token"), ctx.param("invite_code"));
            if (result.ok())
            {
                const auto trip = ctx.service.getTripSnapshot(ctx.param("token"), ctx.param("trip_id"));
                if (trip.ok())
                {
                    ctx.publish_latest_event(ctx.param("token"), trip.value.id);
                }
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/members/change_role" && ctx.req.method() == http::verb::post)
        {
            Role role = Role::Viewer;
            try
            {
                role = parseRole(ctx.param("new_role"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid new_role", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            const auto result = ctx.service.changeMemberRole(ctx.param("token"), ctx.param("trip_id"), ctx.param("target_user_id"), role);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/members/remove" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.removeMember(ctx.param("token"), ctx.param("trip_id"), ctx.param("target_user_id"));
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
