#include "../include/trip/qt_trip_client.hpp"
#include "../include/trip/trip_http_server.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QWebSocket>

#include <atomic>
#include <memory>

namespace
{
    QCoreApplication *ensureCoreApp()
    {
        static int argc = 1;
        static char arg0[] = "trip_qt_client_tests";
        static char *argv[] = {arg0, nullptr};
        static QCoreApplication app(argc, argv);
        return &app;
    }

    QString uniqueLogin(const QString &base)
    {
        static std::atomic<int> seq{0};
        return base + QStringLiteral("_") + QString::number(seq.fetch_add(1, std::memory_order_relaxed));
    }

    bool waitForConnected(QWebSocket &socket, int timeout_ms = 3000)
    {
        bool triggered = false;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&socket, &QWebSocket::connected, &loop, [&]()
                         {
            triggered = true;
            loop.quit(); });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeout_ms);
        loop.exec();
        return triggered;
    }

    QString waitForTextMessage(QWebSocket &socket, int timeout_ms = 3000)
    {
        QString message;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&socket, &QWebSocket::textMessageReceived, &loop, [&](const QString &text)
                         {
            message = text;
            loop.quit(); });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeout_ms);
        loop.exec();
        return message;
    }

    struct SessionTrip
    {
        QString login;
        QString token;
        QString user_id;
        QString trip_id;
    };

    class QtTripClientFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            (void)ensureCoreApp();
        }

        void SetUp() override
        {
            server_ = std::make_unique<trip::TripHttpServer>("127.0.0.1", 0, 4);
            server_->start();
            port_ = server_->port();
            client_.setBaseUrl(QStringLiteral("http://127.0.0.1:") + QString::number(port_));
        }

        void TearDown() override
        {
            if (server_)
            {
                server_->stop();
            }
        }

        SessionTrip createOwnerTrip(const QString &login_base, const QString &title)
        {
            SessionTrip ctx;
            ctx.login = uniqueLogin(login_base);

            const auto reg = client_.registerUser(ctx.login, QStringLiteral("pass"));
            EXPECT_TRUE(reg.ok);
            ctx.user_id = reg.payload.value(QStringLiteral("user_id")).toString();

            const auto login = client_.login(ctx.login, QStringLiteral("pass"));
            EXPECT_TRUE(login.ok);
            ctx.token = login.payload.value(QStringLiteral("token")).toString();

            const auto create = client_.createTrip(
                ctx.token,
                title,
                QStringLiteral("2026-09-01"),
                QStringLiteral("2026-09-05"),
                QStringLiteral("qt integration"));
            EXPECT_TRUE(create.ok);
            ctx.trip_id = create.payload.value(QStringLiteral("trip_id")).toString();
            return ctx;
        }

        quint64 currentRevision(const QString &token, const QString &trip_id)
        {
            const auto revision = client_.getRevision(token, trip_id);
            EXPECT_TRUE(revision.ok);
            return revision.payload.value(QStringLiteral("revision")).toVariant().toULongLong();
        }

        trip::QtTripClient client_;
        std::unique_ptr<trip::TripHttpServer> server_;
        uint16_t port_ = 0;
    };
}

TEST_F(QtTripClientFixture, SupportsAuthTripListingAndRichSnapshot)
{
    const auto health = client_.health();
    ASSERT_TRUE(health.ok);

    const SessionTrip owner = createOwnerTrip(QStringLiteral("qt_owner"), QStringLiteral("Qt Full Trip"));
    ASSERT_FALSE(owner.token.isEmpty());
    ASSERT_FALSE(owner.trip_id.isEmpty());

    quint64 revision = currentRevision(owner.token, owner.trip_id);
    const auto add_day = client_.addDay(owner.token, owner.trip_id, revision, QStringLiteral("Arrival"));
    ASSERT_TRUE(add_day.ok);
    const QString day_id = add_day.payload.value(QStringLiteral("day_id")).toString();
    ASSERT_FALSE(day_id.isEmpty());

    revision = currentRevision(owner.token, owner.trip_id);
    const auto add_task = client_.addTask(owner.token, owner.trip_id, revision, QStringLiteral("Book hotel"));
    ASSERT_TRUE(add_task.ok);

    const auto list = client_.listTrips(owner.token);
    ASSERT_TRUE(list.ok);
    ASSERT_EQ(list.payload.value(QStringLiteral("trips_count")).toInt(), 1);
    const QJsonArray trips = list.payload.value(QStringLiteral("trips")).toArray();
    ASSERT_EQ(trips.size(), 1);
    EXPECT_EQ(trips[0].toObject().value(QStringLiteral("id")).toString(), owner.trip_id);
    EXPECT_EQ(trips[0].toObject().value(QStringLiteral("my_role")).toString(), QStringLiteral("Owner"));

    const auto snapshot = client_.getSnapshot(owner.token, owner.trip_id);
    ASSERT_TRUE(snapshot.ok);
    EXPECT_EQ(snapshot.payload.value(QStringLiteral("tasks_count")).toInt(), 1);
    EXPECT_EQ(snapshot.payload.value(QStringLiteral("days_count")).toInt(), 1);

    const QJsonObject trip = snapshot.payload.value(QStringLiteral("trip")).toObject();
    ASSERT_FALSE(trip.isEmpty());
    EXPECT_EQ(trip.value(QStringLiteral("id")).toString(), owner.trip_id);
    EXPECT_EQ(trip.value(QStringLiteral("revision")).toVariant().toULongLong(), snapshot.payload.value(QStringLiteral("revision")).toVariant().toULongLong());
    EXPECT_EQ(trip.value(QStringLiteral("members")).toObject().value(owner.user_id).toString(), QStringLiteral("Owner"));
    EXPECT_EQ(trip.value(QStringLiteral("days")).toArray().size(), 1);
    EXPECT_EQ(trip.value(QStringLiteral("tasks")).toArray().size(), 1);
    EXPECT_EQ(trip.value(QStringLiteral("days")).toArray()[0].toObject().value(QStringLiteral("id")).toString(), day_id);
}

