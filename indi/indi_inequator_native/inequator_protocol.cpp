/*
    InEquator RA Tracker protocol helpers
    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "inequator_protocol.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace InEquatorProtocol
{

bool parseMotionStatus(const char *response, MotionStatus &status)
{
    if (response == nullptr)
        return false;

    int position = 0;
    int jogRate = 0;
    char tracking[8] = { 0 };
    char moving[8] = { 0 };
    const int matched = std::sscanf(response, " P %d ; T %7[^;] ; Q %d ; M %7[^;]",
                                    &position, tracking, &jogRate, moving);
    if (matched != 4)
        return false;

    std::string trackingText(tracking);
    std::string movingText(moving);
    auto trim = [](std::string &text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            text.clear();
            return;
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        text = text.substr(first, last - first + 1);
    };
    trim(trackingText);
    trim(movingText);
    // tty_nread_section includes the trailing '#' terminator.
    if (!movingText.empty() && movingText.back() == '#')
        movingText.pop_back();
    const bool trackingBool = trackingText == "true" || trackingText == "1";
    const bool trackingFalse = trackingText == "false" || trackingText == "0";
    const bool movingBool = movingText == "true" || movingText == "1";
    const bool movingFalse = movingText == "false" || movingText == "0";
    if ((!trackingBool && !trackingFalse) || (!movingBool && !movingFalse))
        return false;

    status.position = position;
    status.tracking = trackingBool;
    status.jogRate = jogRate;
    status.moving = movingBool;
    return true;
}

bool isSupportedIdentity(const char *response)
{
    if (response == nullptr)
        return false;
    return std::strncmp(response, "InEquator RA Tracker", 20) == 0;
}

bool parseVersion(const char *response, int &version)
{
    if (response == nullptr)
        return false;
    int parsed = 0;
    if (std::sscanf(response, " V %d", &parsed) != 1)
        return false;
    version = parsed;
    return true;
}

bool isErrorResponse(const char *response)
{
    if (response == nullptr)
        return false;
    while (std::isspace(static_cast<unsigned char>(*response)))
        response++;
    return std::strncmp(response, "ERR:", 4) == 0;
}

bool parseJsonInteger(const std::string &json, const char *key, int64_t &value)
{
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;

    const char *start = json.c_str() + pos + needle.size();
    while (*start == ' ')
        start++;
    char *end = nullptr;
    const long long parsed = std::strtoll(start, &end, 10);
    if (end == start)
        return false;
    value = static_cast<int64_t>(parsed);
    return true;
}

bool parseJsonNumber(const std::string &json, const char *key, double &value)
{
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;

    const char *start = json.c_str() + pos + needle.size();
    while (*start == ' ')
        start++;
    char *end = nullptr;
    const double parsed = std::strtod(start, &end);
    if (end == start)
        return false;
    value = parsed;
    return true;
}

bool parseJsonBoolean(const std::string &json, const char *key, bool &value)
{
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;

    const char *start = json.c_str() + pos + needle.size();
    while (*start == ' ')
        start++;
    if (std::strncmp(start, "true", 4) == 0)
    {
        value = true;
        return true;
    }
    if (std::strncmp(start, "false", 5) == 0)
    {
        value = false;
        return true;
    }
    return false;
}

}
