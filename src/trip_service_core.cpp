#include "trip/trip_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>

namespace trip
{
    int64_t TripService::nowMs()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    int TripService::roleRank(Role role)
    {
        switch (role)
        {
        case Role::Owner:
            return 3;
        case Role::Editor:
            return 2;
        case Role::Viewer:
            return 1;
        }
        return 0;
    }

    bool TripService::containsCaseInsensitive(const std::string &text, const std::string &query)
    {
        if (query.empty())
        {
            return true;
        }
        std::string left = text;
        std::string right = query;
        std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return left.find(right) != std::string::npos;
    }

    std::string TripService::nextId(const std::string &prefix)
    {
        ++id_counter_;
        return prefix + std::to_string(id_counter_);
    }

    StatusResult TripService::statusOnly(Status status, const std::string &message)
    {
        return StatusResult{status, message};
    }

    void TripService::appendEvent(
        Trip &trip,
        const std::string &actor,
        const std::string &action,
        const std::string &entity,
        const std::string &entity_id,
        const std::string &details)
    {
        ++trip.revision;
        trip.events.push_back(Event{trip.revision, nowMs(), actor, action, entity, entity_id, details});
    }

    bool TripService::checkRevision(const Trip &trip, uint64_t expected_revision)
    {
        return trip.revision == expected_revision;
    }

    StatusOr<std::string> TripService::authUserIdByToken(const std::string &token) const
    {
        auto it = user_id_by_token_.find(token);
        if (it == user_id_by_token_.end())
        {
            return StatusOr<std::string>{Status::Unauthorized, "Invalid token", {}};
        }
        return StatusOr<std::string>{Status::Ok, {}, it->second};
    }

