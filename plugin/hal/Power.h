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

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

namespace hal {
namespace power {

    // Power APIs
    class IPlatform {
        using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
        using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

    public:
        virtual ~IPlatform() {}
        virtual uint32_t SetPowerState(PowerState newState) = 0;
        virtual uint32_t GetPowerState(PowerState& state) = 0;
        virtual uint32_t SetWakeupSrc(WakeupSrcType wakeSrcType, bool enabled, bool& supported) = 0;
        virtual uint32_t GetWakeupSrc(WakeupSrcType wakeSrcType, bool& enabled, bool& supported) const = 0;
    };
} // namespace power
} // namespace hal

