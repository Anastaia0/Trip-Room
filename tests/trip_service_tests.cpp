#include "../include/trip/trip_service.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using trip::BudgetSettings;
using trip::Expense;
using trip::PlanItem;
using trip::Role;
using trip::Status;
using trip::Task;
using trip::TripInfo;
using trip::TripService;

namespace
{
    struct AuthData
    {
        std::string owner_token;
        std::string editor_token;
        std::string viewer_token;
        std::string owner_user_id;
        std::string editor_user_id;
        std::string viewer_user_id;
    };

    AuthData prepareUsers(TripService &service)
    {
        auto owner_id = service.registerUser("owner", "pass1");
        auto editor_id = service.registerUser("editor", "pass2");
        auto viewer_id = service.registerUser("viewer", "pass3");

        auto owner_token = service.login("owner", "pass1");
        auto editor_token = service.login("editor", "pass2");
        auto viewer_token = service.login("viewer", "pass3");

        return AuthData{
            owner_token.value,
            editor_token.value,
            viewer_token.value,
            owner_id.value,
            editor_id.value,
            viewer_id.value};
    }

    std::string createTripAndInvite(TripService &service, const AuthData &auth, Role role, const std::string &invitee_token)
    {
        TripInfo info{"Europe Trip", "2026-06-01", "2026-06-10", "Summer plan"};
        auto trip = service.createTrip(auth.owner_token, info);
        auto invite = service.createInvite(auth.owner_token, trip.value, role);
        auto accepted = service.acceptInvite(invitee_token, invite.value);
        EXPECT_TRUE(accepted.ok());
        return trip.value;
    }

}

TEST(TripServiceTests, RegistrationLoginAndTripCreation)
{
    TripService service;
    auto auth = prepareUsers(service);

    TripInfo info{"Road Trip", "2026-07-01", "2026-07-12", "Across EU"};
    auto create = service.createTrip(auth.owner_token, info);
    ASSERT_TRUE(create.ok());

    auto snapshot = service.getTripSnapshot(auth.owner_token, create.value);
    ASSERT_TRUE(snapshot.ok());
    EXPECT_EQ(snapshot.value.info.title, "Road Trip");
    EXPECT_EQ(snapshot.value.members.at(auth.owner_user_id), Role::Owner);
}

TEST(TripServiceTests, OwnerCanInviteAndManageRoles)
{
    TripService service;
    auto auth = prepareUsers(service);

    auto trip_id = createTripAndInvite(service, auth, Role::Viewer, auth.viewer_token);
    auto change = service.changeMemberRole(auth.owner_token, trip_id, auth.viewer_user_id, Role::Editor);
    ASSERT_TRUE(change.ok());

    auto remove = service.removeMember(auth.owner_token, trip_id, auth.viewer_user_id);
    ASSERT_TRUE(remove.ok());

    auto snapshot = service.getTripSnapshot(auth.owner_token, trip_id);
    ASSERT_TRUE(snapshot.ok());
    EXPECT_FALSE(snapshot.value.members.contains(auth.viewer_user_id));
}

TEST(TripServiceTests, ViewerCannotEditTripData)
{
    TripService service;
    auto auth = prepareUsers(service);

    auto trip_id = createTripAndInvite(service, auth, Role::Viewer, auth.viewer_token);
    auto rev = service.getTripRevision(auth.owner_token, trip_id);
    ASSERT_TRUE(rev.ok());

    auto add_day = service.addDay(auth.viewer_token, trip_id, rev.value, "Day 1");
    EXPECT_EQ(add_day.status, Status::Forbidden);
}

TEST(TripServiceTests, DetectsRevisionConflict)
{
    TripService service;
    auto auth = prepareUsers(service);

    TripInfo info{"Trip", "2026-01-01", "2026-01-02", "desc"};
    auto trip = service.createTrip(auth.owner_token, info);
    ASSERT_TRUE(trip.ok());

    auto rev = service.getTripRevision(auth.owner_token, trip.value);
    ASSERT_TRUE(rev.ok());

    auto first = service.addDay(auth.owner_token, trip.value, rev.value, "Day 1");
    ASSERT_TRUE(first.ok());

    auto second = service.addDay(auth.owner_token, trip.value, rev.value, "Day 2");
    EXPECT_EQ(second.status, Status::Conflict);
}

