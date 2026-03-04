#include "trip/trip_service.hpp"

#include <sstream>

namespace trip
{
    namespace
    {
        std::string escapeJson(const std::string &input)
        {
            std::string out;
            out.reserve(input.size() + 8);
            for (char ch : input)
            {
                if (ch == '\\')
                {
                    out += "\\\\";
                }
                else if (ch == '"')
                {
                    out += "\\\"";
                }
                else
                {
                    out.push_back(ch);
                }
            }
            return out;
        }

        std::string unescapeJson(const std::string &input)
        {
            std::string out;
            out.reserve(input.size());
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                if (input[i] == '\\' && i + 1 < input.size())
                {
                    ++i;
                    out.push_back(input[i]);
                }
                else
                {
                    out.push_back(input[i]);
                }
            }
            return out;
        }

        bool readStringField(const std::string &json, const std::string &key, std::string &out, std::size_t start_pos = 0)
        {
            const std::string token = "\"" + key + "\":\"";
            std::size_t key_pos = json.find(token, start_pos);
            if (key_pos == std::string::npos)
            {
                return false;
            }

            std::size_t value_start = key_pos + token.size();
            std::size_t value_end = value_start;
            while (value_end < json.size())
            {
                if (json[value_end] == '"' && (value_end == value_start || json[value_end - 1] != '\\'))
                {
                    break;
                }
                ++value_end;
            }
            if (value_end >= json.size())
            {
                return false;
            }

            out = unescapeJson(json.substr(value_start, value_end - value_start));
            return true;
        }

        bool readNumberField(const std::string &json, const std::string &key, double &out)
        {
            const std::string token = "\"" + key + "\":";
            std::size_t key_pos = json.find(token);
            if (key_pos == std::string::npos)
            {
                return false;
            }

            std::size_t pos = key_pos + token.size();
            while (pos < json.size() && json[pos] == ' ')
            {
                ++pos;
            }

            if (json.compare(pos, 4, "null") == 0)
            {
                return false;
            }

            std::size_t end = pos;
            while (end < json.size() && (isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.' || json[end] == '-'))
            {
                ++end;
            }
            if (end == pos)
            {
                return false;
            }

            out = std::stod(json.substr(pos, end - pos));
            return true;
        }

        bool extractArrayBody(const std::string &json, const std::string &key, std::string &body)
        {
            const std::string token = "\"" + key + "\":";
            std::size_t key_pos = json.find(token);
            if (key_pos == std::string::npos)
            {
                return false;
            }

            std::size_t start = json.find('[', key_pos + token.size());
            if (start == std::string::npos)
            {
                return false;
            }

            int depth = 0;
            bool in_string = false;
            for (std::size_t i = start; i < json.size(); ++i)
            {
                char ch = json[i];
                if (ch == '"' && (i == 0 || json[i - 1] != '\\'))
                {
                    in_string = !in_string;
                }
                if (in_string)
                {
                    continue;
                }

                if (ch == '[')
                {
                    ++depth;
                }
                else if (ch == ']')
                {
                    --depth;
                    if (depth == 0)
                    {
                        body = json.substr(start + 1, i - start - 1);
                        return true;
                    }
                }
            }
            return false;
        }

        std::vector<std::string> extractDayNames(const std::string &json)
        {
            std::vector<std::string> names;
            std::string days_body;
            if (!extractArrayBody(json, "days", days_body))
            {
                return names;
            }

            std::size_t pos = 0;
            while (pos < days_body.size())
            {
                std::string day_name;
                if (!readStringField(days_body, "name", day_name, pos))
                {
                    break;
                }
                names.push_back(day_name);

                const std::string marker = "\"name\":\"";
                std::size_t marker_pos = days_body.find(marker, pos);
                if (marker_pos == std::string::npos)
                {
                    break;
                }
                pos = marker_pos + marker.size();
            }

            return names;
        }

    }

    StatusOr<std::string> TripService::exportTripJson(const std::string &token, const std::string &trip_id) const
    {
        std::scoped_lock lock(mutex_);
        auto readable = readableTripFor(token, trip_id);
        if (!readable.ok())
        {
            return StatusOr<std::string>{readable.status, readable.message, {}};
        }

        std::ostringstream out;
        out << "{\"trip\":{";
        out << "\"title\":\"" << escapeJson(readable.value->info.title) << "\",";
        out << "\"start_date\":\"" << escapeJson(readable.value->info.start_date) << "\",";
        out << "\"end_date\":\"" << escapeJson(readable.value->info.end_date) << "\",";
        out << "\"description\":\"" << escapeJson(readable.value->info.description) << "\",";
        out << "\"budget_currency\":\"" << escapeJson(readable.value->budget.currency) << "\",";
        out << "\"budget_limit\":" << readable.value->budget.total_limit << ",";

        out << "\"days\":[";
        bool first = true;
        for (const auto &day : readable.value->days)
        {
            if (!first)
            {
                out << ",";
            }
            first = false;
            out << "{\"name\":\"" << escapeJson(day.name) << "\"}";
        }
        out << "]";

        out << "}}";
        return StatusOr<std::string>{Status::Ok, {}, out.str()};
    }

    StatusOr<std::string> TripService::importTripJson(const std::string &token, const std::string &json_text)
    {
        std::scoped_lock lock(mutex_);
        auto user = authUserIdByToken(token);
        if (!user.ok())
        {
            return StatusOr<std::string>{user.status, user.message, {}};
        }

        if (json_text.find("\"trip\"") == std::string::npos)
        {
            return StatusOr<std::string>{Status::InvalidArgument, "Invalid JSON", {}};
        }

        Trip trip;
        trip.id = nextId("trip_");
        trip.members[user.value] = Role::Owner;

        readStringField(json_text, "title", trip.info.title);
        readStringField(json_text, "start_date", trip.info.start_date);
        readStringField(json_text, "end_date", trip.info.end_date);
        readStringField(json_text, "description", trip.info.description);
        readStringField(json_text, "budget_currency", trip.budget.currency);

        double limit = -1.0;
        if (readNumberField(json_text, "budget_limit", limit))
        {
            trip.budget.total_limit = limit;
        }

        for (const auto &name : extractDayNames(json_text))
        {
            Day day;
            day.id = nextId("day_");
            day.name = name;
            trip.days.push_back(day);
        }

        appendEvent(trip, user.value, "import", "trip", trip.id, "Trip imported from JSON");
        trips_by_id_[trip.id] = trip;
        return StatusOr<std::string>{Status::Ok, {}, trip.id};
    }
}
