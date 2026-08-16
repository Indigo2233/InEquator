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

#include "inequator.h"

#include "connectionplugins/connectionserial.h"
#include "connectionplugins/connectiontcp.h"
#include "indicom.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <termios.h>
#include <unistd.h>

static std::unique_ptr<InEquatorTracker> tracker(new InEquatorTracker());

InEquatorTracker::InEquatorTracker()
{
    setVersion(1, 0);
    setTelescopeConnection(CONNECTION_SERIAL | CONNECTION_TCP);
    SetTelescopeCapability(TELESCOPE_CAN_ABORT | TELESCOPE_CAN_CONTROL_TRACK, SLEW_RATE_COUNT);
}

const char *InEquatorTracker::getDefaultName()
{
    return "InEquator RA Tracker";
}

bool InEquatorTracker::initProperties()
{
    INDI::Telescope::initProperties();

    // Relabel the slew rate switches created by SetTelescopeCapability.
    const char *labels[SLEW_RATE_COUNT] = { "1x", "8x", "32x", "128x", "256x" };
    for (uint8_t i = 0; i < SLEW_RATE_COUNT; i++)
        SlewRateSP[i].setLabel(labels[i]);
    SlewRateSP[0].setState(ISS_ON);

    // Manual W/E motion buttons (single RA axis, no N/S).
    MovementWESP[DIRECTION_WEST].fill("MOTION_WEST", "West", ISS_OFF);
    MovementWESP[DIRECTION_EAST].fill("MOTION_EAST", "East", ISS_OFF);
    MovementWESP.fill(getDeviceName(), "TELESCOPE_MOTION_WE", "Motion W/E",
                      MOTION_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    PositionNP[0].fill("TRACKER_POSITION", "Steps", "%.f", -2e9, 2e9, 1.0, 0.0);
    PositionNP.fill(getDeviceName(), "TRACKER_POSITION", "Position",
                    MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    PpmNP[0].fill("TRACKER_PPM", "ppm", "%.f", -10000.0, 10000.0, 1.0, 0.0);
    PpmNP.fill(getDeviceName(), "TRACKER_PPM", "Rate Correction",
               MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    FirmwareTP[FIRMWARE_VERSION].fill("VERSION", "Firmware", "Unknown");
    FirmwareTP[CONTROLLER_MODEL].fill("MODEL", "Controller", "InEquator RA Tracker");
    FirmwareTP.fill(getDeviceName(), "TRACKER_FIRMWARE", "Firmware",
                    MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    serialConnection->setDefaultBaudRate(Connection::Serial::B_9600);
    tcpConnection->setDefaultHost("192.168.4.1");
    tcpConnection->setDefaultPort(4030);

    setDefaultPollingPeriod(500);
    return true;
}

bool InEquatorTracker::updateProperties()
{
    INDI::Telescope::updateProperties();

    if (isConnected())
    {
        defineProperty(SlewRateSP);
        defineProperty(MovementWESP);
        defineProperty(PositionNP);
        defineProperty(PpmNP);
        defineProperty(FirmwareTP);

        char response[RESPONSE_SIZE] = { 0 };
        if (sendCommand("I#", response, sizeof(response)))
        {
            const std::string json(response);
            int64_t integerValue = 0;

            if (InEquatorProtocol::parseJsonInteger(json, "positionSteps", integerValue))
            {
                PositionNP[0].setValue(integerValue);
                m_LastPosition = static_cast<int32_t>(integerValue);
            }
            if (InEquatorProtocol::parseJsonInteger(json, "ppm", integerValue))
                PpmNP[0].setValue(integerValue);
            if (InEquatorProtocol::parseJsonInteger(json, "jogRate", integerValue))
            {
                SlewRateSP.reset();
                for (uint8_t i = 0; i < SLEW_RATE_COUNT; i++)
                    SlewRateSP[i].setState(
                        integerValue == SLEW_RATE_MULTIPLIERS[i] ? ISS_ON : ISS_OFF);
            }
        }

        PositionNP.apply();
        PpmNP.apply();
        FirmwareTP.apply();
        SlewRateSP.apply();
        MovementWESP.apply();
        LOG_INFO("InEquator tracker is ready.");
    }
    else
    {
        deleteProperty(SlewRateSP);
        deleteProperty(MovementWESP);
        deleteProperty(PositionNP);
        deleteProperty(PpmNP);
        deleteProperty(FirmwareTP);
    }

    return true;
}

bool InEquatorTracker::Handshake()
{
    if (getActiveConnection()->type() == Connection::Interface::CONNECTION_SERIAL)
    {
        // ESP8266 USB bridges reset the controller when the serial port opens.
        usleep(2200000);
        tcflush(PortFD, TCIOFLUSH);
    }

    char response[RESPONSE_SIZE] = { 0 };
    if (!sendCommand("#", response, sizeof(response), true) ||
            !InEquatorProtocol::isSupportedIdentity(response))
    {
        LOG_ERROR("InEquator tracker identification failed. Check the selected port, power, and firmware.");
        return false;
    }

    if (!sendCommand("V#", response, sizeof(response), true) ||
            !InEquatorProtocol::parseVersion(response, m_FirmwareVersion))
    {
        LOG_ERROR("InEquator tracker firmware version query failed.");
        return false;
    }

    char versionText[16] = { 0 };
    std::snprintf(versionText, sizeof(versionText), "%d", m_FirmwareVersion);
    FirmwareTP[FIRMWARE_VERSION].setText(versionText);
    FirmwareTP[CONTROLLER_MODEL].setText("InEquator RA Tracker");

    LOGF_INFO("Connected to InEquator tracker, firmware %d.", m_FirmwareVersion);
    return true;
}

bool InEquatorTracker::ReadScopeStatus()
{
    InEquatorProtocol::MotionStatus status;
    if (!readMotionStatus(status))
        return false;

    applyMotionStatus(status);
    return true;
}

bool InEquatorTracker::Abort()
{
    char response[RESPONSE_SIZE] = { 0 };
    if (!sendCommand("S#", response, sizeof(response)))
        return false;

    MovementWESP.reset();
    MovementWESP[DIRECTION_WEST].setState(ISS_OFF);
    MovementWESP[DIRECTION_EAST].setState(ISS_OFF);
    MovementWESP.apply();
    LOG_INFO("Tracker motion aborted.");
    return true;
}

bool InEquatorTracker::SetTrackEnabled(bool enabled)
{
    char response[RESPONSE_SIZE] = { 0 };
    if (!sendCommand(enabled ? "B 1#" : "B 0#", response, sizeof(response)))
        return false;

    TrackState = enabled ? SCOPE_TRACKING : SCOPE_IDLE;
    updateTrackStateSwitches(enabled);
    LOGF_INFO("Tracking %s.", enabled ? "enabled" : "disabled");
    return true;
}

bool InEquatorTracker::SetTrackMode(uint8_t mode)
{
    if (mode != TRACK_SIDEREAL)
    {
        LOG_WARN("InEquator tracker only supports sidereal tracking.");
        return false;
    }
    return true;
}

bool InEquatorTracker::SetSlewRate(int index)
{
    if (index < 0 || index >= SLEW_RATE_COUNT)
    {
        LOGF_ERROR("Slew rate index %d out of range.", index);
        return false;
    }

    char command[32] = { 0 };
    std::snprintf(command, sizeof(command), "Q %d#", SLEW_RATE_MULTIPLIERS[index]);
    char response[RESPONSE_SIZE] = { 0 };
    if (!sendCommand(command, response, sizeof(response)))
        return false;

    LOGF_INFO("Slew rate set to %s.", SlewRateSP[index].getLabel());
    return true;
}

bool InEquatorTracker::MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command)
{
    char response[RESPONSE_SIZE] = { 0 };
    if (command == MOTION_START)
    {
        const char *jogCommand = (dir == DIRECTION_WEST) ? "M-#" : "M+#";
        if (!sendCommand(jogCommand, response, sizeof(response)))
            return false;

        setMovementSwitch(dir, true);
        LOGF_INFO("Jogging %s.", dir == DIRECTION_WEST ? "west" : "east");
        return true;
    }

    if (command == MOTION_STOP)
    {
        if (!sendCommand("S#", response, sizeof(response)))
            return false;
        setMovementSwitch(dir, false);
        LOGF_INFO("Jog stopped.");
        return true;
    }

    return false;
}

bool InEquatorTracker::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (PpmNP.isNameMatch(name))
        {
            PpmNP.update(values, names, n);
            const double ppm = PpmNP[0].getValue();
            if (ppm < -10000 || ppm > 10000)
            {
                PpmNP.setState(IPS_ALERT);
                PpmNP.apply();
                LOG_ERROR("PPM correction must be between -10000 and 10000.");
                return false;
            }

            char command[32] = { 0 };
            std::snprintf(command, sizeof(command), "D %.0f#", ppm);
            char response[RESPONSE_SIZE] = { 0 };
            if (sendCommand(command, response, sizeof(response)))
            {
                PpmNP.setState(IPS_OK);
                LOGF_INFO("PPM correction set to %.0f.", ppm);
            }
            else
            {
                PpmNP.setState(IPS_ALERT);
            }
            PpmNP.apply();
            return true;
        }
    }

    return INDI::Telescope::ISNewNumber(dev, name, values, names, n);
}

bool InEquatorTracker::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (MovementWESP.isNameMatch(name))
        {
            const int preIndex = MovementWESP.findOnSwitchIndex();
            MovementWESP.update(states, names, n);

            const int index = MovementWESP.findOnSwitchIndex();
            if (index < 0)
            {
                // All switches off: stop any jog.
                char response[RESPONSE_SIZE] = { 0 };
                if (sendCommand("S#", response, sizeof(response)))
                    MovementWESP.setState(IPS_IDLE);
                else
                    MovementWESP.setState(IPS_ALERT);
                MovementWESP.apply();
                return true;
            }

            const INDI_DIR_WE dir = (index == DIRECTION_WEST) ? DIRECTION_WEST : DIRECTION_EAST;
            const bool success = MoveWE(dir, MOTION_START);
            MovementWESP.setState(success ? IPS_BUSY : IPS_ALERT);
            if (!success && preIndex >= 0)
                MovementWESP[preIndex].setState(ISS_ON);
            MovementWESP.apply();
            return true;
        }

        if (SlewRateSP.isNameMatch(name))
        {
            const int preIndex = SlewRateSP.findOnSwitchIndex();
            SlewRateSP.update(states, names, n);
            const int index = SlewRateSP.findOnSwitchIndex();
            if (index == preIndex)
            {
                SlewRateSP.apply();
                return true;
            }

            if (SetSlewRate(index))
                SlewRateSP.setState(IPS_OK);
            else
            {
                SlewRateSP.reset();
                SlewRateSP[preIndex].setState(ISS_ON);
                SlewRateSP.setState(IPS_ALERT);
            }
            SlewRateSP.apply();
            return true;
        }
    }

    return INDI::Telescope::ISNewSwitch(dev, name, states, names, n);
}