TEST(TripServiceTests, MaintainsEventsAndReturnsDeltaSinceRevision)
{
    TripService service;
    auto auth = prepareUsers(service);
    TripInfo info{"Trip", "2026-01-01", "2026-01-02", "desc"};
    auto trip = service.createTrip(auth.owner_token, info);

    auto base_rev = service.getTripRevision(auth.owner_token, trip.value);
    ASSERT_TRUE(base_rev.ok());

    auto add_day = service.addDay(auth.owner_token, trip.value, base_rev.value, "Day A");
    ASSERT_TRUE(add_day.ok());
    auto new_rev = service.getTripRevision(auth.owner_token, trip.value);
    ASSERT_TRUE(new_rev.ok());

    auto add_task = service.addTask(auth.owner_token, trip.value, new_rev.value, Task{"", "Book hotel"});
    ASSERT_TRUE(add_task.ok());

    auto events = service.getEventsSince(auth.owner_token, trip.value, base_rev.value);
    ASSERT_TRUE(events.ok());
    ASSERT_EQ(events.value.size(), 2U);
    EXPECT_EQ(events.value[0].entity, "day");
    EXPECT_EQ(events.value[1].entity, "task");
}

TEST(TripServiceTests, SupportsChatSendAndReadHistory)
{
    TripService service;
    auto auth = prepareUsers(service);
    auto trip_id = createTripAndInvite(service, auth, Role::Viewer, auth.viewer_token);

    auto msg = service.sendMessage(auth.viewer_token, trip_id, "Hello team");
    ASSERT_TRUE(msg.ok());

    auto messages = service.getMessages(auth.owner_token, trip_id);
    ASSERT_TRUE(messages.ok());
    ASSERT_EQ(messages.value.size(), 1U);
    EXPECT_EQ(messages.value[0].text, "Hello team");
}

TEST(TripServiceTests, SearchFindsTasksPlanItemsAndExpenses)
{
    TripService service;
    auto auth = prepareUsers(service);
    TripInfo info{"Trip", "2026-01-01", "2026-01-03", "desc"};
    auto trip = service.createTrip(auth.owner_token, info);
    ASSERT_TRUE(trip.ok());

    auto rev = service.getTripRevision(auth.owner_token, trip.value);
    auto day = service.addDay(auth.owner_token, trip.value, rev.value, "Paris");
    ASSERT_TRUE(day.ok());

    rev = service.getTripRevision(auth.owner_token, trip.value);
    PlanItem item;
    item.name = "Louvre visit";
    item.notes = "Museum ticket";
    item.category = "culture";
    auto item_add = service.addPlanItem(auth.owner_token, trip.value, day.value, rev.value, item);
    ASSERT_TRUE(item_add.ok());

    rev = service.getTripRevision(auth.owner_token, trip.value);
    auto task_add = service.addTask(auth.owner_token, trip.value, rev.value, Task{"", "Buy museum pass"});
    ASSERT_TRUE(task_add.ok());

    rev = service.getTripRevision(auth.owner_token, trip.value);
    Expense expense;
    expense.amount = 50.0;
    expense.category = "museum";
    expense.paid_by_user_id = auth.owner_user_id;
    expense.comment = "Louvre ticket";
    expense.date = "2026-01-01";
    auto exp_add = service.addExpense(auth.owner_token, trip.value, rev.value, expense);
    ASSERT_TRUE(exp_add.ok());

    auto found = service.searchInTrip(auth.owner_token, trip.value, "museum");
    ASSERT_TRUE(found.ok());
    EXPECT_GE(found.value.size(), 2U);
}

