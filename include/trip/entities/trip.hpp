#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/role.hpp"
#include "budget_settings.hpp"
#include "chat_message.hpp"
#include "day.hpp"
#include "event.hpp"
#include "expense.hpp"
#include "task.hpp"
#include "trip_info.hpp"

namespace trip
{
    struct Trip
    {
        std::string id;
        TripInfo info;
        std::unordered_map<std::string, Role> members;
        std::vector<Day> days;
        std::vector<Task> tasks;
        BudgetSettings budget;
        std::vector<Expense> expenses;
        std::vector<ChatMessage> chat;
        std::vector<Event> events;
        uint64_t revision = 0;
    };

}
