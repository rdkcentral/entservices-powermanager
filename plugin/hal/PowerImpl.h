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
#pragma once

#include <cstdint>
#include <unistd.h>

#include "plat_power.h"

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "Power.h"
#include "UtilsLogging.h"

class PowerImpl : public hal::power::IPlatform {
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
    using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

    const char* str(PWRMGR_WakeupSrcType_t src) const
    {
        switch (src) {
        case PWRMGR_WAKEUPSRC_VOICE:
            return "VOICE";
        case PWRMGR_WAKEUPSRC_PRESENCE_DETECTION:
            return "PRESENCE_DETECTION";
        case PWRMGR_WAKEUPSRC_BLUETOOTH:
            return "BLUETOOTH";
        case PWRMGR_WAKEUPSRC_WIFI:
            return "WIFI";
        case PWRMGR_WAKEUPSRC_IR:
            return "IR";
        case PWRMGR_WAKEUPSRC_POWER_KEY:
            return "POWER";
        case PWRMGR_WAKEUPSRC_TIMER:
            return "TIMER";
        case PWRMGR_WAKEUPSRC_CEC:
            return "CEC";
        case PWRMGR_WAKEUPSRC_LAN:
            return "LAN";
        case PWRMGR_WAKEUPSRC_MAX:
        default:
            return "UNKNOWN";
        }
    }

    PWRMGR_WakeupSrcType_t conv(WakeupSrcType wakeupSrc) const
    {
        switch (wakeupSrc) {
        case WakeupSrcType::WAKEUP_SRC_VOICE:
            return PWRMGR_WAKEUPSRC_VOICE;
        case WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED:
            return PWRMGR_WAKEUPSRC_PRESENCE_DETECTION;
        case WakeupSrcType::WAKEUP_SRC_BLUETOOTH:
            return PWRMGR_WAKEUPSRC_BLUETOOTH;
        case WakeupSrcType::WAKEUP_SRC_WIFI:
            return PWRMGR_WAKEUPSRC_WIFI;
        case WakeupSrcType::WAKEUP_SRC_IR:
            return PWRMGR_WAKEUPSRC_IR;
        case WakeupSrcType::WAKEUP_SRC_POWERKEY:
            return PWRMGR_WAKEUPSRC_POWER_KEY;
        case WakeupSrcType::WAKEUP_SRC_TIMER:
            return PWRMGR_WAKEUPSRC_TIMER;
        case WakeupSrcType::WAKEUP_SRC_CEC:
            return PWRMGR_WAKEUPSRC_CEC;
        case WakeupSrcType::WAKEUP_SRC_LAN:
            return PWRMGR_WAKEUPSRC_LAN;
        /*case WakeupSrcType::WAKEUP_SRC_RF4CE:*/
        /*    return PWRMGR_WAKEUPSRC_RF4CE;*/
        default:
            LOGERR("Unknown wakeup source: %d", wakeupSrc);
            return PWRMGR_WAKEUPSRC_MAX;
        }
    }

    PowerState conv(PWRMgr_PowerState_t state) const
    {
        switch (state) {
        case PWRMGR_POWERSTATE_OFF:
            return PowerState::POWER_STATE_OFF;
        case PWRMGR_POWERSTATE_ON:
            return PowerState::POWER_STATE_ON;
        case PWRMGR_POWERSTATE_STANDBY:
            return PowerState::POWER_STATE_STANDBY;
        case PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP:
            return PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP;
        case PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP:
            return PowerState::POWER_STATE_STANDBY_DEEP_SLEEP;
        case PWRMGR_POWERSTATE_MAX:
        default:
            return PowerState::POWER_STATE_UNKNOWN;
        }
    }

    PWRMgr_PowerState_t conv(PowerState state) const
    {
        switch (state) {
        case PowerState::POWER_STATE_OFF:
            return PWRMGR_POWERSTATE_OFF;
        case PowerState::POWER_STATE_ON:
            return PWRMGR_POWERSTATE_ON;
        case PowerState::POWER_STATE_STANDBY:
            return PWRMGR_POWERSTATE_STANDBY;
        case PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP:
            return PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP;
        case PowerState::POWER_STATE_STANDBY_DEEP_SLEEP:
            return PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP;
        default:
            return PWRMGR_POWERSTATE_MAX;
        }
    }

    const char* str(pmStatus_t result) const
    {
        if (result < 0) {
            return "Error";
        }

        switch (result) {
        case PWRMGR_SUCCESS:
            return "Success";
        case PWRMGR_INVALID_ARGUMENT:
            return "Invalid Argument";
        case PWRMGR_ALREADY_INITIALIZED:
            return "Already Initialized";
        case PWRMGR_NOT_INITIALIZED:
            return "Not Initialized";
        case PWRMGR_INIT_FAILURE:
            return "Init Failure";
        case PWRMGR_SET_FAILURE:
            return "Set Failure";
        case PWRMGR_GET_FAILURE:
            return "Get Failure";
        case PWRMGR_OPERATION_NOT_SUPPORTED:
            return "Operation Not Supported";
        case PWRMGR_TERM_FAILURE:
            return "Term Failure";
        case PWRMGR_MAX:
        default:
            return "Max";
        }
    }