bool InEquatorTracker::sendCommand(const char *command, char *response, std::size_t responseSize, bool silent)
{
    if (response == nullptr || responseSize < 2)
        return false;

    if (getActiveConnection()->type() == Connection::Interface::CONNECTION_SERIAL)
        tcflush(PortFD, TCIFLUSH);

    LOGF_DEBUG("CMD <%s>", command);

    int bytesWritten = 0;
    int rc = tty_write_string(PortFD, command, &bytesWritten);
    if (rc != TTY_OK)
    {
        if (!silent)
        {
            char errorMessage[MAXRBUF] = { 0 };
            tty_error_msg(rc, errorMessage, MAXRBUF);
            LOGF_ERROR("InEquator tracker write failed: %s.", errorMessage);
        }
        return false;
    }

    int bytesRead = 0;
    rc = tty_nread_section(PortFD, response, static_cast<int>(responseSize - 1), '#',
                           IO_TIMEOUT_SECONDS, &bytesRead);
    if (rc != TTY_OK)
    {
        if (!silent)
        {
            char errorMessage[MAXRBUF] = { 0 };
            tty_error_msg(rc, errorMessage, MAXRBUF);
            LOGF_ERROR("InEquator tracker read failed: %s.", errorMessage);
        }
        return false;
    }

    response[bytesRead] = '\0';
    LOGF_DEBUG("RES <%s>", response);

    if (InEquatorProtocol::isErrorResponse(response))
    {
        if (!silent)
            LOGF_ERROR("InEquator tracker rejected command <%s> with response <%s>.", command, response);
        return false;
    }

    return true;
}

