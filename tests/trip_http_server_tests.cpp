#include "../include/trip/trip_http_server.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

namespace
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    using tcp = asio::ip::tcp;

    struct HttpResult
    {
        unsigned status_code = 0;
        std::string body;
    };

    std::string urlEncode(const std::string &text)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(text.size() * 3);
        for (unsigned char c : text)
        {
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
            {
                out.push_back(static_cast<char>(c));
            }
            else if (c == ' ')
            {
                out.push_back('+');
            }
            else
            {
                out.push_back('%');
                out.push_back(kHex[(c >> 4) & 0xF]);
                out.push_back(kHex[c & 0xF]);
            }
        }
        return out;
    }

    std::string formBody(std::initializer_list<std::pair<std::string, std::string>> fields)
    {
        std::ostringstream out;
        bool first = true;
        for (const auto &field : fields)
        {
            if (!first)
            {
                out << '&';
            }
            first = false;
            out << urlEncode(field.first) << '=' << urlEncode(field.second);
        }
        return out.str();
    }

    HttpResult doHttpRequest(http::verb method, uint16_t port, const std::string &target, const std::string &body = {})
    {
        asio::io_context io;
        tcp::resolver resolver(io);
        beast::tcp_stream stream(io);

        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
        stream.connect(endpoints);

        http::request<http::string_body> req{method, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::user_agent, "trip-tests");
        req.keep_alive(false);
        if (method == http::verb::post)
        {
            req.set(http::field::content_type, "application/x-www-form-urlencoded");
            req.body() = body;
            req.prepare_payload();
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return HttpResult{res.result_int(), res.body()};
    }

    void sendMalformedAndClose(uint16_t port, const std::string &payload)
    {
        asio::io_context io;
        tcp::resolver resolver(io);
        tcp::socket socket(io);
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
        asio::connect(socket, endpoints);
        asio::write(socket, asio::buffer(payload));
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

    std::string jsonStringField(const std::string &json, const std::string &field)
    {
        const std::string token = "\"" + field + "\":\"";
        const auto start = json.find(token);
        if (start == std::string::npos)
        {
            return {};
        }
        auto pos = start + token.size();
        std::string out;
        while (pos < json.size())
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                ++pos;
                out.push_back(json[pos]);
                ++pos;
                continue;
            }
            if (json[pos] == '"')
            {
                break;
            }
            out.push_back(json[pos]);
            ++pos;
        }
        return out;
    }

    uint64_t jsonUInt64Field(const std::string &json, const std::string &field)
    {
        const std::string token = "\"" + field + "\":";
        const auto start = json.find(token);
        if (start == std::string::npos)
        {
            return 0;
        }
        auto pos = start + token.size();
        auto end = pos;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9')
        {
            ++end;
        }
        if (end == pos)
        {
            return 0;
        }
        return static_cast<uint64_t>(std::stoull(json.substr(pos, end - pos)));
    }

    std::string apiStatus(const HttpResult &result)
    {
        return jsonStringField(result.body, "status");
    }

    struct WsClient
    {
        asio::io_context io;
        tcp::resolver resolver;
        websocket::stream<tcp::socket> ws;

        WsClient(uint16_t port, const std::string &target)
            : resolver(io), ws(io)
        {
            const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
            asio::connect(ws.next_layer(), endpoints);
            ws.handshake("127.0.0.1", target);
        }

        std::string readTextFrame()
        {
            beast::flat_buffer buffer;
            ws.read(buffer);
            return beast::buffers_to_string(buffer.data());
        }

        void close()
        {
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);
        }
    };

    uint64_t fetchRevision(const std::string &token, const std::string &trip_id, uint16_t port)
    {
        const auto rev = doHttpRequest(
            http::verb::get,
            port,
            "/trips/revision?token=" + urlEncode(token) + "&trip_id=" + urlEncode(trip_id));
        if (rev.status_code != 200U || apiStatus(rev) != "Ok")
        {
            return 0;
        }
        return jsonUInt64Field(rev.body, "revision");
    }
}

struct TripContext
{
    std::string token;
    std::string trip_id;
};