TEST_F(QtTripClientFixture, SupportsInviteBudgetChatSearchAndExportImportFlows)
{
    const SessionTrip owner = createOwnerTrip(QStringLiteral("qt_collab_owner"), QStringLiteral("Qt Collaboration"));
    const QString editor_login = uniqueLogin(QStringLiteral("qt_editor"));

    const auto editor_reg = client_.registerUser(editor_login, QStringLiteral("pass"));
    ASSERT_TRUE(editor_reg.ok);
    const QString editor_user_id = editor_reg.payload.value(QStringLiteral("user_id")).toString();

    const auto editor_login_result = client_.login(editor_login, QStringLiteral("pass"));
    ASSERT_TRUE(editor_login_result.ok);
    const QString editor_token = editor_login_result.payload.value(QStringLiteral("token")).toString();

    const auto invite = client_.createInvite(owner.token, owner.trip_id, QStringLiteral("Editor"));
    ASSERT_TRUE(invite.ok);
    const QString invite_code = invite.payload.value(QStringLiteral("invite_code")).toString();
    ASSERT_FALSE(invite_code.isEmpty());

    const auto accept = client_.acceptInvite(editor_token, invite_code);
    ASSERT_TRUE(accept.ok);

    const auto editor_trips = client_.listTrips(editor_token);
    ASSERT_TRUE(editor_trips.ok);
    ASSERT_EQ(editor_trips.payload.value(QStringLiteral("trips_count")).toInt(), 1);
    EXPECT_EQ(editor_trips.payload.value(QStringLiteral("trips")).toArray()[0].toObject().value(QStringLiteral("id")).toString(), owner.trip_id);

    quint64 revision = currentRevision(owner.token, owner.trip_id);
    const auto add_day = client_.addDay(owner.token, owner.trip_id, revision, QStringLiteral("Museums"));
    ASSERT_TRUE(add_day.ok);
    const QString day_id = add_day.payload.value(QStringLiteral("day_id")).toString();

    revision = currentRevision(editor_token, owner.trip_id);
    const auto add_item = client_.addPlanItem(
        editor_token,
        owner.trip_id,
        day_id,
        revision,
        QStringLiteral("Louvre"),
        QStringLiteral("10:00"),
        QStringLiteral("Museum visit"),
        QStringLiteral("culture"),
        QStringLiteral("https://example.test/louvre"));
    ASSERT_TRUE(add_item.ok);

    revision = currentRevision(editor_token, owner.trip_id);
    const auto add_task = client_.addTask(
        editor_token,
        owner.trip_id,
        revision,
        QStringLiteral("Buy museum pass"),
        editor_user_id,
        QStringLiteral("2026-09-01"));
    ASSERT_TRUE(add_task.ok);

    revision = currentRevision(owner.token, owner.trip_id);
    const auto budget = client_.setBudgetSettings(owner.token, owner.trip_id, revision, QStringLiteral("EUR"), 1200.0);
    ASSERT_TRUE(budget.ok);

    revision = currentRevision(editor_token, owner.trip_id);
    const auto expense = client_.addExpense(
        editor_token,
        owner.trip_id,
        revision,
        55.5,
        QStringLiteral("museum"),
        editor_user_id,
        QStringLiteral("Louvre tickets"),
        QStringLiteral("2026-09-01"),
        day_id);
    ASSERT_TRUE(expense.ok);

    const auto message = client_.sendChatMessage(editor_token, owner.trip_id, QStringLiteral("All booked"));
    ASSERT_TRUE(message.ok);

    const auto chat = client_.listChatMessages(owner.token, owner.trip_id);
    ASSERT_TRUE(chat.ok);
    ASSERT_EQ(chat.payload.value(QStringLiteral("messages_count")).toInt(), 1);

    const auto search = client_.searchInTrip(owner.token, owner.trip_id, QStringLiteral("museum"));
    ASSERT_TRUE(search.ok);
    EXPECT_GE(search.payload.value(QStringLiteral("hits_count")).toInt(), 2);

    const auto summary = client_.getBudgetSummary(owner.token, owner.trip_id);
    ASSERT_TRUE(summary.ok);
    EXPECT_DOUBLE_EQ(summary.payload.value(QStringLiteral("summary")).toObject().value(QStringLiteral("total_expenses")).toDouble(), 55.5);

    const auto events = client_.getEventsSince(owner.token, owner.trip_id, 0);
    ASSERT_TRUE(events.ok);
    EXPECT_GT(events.payload.value(QStringLiteral("events")).toArray().size(), 0);

    const auto exported = client_.exportTripJson(owner.token, owner.trip_id);
    ASSERT_TRUE(exported.ok);
    const QString trip_json = exported.payload.value(QStringLiteral("trip_json")).toString();
    ASSERT_FALSE(trip_json.isEmpty());

    const auto imported = client_.importTripJson(owner.token, trip_json);
    ASSERT_TRUE(imported.ok);
    ASSERT_FALSE(imported.payload.value(QStringLiteral("trip_id")).toString().isEmpty());

    const auto role_change = client_.changeMemberRole(owner.token, owner.trip_id, editor_user_id, QStringLiteral("Viewer"));
    ASSERT_TRUE(role_change.ok);

    const auto remove_member = client_.removeMember(owner.token, owner.trip_id, editor_user_id);
    ASSERT_TRUE(remove_member.ok);
}

