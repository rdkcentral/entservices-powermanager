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

#include <map>

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/StrongPointer.h>
#include <binder/Status.h>

#include <com/rdk/hal/boot/IBoot.h>
#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "Power.h"
#include "UtilsLogging.h"

using namespace com::rdk::hal::boot;

class PowerAidlImpl : public hal::power::IPlatform {
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
    using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

public:
    PowerAidlImpl()
        : _boot(nullptr)
        , _powerState(PowerState::POWER_STATE_ON)
    {
        _wakeupSources = {
            {WakeupSrcType::WAKEUP_SRC_IR, true},
            {WakeupSrcType::WAKEUP_SRC_BLUETOOTH, true},
            {WakeupSrcType::WAKEUP_SRC_RF4CE, true},
            {WakeupSrcType::WAKEUP_SRC_TIMER, true},
        };

        try {
            android::ProcessState::self()->startThreadPool();
            android::sp<android::IBinder> binderSvc = android::defaultServiceManager()->getService(
                android::String16(IBoot::serviceName().c_str()));

            if (binderSvc == nullptr) {
                LOGERR("Unable to get AIDL Boot service");
                return;
            }

            _boot = android::interface_cast<IBoot>(binderSvc);

            if (_boot) {
                LOGINFO("AIDL Boot service acquired");
            } else {
                LOGERR("Unable to cast Boot service binder");
            }
        } catch (...) {
            LOGERR("Exception caught while initializing AIDL Boot service");
        }
    }

    bool IsAvailable() const
    {
        return _boot != nullptr;
    }

    uint32_t SetPowerState(PowerState newState) override
    {
        _powerState = newState;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetPowerState(PowerState& state) override
    {
        state = _powerState;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetWakeupSrc(WakeupSrcType wakeSrcType, bool enabled, bool& supported) override
    {
        supported = true;
        _wakeupSources[wakeSrcType] = enabled;
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t GetWakeupSrc(WakeupSrcType wakeSrcType, bool& enabled, bool& supported) const override
    {
        supported = true;
        auto it = _wakeupSources.find(wakeSrcType);
        enabled = (it != _wakeupSources.end()) ? it->second : false;
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetBootReason(std::string& bootReasonStr) const  override
    {
        if (_boot == nullptr) {
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        android::binder::Status status;
        BootReason bootReason;
        status = _boot->getBootReason(&bootReason);
        if (status.isOk()) {
            bootReasonStr = com::rdk::hal::boot::toString(bootReason);
            return WPEFramework::Core::ERROR_NONE;
        }
        LOGINFO("Failed to get boot reason from AIDL Boot service: %s", status.toString8().string());
        return WPEFramework::Core::ERROR_GENERAL;
    }

    virtual uint32_t Reboot(const std::string& requestor, const std::string& reasonCustom, const std::string& reasonOther) override
    {
        if (_boot == nullptr) {
            return WPEFramework::Core::ERROR_UNAVAILABLE;
        }

        android::binder::Status status;
        std::string rebootReason = requestor + " : " + reasonCustom;
        /*TODO:
            Fix it: requestor is right source for deducing ResetType, 
            but we need to get the exact strings of requestor to do proper mapping to ResetType.
            For now, using reasonCustom as a workaround.
        */
        ResetType resetType = stringToResetType(reasonCustom);
        LOGINFO("Calling IBoot::reboot - resetType: %d, rebootReason: %s.", static_cast<int>(resetType), rebootReason.c_str());
        status = _boot->reboot(resetType, android::String16(rebootReason.c_str()));

        if (status.isOk()) {
            return WPEFramework::Core::ERROR_NONE;
        }
        return WPEFramework::Core::ERROR_GENERAL;
    }

private:
    inline ResetType stringToResetType(const std::string& resetTypeStr) const
    {
        if (resetTypeStr == "MAINTENANCE_REBOOT") {
            return ResetType::MAINTENANCE_REBOOT;
        } else {
            return ResetType::SOFTWARE_REBOOT;
        }
    }

    inline BootReason stringToBootReason(const std::string& rebootReasonStr) {
        if (rebootReasonStr == "ERROR_UNKNOWN") {
            return BootReason::ERROR_UNKNOWN;
        } else if (rebootReasonStr == "WATCHDOG") {
            return BootReason::WATCHDOG;
        } else if (rebootReasonStr == "MAINTENANCE_REBOOT") {
            return BootReason::MAINTENANCE_REBOOT;
        } else if (rebootReasonStr == "THERMAL_RESET") {
            return BootReason::THERMAL_RESET;
        } else if (rebootReasonStr == "WARM_RESET") {
            return BootReason::WARM_RESET;
        } else if (rebootReasonStr == "COLD_BOOT") {
            return BootReason::COLD_BOOT;
        } else if (rebootReasonStr == "STR_AUTH_FAILURE") {
            return BootReason::STR_AUTH_FAILURE;
        } else {
            return static_cast<BootReason>(std::stoi(rebootReasonStr));
        }
    }

private:
    android::sp<IBoot> _boot;
    PowerState _powerState;
    std::map<WakeupSrcType, bool> _wakeupSources;
};
