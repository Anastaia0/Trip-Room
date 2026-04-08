#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/role.hpp"
#include "common/status_or.hpp"
#include "entities/budget_settings.hpp"
#include "entities/budget_summary.hpp"
#include "entities/chat_message.hpp"
#include "entities/event.hpp"
#include "entities/expense.hpp"
#include "entities/plan_item.hpp"
#include "entities/search_hit.hpp"
#include "entities/task.hpp"
#include "entities/trip.hpp"
#include "entities/trip_info.hpp"
#include "entities/trip_summary.hpp"

namespace trip
{
    class TripService
    {
    public:
        StatusOr<std::string> registerUser(const std::string &login, const std::string &password);
        StatusOr<std::string> login(const std::string &login, const std::string &password);

        StatusOr<std::string> createTrip(const std::string &token, const TripInfo &info);
        StatusResult deleteTrip(const std::string &token, const std::string &trip_id);

        StatusOr<std::string> createInvite(const std::string &token, const std::string &trip_id, Role role);
        StatusResult acceptInvite(const std::string &token, const std::string &invite_code);
        StatusResult changeMemberRole(
            const std::string &token,
            const std::string &trip_id,
            const std::string &target_user_id,
            Role new_role);
        StatusResult removeMember(const std::string &token, const std::string &trip_id, const std::string &target_user_id);

        StatusResult updateTripInfo(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const TripInfo &info);

        StatusOr<std::string> addDay(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const std::string &day_name);
        StatusResult renameDay(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const std::string &day_id,
            const std::string &new_name);
        StatusResult removeDay(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const std::string &day_id);
        StatusResult reorderDays(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const std::vector<std::string> &day_ids_order);

        StatusOr<std::string> addPlanItem(
            const std::string &token,
            const std::string &trip_id,
            const std::string &day_id,
            uint64_t expected_revision,
            const PlanItem &item);
        StatusResult updatePlanItem(
            const std::string &token,
            const std::string &trip_id,
            const std::string &day_id,
            uint64_t expected_revision,
            const PlanItem &item);
        StatusResult removePlanItem(
            const std::string &token,
            const std::string &trip_id,
            const std::string &day_id,
            uint64_t expected_revision,
            const std::string &item_id);
        StatusResult reorderPlanItems(
            const std::string &token,
            const std::string &trip_id,
            const std::string &day_id,
            uint64_t expected_revision,
            const std::vector<std::string> &item_ids_order);

        StatusOr<std::string> addTask(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const Task &task);
        StatusResult updateTask(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const Task &task);
        StatusResult setTaskDone(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const std::string &task_id,
            bool done);
        StatusResult removeTask(const std::string &token, const std::string &trip_id, uint64_t expected_revision, const std::string &task_id);

        StatusResult setBudgetSettings(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const BudgetSettings &settings);
        StatusOr<std::string> addExpense(
            const std::string &token,
            const std::string &trip_id,
            uint64_t expected_revision,
            const Expense &expense);
        StatusOr<BudgetSummary> getBudgetSummary(const std::string &token, const std::string &trip_id) const;

        StatusOr<std::string> sendMessage(const std::string &token, const std::string &trip_id, const std::string &text);
        StatusOr<std::vector<ChatMessage>> getMessages(const std::string &token, const std::string &trip_id) const;

        StatusOr<std::vector<SearchHit>> searchInTrip(const std::string &token, const std::string &trip_id, const std::string &query) const;
        StatusOr<std::vector<TripSummary>> listTrips(const std::string &token) const;

        StatusOr<Trip> getTripSnapshot(const std::string &token, const std::string &trip_id) const;
        StatusOr<std::vector<Event>> getEventsSince(const std::string &token, const std::string &trip_id, uint64_t since_revision) const;
        StatusOr<uint64_t> getTripRevision(const std::string &token, const std::string &trip_id) const;

        StatusOr<std::string> exportTripJson(const std::string &token, const std::string &trip_id) const;
        StatusOr<std::string> importTripJson(const std::string &token, const std::string &json_text);

    private:
        struct User
        {
            std::string id;
            std::string login;
            std::string password;
        };

        struct Invite
        {
            std::string code;
            std::string trip_id;
            std::string created_by_user_id;
            Role role = Role::Viewer;
        };

        static bool containsCaseInsensitive(const std::string &text, const std::string &query);
        static int64_t nowMs();
        static int roleRank(Role role);

        std::string nextId(const std::string &prefix);
        StatusOr<std::string> authUserIdByToken(const std::string &token) const;
        StatusOr<Trip *> writableTripFor(const std::string &token, const std::string &trip_id, Role min_role);
        StatusOr<const Trip *> readableTripFor(const std::string &token, const std::string &trip_id) const;
        static bool checkRevision(const Trip &trip, uint64_t expected_revision);
        static StatusResult statusOnly(Status status, const std::string &message);
        static void appendEvent(
            Trip &trip,
            const std::string &actor,
            const std::string &action,
            const std::string &entity,
            const std::string &entity_id,
            const std::string &details);

        mutable std::mutex mutex_;
        uint64_t id_counter_ = 0;
        std::unordered_map<std::string, User> users_by_id_;
        std::unordered_map<std::string, std::string> user_id_by_login_;
        std::unordered_map<std::string, std::string> user_id_by_token_;
        std::unordered_map<std::string, Trip> trips_by_id_;
        std::unordered_map<std::string, Invite> invites_by_code_;
    };
}
