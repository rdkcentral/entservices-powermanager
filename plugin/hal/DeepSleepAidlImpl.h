/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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
#include <optional>
#include <vector>

#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "DeepSleep.h"
#include "UtilsLogging.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/StrongPointer.h>

#include <com/rdk/hal/deepsleep/IDeepSleep.h>
#include <com/rdk/hal/deepsleep/KeyCode.h>
#include <com/rdk/hal/deepsleep/WakeUpTrigger.h>

class DeepSleepAidlImpl : public hal::deepsleep::IPlatform {
    using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;

public:
    DeepSleepAidlImpl()
        : _deepsleep(nullptr)
        , _lastWakeupReason(WakeupReason::WAKEUP_REASON_UNKNOWN)
        , _lastWakeupKeyCode(0)
    {
        _enabledWakeupTriggers = {
            com::rdk::hal::deepsleep::WakeUpTrigger::RCU_IR,
            com::rdk::hal::deepsleep::WakeUpTrigger::RCU_BT,
            com::rdk::hal::deepsleep::WakeUpTrigger::RCU_RF4CE,
            com::rdk::hal::deepsleep::WakeUpTrigger::TIMER
        };

        try {
            android::ProcessState::self()->startThreadPool();
            android::sp<android::IBinder> binderSvc = android::defaultServiceManager()->getService(
                android::String16(com::rdk::hal::deepsleep::IDeepSleep::serviceName().c_str()));

            if (binderSvc == nullptr) {
                LOGERR("Unable to get AIDL DeepSleep service");
                return;
            }

            _deepsleep = android::interface_cast<com::rdk::hal::deepsleep::IDeepSleep>(binderSvc);
            if (_deepsleep) {
                LOGINFO("AIDL DeepSleep service acquired");
                android::binder::Status st = _deepsleep->getCapabilities(&_halCapabilities); 
                if (!st.isOk()) {
                    LOGERR("Failed to get DeepSleep capabilities");
                } else {

                    LOGINFO("DeepSleep capabilities acquired successfully. Vendor layer supported triggers: %s", [&]() {
                        std::string triggers;
                        for (const auto& trigger : _halCapabilities.supportedTriggers) {
                            if (!triggers.empty()) {
                                triggers += ", ";
                            }
                            triggers += WakeupTriggerName(trigger);
                        }
                        return triggers;
                    }().c_str());
                    
                    _enabledWakeupTriggers.erase(std::remove_if(_enabledWakeupTriggers.begin(), _enabledWakeupTriggers.end(),
                        [this](const auto& trigger) {
                            return std::find(_halCapabilities.supportedTriggers.begin(),
                                           _halCapabilities.supportedTriggers.end(),
                                           trigger) == _halCapabilities.supportedTriggers.end();
                        }), _enabledWakeupTriggers.end());
                }
            } else {
                LOGERR("Unable to create DeepSleep service binder");
            }
        } catch (...) {
            LOGERR("Exception caught while initializing AIDL DeepSleep service");
        }
    }

    const std::string& WakeupTriggerName(com::rdk::hal::deepsleep::WakeUpTrigger wakeupTrigger)
    {
        static std::string name;
        switch (wakeupTrigger) {
            case com::rdk::hal::deepsleep::WakeUpTrigger::RCU_IR: name = "RCU_IR"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::RCU_BT: name = "RCU_BT"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::RCU_RF4CE: name = "RCU_RF4CE"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::LAN: name = "LAN"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::WLAN: name = "WLAN"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::TIMER: name = "TIMER"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::FRONT_PANEL: name = "POWERKEY"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::CEC: name = "CEC"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::PRESENCE: name = "PRESENCEDETECTED"; break;
            case com::rdk::hal::deepsleep::WakeUpTrigger::VOICE: name = "VOICE"; break;
            default: name = "UNKNOWN"; break;
        }
        return name;
    }
 

    uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) override
    {
        if (_deepsleep == nullptr) {
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        std::vector<com::rdk::hal::deepsleep::WakeUpTrigger> triggers(_enabledWakeupTriggers);

        auto itTimerTrigger = std::find(triggers.begin(), triggers.end(), com::rdk::hal::deepsleep::WakeUpTrigger::TIMER); 
        if (itTimerTrigger != triggers.end()) {
            bool result = false;
            android::binder::Status st = _deepsleep->setWakeUpTimer(static_cast<int32_t>(deepSleepTime), &result);
            if (!st.isOk() || !result) {
                LOGERR("IDeepSleep::setWakeUpTimer failed. Removing TIMER trigger from wakeup triggers.");
                triggers.erase(itTimerTrigger);
            }
        }

        if (networkStandby) {
            if (std::find(triggers.begin(), triggers.end(), com::rdk::hal::deepsleep::WakeUpTrigger::LAN) == triggers.end()) {
                triggers.push_back(com::rdk::hal::deepsleep::WakeUpTrigger::LAN);
            }
            if (std::find(triggers.begin(), triggers.end(), com::rdk::hal::deepsleep::WakeUpTrigger::WLAN) == triggers.end()) {
                triggers.push_back(com::rdk::hal::deepsleep::WakeUpTrigger::WLAN);
            }
        }

        std::vector<com::rdk::hal::deepsleep::WakeUpTrigger> wokeUpByTriggers;
        std::optional<com::rdk::hal::deepsleep::KeyCode> keyCode;
        bool success = false;

        LOGINFO("Entering deep sleep for %u seconds with %zu wakeup triggers : %s", deepSleepTime, triggers.size(), [&]() {
            std::string triggerNames;
            for (const auto& trigger : triggers) {
                if (!triggerNames.empty()) {
                    triggerNames += ", ";
                }
                triggerNames += WakeupTriggerName(trigger);
            }
            return triggerNames;
        }().c_str());
        
        android::binder::Status st = _deepsleep->enterDeepSleep(triggers, &wokeUpByTriggers, &keyCode, &success);
        if (!st.isOk()) {
            LOGERR("IDeepSleep::enterDeepSleep failed");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if (!success) {
            LOGERR("IDeepSleep::enterDeepSleep reported failure");
            return WPEFramework::Core::ERROR_ABORTED;
        }

        _lastWakeupReason = ConvertToWakeupReason(wokeUpByTriggers);
        _lastWakeupKeyCode = keyCode.has_value() ? keyCode.value().keyCode : 0;

        // GPIO is not an explicit trigger in the current DeepSleep AIDL contract.
        isGPIOWakeup = (_lastWakeupReason == WakeupReason::WAKEUP_REASON_GPIO);

        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t DeepSleepWakeup(void) override
    {
        // AIDL enterDeepSleep call is blocking and returns on wakeup.
        // There is no explicit wakeup API, so this is a no-op.
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const override
    {
        wakeupReason = _lastWakeupReason;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetLastWakeupKeyCode(int& wakeupKeyCode) const override
    {
        wakeupKeyCode = _lastWakeupKeyCode;
        return WPEFramework::Core::ERROR_NONE;
    }

    com::rdk::hal::deepsleep::WakeUpTrigger wakeSrcTypeToWakeUpTrigger(WakeupSrcType wakeSrcType) {
        switch (wakeSrcType) {
        case WakeupSrcType::WAKEUP_SRC_VOICE:
            return com::rdk::hal::deepsleep::WakeUpTrigger::VOICE;
        case WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED:
            return com::rdk::hal::deepsleep::WakeUpTrigger::PRESENCE;
        case WakeupSrcType::WAKEUP_SRC_BLUETOOTH:
            return com::rdk::hal::deepsleep::WakeUpTrigger::RCU_BT;
        case WakeupSrcType::WAKEUP_SRC_WIFI:
            return com::rdk::hal::deepsleep::WakeUpTrigger::WLAN;
        case WakeupSrcType::WAKEUP_SRC_IR:
            return com::rdk::hal::deepsleep::WakeUpTrigger::RCU_IR;
        case WakeupSrcType::WAKEUP_SRC_POWERKEY:
            return com::rdk::hal::deepsleep::WakeUpTrigger::FRONT_PANEL;
        case WakeupSrcType::WAKEUP_SRC_TIMER:
            return com::rdk::hal::deepsleep::WakeUpTrigger::TIMER;
        case WakeupSrcType::WAKEUP_SRC_CEC:
            return com::rdk::hal::deepsleep::WakeUpTrigger::CEC;
        case WakeupSrcType::WAKEUP_SRC_LAN:
            return com::rdk::hal::deepsleep::WakeUpTrigger::LAN;
        case WakeupSrcType::WAKEUP_SRC_RF4CE:
            return com::rdk::hal::deepsleep::WakeUpTrigger::RCU_RF4CE;
        default:
            LOGERR("Unknown wakeup source type: %d", wakeSrcType);
            return com::rdk::hal::deepsleep::WakeUpTrigger::ERROR_UNKNOWN;
        }
    }

    uint32_t SetWakeupSrc(WakeupSrcType wakeSrcType, bool enabled) override
    {
        if (_deepsleep == nullptr) {
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        com::rdk::hal::deepsleep::WakeUpTrigger trigger = wakeSrcTypeToWakeUpTrigger(wakeSrcType);

        if (trigger == com::rdk::hal::deepsleep::WakeUpTrigger::ERROR_UNKNOWN) {
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        }

        if (enabled) {
            if (std::find(_enabledWakeupTriggers.begin(), _enabledWakeupTriggers.end(), trigger) == _enabledWakeupTriggers.end()) {
                if (std::find(_halCapabilities.supportedTriggers.begin(), _halCapabilities.supportedTriggers.end(), trigger) != _halCapabilities.supportedTriggers.end()) {
                    _enabledWakeupTriggers.push_back(trigger);
                    LOGINFO("Added wakeup trigger to enabled list: %d, total enabled triggers: %zu", static_cast<int>(trigger), _enabledWakeupTriggers.size());
                } else {
                    LOGERR("Wakeup trigger not supported: %d", static_cast<int>(trigger));
                    return WPEFramework::Core::ERROR_NONE; //TODO: Is it the right return value?
                }
            }
        } else {
            _enabledWakeupTriggers.erase(std::remove(_enabledWakeupTriggers.begin(), _enabledWakeupTriggers.end(), trigger), _enabledWakeupTriggers.end());
            LOGINFO("Removed wakeup trigger from enabled list: %d, total enabled triggers: %zu", static_cast<int>(trigger), _enabledWakeupTriggers.size());
        }
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetWakeupSrc(WakeupSrcType wakeSrcType, bool& enabled, bool& supported) {
        supported = false;
        enabled = false;

        if (_deepsleep == nullptr) {
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        com::rdk::hal::deepsleep::WakeUpTrigger trigger = wakeSrcTypeToWakeUpTrigger(wakeSrcType);
        if (trigger == com::rdk::hal::deepsleep::WakeUpTrigger::ERROR_UNKNOWN) {
            return WPEFramework::Core::ERROR_INVALID_PARAMETER;
        }

        LOGINFO("Checking wakeup source: %d", wakeSrcType);
        supported = std::find(_halCapabilities.supportedTriggers.begin(), _halCapabilities.supportedTriggers.end(), trigger) != _halCapabilities.supportedTriggers.end();
        enabled = std::find(_enabledWakeupTriggers.begin(), _enabledWakeupTriggers.end(), trigger) != _enabledWakeupTriggers.end();

        return supported ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    uint32_t CacheBootReason(const std::string& bootReason) override
    {
        _lastWakeupReason = BootReasonStrToWakeupReason(bootReason);
        return WPEFramework::Core::ERROR_NONE;
    }

private:
    WakeupReason TriggerToWakeupReason(com::rdk::hal::deepsleep::WakeUpTrigger trigger)
    {
        using Trigger = com::rdk::hal::deepsleep::WakeUpTrigger;

        switch (trigger) {
            case Trigger::RCU_IR:
                return WakeupReason::WAKEUP_REASON_IR;
            case Trigger::RCU_BT:
                return WakeupReason::WAKEUP_REASON_BLUETOOTH;
            case Trigger::RCU_RF4CE:
                return WakeupReason::WAKEUP_REASON_RF4CE;
            case Trigger::LAN:
                return WakeupReason::WAKEUP_REASON_LAN;
            case Trigger::WLAN:
                return WakeupReason::WAKEUP_REASON_WIFI;
            case Trigger::TIMER:
                return WakeupReason::WAKEUP_REASON_TIMER;
            case Trigger::FRONT_PANEL:
                return WakeupReason::WAKEUP_REASON_FRONTPANEL;
            case Trigger::CEC:
                return WakeupReason::WAKEUP_REASON_CEC;
            case Trigger::PRESENCE:
                return WakeupReason::WAKEUP_REASON_PRESENCE;
            case Trigger::VOICE:
                return WakeupReason::WAKEUP_REASON_VOICE;
            case Trigger::ERROR_UNKNOWN:
            default:
                return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }
    }

    WakeupReason ConvertToWakeupReason(const std::vector<com::rdk::hal::deepsleep::WakeUpTrigger>& wokeUpByTriggers)
    {
        if (wokeUpByTriggers.empty()) {
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }

        return TriggerToWakeupReason(wokeUpByTriggers.front());
    }

    WakeupReason BootReasonStrToWakeupReason(const std::string& bootReasonStr)
    {
        if (bootReasonStr == "ERROR_UNKNOWN") {
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        } else if (bootReasonStr == "WATCHDOG") {
            return WakeupReason::WAKEUP_REASON_WATCHDOG;
        } else if (bootReasonStr == "MAINTENANCE_REBOOT") {
            return WakeupReason::WAKEUP_REASON_SOFTWARERESET;
        } else if (bootReasonStr == "THERMAL_RESET") {
            return WakeupReason::WAKEUP_REASON_THERMALRESET;
        } else if (bootReasonStr == "WARM_RESET") {
            return WakeupReason::WAKEUP_REASON_WARMRESET;
        } else if (bootReasonStr == "COLD_BOOT") {
            return WakeupReason::WAKEUP_REASON_COLDBOOT;
        } else if (bootReasonStr == "STR_AUTH_FAILURE") {
            return WakeupReason::WAKEUP_REASON_STRAUTHFAIL;
        } else {
            return WakeupReason::WAKEUP_REASON_UNKNOWN;
        }
    }

private:
    android::sp<com::rdk::hal::deepsleep::IDeepSleep> _deepsleep;
    com::rdk::hal::deepsleep::Capabilities            _halCapabilities;
    WakeupReason _lastWakeupReason;
    int _lastWakeupKeyCode;
    std::vector<com::rdk::hal::deepsleep::WakeUpTrigger> _enabledWakeupTriggers;
};
