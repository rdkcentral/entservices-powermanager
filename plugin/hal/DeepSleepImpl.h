#pragma once

#include "deepSleepMgr.h"

#include "DeepSleep.h"
#include "PowerUtils.h"
#include "UtilsLogging.h"

class DeepSleepImpl : public hal::deepsleep::IPlatform {
    using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
    using Utils = PowerUtils;

    // delete copy constructor and assignment operator
    DeepSleepImpl(const DeepSleepImpl&) = delete;
    DeepSleepImpl& operator=(const DeepSleepImpl&) = delete;

public:
    DeepSleepImpl()
    {
        PLAT_DS_INIT();
    }

    virtual ~DeepSleepImpl()
    {
        PLAT_DS_TERM();
    }

    WakeupReason conv(DeepSleep_WakeupReason_t reason) const
    {
        switch (reason) {
        case DEEPSLEEP_WAKEUPREASON_IR:
            return WakeupReason::WAKEUP_REASON_IR;
        case DEEPSLEEP_WAKEUPREASON_RCU_BT:
            return WakeupReason::WAKEUP_REASON_BLUETOOTH;
        case DEEPSLEEP_WAKEUPREASON_RCU_RF4CE:
            return WakeupReason::WAKEUP_REASON_RF4CE;
        case DEEPSLEEP_WAKEUPREASON_GPIO:
            return WakeupReason::WAKEUP_REASON_GPIO;
        case DEEPSLEEP_WAKEUPREASON_LAN:
            return WakeupReason::WAKEUP_REASON_LAN;
        case DEEPSLEEP_WAKEUPREASON_WLAN:
            return WakeupReason::WAKEUP_REASON_WIFI;
        case DEEPSLEEP_WAKEUPREASON_TIMER:
            return WakeupReason::WAKEUP_REASON_TIMER;
        case DEEPSLEEP_WAKEUPREASON_FRONT_PANEL:
            return WakeupReason::WAKEUP_REASON_FRONTPANEL;
        case DEEPSLEEP_WAKEUPREASON_WATCHDOG:
            return WakeupReason::WAKEUP_REASON_WATCHDOG;
        case DEEPSLEEP_WAKEUPREASON_SOFTWARE_RESET:
            return WakeupReason::WAKEUP_REASON_SOFTWARERESET;
        case DEEPSLEEP_WAKEUPREASON_THERMAL_RESET:
            return WakeupReason::WAKEUP_REASON_THERMALRESET;
        case DEEPSLEEP_WAKEUPREASON_WARM_RESET:
            return WakeupReason::WAKEUP_REASON_WARMRESET;
        case DEEPSLEEP_WAKEUPREASON_COLDBOOT:
            return WakeupReason::WAKEUP_REASON_COLDBOOT;
        case DEEPSLEEP_WAKEUPREASON_STR_AUTH_FAILURE:
            return WakeupReason::WAKEUP_REASON_STRAUTHFAIL;
        case DEEPSLEEP_WAKEUPREASON_CEC:
            return WakeupReason::WAKEUP_REASON_CEC;
        case DEEPSLEEP_WAKEUPREASON_PRESENCE:
            return WakeupReason::WAKEUP_REASON_PRESENCE;
        case DEEPSLEEP_WAKEUPREASON_VOICE:
            return WakeupReason::WAKEUP_REASON_VOICE;
        case DEEPSLEEP_WAKEUPREASON_UNKNOWN:
        default:
            LOGERR("Unknown wakeup reason: %d", reason);
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }
    }

    const char* str(DeepSleep_Return_Status_t status) const
    {
        switch (status) {
        case DEEPSLEEPMGR_SUCCESS:
            return "Success";
        case DEEPSLEEPMGR_INVALID_ARGUMENT:
            return "Invalid argument";
        case DEEPSLEEPMGR_ALREADY_INITIALIZED:
            return "Already initialized";
        case DEEPSLEEPMGR_NOT_INITIALIZED:
            return "Not initialized";
        case DEEPSLEEPMGR_INIT_FAILURE:
            return "Init failure";
        case DEEPSLEEPMGR_SET_FAILURE:
            return "Set failure";
        case DEEPSLEEPMGR_WAKEUP_FAILURE:
            return "Wakeup failure";
        case DEEPSLEEPMGR_TERM_FAILURE:
            return "Term failure";
        default:
            return "Unknown status";
        }
    }

    uint32_t conv(DeepSleep_Return_Status_t status) const
    {
        switch (status) {
        case DEEPSLEEPMGR_SUCCESS:
            return WPEFramework::Core::ERROR_NONE;
        case DEEPSLEEPMGR_INVALID_ARGUMENT:
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        case DEEPSLEEPMGR_ALREADY_INITIALIZED:
        case DEEPSLEEPMGR_NOT_INITIALIZED:
        case DEEPSLEEPMGR_INIT_FAILURE:
        case DEEPSLEEPMGR_SET_FAILURE:
        case DEEPSLEEPMGR_WAKEUP_FAILURE:
        case DEEPSLEEPMGR_TERM_FAILURE:
            return WPEFramework::Core::ERROR_GENERAL;
        default:
            LOGERR("Unknown status: %d", status);
            return WPEFramework::Core::ERROR_GENERAL;
        }
    }

    virtual uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) override
    {
        DeepSleep_Return_Status_t status = PLAT_DS_SetDeepSleep(deepSleepTime, &isGPIOWakeup, networkStandby);

        uint32_t retCode = conv(status);

        if (WPEFramework::Core::ERROR_NONE == retCode) {
            LOGINFO("Device wake-up from Deepsleep Mode! GPIOWakeup: %d, networkStandby: %d",
                isGPIOWakeup, networkStandby);
        } else {
            LOGERR("Failed to enter deep sleep mode: %s", str(status));
        }

        return retCode;
    }

    virtual uint32_t DeepSleepWakeup(void) override
    {
        DeepSleep_Return_Status_t status = PLAT_DS_DeepSleepWakeup();

        uint32_t retCode = conv(status);

        if (WPEFramework::Core::ERROR_NONE == retCode) {
            LOGINFO("Device resumed from Deep sleep Mode, status :%s", str(status));
        } else {
            LOGERR("Failed to resume from deep sleep mode: %s", str(status));
        }

        return retCode;
    }

    virtual uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const override
    {
        DeepSleep_WakeupReason_t reason = DEEPSLEEP_WAKEUPREASON_UNKNOWN;
        DeepSleep_Return_Status_t status = PLAT_DS_GetLastWakeupReason(&reason);

        uint32_t retCode = conv(status);

        if (WPEFramework::Core::ERROR_NONE == retCode) {
            wakeupReason = conv(reason);
        }

        LOGINFO("wakeupReason: %s, status:%s", Utils::str(wakeupReason), str(status));

        return retCode;
    }

    virtual uint32_t GetLastWakeupKeyCode(int& wakeupKeyCode) const override
    {
        DeepSleepMgr_WakeupKeyCode_Param_t param = { 0 };
        DeepSleep_Return_Status_t status = PLAT_DS_GetLastWakeupKeyCode(&param);

        uint32_t retCode = conv(status);

        if (WPEFramework::Core::ERROR_NONE == retCode) {
            wakeupKeyCode = param.keyCode;
        }

        LOGINFO("wakeupKeyCode: %d, status:%s", wakeupKeyCode, str(status));

        return retCode;
    }
};
