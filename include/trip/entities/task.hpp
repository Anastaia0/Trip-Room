#pragma once

#include <string>

namespace trip
{
    struct Task
    {
        std::string id;
        std::string text;
        bool done = false;
        std::string assignee_user_id;
        std::string deadline;
    };

}
