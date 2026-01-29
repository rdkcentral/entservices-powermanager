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

namespace hal {
namespace Thermal {
    class IPlatform {
        using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

    public:
        virtual ~IPlatform() {}

        virtual uint32_t GetTemperatureThresholds(float &tempHigh,float &tempCritical) const = 0;
        virtual uint32_t SetTemperatureThresholds(float tempHigh,float tempCritical) = 0;
        virtual uint32_t GetClockSpeed(uint32_t &speed) const = 0;
        virtual uint32_t SetClockSpeed(uint32_t speed) = 0;
        virtual uint32_t DetemineClockSpeeds(uint32_t &cpu_rate_Normal, uint32_t &cpu_rate_Scaled, uint32_t &cpu_rate_Minimal) = 0;
        virtual uint32_t GetTemperature(ThermalTemperature &curState, float &curTemperature, float &wifiTemperature) const = 0;

    };
}
}