bool InEquatorTracker::readMotionStatus(InEquatorProtocol::MotionStatus &status, bool silent)
{
    char response[RESPONSE_SIZE] = { 0 };
    if (!sendCommand("G#", response, sizeof(response), silent))
        return false;

    if (!InEquatorProtocol::parseMotionStatus(response, status))
    {
        if (!silent)
            LOGF_ERROR("Invalid InEquator tracker status response <%s>.", response);
        return false;
    }

    return true;
}

void InEquatorTracker::applyMotionStatus(const InEquatorProtocol::MotionStatus &status)
{
    if (status.position != m_LastPosition)
    {
        PositionNP[0].setValue(status.position);
        PositionNP.apply();
        m_LastPosition = status.position;
    }

    if (status.tracking)
    {
        if (TrackState != SCOPE_TRACKING)
        {
            TrackState = SCOPE_TRACKING;
            updateTrackStateSwitches(true);
        }
    }
    else
    {
        if (TrackState != SCOPE_IDLE)
        {
            TrackState = SCOPE_IDLE;
            updateTrackStateSwitches(false);
        }
    }

    if (status.moving)
    {
        if (MovementWESP.getState() != IPS_BUSY)
        {
            MovementWESP.setState(IPS_BUSY);
            MovementWESP.apply();
        }
    }
    else
    {
        if (MovementWESP.getState() != IPS_IDLE)
        {
            MovementWESP.reset();
            MovementWESP.setState(IPS_IDLE);
            MovementWESP.apply();
        }
    }
}

void InEquatorTracker::updateTrackStateSwitches(bool tracking)
{
    TrackStateSP[TRACK_ON].setState(tracking ? ISS_ON : ISS_OFF);
    TrackStateSP[TRACK_OFF].setState(tracking ? ISS_OFF : ISS_ON);
    TrackStateSP.setState(tracking ? IPS_OK : IPS_IDLE);
    TrackStateSP.apply();
}

void InEquatorTracker::setMovementSwitch(int dirIndex, bool active)
{
    MovementWESP.reset();
    MovementWESP[dirIndex].setState(active ? ISS_ON : ISS_OFF);
    MovementWESP.setState(active ? IPS_BUSY : IPS_IDLE);
    MovementWESP.apply();
}
