#include "trip_http_server_detail.hpp"

#include <cctype>
#include <stdexcept>

namespace trip::detail
{
    namespace
    {
        std::string percentDecode(std::string_view encoded)
        {
            std::string decoded;
            decoded.reserve(encoded.size());
            for (std::size_t i = 0; i < encoded.size(); ++i)
            {
                const char ch = encoded[i];
                if (ch == '+')
                {
                    decoded.push_back(' ');
                    continue;
                }
                if (ch == '%' && i + 2 < encoded.size())
                {
                    auto hexToInt = [](char c) -> int
                    {
                        if (c >= '0' && c <= '9')
                        {
                            return c - '0';
                        }
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (c >= 'a' && c <= 'f')
                        {
                            return 10 + (c - 'a');
                        }
                        return -1;
                    };

                    const int hi = hexToInt(encoded[i + 1]);
                    const int lo = hexToInt(encoded[i + 2]);
                    if (hi >= 0 && lo >= 0)
                    {
                        decoded.push_back(static_cast<char>((hi << 4) | lo));
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(ch);
            }
            return decoded;
        }
    }

    std::unordered_map<std::string, std::string> parseForm(std::string_view payload)
    {
        std::unordered_map<std::string, std::string> result;
        std::size_t start = 0;
        while (start <= payload.size())
        {
            const std::size_t amp = payload.find('&', start);
            const std::size_t end = amp == std::string_view::npos ? payload.size() : amp;
            const std::string_view token = payload.substr(start, end - start);
            if (!token.empty())
            {
                const std::size_t eq = token.find('=');
                if (eq == std::string_view::npos)
                {
                    result.emplace(percentDecode(token), std::string{});
                }
                else
                {
                    result[percentDecode(token.substr(0, eq))] = percentDecode(token.substr(eq + 1));
                }
            }

            if (amp == std::string_view::npos)
            {
                break;
            }
            start = amp + 1;
        }
        return result;
    }

    std::pair<std::string, std::unordered_map<std::string, std::string>> splitTarget(const std::string &target)
    {
        const std::size_t question_pos = target.find('?');
        if (question_pos == std::string::npos)
        {
            return {target, {}};
        }
        return {target.substr(0, question_pos), parseForm(target.substr(question_pos + 1))};
    }

    std::string authorizationBearerToken(const http::request<http::string_body> &req)
    {
        const auto header = req[http::field::authorization];
        if (header.empty())
        {
            return {};
        }

        constexpr std::string_view prefix = "Bearer ";
        std::string_view value{header.data(), header.size()};
        if (!value.starts_with(prefix) || value.size() <= prefix.size())
        {
            return {};
        }
        return std::string(value.substr(prefix.size()));
    }

    uint64_t parseUint64(const std::string &text)
    {
        if (text.empty())
        {
            throw std::invalid_argument("empty");
        }
        std::size_t used = 0;
        const auto value = std::stoull(text, &used);
        if (used != text.size())
        {
            throw std::invalid_argument("trailing");
        }
        return value;
    }

    double parseDouble(const std::string &text)
    {
        if (text.empty())
        {
            throw std::invalid_argument("empty");
        }
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size())
        {
            throw std::invalid_argument("trailing");
        }
        return value;
    }

    bool parseBool(const std::string &text)
    {
        if (text == "1" || text == "true" || text == "True" || text == "TRUE")
        {
            return true;
        }
        if (text == "0" || text == "false" || text == "False" || text == "FALSE")
        {
            return false;
        }
        throw std::invalid_argument("invalid_bool");
    }

    std::vector<std::string> splitCsv(const std::string &text)
    {
        std::vector<std::string> items;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const std::size_t comma = text.find(',', start);
            const std::size_t end = comma == std::string::npos ? text.size() : comma;
            const std::string token = text.substr(start, end - start);
            if (!token.empty())
            {
                items.push_back(token);
            }
            if (comma == std::string::npos)
            {
                break;
            }
            start = comma + 1;
        }
        return items;
    }

    Role parseRole(const std::string &text)
    {
        if (text == "Owner" || text == "owner")
        {
            return Role::Owner;
        }
        if (text == "Editor" || text == "editor")
        {
            return Role::Editor;
        }
        if (text == "Viewer" || text == "viewer")
        {
            return Role::Viewer;
        }
        throw std::invalid_argument("invalid_role");
    }
}
