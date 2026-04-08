#include "../include/trip/trip_http_server.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
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

    struct UserSession
    {
        std::string login;
        std::string user_id;
        std::string token;
    };

    struct TripContext
    {
        UserSession owner;
        std::string trip_id;
    };

    struct ConcurrentWriteStats
    {
        int successful_writes = 0;
        int conflict_retries = 0;
        int hard_failures = 0;
        uint64_t revision_before = 0;
        uint64_t revision_after = 0;
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

    double jsonDoubleField(const std::string &json, const std::string &field)
    {
        const std::string token = "\"" + field + "\":";
        const auto start = json.find(token);
        if (start == std::string::npos)
        {
            return 0.0;
        }

        auto pos = start + token.size();
        while (pos < json.size() && json[pos] == ' ')
        {
            ++pos;
        }

        auto end = pos;
        if (end < json.size() && json[end] == '-')
        {
            ++end;
        }
        while (end < json.size() && json[end] >= '0' && json[end] <= '9')
        {
            ++end;
        }
        if (end < json.size() && json[end] == '.')
        {
            ++end;
            while (end < json.size() && json[end] >= '0' && json[end] <= '9')
            {
                ++end;
            }
        }
        if (end == pos)
        {
            return 0.0;
        }
        return std::stod(json.substr(pos, end - pos));
    }

    std::size_t countOccurrences(const std::string &text, const std::string &needle)
    {
        std::size_t count = 0;
        std::size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    std::string apiStatus(const HttpResult &result)
    {
        return jsonStringField(result.body, "status");
    }

    void classifyMutationResult(
        const HttpResult &result,
        std::atomic<int> &ok_count,
        std::atomic<int> &conflict_count,
        std::atomic<int> &other_count)
    {
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
        }
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

    std::string makeUniqueLogin(const std::string &base)
    {
        static std::atomic<uint64_t> sequence{0};
        return base + "_" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    }

    UserSession createUserSession(uint16_t port, const std::string &login_base)
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

        UserSession session;
        session.login = login;
        session.user_id = jsonStringField(reg.body, "user_id");
        session.token = jsonStringField(auth.body, "token");
        return session;
    }

    TripContext createTripContext(uint16_t port, const std::string &login_base, const std::string &trip_title)
    {
        TripContext ctx;
        ctx.owner = createUserSession(port, login_base);
        if (ctx.owner.token.empty())
        {
            return {};
        }

        const auto created_trip = doHttpRequest(
            http::verb::post,
            port,
            "/trips/create",
            formBody({{"token", ctx.owner.token},
                      {"title", trip_title},
                      {"start_date", "2026-08-01"},
                      {"end_date", "2026-08-02"},
                      {"description", "http-tests"}}));
        if (created_trip.status_code != 200U || apiStatus(created_trip) != "Ok")
        {
            return {};
        }

        ctx.trip_id = jsonStringField(created_trip.body, "trip_id");
        if (ctx.trip_id.empty())
        {
            return {};
        }

        return ctx;
    }

    HttpResult addTaskRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &text)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/tasks/add",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"text", text}}));
    }

    HttpResult setTaskDoneRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &task_id,
        bool done)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/tasks/set_done",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"task_id", task_id},
                      {"done", done ? "true" : "false"}}));
    }

    HttpResult addDayRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &day_name)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/days/add",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"day_name", day_name}}));
    }

    HttpResult renameDayRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &day_id,
        const std::string &new_name)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/days/rename",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"day_id", day_id},
                      {"new_name", new_name}}));
    }

    HttpResult addPlanItemRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        const std::string &day_id,
        uint64_t expected_revision,
        const std::string &name,
        const std::string &time,
        const std::string &notes,
        const std::string &category)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/plan/add",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"day_id", day_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"name", name},
                      {"time", time},
                      {"notes", notes},
                      {"category", category},
                      {"link", "https://example.test/" + name}}));
    }

    HttpResult setBudgetSettingsRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &currency,
        double total_limit)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/budget/settings",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"currency", currency},
                      {"total_limit", std::to_string(total_limit)}}));
    }

    HttpResult addExpenseRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        double amount,
        const std::string &category,
        const std::string &paid_by_user_id,
        const std::string &comment,
        const std::string &date)
    {
        return doHttpRequest(
            http::verb::post,
            port,
            "/budget/add_expense",
            formBody({{"token", token},
                      {"trip_id", trip_id},
                      {"expected_revision", std::to_string(expected_revision)},
                      {"amount", std::to_string(amount)},
                      {"category", category},
                      {"paid_by_user_id", paid_by_user_id},
                      {"comment", comment},
                      {"date", date},
                      {"day_id", ""}}));
    }

    HttpResult searchTripRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        const std::string &query)
    {
        return doHttpRequest(
            http::verb::get,
            port,
            "/search?token=" + urlEncode(token) +
                "&trip_id=" + urlEncode(trip_id) +
                "&query=" + urlEncode(query));
    }

    HttpResult getEventsSinceRequest(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        uint64_t since_revision)
    {
        return doHttpRequest(
            http::verb::get,
            port,
            "/events/since?token=" + urlEncode(token) +
                "&trip_id=" + urlEncode(trip_id) +
                "&since_revision=" + std::to_string(since_revision));
    }

    HttpResult getSnapshotRequest(uint16_t port, const std::string &token, const std::string &trip_id)
    {
        return doHttpRequest(
            http::verb::get,
            port,
            "/trips/snapshot?token=" + urlEncode(token) +
                "&trip_id=" + urlEncode(trip_id));
    }

    HttpResult exportTripJsonRequest(uint16_t port, const std::string &token, const std::string &trip_id)
    {
        return doHttpRequest(
            http::verb::get,
            port,
            "/trips/export_json?token=" + urlEncode(token) +
                "&trip_id=" + urlEncode(trip_id));
    }

    HttpResult getBudgetSummaryRequest(uint16_t port, const std::string &token, const std::string &trip_id)
    {
        return doHttpRequest(
            http::verb::get,
            port,
            "/budget/summary?token=" + urlEncode(token) +
                "&trip_id=" + urlEncode(trip_id));
    }

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

    template <typename AttemptFn>
    ConcurrentWriteStats runConcurrentRevisionRetriedWrites(
        uint16_t port,
        const std::string &token,
        const std::string &trip_id,
        int threads_count,
        int writes_per_thread,
        AttemptFn &&attempt)
    {
        ConcurrentWriteStats stats;
        stats.revision_before = fetchRevision(token, trip_id, port);

        std::atomic<int> successful_writes{0};
        std::atomic<int> conflict_retries{0};
        std::atomic<int> hard_failures{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threads_count));

        for (int t = 0; t < threads_count; ++t)
        {
            workers.emplace_back([&, t]()
                                 {
                for (int i = 0; i < writes_per_thread; ++i)
                {
                    bool done = false;
                    for (int attempt_index = 0; attempt_index < 200 && !done; ++attempt_index)
                    {
                        const uint64_t current_revision = fetchRevision(token, trip_id, port);
                        if (current_revision == 0U)
                        {
                            break;
                        }

                        const auto result = attempt(current_revision, t, i);
                        if (result.status_code != 200U)
                        {
                            break;
                        }

                        const std::string status = apiStatus(result);
                        if (status == "Ok")
                        {
                            successful_writes.fetch_add(1, std::memory_order_relaxed);
                            done = true;
                        }
                        else if (status == "Conflict")
                        {
                            conflict_retries.fetch_add(1, std::memory_order_relaxed);
                        }
                        else
                        {
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

        stats.successful_writes = successful_writes.load(std::memory_order_relaxed);
        stats.conflict_retries = conflict_retries.load(std::memory_order_relaxed);
        stats.hard_failures = hard_failures.load(std::memory_order_relaxed);
        stats.revision_after = fetchRevision(token, trip_id, port);
        return stats;
    }
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

class TripHttpServerTripFixture : public TripHttpServerFixture
{
protected:
    void SetUp() override
    {
        TripHttpServerFixture::SetUp();
        ctx_ = createTripContext(port(), "owner_http_fixture", "HTTP Fixture Trip");
        ASSERT_FALSE(ctx_.owner.token.empty());
        ASSERT_FALSE(ctx_.owner.user_id.empty());
        ASSERT_FALSE(ctx_.trip_id.empty());
    }

    const UserSession &owner() const
    {
        return ctx_.owner;
    }

    const std::string &tripId() const
    {
        return ctx_.trip_id;
    }

    uint64_t currentRevision() const
    {
        return fetchRevision(owner().token, tripId(), port());
    }

    std::string addTaskOrFail(const std::string &text)
    {
        const uint64_t revision = currentRevision();
        EXPECT_GT(revision, 0U);
        if (revision == 0U)
        {
            return {};
        }

        const auto add = addTaskRequest(port(), owner().token, tripId(), revision, text);
        EXPECT_EQ(add.status_code, 200U);
        EXPECT_EQ(apiStatus(add), "Ok");
        return jsonStringField(add.body, "task_id");
    }

    std::string addDayOrFail(const std::string &name)
    {
        const uint64_t revision = currentRevision();
        EXPECT_GT(revision, 0U);
        if (revision == 0U)
        {
            return {};
        }

        const auto add = addDayRequest(port(), owner().token, tripId(), revision, name);
        EXPECT_EQ(add.status_code, 200U);
        EXPECT_EQ(apiStatus(add), "Ok");
        return jsonStringField(add.body, "day_id");
    }

private:
    TripContext ctx_;
};

TEST_F(TripHttpServerTripFixture, ConcurrentTaskCreationPersistsAllTasks)
{
    constexpr int threads_count = 10;
    constexpr int tasks_per_thread = 20;
    const auto stats = runConcurrentRevisionRetriedWrites(
        port(),
        owner().token,
        tripId(),
        threads_count,
        tasks_per_thread,
        [&](uint64_t current_revision, int t, int i)
        {
            return addTaskRequest(
                port(),
                owner().token,
                tripId(),
                current_revision,
                "http-task-" + std::to_string(t) + "-" + std::to_string(i));
        });

    EXPECT_EQ(stats.successful_writes, threads_count * tasks_per_thread);
    EXPECT_EQ(stats.hard_failures, 0);

    const auto snapshot = getSnapshotRequest(port(), owner().token, tripId());
    ASSERT_EQ(snapshot.status_code, 200U);
    ASSERT_EQ(apiStatus(snapshot), "Ok");
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "tasks_count"), static_cast<uint64_t>(threads_count * tasks_per_thread));
}

TEST_F(TripHttpServerTripFixture, ConcurrentTaskCreationRevisionDeltaMatchesWrites)
{
    constexpr int threads_count = 6;
    constexpr int tasks_per_thread = 15;
    const auto stats = runConcurrentRevisionRetriedWrites(
        port(),
        owner().token,
        tripId(),
        threads_count,
        tasks_per_thread,
        [&](uint64_t current_revision, int t, int i)
        {
            return addTaskRequest(
                port(),
                owner().token,
                tripId(),
                current_revision,
                "revision-task-" + std::to_string(t) + "-" + std::to_string(i));
        });

    ASSERT_EQ(stats.hard_failures, 0);
    ASSERT_EQ(stats.successful_writes, threads_count * tasks_per_thread);
    ASSERT_GE(stats.revision_after, stats.revision_before);
    EXPECT_EQ(stats.revision_after - stats.revision_before, static_cast<uint64_t>(stats.successful_writes));
}

TEST_F(TripHttpServerTripFixture, ConcurrentTaskSetDoneOnSameRevisionProducesSingleWinner)
{
    const std::string task_id = addTaskOrFail("Shared task");
    ASSERT_FALSE(task_id.empty());

    constexpr int rounds = 40;
    for (int round = 0; round < rounds; ++round)
    {
        const uint64_t round_revision = currentRevision();
        ASSERT_GT(round_revision, 0U);

        std::atomic<int> ok_count{0};
        std::atomic<int> conflict_count{0};
        std::atomic<int> other_count{0};

        std::thread thread_a([&]()
                             {
            classifyMutationResult(
                setTaskDoneRequest(port(), owner().token, tripId(), round_revision, task_id, true),
                ok_count,
                conflict_count,
                other_count); });

        std::thread thread_b([&]()
                             {
            classifyMutationResult(
                setTaskDoneRequest(port(), owner().token, tripId(), round_revision, task_id, false),
                ok_count,
                conflict_count,
                other_count); });

        thread_a.join();
        thread_b.join();

        EXPECT_EQ(ok_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(conflict_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(other_count.load(std::memory_order_relaxed), 0);
    }

    const auto snapshot = getSnapshotRequest(port(), owner().token, tripId());
    ASSERT_EQ(snapshot.status_code, 200U);
    ASSERT_EQ(apiStatus(snapshot), "Ok");
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "tasks_count"), 1U);
}

TEST_F(TripHttpServerTripFixture, ConcurrentDayCreationPersistsAllDaysInExportJson)
{
    constexpr int threads_count = 6;
    constexpr int days_per_thread = 12;
    const auto stats = runConcurrentRevisionRetriedWrites(
        port(),
        owner().token,
        tripId(),
        threads_count,
        days_per_thread,
        [&](uint64_t current_revision, int t, int i)
        {
            return addDayRequest(
                port(),
                owner().token,
                tripId(),
                current_revision,
                "http-day-" + std::to_string(t) + "-" + std::to_string(i));
        });

    EXPECT_EQ(stats.successful_writes, threads_count * days_per_thread);
    EXPECT_EQ(stats.hard_failures, 0);
    EXPECT_EQ(stats.revision_after - stats.revision_before, static_cast<uint64_t>(stats.successful_writes));

    const auto exported = exportTripJsonRequest(port(), owner().token, tripId());
    ASSERT_EQ(exported.status_code, 200U);
    ASSERT_EQ(apiStatus(exported), "Ok");
    EXPECT_EQ(countOccurrences(exported.body, "\\\"name\\\":\\\"http-day-"), static_cast<std::size_t>(threads_count * days_per_thread));

    const auto events = getEventsSinceRequest(port(), owner().token, tripId(), stats.revision_before);
    ASSERT_EQ(events.status_code, 200U);
    ASSERT_EQ(apiStatus(events), "Ok");
    EXPECT_EQ(countOccurrences(events.body, "\"entity\":\"day\""), static_cast<std::size_t>(threads_count * days_per_thread));
    EXPECT_EQ(jsonUInt64Field(events.body, "latest_revision"), stats.revision_after);
}

TEST_F(TripHttpServerTripFixture, ConcurrentDayRenameOnSameRevisionProducesSingleWinner)
{
    const std::string day_id = addDayOrFail("Initial day");
    ASSERT_FALSE(day_id.empty());

    constexpr int rounds = 30;
    for (int round = 0; round < rounds; ++round)
    {
        const uint64_t round_revision = currentRevision();
        ASSERT_GT(round_revision, 0U);

        const std::string name_a = "rename-a-" + std::to_string(round);
        const std::string name_b = "rename-b-" + std::to_string(round);

        std::atomic<int> ok_count{0};
        std::atomic<int> conflict_count{0};
        std::atomic<int> other_count{0};

        std::thread rename_a([&]()
                             {
            classifyMutationResult(
                renameDayRequest(port(), owner().token, tripId(), round_revision, day_id, name_a),
                ok_count,
                conflict_count,
                other_count); });

        std::thread rename_b([&]()
                             {
            classifyMutationResult(
                renameDayRequest(port(), owner().token, tripId(), round_revision, day_id, name_b),
                ok_count,
                conflict_count,
                other_count); });

        rename_a.join();
        rename_b.join();

        EXPECT_EQ(ok_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(conflict_count.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(other_count.load(std::memory_order_relaxed), 0);

        const auto exported = exportTripJsonRequest(port(), owner().token, tripId());
        ASSERT_EQ(exported.status_code, 200U);
        ASSERT_EQ(apiStatus(exported), "Ok");
        EXPECT_EQ(countOccurrences(exported.body, "\\\"name\\\":"), 1U);
        EXPECT_TRUE(exported.body.find(name_a) != std::string::npos || exported.body.find(name_b) != std::string::npos);
    }
}

TEST_F(TripHttpServerTripFixture, ConcurrentPlanItemCreationIsSearchableAndEmitsEvents)
{
    const std::string day_id = addDayOrFail("Plan day");
    ASSERT_FALSE(day_id.empty());

    constexpr int threads_count = 8;
    constexpr int items_per_thread = 8;
    const auto stats = runConcurrentRevisionRetriedWrites(
        port(),
        owner().token,
        tripId(),
        threads_count,
        items_per_thread,
        [&](uint64_t current_revision, int t, int i)
        {
            return addPlanItemRequest(
                port(),
                owner().token,
                tripId(),
                day_id,
                current_revision,
                "http-item-" + std::to_string(t) + "-" + std::to_string(i),
                "08:00",
                "museum-note",
                "culture");
        });

    EXPECT_EQ(stats.successful_writes, threads_count * items_per_thread);
    EXPECT_EQ(stats.hard_failures, 0);

    const auto search = searchTripRequest(port(), owner().token, tripId(), "http-item-");
    ASSERT_EQ(search.status_code, 200U);
    ASSERT_EQ(apiStatus(search), "Ok");
    EXPECT_EQ(jsonUInt64Field(search.body, "hits_count"), static_cast<uint64_t>(threads_count * items_per_thread));

    const auto events = getEventsSinceRequest(port(), owner().token, tripId(), stats.revision_before);
    ASSERT_EQ(events.status_code, 200U);
    ASSERT_EQ(apiStatus(events), "Ok");
    EXPECT_EQ(countOccurrences(events.body, "\"entity\":\"item\""), static_cast<std::size_t>(threads_count * items_per_thread));
}

TEST_F(TripHttpServerTripFixture, ConcurrentExpenseCreationAccumulatesBudgetSummaryAndSearchHits)
{
    const uint64_t budget_revision = currentRevision();
    ASSERT_GT(budget_revision, 0U);

    const auto budget = setBudgetSettingsRequest(port(), owner().token, tripId(), budget_revision, "EUR", 5000.0);
    ASSERT_EQ(budget.status_code, 200U);
    ASSERT_EQ(apiStatus(budget), "Ok");

    constexpr int threads_count = 6;
    constexpr int expenses_per_thread = 10;
    const auto stats = runConcurrentRevisionRetriedWrites(
        port(),
        owner().token,
        tripId(),
        threads_count,
        expenses_per_thread,
        [&](uint64_t current_revision, int t, int i)
        {
            return addExpenseRequest(
                port(),
                owner().token,
                tripId(),
                current_revision,
                1.0,
                "stress-category",
                owner().user_id,
                "http-expense-" + std::to_string(t) + "-" + std::to_string(i),
                "2026-08-01");
        });

    EXPECT_EQ(stats.successful_writes, threads_count * expenses_per_thread);
    EXPECT_EQ(stats.hard_failures, 0);

    const auto summary = getBudgetSummaryRequest(port(), owner().token, tripId());
    ASSERT_EQ(summary.status_code, 200U);
    ASSERT_EQ(apiStatus(summary), "Ok");
    EXPECT_DOUBLE_EQ(jsonDoubleField(summary.body, "total_expenses"), static_cast<double>(threads_count * expenses_per_thread));

    const auto search = searchTripRequest(port(), owner().token, tripId(), "http-expense-");
    ASSERT_EQ(search.status_code, 200U);
    ASSERT_EQ(apiStatus(search), "Ok");
    EXPECT_EQ(jsonUInt64Field(search.body, "hits_count"), static_cast<uint64_t>(threads_count * expenses_per_thread));

    const auto events = getEventsSinceRequest(port(), owner().token, tripId(), stats.revision_before);
    ASSERT_EQ(events.status_code, 200U);
    ASSERT_EQ(apiStatus(events), "Ok");
    EXPECT_EQ(countOccurrences(events.body, "\"entity\":\"expense\""), static_cast<std::size_t>(threads_count * expenses_per_thread));
}

TEST_F(TripHttpServerTripFixture, SnapshotReturnsDetailedTripPayloadForQtClient)
{
    const std::string day_id = addDayOrFail("Arrival");
    ASSERT_FALSE(day_id.empty());

    const std::string task_id = addTaskOrFail("Book hotel");
    ASSERT_FALSE(task_id.empty());

    const auto snapshot = getSnapshotRequest(port(), owner().token, tripId());
    ASSERT_EQ(snapshot.status_code, 200U);
    ASSERT_EQ(apiStatus(snapshot), "Ok");

    EXPECT_NE(snapshot.body.find("\"trip\":{"), std::string::npos);
    EXPECT_NE(snapshot.body.find("\"members\":{"), std::string::npos);
    EXPECT_NE(snapshot.body.find("\"days\":[{"), std::string::npos);
    EXPECT_NE(snapshot.body.find("\"tasks\":[{"), std::string::npos);
    EXPECT_NE(snapshot.body.find("\"id\":\"" + day_id + "\""), std::string::npos);
    EXPECT_NE(snapshot.body.find("\"id\":\"" + task_id + "\""), std::string::npos);
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "days_count"), 1U);
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "tasks_count"), 1U);
    EXPECT_EQ(jsonUInt64Field(snapshot.body, "members_count"), 1U);
}

TEST_F(TripHttpServerFixture, MalformedRequestsDoNotBreakHealthChecks)
{
    sendMalformedAndClose(port(), "BROKEN / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    const auto health = doHttpRequest(http::verb::get, port(), "/health");
    ASSERT_EQ(health.status_code, 200U);
    ASSERT_EQ(apiStatus(health), "Ok");

    const auto unknown = doHttpRequest(http::verb::get, port(), "/unknown-route");
    EXPECT_EQ(unknown.status_code, 404U);
    EXPECT_EQ(apiStatus(unknown), "NotFound");
}

TEST_F(TripHttpServerFixture, AbruptClientsDoNotPreventSubsequentValidRequests)
{
    std::vector<std::thread> noisy_clients;
    noisy_clients.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        noisy_clients.emplace_back([this]()
                                   {
            sendMalformedAndClose(
                port(),
                "POST /tasks/add HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 200\r\n\r\nshort"); });
    }

    for (auto &client : noisy_clients)
    {
        client.join();
    }

    const UserSession user = createUserSession(port(), "after_noise");
    EXPECT_FALSE(user.user_id.empty());
    EXPECT_FALSE(user.token.empty());
}

TEST_F(TripHttpServerTripFixture, StreamsRealtimeWebsocketEvents)
{
    const uint64_t base_revision = currentRevision();
    ASSERT_GT(base_revision, 0U);

    WsClient live_client(
        port(),
        "/ws/updates?token=" + urlEncode(owner().token) +
            "&trip_id=" + urlEncode(tripId()) +
            "&since_revision=" + std::to_string(base_revision));

    const auto add = addTaskRequest(port(), owner().token, tripId(), base_revision, "task-live");
    ASSERT_EQ(add.status_code, 200U);
    ASSERT_EQ(apiStatus(add), "Ok");

    const std::string push = live_client.readTextFrame();
    EXPECT_NE(push.find("\"type\":\"event\""), std::string::npos);
    EXPECT_NE(push.find("\"entity\":\"task\""), std::string::npos);
    EXPECT_GT(jsonUInt64Field(push, "revision"), base_revision);

    live_client.close();
}

TEST_F(TripHttpServerTripFixture, ReplaysMissedEventsAfterReconnectBySinceRevision)
{
    const uint64_t base_revision = currentRevision();
    ASSERT_GT(base_revision, 0U);

    const auto add_first = addTaskRequest(port(), owner().token, tripId(), base_revision, "task-first");
    ASSERT_EQ(add_first.status_code, 200U);
    ASSERT_EQ(apiStatus(add_first), "Ok");

    const uint64_t revision_after_first = currentRevision();
    ASSERT_GT(revision_after_first, base_revision);

    const auto add_second = addTaskRequest(port(), owner().token, tripId(), revision_after_first, "task-reconnect");
    ASSERT_EQ(add_second.status_code, 200U);
    ASSERT_EQ(apiStatus(add_second), "Ok");

    WsClient reconnect_client(
        port(),
        "/ws/updates?token=" + urlEncode(owner().token) +
            "&trip_id=" + urlEncode(tripId()) +
            "&since_revision=" + std::to_string(revision_after_first));

    const std::string backlog_push = reconnect_client.readTextFrame();
    EXPECT_NE(backlog_push.find("\"type\":\"event\""), std::string::npos);
    EXPECT_NE(backlog_push.find("\"entity\":\"task\""), std::string::npos);
    EXPECT_GT(jsonUInt64Field(backlog_push, "revision"), revision_after_first);

    const auto events_since = getEventsSinceRequest(port(), owner().token, tripId(), revision_after_first);
    ASSERT_EQ(events_since.status_code, 200U);
    ASSERT_EQ(apiStatus(events_since), "Ok");
    EXPECT_NE(events_since.body.find("\"events\":[{"), std::string::npos);
    EXPECT_NE(events_since.body.find("\"entity\":\"task\""), std::string::npos);

    reconnect_client.close();
}