TEST(TripServiceTests, CalculatesBudgetSummaryWithEqualSplit)
{
    TripService service;
    auto auth = prepareUsers(service);
    auto trip_id = createTripAndInvite(service, auth, Role::Editor, auth.editor_token);

    auto rev = service.getTripRevision(auth.owner_token, trip_id);
    ASSERT_TRUE(rev.ok());
    auto budget = service.setBudgetSettings(auth.owner_token, trip_id, rev.value, BudgetSettings{"EUR", 1000.0});
    ASSERT_TRUE(budget.ok());

    rev = service.getTripRevision(auth.owner_token, trip_id);
    Expense expense1;
    expense1.amount = 90.0;
    expense1.category = "food";
    expense1.paid_by_user_id = auth.owner_user_id;
    expense1.comment = "Dinner";
    expense1.date = "2026-06-01";
    auto add1 = service.addExpense(auth.owner_token, trip_id, rev.value, expense1);
    ASSERT_TRUE(add1.ok());

    rev = service.getTripRevision(auth.owner_token, trip_id);
    Expense expense2;
    expense2.amount = 30.0;
    expense2.category = "transport";
    expense2.paid_by_user_id = auth.editor_user_id;
    expense2.comment = "Metro";
    expense2.date = "2026-06-01";
    auto add2 = service.addExpense(auth.editor_token, trip_id, rev.value, expense2);
    ASSERT_TRUE(add2.ok());

    auto summary = service.getBudgetSummary(auth.owner_token, trip_id);
    ASSERT_TRUE(summary.ok());
    EXPECT_DOUBLE_EQ(summary.value.total_expenses, 120.0);
    EXPECT_DOUBLE_EQ(summary.value.by_category["food"], 90.0);
    EXPECT_DOUBLE_EQ(summary.value.by_category["transport"], 30.0);

    const double owner_balance = summary.value.balance_by_user[auth.owner_user_id];
    const double editor_balance = summary.value.balance_by_user[auth.editor_user_id];
    EXPECT_NEAR(owner_balance + editor_balance, 0.0, 1e-9);
}

TEST(TripServiceTests, ExportsAndImportsTripJson)
{
    TripService service;
    auto auth = prepareUsers(service);
    TripInfo info{"Trip", "2026-01-01", "2026-01-03", "desc"};
    auto trip = service.createTrip(auth.owner_token, info);
    ASSERT_TRUE(trip.ok());

    auto rev = service.getTripRevision(auth.owner_token, trip.value);
    auto day = service.addDay(auth.owner_token, trip.value, rev.value, "Day 1");
    ASSERT_TRUE(day.ok());

    auto exported = service.exportTripJson(auth.owner_token, trip.value);
    ASSERT_TRUE(exported.ok());

    auto imported = service.importTripJson(auth.owner_token, exported.value);
    ASSERT_TRUE(imported.ok());

    auto imported_snapshot = service.getTripSnapshot(auth.owner_token, imported.value);
    ASSERT_TRUE(imported_snapshot.ok());
    EXPECT_EQ(imported_snapshot.value.info.title, "Trip");
    EXPECT_EQ(imported_snapshot.value.days.size(), 1U);
}

TEST(TripServiceTests, HandlesConcurrentTaskCreationWithRevisionRetry)
{
    TripService service;
    auto auth = prepareUsers(service);
    TripInfo info{"Trip", "2026-01-01", "2026-01-03", "desc"};
    auto trip = service.createTrip(auth.owner_token, info);
    ASSERT_TRUE(trip.ok());

    constexpr int threads_count = 8;
    constexpr int tasks_per_thread = 25;
    std::atomic<int> created{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(threads_count);

    for (int t = 0; t < threads_count; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            for (int i = 0; i < tasks_per_thread; ++i) {
                bool done = false;
                while (!done) {
                    auto rev = service.getTripRevision(auth.owner_token, trip.value);
                    if (!rev.ok()) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    Task task;
                    task.text = "Task " + std::to_string(t) + "-" + std::to_string(i);
                    auto result = service.addTask(auth.owner_token, trip.value, rev.value, task);
                    if (result.status == Status::Ok) {
                        created.fetch_add(1, std::memory_order_relaxed);
                        done = true;
                    } else if (result.status != Status::Conflict) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
            } });
    }

    for (auto &thread_ref : threads)
    {
        thread_ref.join();
    }

    EXPECT_EQ(created.load(), threads_count * tasks_per_thread);
    EXPECT_EQ(failures.load(), 0);
    auto snapshot = service.getTripSnapshot(auth.owner_token, trip.value);
    ASSERT_TRUE(snapshot.ok());
    EXPECT_EQ(snapshot.value.tasks.size(), static_cast<std::size_t>(threads_count * tasks_per_thread));
}
