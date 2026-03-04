#include "trip/trip_service.hpp"

#include <algorithm>

namespace trip
{
    StatusOr<std::string> TripService::addTask(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const Task &task)
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
        if (task.text.empty())
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Task text must not be empty", {}};
        }
        auto actor = authUserIdByToken(token);
        Task to_add = task;
        to_add.id = nextId("task_");
        writable.value->tasks.push_back(to_add);
        appendEvent(*writable.value, actor.value, "add", "task", to_add.id, "Task added");
        return StatusOr<std::string>{Status::Ok, {}, to_add.id};
    }

    StatusResult TripService::updateTask(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const Task &task)
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
        auto task_it = std::find_if(writable.value->tasks.begin(), writable.value->tasks.end(), [&](const Task &candidate)
                                    { return candidate.id == task.id; });
        if (task_it == writable.value->tasks.end())
        {
            return statusOnly(Status::NotFound, "Task not found");
        }
        auto actor = authUserIdByToken(token);
        *task_it = task;
        appendEvent(*writable.value, actor.value, "update", "task", task.id, "Task updated");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::setTaskDone(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &task_id,
        bool done)
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
        auto task_it = std::find_if(writable.value->tasks.begin(), writable.value->tasks.end(), [&](const Task &candidate)
                                    { return candidate.id == task_id; });
        if (task_it == writable.value->tasks.end())
        {
            return statusOnly(Status::NotFound, "Task not found");
        }
        auto actor = authUserIdByToken(token);
        task_it->done = done;
        appendEvent(*writable.value, actor.value, "toggle", "task", task_id, done ? "Task done" : "Task undone");
        return statusOnly(Status::Ok, {});
    }

    StatusResult TripService::removeTask(
        const std::string &token,
        const std::string &trip_id,
        uint64_t expected_revision,
        const std::string &task_id)
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
        auto previous_size = writable.value->tasks.size();
        std::erase_if(writable.value->tasks, [&](const Task &task)
                      { return task.id == task_id; });
        if (previous_size == writable.value->tasks.size())
        {
            return statusOnly(Status::NotFound, "Task not found");
        }
        auto actor = authUserIdByToken(token);
        appendEvent(*writable.value, actor.value, "remove", "task", task_id, "Task removed");
        return statusOnly(Status::Ok, {});
    }

}
