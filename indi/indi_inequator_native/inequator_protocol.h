/*
    InEquator RA Tracker protocol helpers
    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

#include <cstdint>
#include <string>

namespace InEquatorProtocol
{
struct MotionStatus
{
    int32_t position { 0 };   // steps
    bool tracking { false };
    int32_t jogRate { 0 };    // x10000 of sidereal
    bool moving { false };
};

// "P <steps>;T <true|false>;Q <rate>;M <true|false>[#]"
bool parseMotionStatus(const char *response, MotionStatus &status);

// "#" identification: "InEquator RA Tracker ver <n>"
bool isSupportedIdentity(const char *response);

// "V <n>[#]"
bool parseVersion(const char *response, int &version);

bool isErrorResponse(const char *response);

bool parseJsonInteger(const std::string &json, const char *key, int64_t &value);
bool parseJsonNumber(const std::string &json, const char *key, double &value);
bool parseJsonBoolean(const std::string &json, const char *key, bool &value);
}
