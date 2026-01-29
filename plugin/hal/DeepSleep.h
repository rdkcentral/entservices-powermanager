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

#include <stdint.h> // for uint32_t


#include <core/Portability.h>         // for ErrorCodes
#include <interfaces/IPowerManager.h> // for IPowerManager


namespace hal {
namespace deepsleep {

    class IPlatform {
        using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;

    public:
        virtual ~IPlatform() {}

        virtual uint32_t SetDeepSleep(uint32_t deepSleepTime, bool& isGPIOWakeup, bool networkStandby) = 0;
        virtual uint32_t DeepSleepWakeup(void) = 0;
        virtual uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const = 0;
        virtual uint32_t GetLastWakeupKeyCode(int& wakeupKeyCode) const = 0;
    };
}
}
