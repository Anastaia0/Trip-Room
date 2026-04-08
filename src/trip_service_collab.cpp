#include "trip/trip_service.hpp"

#include <algorithm>
#include <sstream>

namespace trip
{
    StatusOr<std::string> TripService::sendMessage(
        const std::string &token,
        const std::string &trip_id,
        const std::string &text)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Viewer);
        if (!writable.ok())
        {
            return StatusOr<std::string>{writable.status, writable.message, {}};
        }
        if (text.empty())
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Message text must not be empty", {}};
        }
        auto actor = authUserIdByToken(token);
        ChatMessage message;
        message.id = nextId("msg_");
        message.user_id = actor.value;
        message.text = text;
        message.timestamp_ms = nowMs();
        writable.value->chat.push_back(message);
        appendEvent(*writable.value, actor.value, "send", "chat", message.id, "Message sent");
        return StatusOr<std::string>{Status::Ok, {}, message.id};
    }

    StatusOr<std::vector<ChatMessage>> TripService::getMessages(
        const std::string &token,
        const std::string &trip_id) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<std::vector<ChatMessage>>{readable.status, readable.message, {}};
        }
        return StatusOr<std::vector<ChatMessage>>{Status::Ok, {}, readable.value->chat};
    }

    StatusOr<std::vector<SearchHit>> TripService::searchInTrip(
        const std::string &token,
        const std::string &trip_id,
        const std::string &query) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<std::vector<SearchHit>>{readable.status, readable.message, {}};
        }

        std::vector<SearchHit> results;
        for (const auto &day : readable.value->days)
        {
            for (const auto &item : day.items)
            {
                if (containsCaseInsensitive(item.name, query) ||
                    containsCaseInsensitive(item.notes, query) ||
                    containsCaseInsensitive(item.category, query))
                {
                    results.push_back(SearchHit{"plan_item", item.id, item.name});
                }
            }
        }
        for (const auto &task : readable.value->tasks)
        {
            if (containsCaseInsensitive(task.text, query))
            {
                results.push_back(SearchHit{"task", task.id, task.text});
            }
        }
        for (const auto &expense : readable.value->expenses)
        {
            if (containsCaseInsensitive(expense.category, query) || containsCaseInsensitive(expense.comment, query))
            {
                std::ostringstream os;
                os << expense.category << " " << expense.amount;
                results.push_back(SearchHit{"expense", expense.id, os.str()});
            }
        }
        return StatusOr<std::vector<SearchHit>>{Status::Ok, {}, results};
    }

    StatusOr<std::vector<TripSummary>> TripService::listTrips(const std::string &token) const
    {
        std::scoped_lock lock(mutex_);
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return StatusOr<std::vector<TripSummary>>{user.status, user.message, {}};
        }

        std::vector<TripSummary> trips;
        trips.reserve(trips_by_id_.size());
        for (const auto &[trip_id, trip] : trips_by_id_)
        {
            const auto member_it = trip.members.find(user.value);
            if (member_it == trip.members.end())
            {
                continue;
            }

            TripSummary summary;
            summary.id = trip_id;
            summary.info = trip.info;
            summary.my_role = member_it->second;
            summary.revision = trip.revision;
            summary.members_count = trip.members.size();
            summary.days_count = trip.days.size();
            summary.tasks_count = trip.tasks.size();
            summary.expenses_count = trip.expenses.size();
            trips.push_back(std::move(summary));
        }

        std::sort(
            trips.begin(),
            trips.end(),
            [](const TripSummary &left, const TripSummary &right)
            {
                return left.id < right.id;
            });

        return StatusOr<std::vector<TripSummary>>{Status::Ok, {}, trips};
    }

    StatusOr<Trip> TripService::getTripSnapshot(const std::string &token, const std::string &trip_id) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<Trip>{readable.status, readable.message, {}};
        }
        return StatusOr<Trip>{Status::Ok, {}, *readable.value};
    }

    StatusOr<std::vector<Event>> TripService::getEventsSince(
        const std::string &token,
        const std::string &trip_id,
        uint64_t since_revision) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<std::vector<Event>>{readable.status, readable.message, {}};
        }
        std::vector<Event> events;
        for (const auto &event : readable.value->events)
        {
            if (event.revision > since_revision)
            {
                events.push_back(event);
            }
        }
        return StatusOr<std::vector<Event>>{Status::Ok, {}, events};
    }

}
