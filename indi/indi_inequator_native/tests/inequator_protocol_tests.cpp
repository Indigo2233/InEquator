/*
    InEquator RA Tracker protocol unit tests
    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "inequator_protocol.h"

#include <cassert>
#include <cstdio>
#include <string>

using InEquatorProtocol::MotionStatus;

static int failures = 0;

static void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main()
{
    {
        MotionStatus status;
        check(InEquatorProtocol::parseMotionStatus(
                  "P 12345;T true;Q 80000;Y 100;M false#", status),
              "parse full motion status");
        check(status.position == 12345, "position parsed");
        check(status.tracking == true, "tracking parsed");
        check(status.jogRate == 80000, "jog rate parsed");
        check(status.jogStepsPerSec == 100, "jog steps/s parsed");
        check(status.moving == false, "moving parsed");
    }
    {
        MotionStatus status;
        check(InEquatorProtocol::parseMotionStatus("P -12;T false;Q 10000;Y 4000;M true#", status),
              "parse negative position");
        check(status.position == -12 && status.moving == true, "negative/moving values");
    }
    {
        MotionStatus status;
        check(!InEquatorProtocol::parseMotionStatus("P 1;T maybe;Q 2;Y 3;M false#", status),
              "reject invalid boolean");
        check(!InEquatorProtocol::parseMotionStatus("P 1;T true;Q 2;M false#", status),
              "reject old format without Y");
        check(!InEquatorProtocol::parseMotionStatus("garbage", status), "reject garbage");
    }

    check(InEquatorProtocol::isSupportedIdentity("InEquator RA Tracker ver 2001#"),
          "identity recognized");
    check(!InEquatorProtocol::isSupportedIdentity("EFucoser ESP8266 Focuser ver 1005#"),
          "foreign identity rejected");

    int version = 0;
    check(InEquatorProtocol::parseVersion("V 2001#", version) && version == 2001,
          "version parsed");

    check(InEquatorProtocol::isErrorResponse("ERR:jog_rate#"), "error response detected");
    check(!InEquatorProtocol::isErrorResponse("P 1;T true;Q 1;Y 100;M false#"),
          "normal response not an error");

    const std::string json =
        "{\"firmware\":2001,\"positionSteps\":-42,\"tracking\":true,"
        "\"stepsPerOutputRev\":307200,\"trackingRate\":3.5653,\"ppm\":15}";
    int64_t integer = 0;
    double number = 0.0;
    bool boolean = false;
    check(InEquatorProtocol::parseJsonInteger(json, "positionSteps", integer) && integer == -42,
          "json integer parsed");
    check(InEquatorProtocol::parseJsonInteger(json, "stepsPerOutputRev", integer) && integer == 307200,
          "json integer parsed 2");
    check(InEquatorProtocol::parseJsonNumber(json, "trackingRate", number) && number > 3.5,
          "json number parsed");
    check(InEquatorProtocol::parseJsonBoolean(json, "tracking", boolean) && boolean,
          "json boolean parsed");
    check(InEquatorProtocol::parseJsonInteger(json, "ppm", integer) && integer == 15,
          "json ppm parsed");
    check(!InEquatorProtocol::parseJsonBoolean(json, "missing", boolean),
          "json missing key rejected");

    if (failures == 0)
    {
        std::printf("All InEquator protocol tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d protocol test(s) failed.\n", failures);
    return 1;
}
