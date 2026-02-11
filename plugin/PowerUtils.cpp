/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PowerUtils.h"
#include "UtilsLogging.h"

using PowerState    = WPEFramework::Exchange::IPowerManager::PowerState;
using WakeupReason  = WPEFramework::Exchange::IPowerManager::WakeupReason;
using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

const char* PowerUtils::str(const WakeupReason reason)
{
    switch (reason) {
    case WakeupReason::WAKEUP_REASON_IR:
        return "IR";
    case WakeupReason::WAKEUP_REASON_BLUETOOTH:
        return "BLUETOOTH";
    case WakeupReason::WAKEUP_REASON_RF4CE:
        return "RF4CE";
    case WakeupReason::WAKEUP_REASON_GPIO:
        return "GPIO";
    case WakeupReason::WAKEUP_REASON_LAN:
        return "LAN";
    case WakeupReason::WAKEUP_REASON_WIFI:
        return "WIFI";
    case WakeupReason::WAKEUP_REASON_TIMER:
        return "TIMER";
    case WakeupReason::WAKEUP_REASON_FRONTPANEL:
        return "FRONTPANEL";
    case WakeupReason::WAKEUP_REASON_WATCHDOG:
        return "WATCHDOG";
    case WakeupReason::WAKEUP_REASON_SOFTWARERESET:
        return "SOFTWARERESET";
    case WakeupReason::WAKEUP_REASON_THERMALRESET:
        return "THERMALRESET";
    case WakeupReason::WAKEUP_REASON_WARMRESET:
        return "WARMRESET";
    case WakeupReason::WAKEUP_REASON_COLDBOOT:
        return "COLDBOOT";
    case WakeupReason::WAKEUP_REASON_STRAUTHFAIL:
        return "STR_AUTH_FAIL";
    case WakeupReason::WAKEUP_REASON_CEC:
        return "CEC";
    case WakeupReason::WAKEUP_REASON_PRESENCE:
        return "PRESENCE";
    case WakeupReason::WAKEUP_REASON_VOICE:
        return "VOICE";
    default:
        LOGERR("Unknown wakeup reason: %d", reason);
        return "UNKNOWN";
    }
}

const char* PowerUtils::str(const PowerState state)
{
    switch (state) {
    case PowerState::POWER_STATE_UNKNOWN:
        return "UNKNOWN";
    case PowerState::POWER_STATE_OFF:
        return "OFF";
    case PowerState::POWER_STATE_STANDBY:
        return "STANDBY";
    case PowerState::POWER_STATE_ON:
        return "ON";
    case PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP:
        return "LIGHT_SLEEP";
    case PowerState::POWER_STATE_STANDBY_DEEP_SLEEP:
        return "DEEP_SLEEP";
    default:
        LOGERR("Unknown power state: %d", state);
        return "UNKNOWN";
    }
}

// IMPORTANT: Keep this in sync with the text alias provided for
//            WakeupSrcType in IPowerManager.h interface file
const char* PowerUtils::str(WakeupSrcType wakeupSrc)
{
    switch (wakeupSrc) {
    case WakeupSrcType::WAKEUP_SRC_VOICE:
        return "VOICE";
    case WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED:
        return "PRESENCEDETECTED";
    case WakeupSrcType::WAKEUP_SRC_BLUETOOTH:
        return "BLUETOOTH";
    case WakeupSrcType::WAKEUP_SRC_WIFI:
        return "WIFI";
    case WakeupSrcType::WAKEUP_SRC_IR:
        return "IR";
    case WakeupSrcType::WAKEUP_SRC_POWERKEY:
        return "POWERKEY";
    case WakeupSrcType::WAKEUP_SRC_TIMER:
        return "TIMER";
    case WakeupSrcType::WAKEUP_SRC_CEC:
        return "CEC";
    case WakeupSrcType::WAKEUP_SRC_LAN:
        return "LAN";
    case WakeupSrcType::WAKEUP_SRC_RF4CE:
        return "RF4CE";
    default:
        LOGERR("Unknown wakeup source: %d", wakeupSrc);
        return "UNKNOWN";
    }
}

// IMPORTANT: Keep this conversion in sync with the text alias provided for
//            WakeupSrcType in IPowerManager.h interface file
WakeupSrcType PowerUtils::conv(const std::string& wakeupSrc)
{
    std::string src = wakeupSrc;
    std::transform(src.begin(), src.end(), src.begin(), ::toupper);

    if (src == "VOICE") {
        return WakeupSrcType::WAKEUP_SRC_VOICE;
    } else if (src == "PRESENCEDETECTED") {
        return WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED;
    } else if (src == "BLUETOOTH") {
        return WakeupSrcType::WAKEUP_SRC_BLUETOOTH;
    } else if (src == "WIFI") {
        return WakeupSrcType::WAKEUP_SRC_WIFI;
    } else if (src == "IR") {
        return WakeupSrcType::WAKEUP_SRC_IR;
    } else if (src == "POWERKEY") {
        return WakeupSrcType::WAKEUP_SRC_POWERKEY;
    } else if (src == "TIMER") {
        return WakeupSrcType::WAKEUP_SRC_TIMER;
    } else if (src == "CEC") {
        return WakeupSrcType::WAKEUP_SRC_CEC;
    } else if (src == "LAN") {
        return WakeupSrcType::WAKEUP_SRC_LAN;
    } else if (src == "RF4CE") {
        return WakeupSrcType::WAKEUP_SRC_RF4CE;
    } else {
        LOGERR("Unknown wakeup source string: %s", wakeupSrc.c_str());
        return WakeupSrcType::WAKEUP_SRC_UNKNOWN;
    }
}
