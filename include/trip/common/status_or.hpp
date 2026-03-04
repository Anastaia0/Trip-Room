#pragma once

#include <string>

#include "status.hpp"

namespace trip
{
    struct StatusResult
    {
        Status status = Status::Ok;
        std::string message;

        [[nodiscard]] bool ok() const { return status == Status::Ok; }
    };

    template <typename T>
    struct StatusOr
    {
        Status status = Status::Ok;
        std::string message;
        T value{};

        [[nodiscard]] bool ok() const { return status == Status::Ok; }
    };
}
