#pragma once

#include <string>

namespace trip
{
    struct BudgetSettings
    {
        std::string currency = "USD";
        double total_limit = -1.0;
    };

}