    StatusOr<Trip *> TripService::writableTripFor(const std::string &token, const std::string &trip_id, Role min_role)
    {
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return StatusOr<Trip *>{user.status, user.message, nullptr};
        }
        auto trip_it = trips_by_id_.find(trip_id);
        if (trip_it == trips_by_id_.end())
        {
            return StatusOr<Trip *>{Status::NotFound, "Trip not found", nullptr};
        }
        auto member_it = trip_it->second.members.find(user.value);
        if (member_it == trip_it->second.members.end())
        {
            return StatusOr<Trip *>{Status::Forbidden, "User is not a member of trip", nullptr};
        }
        if (roleRank(member_it->second) < roleRank(min_role))
        {
            return StatusOr<Trip *>{Status::Forbidden, "Insufficient role", nullptr};
        }
        return StatusOr<Trip *>{Status::Ok, {}, &trip_it->second};
    }

    StatusOr<const Trip *> TripService::readableTripFor(const std::string &token, const std::string &trip_id) const
    {
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return StatusOr<const Trip *>{user.status, user.message, nullptr};
        }
        auto trip_it = trips_by_id_.find(trip_id);
        if (trip_it == trips_by_id_.end())
        {
            return StatusOr<const Trip *>{Status::NotFound, "Trip not found", nullptr};
        }
        auto member_it = trip_it->second.members.find(user.value);
        if (member_it == trip_it->second.members.end())
        {
            return StatusOr<const Trip *>{Status::Forbidden, "User is not a member of trip", nullptr};
        }
        return StatusOr<const Trip *>{Status::Ok, {}, &trip_it->second};
    }

    StatusOr<std::string> TripService::registerUser(const std::string &login, const std::string &password)
    {
        std::scoped_lock lock(mutex_);
        if (login.empty() || password.empty())
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Login and password must not be empty", {}};
        }
        if (user_id_by_login_.contains(login))
        {
            return StatusOr<std::string>{Status::Conflict, "Login already exists", {}};
        }
        const std::string user_id = nextId("u_");
        users_by_id_[user_id] = User{user_id, login, password};
        user_id_by_login_[login] = user_id;
        return StatusOr<std::string>{Status::Ok, {}, user_id};
    }

    StatusOr<std::string> TripService::login(const std::string &login_name, const std::string &password)
    {
        std::scoped_lock lock(mutex_);
        auto login_it = user_id_by_login_.find(login_name);
        if (login_it == user_id_by_login_.end())
        {
            return StatusOr<std::string>{Status::Unauthorized, "Unknown login", {}};
        }
        auto user_it = users_by_id_.find(login_it->second);
        if (user_it == users_by_id_.end() || user_it->second.password != password)
        {
            return StatusOr<std::string>{Status::Unauthorized, "Invalid password", {}};
        }
        const std::string token = nextId("tok_");
        user_id_by_token_[token] = user_it->second.id;
        return StatusOr<std::string>{Status::Ok, {}, token};
    }

    StatusOr<std::string> TripService::createTrip(const std::string &token, const TripInfo &info)
    {
        std::scoped_lock lock(mutex_);
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return StatusOr<std::string>{user.status, user.message, {}};
        }
        if (info.title.empty())
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Trip title must not be empty", {}};
        }

        const std::string trip_id = nextId("trip_");
        Trip trip;
        trip.id = trip_id;
        trip.info = info;
        trip.members[user.value] = Role::Owner;
        appendEvent(trip, user.value, "create", "trip", trip.id, "Trip created");
        trips_by_id_[trip_id] = trip;
        return StatusOr<std::string>{Status::Ok, {}, trip_id};
    }

    StatusResult TripService::deleteTrip(const std::string &token, const std::string &trip_id)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Owner);
        if (!writable.ok())
        {
            return statusOnly(writable.status, writable.message);
        }
        trips_by_id_.erase(trip_id);
        return statusOnly(Status::Ok, {});
    }

    StatusOr<std::string> TripService::createInvite(
        const std::string &token,
        const std::string &trip_id,
        Role role)
    {
        std::scoped_lock lock(mutex_);
        if (role == Role::Owner)
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Invite role cannot be owner", {}};
        }
        auto writable = writableTripFor(token, trip_id, Role::Owner);
        if (!writable.ok())
        {
            return StatusOr<std::string>{writable.status, writable.message, {}};
        }
        auto user = authUserIdByToken(token);
        const std::string code = nextId("inv_");
        invites_by_code_[code] = Invite{code, trip_id, user.value, role};
        return StatusOr<std::string>{Status::Ok, {}, code};
    }

    StatusResult TripService::acceptInvite(const std::string &token, const std::string &invite_code)
    {
        std::scoped_lock lock(mutex_);
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return statusOnly(user.status, user.message);
        }
        auto invite_it = invites_by_code_.find(invite_code);
        if (invite_it == invites_by_code_.end())
        {
            return statusOnly(Status::NotFound, "Invite not found");
        }
        auto trip_it = trips_by_id_.find(invite_it->second.trip_id);
        if (trip_it == trips_by_id_.end())
        {
            return statusOnly(Status::NotFound, "Trip not found");
        }
        trip_it->second.members[user.value] = invite_it->second.role;
        appendEvent(trip_it->second, user.value, "join", "member", user.value, "Joined by invite");
        invites_by_code_.erase(invite_it);
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::changeMemberRole(
        const std::string &token,
        const std::string &trip_id,
        const std::string &target_user_id,
        Role new_role)
    {
        std::scoped_lock lock(mutex_);
        if (new_role == Role::Owner)
        {
            return statusOnly(Status::InvalidArgument, "Role transfer is not supported");
        }
        auto writable = writableTripFor(token, trip_id, Role::Owner);
        if (!writable.ok())
        {
            return statusOnly(writable.status, writable.message);
        }
        auto actor = authUserIdByToken(token);
        auto member_it = writable.value->members.find(target_user_id);
        if (member_it == writable.value->members.end())
        {
            return statusOnly(Status::NotFound, "Target member not found");
        }
        if (member_it->second == Role::Owner)
        {
            return statusOnly(Status::Forbidden, "Cannot modify owner role");
        }
        member_it->second = new_role;
        appendEvent(*writable.value, actor.value, "change_role", "member", target_user_id, "Role changed");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::removeMember(
        const std::string &token,
        const std::string &trip_id,
        const std::string &target_user_id)
    {
        std::scoped_lock lock(mutex_);
        auto writable = writableTripFor(token, trip_id, Role::Owner);
        if (!writable.ok())
        {
            return statusOnly(writable.status, writable.message);
        }
        auto actor = authUserIdByToken(token);
        auto member_it = writable.value->members.find(target_user_id);
        if (member_it == writable.value->members.end())
        {
            return statusOnly(Status::NotFound, "Target member not found");
        }
        if (member_it->second == Role::Owner)
        {
            return statusOnly(Status::Forbidden, "Cannot remove owner");
        }
        writable.value->members.erase(member_it);
        appendEvent(*writable.value, actor.value, "remove_member", "member", target_user_id, "Member removed");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::updateTripInfo(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const TripInfo &info)
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
        writable.value->info = info;
        appendEvent(*writable.value, actor.value, "update", "trip", trip_id, "Trip info updated");
        return statusOnly(Status::Ok, {});
    }

    StatusOr<uint64_t> TripService::getTripRevision(const std::string &token, const std::string &trip_id) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<uint64_t>{readable.status, readable.message, 0};
        }
        return StatusOr<uint64_t>{Status::Ok, {}, readable.value->revision};
    }

}
