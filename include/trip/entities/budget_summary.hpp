#pragma once

#include <string>
#include <unordered_map>

namespace trip
{
    struct BudgetSummary
    {
        double total_expenses = 0.0;
        std::unordered_map<std::string, double> by_category;
        std::unordered_map<std::string, double> paid_by_user;
        std::unordered_map<std::string, double> balance_by_user;
    };

}
