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
#include <mutex>
#include <optional>
#include <vector>

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <utils/StrongPointer.h>

#include <com/rdk/hal/deepsleep/IDeepSleep.h>
#include <com/rdk/hal/deepsleep/WakeUpTrigger.h>
#include <com/rdk/hal/deepsleep/KeyCode.h>
#include <com/rdk/hal/deepsleep/Capabilities.h>

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "DeepSleep.h"
#include "PowerUtils.h"
#include "UtilsLogging.h"
#include "secure_wrapper.h" // for v_secure_system

class DeepSleepAidlImpl : public hal::deepsleep::IPlatform {
    using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
    using Utils = PowerUtils;

    // ── AIDL type aliases ──
    using AidlWakeUpTrigger = ::com::rdk::hal::deepsleep::WakeUpTrigger;
    using AidlKeyCode = ::com::rdk::hal::deepsleep::KeyCode;
    using AidlCapabilities = ::com::rdk::hal::deepsleep::Capabilities;
    using AidlIDeepSleep = ::com::rdk::hal::deepsleep::IDeepSleep;

    // delete copy
    DeepSleepAidlImpl(const DeepSleepAidlImpl&) = delete;
    DeepSleepAidlImpl& operator=(const DeepSleepAidlImpl&) = delete;

    // ── Conversion helpers ──

    // Map AIDL WakeUpTrigger to plugin WakeupReason
    WakeupReason convTriggerToReason(AidlWakeUpTrigger trigger) const
    {
        switch (trigger) {
        case AidlWakeUpTrigger::RCU_IR:
            return WakeupReason::WAKEUP_REASON_IR;
        case AidlWakeUpTrigger::RCU_BT:
            return WakeupReason::WAKEUP_REASON_BLUETOOTH;
        case AidlWakeUpTrigger::RCU_RF4CE:
            return WakeupReason::WAKEUP_REASON_RF4CE;
        case AidlWakeUpTrigger::LAN:
            return WakeupReason::WAKEUP_REASON_LAN;
        case AidlWakeUpTrigger::WLAN:
            return WakeupReason::WAKEUP_REASON_WIFI;
        case AidlWakeUpTrigger::TIMER:
            return WakeupReason::WAKEUP_REASON_TIMER;
        case AidlWakeUpTrigger::FRONT_PANEL:
            return WakeupReason::WAKEUP_REASON_FRONTPANEL;
        case AidlWakeUpTrigger::CEC:
            return WakeupReason::WAKEUP_REASON_CEC;
        case AidlWakeUpTrigger::PRESENCE:
            return WakeupReason::WAKEUP_REASON_PRESENCE;
        case AidlWakeUpTrigger::VOICE:
            return WakeupReason::WAKEUP_REASON_VOICE;
        case AidlWakeUpTrigger::ERROR_UNKNOWN:
        default:
            LOGERR("Unknown AIDL WakeUpTrigger: %d", static_cast<int>(trigger));
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }
    }

    // Build the set of triggers to pass into enterDeepSleep based on networkStandby
    std::vector<AidlWakeUpTrigger> buildTriggers(bool networkStandby) const
    {
        // Always wake on timer and RCU sources
        std::vector<AidlWakeUpTrigger> triggers = {
            AidlWakeUpTrigger::TIMER,
            AidlWakeUpTrigger::RCU_IR,
            AidlWakeUpTrigger::RCU_BT,
            AidlWakeUpTrigger::RCU_RF4CE,
            AidlWakeUpTrigger::CEC,
            AidlWakeUpTrigger::FRONT_PANEL,
        };

        if (networkStandby) {
            triggers.push_back(AidlWakeUpTrigger::LAN);
            triggers.push_back(AidlWakeUpTrigger::WLAN);
        }

        return triggers;
    }

    // ── AIDL service access ──
    android::sp<AidlIDeepSleep> getService()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_service == nullptr) {
            android::sp<android::IServiceManager> sm = android::defaultServiceManager();
            if (sm == nullptr) {
                LOGERR("Failed to get Android service manager");
                return nullptr;
            }
            android::sp<android::IBinder> binder =
                sm->getService(android::String16(AidlIDeepSleep::serviceName().c_str()));
            if (binder == nullptr) {
                LOGERR("Failed to get IDeepSleep AIDL service");
                return nullptr;
            }
            _service = android::interface_cast<AidlIDeepSleep>(binder);
            if (_service == nullptr) {
                LOGERR("Failed to cast binder to IDeepSleep interface");
            }
        }
        return _service;
    }

    // ── Member state ──
    mutable std::mutex _mutex;
    android::sp<AidlIDeepSleep> _service;

    // Cached results from the last enterDeepSleep call
    mutable std::mutex _resultMutex;
    std::vector<AidlWakeUpTrigger> _lastWokeUpByTriggers;
    std::optional<AidlKeyCode> _lastKeyCode;

