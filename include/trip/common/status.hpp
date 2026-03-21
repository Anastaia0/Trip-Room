#pragma once

namespace trip
{
    enum class Status
    {
        Ok,
        Unauthorized,
        Forbidden,
        NotFound,
        Conflict,
        InvalidArgument
    };

}
