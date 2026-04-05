#include "trip_http_server_routes_support.hpp"

namespace trip::detail
{
    bool handleTaskChatRoutes(const RequestContext &ctx, const std::string &path, StringResponse &response)
    {
        if (path == "/tasks/add" && ctx.req.method() == http::verb::post)
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

            Task task;
            task.text = ctx.param("text");
            task.assignee_user_id = ctx.param("assignee_user_id");
            task.deadline = ctx.param("deadline");

            const auto result = ctx.service.addTask(ctx.param("token"), ctx.param("trip_id"), expected_revision, task);
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &task_id)
                { return "\"task_id\":\"" + escapeJson(task_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            return true;
        }

        if (path == "/tasks/update" && ctx.req.method() == http::verb::post)
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

            Task task;
            task.id = ctx.param("task_id");
            task.text = ctx.param("text");
            task.assignee_user_id = ctx.param("assignee_user_id");
            task.deadline = ctx.param("deadline");
            try
            {
                task.done = parseBool(ctx.param("done"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid done", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            const auto result = ctx.service.updateTask(ctx.param("token"), ctx.param("trip_id"), expected_revision, task);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/tasks/set_done" && ctx.req.method() == http::verb::post)
        {
            uint64_t expected_revision = 0;
            bool done = false;
            try
            {
                expected_revision = parseUint64(ctx.param("expected_revision"));
                done = parseBool(ctx.param("done"));
            }
            catch (const std::exception &)
            {
                response = makeStatusResponse("InvalidArgument", "Invalid expected_revision or done", ctx.req.version(), ctx.req.keep_alive());
                return true;
            }

            const auto result = ctx.service.setTaskDone(ctx.param("token"), ctx.param("trip_id"), expected_revision, ctx.param("task_id"), done);
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/tasks/remove" && ctx.req.method() == http::verb::post)
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

            const auto result = ctx.service.removeTask(ctx.param("token"), ctx.param("trip_id"), expected_revision, ctx.param("task_id"));
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            response = makeStatusResultResponse(result, ctx.req.version(), ctx.req.keep_alive());
            return true;
        }

        if (path == "/chat/send" && ctx.req.method() == http::verb::post)
        {
            const auto result = ctx.service.sendMessage(ctx.param("token"), ctx.param("trip_id"), ctx.param("text"));
            response = makeStatusOrResponse<std::string>(
                result,
                [](const std::string &message_id)
                { return "\"message_id\":\"" + escapeJson(message_id) + "\""; },
                ctx.req.version(),
                ctx.req.keep_alive());
            if (result.ok())
            {
                ctx.publish_latest_event(ctx.param("token"), ctx.param("trip_id"));
            }
            return true;
        }

        if (path == "/chat/list" && ctx.req.method() == http::verb::get)
        {
            const auto result = ctx.service.getMessages(ctx.param("token"), ctx.param("trip_id"));
            response = makeStatusOrResponse<std::vector<ChatMessage>>(
                result,
                [](const std::vector<ChatMessage> &messages)
                {
                    return "\"messages\":" + messagesArrayToJson(messages) + ",\"messages_count\":" + std::to_string(messages.size());
                },
                ctx.req.version(),
                ctx.req.keep_alive());
            return true;
        }

        return false;
    }
}