std::string makeUniqueLogin(const std::string &base)
{
    static std::atomic<uint64_t> sequence{0};
    return base + "_" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

TripContext createTripContext(uint16_t port, const std::string &login_base, const std::string &trip_title)
{
    const std::string login = makeUniqueLogin(login_base);
    const auto reg = doHttpRequest(
        http::verb::post,
        port,
        "/register",
        formBody({{"login", login}, {"password", "pass"}}));
    if (reg.status_code != 200U || apiStatus(reg) != "Ok")
    {
        return {};
    }

    const auto auth = doHttpRequest(
        http::verb::post,
        port,
        "/login",
        formBody({{"login", login}, {"password", "pass"}}));
    if (auth.status_code != 200U || apiStatus(auth) != "Ok")
    {
        return {};
    }

    const std::string token = jsonStringField(auth.body, "token");
    if (token.empty())
    {
        return {};
    }

    const auto created_trip = doHttpRequest(
        http::verb::post,
        port,
        "/trips/create",
        formBody({{"token", token},
                  {"title", trip_title},
                  {"start_date", "2026-08-01"},
                  {"end_date", "2026-08-02"},
                  {"description", "http-tests"}}));
    if (created_trip.status_code != 200U || apiStatus(created_trip) != "Ok")
    {
        return {};
    }

    const std::string trip_id = jsonStringField(created_trip.body, "trip_id");
    if (trip_id.empty())
    {
        return {};
    }

    return TripContext{token, trip_id};
}

struct ConcurrentTaskAddStats
{
    int successful_creates = 0;
    int hard_failures = 0;
    uint64_t tasks_count = 0;
    uint64_t revision_before = 0;
    uint64_t revision_after = 0;
};

ConcurrentTaskAddStats runConcurrentTaskAdds(
    uint16_t port,
    const std::string &token,
    const std::string &trip_id,
    int threads_count,
    int tasks_per_thread)
{
    ConcurrentTaskAddStats stats;
    stats.revision_before = fetchRevision(token, trip_id, port);

    std::atomic<int> created{0};
    std::atomic<int> hard_failures{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads_count));

    for (int t = 0; t < threads_count; ++t)
    {
        workers.emplace_back([&, t]()
                             {
                for (int i = 0; i < tasks_per_thread; ++i)
                {
                    bool done = false;
                    for (int attempt = 0; attempt < 200 && !done; ++attempt)
                    {
                        const auto rev = doHttpRequest(
                            http::verb::get,
                            port,
                            "/trips/revision?token=" + urlEncode(token) + "&trip_id=" + urlEncode(trip_id));
                        if (rev.status_code != 200U || apiStatus(rev) != "Ok")
                        {
                            hard_failures.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }

                        const uint64_t current_revision = jsonUInt64Field(rev.body, "revision");
                        const auto add = doHttpRequest(
                            http::verb::post,
                            port,
                            "/tasks/add",
                            formBody({
                                {"token", token},
                                {"trip_id", trip_id},
                                {"expected_revision", std::to_string(current_revision)},
                                {"text", "Task " + std::to_string(t) + "-" + std::to_string(i)}}));

                        if (add.status_code != 200U)
                        {
                            hard_failures.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }

                        const std::string status = apiStatus(add);
                        if (status == "Ok")
                        {
                            created.fetch_add(1, std::memory_order_relaxed);
                            done = true;
                        }
                        else if (status != "Conflict")
                        {
                            hard_failures.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                    }

                    if (!done)
                    {
                        hard_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                } });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    const auto snapshot = doHttpRequest(
        http::verb::get,
        port,
        "/trips/snapshot?token=" + urlEncode(token) + "&trip_id=" + urlEncode(trip_id));
    if (snapshot.status_code == 200U && apiStatus(snapshot) == "Ok")
    {
        stats.tasks_count = jsonUInt64Field(snapshot.body, "tasks_count");
        stats.revision_after = jsonUInt64Field(snapshot.body, "revision");
    }

    stats.successful_creates = created.load(std::memory_order_relaxed);
    stats.hard_failures = hard_failures.load(std::memory_order_relaxed);
    return stats;
}

class TripHttpServerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        server_ = std::make_unique<trip::TripHttpServer>("127.0.0.1", 0, 8);
        server_->start();
        port_ = server_->port();
    }

    void TearDown() override
    {
        if (server_)
        {
            server_->stop();
        }
    }

    uint16_t port() const
    {
        return port_;
    }

private:
    std::unique_ptr<trip::TripHttpServer> server_;
    uint16_t port_ = 0;
};

TEST_F(TripHttpServerFixture, ConcurrentTaskCreationPersistsAllTasks)
{
    const TripContext ctx = createTripContext(port(), "owner_http_parallel", "API Trip");
    ASSERT_FALSE(ctx.token.empty());
    ASSERT_FALSE(ctx.trip_id.empty());

    constexpr int threads_count = 10;
    constexpr int tasks_per_thread = 20;
    const ConcurrentTaskAddStats stats = runConcurrentTaskAdds(
        port(),
        ctx.token,
        ctx.trip_id,
        threads_count,
        tasks_per_thread);

    EXPECT_EQ(stats.successful_creates, threads_count * tasks_per_thread);
    EXPECT_EQ(stats.hard_failures, 0);
    EXPECT_EQ(stats.tasks_count, static_cast<uint64_t>(threads_count * tasks_per_thread));
}

TEST_F(TripHttpServerFixture, ConcurrentTaskCreationRevisionDeltaMatchesWrites)
{
    const TripContext ctx = createTripContext(port(), "owner_http_revision", "Revision Trip");
    ASSERT_FALSE(ctx.token.empty());
    ASSERT_FALSE(ctx.trip_id.empty());

    constexpr int threads_count = 6;
    constexpr int tasks_per_thread = 15;
    const ConcurrentTaskAddStats stats = runConcurrentTaskAdds(
        port(),
        ctx.token,
        ctx.trip_id,
        threads_count,
        tasks_per_thread);

    ASSERT_EQ(stats.hard_failures, 0);
    ASSERT_EQ(stats.successful_creates, threads_count * tasks_per_thread);
    ASSERT_GE(stats.revision_after, stats.revision_before);
    EXPECT_EQ(stats.revision_after - stats.revision_before, static_cast<uint64_t>(stats.successful_creates));
}

TEST_F(TripHttpServerFixture, ConcurrentSetDoneOnSameRevisionProducesSingleWinner)
{
    const TripContext ctx = createTripContext(port(), "owner_http_toggle", "Toggle Trip");
    ASSERT_FALSE(ctx.token.empty());
    ASSERT_FALSE(ctx.trip_id.empty());

    const uint64_t base_revision = fetchRevision(ctx.token, ctx.trip_id, port());
    ASSERT_GT(base_revision, 0U);

    const auto created_task = doHttpRequest(
        http::verb::post,
        port(),
        "/tasks/add",
        formBody({{"token", ctx.token},
                  {"trip_id", ctx.trip_id},
                  {"expected_revision", std::to_string(base_revision)},
                  {"text", "Shared task"}}));
    ASSERT_EQ(created_task.status_code, 200U);
    ASSERT_EQ(apiStatus(created_task), "Ok");
    const std::string task_id = jsonStringField(created_task.body, "task_id");
    ASSERT_FALSE(task_id.empty());

    constexpr int rounds = 40;
    for (int round = 0; round < rounds; ++round)
    {
        const uint64_t round_revision = fetchRevision(ctx.token, ctx.trip_id, port());
        ASSERT_GT(round_revision, 0U);

        std::atomic<int> ok_count{0};
        std::atomic<int> conflict_count{0};
        std::atomic<int> other_count{0};

        std::thread thread_a([&]()
                             {
            const auto result = doHttpRequest(
                http::verb::post,
                port(),
                "/tasks/set_done",
                formBody({{"token", ctx.token},
                          {"trip_id", ctx.trip_id},
                          {"expected_revision", std::to_string(round_revision)},
                          {"task_id", task_id},
                          {"done", "true"}}));
            if (result.status_code != 200U)
            {
                other_count.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            const std::string status = apiStatus(result);
            if (status == "Ok")
            {
                ok_count.fetch_add(1, std::memory_order_relaxed);
            }
            else if (status == "Conflict")
            {
                conflict_count.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                other_count.fetch_add(1, std::memory_order_relaxed);
            } });

        std::thread thread_b([&]()
                             {
            const auto result = doHttpRequest(
                http::verb::post,
                port(),
                "/tasks/set_done",
                formBody({{"token", ctx.token},
                          {"trip_id", ctx.trip_id},
                          {"expected_revision", std::to_string(round_revision)},
                          {"task_id", task_id},
                          {"done", "false"}}));
            if (result.status_code != 200U)
            {
                other_count.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            const std::string status = apiStatus(result);
            if (status == "Ok")
            {
                ok_count.fetch_add(1, std::memory_order_relaxed);
            }
            else if (status == "Conflict")
            {
                conflict_count.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                other_count.fetch_add(1, std::memory_order_relaxed);
            } });

        thread_a.join();
        thread_b.join();

        EXPECT_EQ(ok_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(conflict_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(other_count.load(std::memory_order_relaxed), 0);
    }

    const auto snapshot = doHttpRequest(
        http::verb::get,
        port(),
        "/trips/snapshot?token=" + urlEncode(ctx.token) + "&trip_id=" + urlEncode(ctx.trip_id));
    ASSERT_EQ(snapshot.status_code, 200U);
    ASSERT_EQ(apiStatus(snapshot), "Ok");
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "tasks_count"), 1U);
}

TEST_F(TripHttpServerFixture, SurvivesMalformedAndAbruptConnections)
{
    sendMalformedAndClose(port(), "BROKEN / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    std::vector<std::thread> noisy_clients;
    for (int i = 0; i < 16; ++i)
    {
        noisy_clients.emplace_back([this]()
                                   { sendMalformedAndClose(
                                         port(),
                                         "POST /tasks/add HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 200\r\n\r\nshort"); });
    }

    for (auto &client : noisy_clients)
    {
        client.join();
    }

    const auto health = doHttpRequest(http::verb::get, port(), "/health");
    ASSERT_EQ(health.status_code, 200U);
    ASSERT_EQ(apiStatus(health), "Ok");

    const auto unknown = doHttpRequest(http::verb::get, port(), "/unknown-route");
    EXPECT_EQ(unknown.status_code, 404U);
    EXPECT_EQ(apiStatus(unknown), "NotFound");

    const auto reg = doHttpRequest(
        http::verb::post,
        port(),
        "/register",
        formBody({{"login", "after_noise"}, {"password", "pass"}}));
    EXPECT_EQ(reg.status_code, 200U);
    EXPECT_EQ(apiStatus(reg), "Ok");
}

TEST_F(TripHttpServerFixture, StreamsRealtimeWebsocketEvents)
{
    const TripContext ctx = createTripContext(port(), "ws_owner_live", "WS Trip Live");
    ASSERT_FALSE(ctx.token.empty());
    ASSERT_FALSE(ctx.trip_id.empty());

    const uint64_t base_revision = fetchRevision(ctx.token, ctx.trip_id, port());
    ASSERT_GT(base_revision, 0U);

    WsClient live_client(
        port(),
        "/ws/updates?token=" + urlEncode(ctx.token) +
            "&trip_id=" + urlEncode(ctx.trip_id) +
            "&since_revision=" + std::to_string(base_revision));

    const auto add_first = doHttpRequest(
        http::verb::post,
        port(),
        "/tasks/add",
        formBody({{"token", ctx.token},
                  {"trip_id", ctx.trip_id},
                  {"expected_revision", std::to_string(base_revision)},
                  {"text", "task-live"}}));
    ASSERT_EQ(add_first.status_code, 200U);
    ASSERT_EQ(apiStatus(add_first), "Ok");

    const std::string push1 = live_client.readTextFrame();
    EXPECT_NE(push1.find("\"type\":\"event\""), std::string::npos);
    EXPECT_NE(push1.find("\"entity\":\"task\""), std::string::npos);
    const uint64_t revision_after_first = jsonUInt64Field(push1, "revision");
    ASSERT_GT(revision_after_first, base_revision);

    live_client.close();
}

TEST_F(TripHttpServerFixture, ReplaysMissedEventsAfterReconnectBySinceRevision)
{
    const TripContext ctx = createTripContext(port(), "ws_owner_reconnect", "WS Trip Replay");
    ASSERT_FALSE(ctx.token.empty());
    ASSERT_FALSE(ctx.trip_id.empty());

    const uint64_t base_revision = fetchRevision(ctx.token, ctx.trip_id, port());
    ASSERT_GT(base_revision, 0U);

    const auto add_first = doHttpRequest(
        http::verb::post,
        port(),
        "/tasks/add",
        formBody({{"token", ctx.token},
                  {"trip_id", ctx.trip_id},
                  {"expected_revision", std::to_string(base_revision)},
                  {"text", "task-first"}}));
    ASSERT_EQ(add_first.status_code, 200U);
    ASSERT_EQ(apiStatus(add_first), "Ok");

    const uint64_t revision_after_first = fetchRevision(ctx.token, ctx.trip_id, port());
    ASSERT_GT(revision_after_first, base_revision);

    const auto add_second = doHttpRequest(
        http::verb::post,
        port(),
        "/tasks/add",
        formBody({{"token", ctx.token},
                  {"trip_id", ctx.trip_id},
                  {"expected_revision", std::to_string(revision_after_first)},
                  {"text", "task-reconnect"}}));
    ASSERT_EQ(add_second.status_code, 200U);
    ASSERT_EQ(apiStatus(add_second), "Ok");

    WsClient reconnect_client(
        port(),
        "/ws/updates?token=" + urlEncode(ctx.token) +
            "&trip_id=" + urlEncode(ctx.trip_id) +
            "&since_revision=" + std::to_string(revision_after_first));

    const std::string backlog_push = reconnect_client.readTextFrame();
    EXPECT_NE(backlog_push.find("\"type\":\"event\""), std::string::npos);
    EXPECT_NE(backlog_push.find("\"entity\":\"task\""), std::string::npos);
    const uint64_t revision_after_reconnect = jsonUInt64Field(backlog_push, "revision");
    EXPECT_GT(revision_after_reconnect, revision_after_first);

    const auto events_since = doHttpRequest(
        http::verb::get,
        port(),
        "/events/since?token=" + urlEncode(ctx.token) +
            "&trip_id=" + urlEncode(ctx.trip_id) +
            "&since_revision=" + std::to_string(revision_after_first));
    ASSERT_EQ(events_since.status_code, 200U);
    ASSERT_EQ(apiStatus(events_since), "Ok");
    EXPECT_NE(events_since.body.find("\"events\":[{"), std::string::npos);
    EXPECT_NE(events_since.body.find("\"entity\":\"task\""), std::string::npos);

    reconnect_client.close();
}
