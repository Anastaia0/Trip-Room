#include "trip/trip_service.hpp"

namespace trip
{
    StatusResult TripService::setBudgetSettings(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const BudgetSettings &settings)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Editor);
        if (!writable.ok())
        {
            return statusOnly(writable.status, writable.message);
        }
        if (!checkRevision(*writable.value, expected_revision))
        {
            return statusOnly(Status::Conflict, "Revision conflict");
        }
        auto actor = authUserIdByToken(token);
        writable.value->budget = settings;
        appendEvent(*writable.value, actor.value, "update", "budget", trip_id, "Budget settings updated");
        return statusOnly(Status::Ok, {});
    }

    StatusOr<std::string> TripService::addExpense(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const Expense &expense)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Editor);
        if (!writable.ok())
        {
            return StatusOr<std::string>{writable.status, writable.message, {}};
        }
        if (!checkRevision(*writable.value, expected_revision))
        {
            return StatusOr<std::string>{Status::Conflict, "Revision conflict", {}};
        }
        if (expense.amount <= 0.0)
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Expense amount must be positive", {}};
        }
        auto actor = authUserIdByToken(token);
        Expense to_add = expense;
        to_add.id = nextId("exp_");
        writable.value->expenses.push_back(to_add);
        appendEvent(*writable.value, actor.value, "add", "expense", to_add.id, "Expense added");
        return StatusOr<std::string>{Status::Ok, {}, to_add.id};
    }

    StatusOr<BudgetSummary> TripService::getBudgetSummary(const std::string &token, const std::string &trip_id) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<BudgetSummary>{readable.status, readable.message, {}};
        }
        BudgetSummary summary;
        for (const auto &expense : readable.value->expenses)
        {
            summary.total_expenses += expense.amount;
            summary.by_category[expense.category] += expense.amount;
            summary.paid_by_user[expense.paid_by_user_id] += expense.amount;
        }
        const std::size_t member_count = readable.value->members.size();
        if (member_count > 0)
        {
            const double share = summary.total_expenses / static_cast<double>(member_count);
            for (const auto &[user_id, _role] : readable.value->members)
            {
                summary.balance_by_user[user_id] = summary.paid_by_user[user_id] - share;
            }
        }
        return StatusOr<BudgetSummary>{Status::Ok, {}, summary};
    }

}