    uint32_t conv(pmStatus_t result) const
    {
        if (result < 0) {
            return WPEFramework::Core::ERROR_GENERAL;
        }

        switch (result) {
        case PWRMGR_SUCCESS:
            return WPEFramework::Core::ERROR_NONE;
        case PWRMGR_INVALID_ARGUMENT:
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        case PWRMGR_NOT_INITIALIZED:
            return WPEFramework::Core::ERROR_ILLEGAL_STATE;
        case PWRMGR_SET_FAILURE:
            return WPEFramework::Core::ERROR_WRITE_ERROR;
        case PWRMGR_GET_FAILURE:
            return WPEFramework::Core::ERROR_READ_ERROR;
        case PWRMGR_ALREADY_INITIALIZED:
        case PWRMGR_OPERATION_NOT_SUPPORTED:
        case PWRMGR_INIT_FAILURE:
        case PWRMGR_TERM_FAILURE:
        case PWRMGR_MAX:
        default:
            return WPEFramework::Core::ERROR_GENERAL;
        }
    }

    const char* str(PWRMgr_PowerState_t state) const
    {
        switch (state) {
        case PWRMGR_POWERSTATE_OFF:
            return "OFF";
        case PWRMGR_POWERSTATE_ON:
            return "ON";
        case PWRMGR_POWERSTATE_STANDBY:
            return "STANDBY";
        case PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP:
            return "LIGHT_SLEEP";
        case PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP:
            return "DEEP_SLEEP";
        case PWRMGR_POWERSTATE_MAX:
        default:
            LOGERR("Unknown PowerState: %d", state);
            return "UNKNOWN";
        }
    }

    uint32_t SetWakeupSrc(WakeupSrcType wakeSrcType, bool enabled, bool& supported) override
    {
        pmStatus_t result = PLAT_API_SetWakeupSrc(conv(wakeSrcType), enabled);

        supported = !(result > 0);

        uint32_t retCode = conv(result);

        LOGINFO("wakeSrc: %s, enabled: %d, supported: %d, result: %s, retCode: %d",
            str(conv(wakeSrcType)), enabled, supported, str(result), retCode);

        return retCode;
    }

    uint32_t GetWakeupSrc(WakeupSrcType wakeSrcType, bool& enabled, bool& supported) const override
    {
        pmStatus_t result = PLAT_API_GetWakeupSrc(conv(wakeSrcType), &enabled);

        supported = !(result > 0);

        uint32_t retCode = conv(result);

        LOGINFO("wakeupSrc: %s, enabled: %d, supported: %d, result: %s, retCode: %d",
            str(conv(wakeSrcType)), enabled, supported, str(result), retCode);

        return retCode;
    }
private:
    bool m_isPlatformInitialized;

public:
    PowerImpl()
    {
        pmStatus_t result = PWRMGR_SUCCESS;
        unsigned int retryCount = 1;
        m_isPlatformInitialized = false;
        do {
            try{
                // Initialize the platform
                result = PLAT_INIT();
                if (PWRMGR_SUCCESS != result) {
                    LOGERR("Failed to initialize power manager:[%s]", str(result));
                } else {
                    m_isPlatformInitialized = true;
                    LOGINFO("PowerManager initialized successfully.");
                }
            }
            catch (const std::exception& e) {
                LOGERR("Exception occurred during PLAT_INIT:[%s]", e.what());
            }
            catch (...) {
                LOGERR("Unknown exception occurred during PLAT_INIT");
            }

            if (!m_isPlatformInitialized) {
                LOGINFO("Retrying power manager PLAT_INIT... (%d/35)", retryCount);
                usleep(100000); // Sleep for 100ms before retrying
            }
        }
        while ((!m_isPlatformInitialized) && (retryCount++ < 35));
    }

    virtual ~PowerImpl()
    {
        // Terminate the platform
        pmStatus_t result = PLAT_TERM();
        if (PWRMGR_SUCCESS != result) {
            LOGERR("Failed to terminate power manager: %s", str(result));
        }
    }

    virtual uint32_t SetPowerState(PowerState newState) override
    {
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

        // arg validation to be done in the caller
        PWRMgr_PowerState_t state = conv(newState);

        pmStatus_t result = PLAT_API_SetPowerState(state);

        if (PWRMGR_SUCCESS == result) {
            retCode = WPEFramework::Core::ERROR_NONE;
        }

        return retCode;
    }

    virtual uint32_t GetPowerState(PowerState& curState) override
    {
        PWRMgr_PowerState_t state = PWRMGR_POWERSTATE_MAX;
        uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;
        pmStatus_t result = PLAT_API_GetPowerState(&state);

        if (PWRMGR_SUCCESS == result) {
            curState = conv(state);
            retCode = WPEFramework::Core::ERROR_NONE;
        }

        LOGINFO("PowerState: %s, result: %s", str(state), str(result));

        return retCode;
    }
};
