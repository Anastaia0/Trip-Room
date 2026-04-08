#pragma once

#include <cstdint>
#include <string>

#include "../common/role.hpp"
#include "trip_info.hpp"

namespace trip
{
    struct TripSummary
    {
        std::string id;
        TripInfo info;
        Role my_role = Role::Viewer;
        uint64_t revision = 0;
        std::size_t members_count = 0;
        std::size_t days_count = 0;
        std::size_t tasks_count = 0;
        std::size_t expenses_count = 0;
    };
}