public:
    DeepSleepAidlImpl()
        : _service(nullptr)
    {
        // Start binder thread pool (safe to call multiple times)
        android::ProcessState::self()->startThreadPool();

        auto svc = getService();
        if (svc == nullptr) {
            LOGERR("DeepSleep AIDL service not available at construction time");
        } else {
            LOGINFO("DeepSleep AIDL service connected successfully.");
        }
    }

    virtual ~DeepSleepAidlImpl()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _service = nullptr;
    }

    virtual uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) override
    {
        auto svc = getService();
        if (svc == nullptr) {
            LOGERR("IDeepSleep AIDL service unavailable");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        LOGINFO("Update the Deepsleep marker");
        int32_t ret = v_secure_system("sh /lib/rdk/alertSystem.sh deepSleepMgrMain SYST_INFO_devicetoDS");
        if (ret != 0) {
            LOGERR("Failed to update the Deepsleep marker");
        }

        // Set the wake-up timer before entering deep sleep
        bool timerSet = false;
        android::binder::Status bs = svc->setWakeUpTimer(static_cast<int32_t>(deepSleepTime), &timerSet);
        if (!bs.isOk()) {
            LOGERR("AIDL setWakeUpTimer binder error: %s", bs.toString8().c_str());
            return WPEFramework::Core::ERROR_GENERAL;
        }
        if (!timerSet) {
            LOGERR("setWakeUpTimer returned false (invalid period: %u)", deepSleepTime);
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        }

        // Build trigger list and enter deep sleep (blocks until wake)
        std::vector<AidlWakeUpTrigger> triggers = buildTriggers(networkStandby);
        std::vector<AidlWakeUpTrigger> wokeUpByTriggers;
        std::optional<AidlKeyCode> keyCode;
        bool success = false;

        bs = svc->enterDeepSleep(triggers, &wokeUpByTriggers, &keyCode, &success);
        if (!bs.isOk()) {
            LOGERR("AIDL enterDeepSleep binder error: %s", bs.toString8().c_str());
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if (!success) {
            LOGERR("enterDeepSleep returned false - deep sleep could not be entered");
            return WPEFramework::Core::ERROR_ABORTED;
        }

        // Cache results for GetLastWakeupReason/GetLastWakeupKeyCode queries
        {
            std::lock_guard<std::mutex> lock(_resultMutex);
            _lastWokeUpByTriggers = wokeUpByTriggers;
            _lastKeyCode = keyCode;
        }

        // Determine if GPIO wakeup (FRONT_PANEL is the closest equivalent)
        isGPIOWakeup = false;
        for (const auto& trigger : wokeUpByTriggers) {
            if (trigger == AidlWakeUpTrigger::FRONT_PANEL) {
                isGPIOWakeup = true;
                break;
            }
        }

        LOGINFO("Device wake-up from Deepsleep Mode! GPIOWakeup: %d, networkStandby: %d",
            isGPIOWakeup, networkStandby);

        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t DeepSleepWakeup(void) override
    {
        // The AIDL enterDeepSleep() blocks until wakeup occurs and then returns.
        // There is no separate "wakeup" call in the AIDL interface.
        // When enterDeepSleep() returns, the device has already woken up.
        LOGINFO("DeepSleepWakeup: no-op in AIDL model (enterDeepSleep returns on wakeup)");
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const override
    {
        std::lock_guard<std::mutex> lock(_resultMutex);

        if (_lastWokeUpByTriggers.empty()) {
            wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
            LOGINFO("No wakeup triggers recorded yet");
            return WPEFramework::Core::ERROR_NONE;
        }

        // Return the first trigger as the primary wakeup reason
        wakeupReason = convTriggerToReason(_lastWokeUpByTriggers[0]);
        LOGINFO("wakeupReason: %s", Utils::str(wakeupReason));
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetLastWakeupKeyCode(int& wakeupKeyCode) const override
    {
        std::lock_guard<std::mutex> lock(_resultMutex);

        if (_lastKeyCode.has_value()) {
            wakeupKeyCode = _lastKeyCode->keyCode;
        } else {
            wakeupKeyCode = 0;
        }

        LOGINFO("wakeupKeyCode: %d", wakeupKeyCode);
        return WPEFramework::Core::ERROR_NONE;
    }
};
