/*
    INDI driver for the InEquator single-axis RA tracker
    Copyright (C) 2026 InEquator contributors
    SPDX-License-Identifier: LGPL-2.1-or-later

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.
*/

#pragma once

#include "inequator_protocol.h"
#include "inditelescope.h"

#include <cstddef>
#include <cstdint>

class InEquatorTracker : public INDI::Telescope
{
    public:
        InEquatorTracker();
        ~InEquatorTracker() override = default;

        const char *getDefaultName() override;
        bool initProperties() override;
        bool updateProperties() override;
        bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
        bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;

    protected:
        bool Handshake() override;
        bool ReadScopeStatus() override;
        bool Abort() override;
        bool SetTrackEnabled(bool enabled) override;
        bool SetTrackMode(uint8_t mode) override;
        bool SetSlewRate(int index) override;
        bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override;

    private:
        enum
        {
            FIRMWARE_VERSION,
            CONTROLLER_MODEL
        };

        bool sendCommand(const char *command, char *response, std::size_t responseSize, bool silent = false);
        bool readMotionStatus(InEquatorProtocol::MotionStatus &status, bool silent = false);
        void applyMotionStatus(const InEquatorProtocol::MotionStatus &status);
        void updateTrackStateSwitches(bool tracking);
        void setMovementSwitch(int dirIndex, bool active);

        INDI::PropertyNumber PositionNP { 1 };
        INDI::PropertyNumber PpmNP { 1 };
        INDI::PropertyText FirmwareTP { 2 };

        int32_t m_LastPosition { 0 };
        int m_FirmwareVersion { 0 };

        static constexpr std::size_t RESPONSE_SIZE = 1024;
        static constexpr uint8_t IO_TIMEOUT_SECONDS = 3;
        static constexpr uint8_t SLEW_RATE_COUNT = 5;
        static constexpr int32_t SLEW_RATE_MULTIPLIERS[SLEW_RATE_COUNT] =
        {
            10000,    // 1x
            80000,    // 8x
            320000,   // 32x
            1280000,  // 128x
            2560000   // 256x
        };
};
