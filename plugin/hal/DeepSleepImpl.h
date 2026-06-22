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

#ifdef ENABLE_POWERMANAGER_AIDL
#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

// Android binder logging headers define LOG_PRI with a different signature
// than the syslog macro already pulled in by WPEFramework headers.
#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/deepsleep/IDeepSleep.h>
#include <com/rdk/hal/deepsleep/KeyCode.h>
#include <com/rdk/hal/deepsleep/WakeUpTrigger.h>
#include <utils/String16.h>

#include "hal/AidlPowerState.h"
#else
#include "deepSleepMgr.h"
#endif

#include "DeepSleep.h"
#include "PowerUtils.h"
#include "UtilsLogging.h"
#include "secure_wrapper.h" // for v_secure_system

class DeepSleepImpl : public hal::deepsleep::IPlatform {
    using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
    using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;
    using Utils = PowerUtils;

    // delete copy constructor and assignment operator
    DeepSleepImpl(const DeepSleepImpl&) = delete;
    DeepSleepImpl& operator=(const DeepSleepImpl&) = delete;

public:
#ifdef ENABLE_POWERMANAGER_AIDL
    using AidlService = com::rdk::hal::deepsleep::IDeepSleep;
    using AidlKeyCode = com::rdk::hal::deepsleep::KeyCode;
    using AidlTrigger = com::rdk::hal::deepsleep::WakeUpTrigger;

    DeepSleepImpl()
        : _aidlService(nullptr)
        , _lastWakeupTrigger(AidlTrigger::ERROR_UNKNOWN)
        , _lastWakeupKeyCode(-1)
    {
        LOGINFO("DeepSleep AIDL prototype backend enabled");
    }

    virtual ~DeepSleepImpl()
    {
        {
            std::lock_guard<std::mutex> lock(_aidlAdminLock);
            _aidlService = nullptr;
        }

        resetWakeupResult();
    }

    WakeupReason conv(AidlTrigger trigger) const
    {
        switch (trigger) {
        case AidlTrigger::RCU_IR:
            return WakeupReason::WAKEUP_REASON_IR;
        case AidlTrigger::RCU_BT:
            return WakeupReason::WAKEUP_REASON_BLUETOOTH;
        case AidlTrigger::RCU_RF4CE:
            return WakeupReason::WAKEUP_REASON_RF4CE;
        case AidlTrigger::LAN:
            return WakeupReason::WAKEUP_REASON_LAN;
        case AidlTrigger::WLAN:
            return WakeupReason::WAKEUP_REASON_WIFI;
        case AidlTrigger::TIMER:
            return WakeupReason::WAKEUP_REASON_TIMER;
        case AidlTrigger::FRONT_PANEL:
            return WakeupReason::WAKEUP_REASON_FRONTPANEL;
        case AidlTrigger::CEC:
            return WakeupReason::WAKEUP_REASON_CEC;
        case AidlTrigger::PRESENCE:
            return WakeupReason::WAKEUP_REASON_PRESENCE;
        case AidlTrigger::VOICE:
            return WakeupReason::WAKEUP_REASON_VOICE;
        case AidlTrigger::ERROR_UNKNOWN:
        default:
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }
    }

    AidlTrigger conv(WakeupSrcType wakeupSrc) const
    {
        switch (wakeupSrc) {
        case WakeupSrcType::WAKEUP_SRC_VOICE:
            return AidlTrigger::VOICE;
        case WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED:
            return AidlTrigger::PRESENCE;
        case WakeupSrcType::WAKEUP_SRC_BLUETOOTH:
            return AidlTrigger::RCU_BT;
        case WakeupSrcType::WAKEUP_SRC_WIFI:
            return AidlTrigger::WLAN;
        case WakeupSrcType::WAKEUP_SRC_IR:
            return AidlTrigger::RCU_IR;
        case WakeupSrcType::WAKEUP_SRC_TIMER:
            return AidlTrigger::TIMER;
        case WakeupSrcType::WAKEUP_SRC_CEC:
            return AidlTrigger::CEC;
        case WakeupSrcType::WAKEUP_SRC_LAN:
            return AidlTrigger::LAN;
        case WakeupSrcType::WAKEUP_SRC_RF4CE:
            return AidlTrigger::RCU_RF4CE;
        case WakeupSrcType::WAKEUP_SRC_POWERKEY:
            return AidlTrigger::FRONT_PANEL;
        default:
            return AidlTrigger::ERROR_UNKNOWN;
        }
    }

    bool isInteractiveWakeupTrigger(AidlTrigger trigger) const
    {
        switch (trigger) {
        case AidlTrigger::RCU_IR:
        case AidlTrigger::RCU_BT:
        case AidlTrigger::RCU_RF4CE:
        case AidlTrigger::FRONT_PANEL:
        case AidlTrigger::CEC:
        case AidlTrigger::PRESENCE:
        case AidlTrigger::VOICE:
            return true;
        default:
            return false;
        }
    }

    void addTrigger(std::vector<AidlTrigger>& triggers, AidlTrigger trigger) const
    {
        if (trigger == AidlTrigger::ERROR_UNKNOWN) {
            return;
        }

        if (std::find(triggers.begin(), triggers.end(), trigger) == triggers.end()) {
            triggers.push_back(trigger);
        }
    }

    android::sp<AidlService> getAidlService()
    {
        std::lock_guard<std::mutex> lock(_aidlAdminLock);

        if (_aidlService == nullptr) {
            android::ProcessState::self()->startThreadPool();

            android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();

            if (serviceManager != nullptr) {
                _aidlService = android::interface_cast<AidlService>(
                    serviceManager->getService(android::String16(AidlService::serviceName().c_str())));
                if (_aidlService == nullptr) {
                    LOGERR("Failed to get AIDL DeepSleep service");
                }
            } else {
                LOGERR("Failed to get Binder service manager for DeepSleep AIDL service");
            }
        }

        return _aidlService;
    }

