#pragma once

#include <string>

namespace trip
{
    struct Expense
    {
        std::string id;
        double amount = 0.0;
        std::string category;
        std::string paid_by_user_id;
        std::string comment;
        std::string date;
        std::string day_id;
    };

}