TEST_F(QtTripClientFixture, ReceivesRealtimeUpdatesAndReconnectBacklog)
{
    const SessionTrip owner = createOwnerTrip(QStringLiteral("qt_ws_owner"), QStringLiteral("Qt WS"));
    const quint64 base_revision = currentRevision(owner.token, owner.trip_id);
    ASSERT_GT(base_revision, 0ULL);

    QWebSocket live_socket;
    QStringList live_messages;
    QObject::connect(&live_socket, &QWebSocket::textMessageReceived, [&](const QString &text)
                     { live_messages.push_back(text); });
    live_socket.open(client_.updatesWebSocketUrl(owner.token, owner.trip_id, base_revision));
    ASSERT_TRUE(waitForConnected(live_socket));

    const auto add_live = client_.addTask(owner.token, owner.trip_id, base_revision, QStringLiteral("Realtime task"));
    ASSERT_TRUE(add_live.ok);

    const auto take_or_wait = [](QWebSocket &socket, QStringList &messages, int timeout_ms = 3000)
    {
        if (!messages.isEmpty())
        {
            return messages.takeFirst();
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&socket, &QWebSocket::textMessageReceived, &loop, [&](const QString &text)
                         {
            messages.push_back(text);
            loop.quit(); });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeout_ms);
        loop.exec();

        return messages.isEmpty() ? QString() : messages.takeFirst();
    };

    const QString live_message = take_or_wait(live_socket, live_messages);
    ASSERT_FALSE(live_message.isEmpty());
    EXPECT_NE(live_message.indexOf(QStringLiteral("\"entity\":\"task\"")), -1);

    live_socket.close();

    const quint64 after_first = currentRevision(owner.token, owner.trip_id);
    ASSERT_GT(after_first, base_revision);

    const auto add_backlog = client_.addTask(owner.token, owner.trip_id, after_first, QStringLiteral("Backlog task"));
    ASSERT_TRUE(add_backlog.ok);

    QWebSocket reconnect_socket;
    QStringList backlog_messages;
    QObject::connect(&reconnect_socket, &QWebSocket::textMessageReceived, [&](const QString &text)
                     { backlog_messages.push_back(text); });
    reconnect_socket.open(client_.updatesWebSocketUrl(owner.token, owner.trip_id, after_first));
    ASSERT_TRUE(waitForConnected(reconnect_socket));

    const QString backlog_message = take_or_wait(reconnect_socket, backlog_messages);
    ASSERT_FALSE(backlog_message.isEmpty());
    EXPECT_NE(backlog_message.indexOf(QStringLiteral("\"entity\":\"task\"")), -1);

    reconnect_socket.close();
}
