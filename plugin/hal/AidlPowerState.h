/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

#pragma once

#include <map>
#include <mutex>

#include <interfaces/IPowerManager.h>

namespace PowerManagerAidlState {

class Store {
public:
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
    using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

    static Store& Instance()
    {
        static Store instance;
        return instance;
    }

    bool IsSupported(const WakeupSrcType wakeupSrc) const
    {
        switch (wakeupSrc) {
        case WakeupSrcType::WAKEUP_SRC_VOICE:
        case WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED:
        case WakeupSrcType::WAKEUP_SRC_BLUETOOTH:
        case WakeupSrcType::WAKEUP_SRC_WIFI:
        case WakeupSrcType::WAKEUP_SRC_IR:
        case WakeupSrcType::WAKEUP_SRC_POWERKEY:
        case WakeupSrcType::WAKEUP_SRC_TIMER:
        case WakeupSrcType::WAKEUP_SRC_CEC:
        case WakeupSrcType::WAKEUP_SRC_LAN:
        case WakeupSrcType::WAKEUP_SRC_RF4CE:
            return true;
        default:
            return false;
        }
    }

    void SetPowerState(const PowerState state)
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        _powerState = state;
    }

    PowerState GetPowerState() const
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        return _powerState;
    }

    void SetWakeupSrc(const WakeupSrcType wakeupSrc, const bool enabled)
    {
        if (!IsSupported(wakeupSrc)) {
            return;
        }

        std::lock_guard<std::mutex> lock(_adminLock);
        _wakeupSources[wakeupSrc] = enabled;
    }

    bool GetWakeupSrc(const WakeupSrcType wakeupSrc, bool& enabled) const
    {
        if (!IsSupported(wakeupSrc)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_adminLock);
        const auto index = _wakeupSources.find(wakeupSrc);
        enabled = (index != _wakeupSources.end() ? index->second : false);
        return true;
    }

    std::map<WakeupSrcType, bool> GetWakeupSources() const
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        return _wakeupSources;
    }

private:
    Store()
        : _powerState(PowerState::POWER_STATE_ON)
    {
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_VOICE] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_PRESENCEDETECTED] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_BLUETOOTH] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_WIFI] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_IR] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_POWERKEY] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_TIMER] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_CEC] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_LAN] = false;
        _wakeupSources[WakeupSrcType::WAKEUP_SRC_RF4CE] = false;
    }

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

private:
    mutable std::mutex _adminLock;
    PowerState _powerState;
    std::map<WakeupSrcType, bool> _wakeupSources;
};

}
