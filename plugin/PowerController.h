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

#include <cstdint>     // for uint32_t
#include <memory>      // for unique_ptr, default_delete
#include <string>      // for basic_string, string
#include <type_traits> // for is_base_of
#include <utility>     // for forward

#include <core/Portability.h>         // for string, ErrorCodes
#include <core/Proxy.h>               // for ProxyType
#include <core/Trace.h>               // for ASSERT
#include <interfaces/IPowerManager.h> // for IPowerManager

#include "DeepSleepController.h" // for DeepSleepController (ptr only)
#include "RebootController.h"    // for RebootController
#include "Settings.h"            // for Settings
#include "hal/PowerImpl.h"       // for IPlatform, PowerImpl

namespace WPEFramework {
namespace Core {
    struct IDispatch;
    struct IWorkerPool;
}
}

class PowerController {
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
    using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;
    using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
    using IPlatform = hal::power::IPlatform;
    using DefaultImpl = PowerImpl;

    PowerController(DeepSleepController& deepSleep, std::unique_ptr<IPlatform> platform);

    inline IPlatform& platform()
    {
        ASSERT(nullptr != _platform);
        return *_platform;
    }

    inline IPlatform& platform() const
    {
        return *_platform;
    }

    void init();

public:
    uint32_t SetPowerState(const int keyCode, const PowerState powerState, const std::string& reason);
    uint32_t ActivateDeepSleep();

    inline uint32_t GetPowerState(PowerState& currentState, PowerState& prevState) const
    {
        currentState = _settings.powerState();
        prevState = _lastKnownPowerState;
        return WPEFramework::Core::ERROR_NONE;
    }

    inline uint32_t GetPowerStateBeforeReboot(PowerState& state)
    {
        state = _settings.powerStateBeforeReboot();
        return WPEFramework::Core::ERROR_NONE;
    }

    uint32_t SetNetworkStandbyMode(const bool standbyMode);
    uint32_t GetNetworkStandbyMode(bool& standbyMode) const;
    uint32_t SetWakeupSourceConfig(const std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig>& configs);
    uint32_t GetWakeupSourceConfig(std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig>& configs) const;
    uint32_t GetWakeupSourceConfig(int& powerMode, int& srcType, int& config) const;
    uint32_t Reboot(const string& requestor, const string& reasonCustom, const string& reasonOther);
    uint32_t SetDeepSleepTimer(const int timeOut);

    template <typename IMPL = DefaultImpl, typename... Args>
    static PowerController Create(DeepSleepController& deepSleep, Args&&... args)
    {
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::power::IPlatform");
        IMPL* api = new IMPL(std::forward<Args>(args)...);
        return PowerController(deepSleep, std::unique_ptr<IPlatform>(api));
    }

    // Avoid copying this obj
    PowerController(const PowerController&) = delete;            // copy constructor
    PowerController& operator=(const PowerController&) = delete; // copy assignment operator
    PowerController& operator=(PowerController&&) = delete;      // move assignment operator

    PowerController(PowerController&&) = default;

private:
    const std::string m_settingsFile = "/opt/uimgr_settings.bin";
    std::unique_ptr<IPlatform> _platform;
    PowerState _powerStateBeforeReboot;
    PowerState _lastKnownPowerState;
    Settings _settings;
    DeepSleepWakeupSettings _deepSleepWakeupSettings;
    WPEFramework::Core::IWorkerPool& _workerPool;

    // keep this last
    DeepSleepController& _deepSleep;
#ifdef OFFLINE_MAINT_REBOOT
    RebootController _rebootController;
#endif
};
