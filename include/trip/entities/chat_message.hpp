#pragma once

#include <cstdint>
#include <string>

namespace trip
{
    struct ChatMessage
    {
        std::string id;
        std::string user_id;
        std::string text;
        int64_t timestamp_ms = 0;
    };

}
