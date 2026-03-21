#pragma once

#include <string>
#include <vector>

#include "plan_item.hpp"

namespace trip
{
    struct Day
    {
        std::string id;
        std::string name;
        std::vector<PlanItem> items;
    };

}
