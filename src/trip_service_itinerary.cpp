#include "trip/trip_service.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace trip
{
    namespace
    {

        template <typename Container>
        bool sameIds(const Container &order, std::size_t expected_size)
        {
            if (order.size() != expected_size)
            {
                return false;
            }
            std::unordered_set<std::string> unique(order.begin(), order.end());
            return unique.size() == expected_size;
        }

    }

    StatusOr<std::string> TripService::addDay(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &day_name)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Editor);
        if (!writable.ok())
        {
            return StatusOr<std::string>{writable.status, writable.message, {}};
        }
        if (day_name.empty())
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Day name must not be empty", {}};
        }
        if (!checkRevision(*writable.value, expected_revision))
        {
            return StatusOr<std::string>{Status::Conflict, "Revision conflict", {}};
        }
        auto actor = authUserIdByToken(token);
        Day day;
        day.id = nextId("day_");
        day.name = day_name;
        writable.value->days.push_back(day);
        appendEvent(*writable.value, actor.value, "add", "day", day.id, "Day added");
        return StatusOr<std::string>{Status::Ok, {}, day.id};
    }

    StatusResult TripService::renameDay(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &day_id,
        const std::string &new_name)
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
        auto day_it = std::find_if(writable.value->days.begin(), writable.value->days.end(), [&](const Day &day)
                                   { return day.id == day_id; });
        if (day_it == writable.value->days.end())
        {
            return statusOnly(Status::NotFound, "Day not found");
        }
        auto actor = authUserIdByToken(token);
        day_it->name = new_name;
        appendEvent(*writable.value, actor.value, "rename", "day", day_id, "Day renamed");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::removeDay(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &day_id)
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
        auto size_before = writable.value->days.size();
        std::erase_if(writable.value->days, [&](const Day &day)
                      { return day.id == day_id; });
        if (writable.value->days.size() == size_before)
        {
            return statusOnly(Status::NotFound, "Day not found");
        }
        auto actor = authUserIdByToken(token);
        appendEvent(*writable.value, actor.value, "remove", "day", day_id, "Day removed");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::reorderDays(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::vector<std::string> &day_ids_order)
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
        if (!sameIds(day_ids_order, writable.value->days.size()))
        {
            return statusOnly(Status::InvalidArgument, "Invalid order list");
        }
        std::unordered_map<std::string, Day> day_by_id;
        for (const auto &day : writable.value->days)
        {
            day_by_id.emplace(day.id, day);
        }
        std::vector<Day> reordered;
        reordered.reserve(writable.value->days.size());
        for (const auto &id : day_ids_order)
        {
            auto it = day_by_id.find(id);
            if (it == day_by_id.end())
            {
                return statusOnly(Status::InvalidArgument, "Unknown day id in order");
            }
            reordered.push_back(it->second);
        }
        auto actor = authUserIdByToken(token);
        writable.value->days = std::move(reordered);
        appendEvent(*writable.value, actor.value, "reorder", "day", "", "Days reordered");
        return statusOnly(Status::Ok, {});
    }

    StatusOr<std::string> TripService::addPlanItem(
        const std::string &token,
        const std::string &trip_id,
        const std::string &day_id,
        uint64_t expected_revision,
        const PlanItem &item)
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
        auto day_it = std::find_if(writable.value->days.begin(), writable.value->days.end(), [&](const Day &day)
                                   { return day.id == day_id; });
        if (day_it == writable.value->days.end())
        {
            return StatusOr<std::string>{Status::NotFound, "Day not found", {}};
        }
        auto actor = authUserIdByToken(token);
        PlanItem to_add = item;
        to_add.id = nextId("item_");
        day_it->items.push_back(to_add);
        appendEvent(*writable.value, actor.value, "add", "item", to_add.id, "Plan item added");
        return StatusOr<std::string>{Status::Ok, {}, to_add.id};
    }

    StatusResult TripService::updatePlanItem(
        const std::string &token,
        const std::string &trip_id,
        const std::string &day_id,
        uint64_t expected_revision,
        const PlanItem &item)
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
        auto day_it = std::find_if(writable.value->days.begin(), writable.value->days.end(), [&](const Day &day)
                                   { return day.id == day_id; });
        if (day_it == writable.value->days.end())
        {
            return statusOnly(Status::NotFound, "Day not found");
        }
        auto item_it = std::find_if(day_it->items.begin(), day_it->items.end(), [&](const PlanItem &candidate)
                                    { return candidate.id == item.id; });
        if (item_it == day_it->items.end())
        {
            return statusOnly(Status::NotFound, "Item not found");
        }
        auto actor = authUserIdByToken(token);
        *item_it = item;
        appendEvent(*writable.value, actor.value, "update", "item", item.id, "Plan item updated");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::removePlanItem(
        const std::string &token,
        const std::string &trip_id,
        const std::string &day_id,
        uint64_t expected_revision,
        const std::string &item_id)
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
        auto day_it = std::find_if(writable.value->days.begin(), writable.value->days.end(), [&](const Day &day)
                                   { return day.id == day_id; });
        if (day_it == writable.value->days.end())
        {
            return statusOnly(Status::NotFound, "Day not found");
        }
        auto previous_size = day_it->items.size();
        std::erase_if(day_it->items, [&](const PlanItem &item)
                      { return item.id == item_id; });
        if (day_it->items.size() == previous_size)
        {
            return statusOnly(Status::NotFound, "Item not found");
        }
        auto actor = authUserIdByToken(token);
        appendEvent(*writable.value, actor.value, "remove", "item", item_id, "Plan item removed");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::reorderPlanItems(
        const std::string &token,
        const std::string &trip_id,
        const std::string &day_id,
        uint64_t expected_revision,
        const std::vector<std::string> &item_ids_order)
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
        auto day_it = std::find_if(writable.value->days.begin(), writable.value->days.end(), [&](const Day &day)
                                   { return day.id == day_id; });
        if (day_it == writable.value->days.end())
        {
            return statusOnly(Status::NotFound, "Day not found");
        }
        if (!sameIds(item_ids_order, day_it->items.size()))
        {
            return statusOnly(Status::InvalidArgument, "Invalid order list");
        }
        std::unordered_map<std::string, PlanItem> item_by_id;
        for (const auto &item : day_it->items)
        {
            item_by_id.emplace(item.id, item);
        }
        std::vector<PlanItem> reordered;
        reordered.reserve(day_it->items.size());
        for (const auto &id : item_ids_order)
        {
            auto it = item_by_id.find(id);
            if (it == item_by_id.end())
            {
                return statusOnly(Status::InvalidArgument, "Unknown item id in order");
            }
            reordered.push_back(it->second);
        }
        auto actor = authUserIdByToken(token);
        day_it->items = std::move(reordered);
        appendEvent(*writable.value, actor.value, "reorder", "item", day_id, "Items reordered");
        return statusOnly(Status::Ok, {});
    }

}
