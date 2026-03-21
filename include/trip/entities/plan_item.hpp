#pragma once

#include <string>

namespace trip
{
    struct PlanItem
    {
        std::string id;
        std::string name;
        std::string time;
        std::string notes;
        std::string category;
        std::string link;
    };

}