    void resetWakeupResult()
    {
        std::lock_guard<std::mutex> lock(_wakeupStateLock);
        _lastWakeupTrigger = AidlTrigger::ERROR_UNKNOWN;
        _lastWakeupKeyCode = -1;
    }

    void cacheWakeupResult(const std::vector<AidlTrigger>& wokeUpByTriggers, const std::optional<AidlKeyCode>& keyCode)
    {
        std::lock_guard<std::mutex> lock(_wakeupStateLock);
        _lastWakeupTrigger = wokeUpByTriggers.empty() ? AidlTrigger::ERROR_UNKNOWN : wokeUpByTriggers.front();
        _lastWakeupKeyCode = (keyCode.has_value() ? keyCode->keyCode : -1);
    }

    virtual uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) override
    {
        int32_t ret = -1;
        resetWakeupResult();
        LOGINFO("Update the Deepsleep marker ");
        ret = v_secure_system("sh /lib/rdk/alertSystem.sh deepSleepMgrMain SYST_INFO_devicetoDS");
        if (ret != 0) {
            LOGERR("Failed to update the Deepsleep marker");
        }

        android::sp<AidlService> service = getAidlService();
        if (service == nullptr) {
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if (deepSleepTime > 0) {
            bool timerAccepted = false;
            android::binder::Status status = service->setWakeUpTimer(static_cast<int32_t>(deepSleepTime), &timerAccepted);

            if (!status.isOk()) {
                LOGERR("setWakeUpTimer failed: %s", status.toString8().c_str());
                return WPEFramework::Core::ERROR_GENERAL;
            }

            if (!timerAccepted) {
                LOGERR("AIDL setWakeUpTimer rejected timeout: %u", deepSleepTime);
                return WPEFramework::Core::ERROR_INVALID_PARAMETER;
            }
        }

        std::vector<AidlTrigger> triggersToWakeUpon;
        std::map<WakeupSrcType, bool> wakeupSources = PowerManagerAidlState::Store::Instance().GetWakeupSources();

        for (std::map<WakeupSrcType, bool>::const_iterator index = wakeupSources.begin(); index != wakeupSources.end(); ++index) {
            if (index->second) {
                addTrigger(triggersToWakeUpon, conv(index->first));
            }
        }

        if (networkStandby) {
            addTrigger(triggersToWakeUpon, AidlTrigger::LAN);
            addTrigger(triggersToWakeUpon, AidlTrigger::WLAN);
        }

        if (deepSleepTime > 0) {
            addTrigger(triggersToWakeUpon, AidlTrigger::TIMER);
        }

        if (triggersToWakeUpon.empty()) {
            LOGERR("No wakeup trigger configured for AIDL deep sleep entry");
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        }

        std::vector<AidlTrigger> wokeUpByTriggers;
        std::optional<AidlKeyCode> keyCode;
        bool enteredDeepSleep = false;

        android::binder::Status status = service->enterDeepSleep(triggersToWakeUpon, &wokeUpByTriggers, &keyCode, &enteredDeepSleep);

        if (!status.isOk()) {
            LOGERR("enterDeepSleep failed: %s", status.toString8().c_str());
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if (!enteredDeepSleep) {
            LOGERR("AIDL enterDeepSleep returned failure");
            return WPEFramework::Core::ERROR_ABORTED;
        }

        cacheWakeupResult(wokeUpByTriggers, keyCode);

        isGPIOWakeup = std::any_of(wokeUpByTriggers.begin(), wokeUpByTriggers.end(),
            [this](const AidlTrigger trigger) { return isInteractiveWakeupTrigger(trigger); });

        LOGINFO("Device wake-up from AIDL deep sleep. interactiveWakeup: %d, networkStandby: %d", isGPIOWakeup, networkStandby);

        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t DeepSleepWakeup(void) override
    {
        LOGINFO("AIDL DeepSleep service has no explicit wakeup API; preserving existing plugin flow with a safe no-op fallback");
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const override
    {
        std::lock_guard<std::mutex> lock(_wakeupStateLock);
        wakeupReason = conv(_lastWakeupTrigger);
        LOGINFO("wakeupReason: %s", Utils::str(wakeupReason));
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetLastWakeupKeyCode(int& wakeupKeyCode) const override
    {
        std::lock_guard<std::mutex> lock(_wakeupStateLock);
        wakeupKeyCode = _lastWakeupKeyCode;
        LOGINFO("wakeupKeyCode: %d", wakeupKeyCode);
        return WPEFramework::Core::ERROR_NONE;
    }

private:
    mutable std::mutex _aidlAdminLock;
    android::sp<AidlService> _aidlService;
    mutable std::mutex _wakeupStateLock;
    AidlTrigger _lastWakeupTrigger;
    int _lastWakeupKeyCode;
#else
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
        case DEEPSLEEPMGR_WAKEUP_FAILURE:
        case DEEPSLEEPMGR_TERM_FAILURE:
            return WPEFramework::Core::ERROR_GENERAL;
        case DEEPSLEEPMGR_SET_FAILURE:
            return WPEFramework::Core::ERROR_ABORTED;
        default:
            LOGERR("Unknown status: %d", status);
            return WPEFramework::Core::ERROR_GENERAL;
        }
    }

    virtual uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) override
    {
        int32_t ret = -1;
        LOGINFO("Update the Deepsleep marker ");
        ret = v_secure_system("sh /lib/rdk/alertSystem.sh deepSleepMgrMain SYST_INFO_devicetoDS");
        if(ret != 0) {
            LOGERR("Failed to update the Deepsleep marker");
        }
        
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
#endif
};

