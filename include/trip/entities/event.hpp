#pragma once

#include <cstdint>
#include <string>

namespace trip
{
    struct Event
    {
        uint64_t revision = 0;
        int64_t timestamp_ms = 0;
        std::string actor_user_id;
        std::string action;
        std::string entity;
        std::string entity_id;
        std::string details;
    };

}
