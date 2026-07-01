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
#include <vector>

// Workaround: Linux syslog.h defines LOG_PRI(p) as a single-arg macro.
// Android binder/log headers define LOG_PRI(priority, tag, ...) as multi-arg.
// If syslog.h was included earlier (via WPEFramework), undef the conflict.
#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/IPCThreadState.h>
#include <utils/StrongPointer.h>

#include <com/rdk/hal/sensor/thermal/IThermalSensor.h>
#include <com/rdk/hal/sensor/thermal/IThermalEventListener.h>
#include <com/rdk/hal/sensor/thermal/BnThermalEventListener.h>
#include <com/rdk/hal/sensor/thermal/State.h>
#include <com/rdk/hal/sensor/thermal/ActionEvent.h>
#include <com/rdk/hal/sensor/thermal/TemperatureReading.h>

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "Thermal.h"
#include "UtilsLogging.h"

class ThermalAidlImpl : public hal::Thermal::IPlatform {
    using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

    // ── AIDL type aliases ──
    using AidlState = ::com::rdk::hal::sensor::thermal::State;
    using AidlIThermalSensor = ::com::rdk::hal::sensor::thermal::IThermalSensor;
    using AidlIThermalEventListener = ::com::rdk::hal::sensor::thermal::IThermalEventListener;
    using AidlBnThermalEventListener = ::com::rdk::hal::sensor::thermal::BnThermalEventListener;
    using AidlActionEvent = ::com::rdk::hal::sensor::thermal::ActionEvent;
    using AidlTemperatureReading = ::com::rdk::hal::sensor::thermal::TemperatureReading;

    // delete copy
    ThermalAidlImpl(const ThermalAidlImpl&) = delete;
    ThermalAidlImpl& operator=(const ThermalAidlImpl&) = delete;

    // ── Conversion helpers ──
    ThermalTemperature conv(AidlState state) const
    {
        switch (state) {
        case AidlState::NORMAL:
            return ThermalTemperature::THERMAL_TEMPERATURE_NORMAL;
        case AidlState::CRITICAL_TEMPERATURE_EXCEEDED:
            return ThermalTemperature::THERMAL_TEMPERATURE_HIGH;
        case AidlState::CRITICAL_SHUTDOWN_IMMINENT:
            return ThermalTemperature::THERMAL_TEMPERATURE_CRITICAL;
        case AidlState::CRITICAL_TEMPERATURE_RECOVERED:
            return ThermalTemperature::THERMAL_TEMPERATURE_NORMAL;
        default:
            return ThermalTemperature::THERMAL_TEMPERATURE_UNKNOWN;
        }
    }

    // ── Event listener (BnThermalEventListener) ──
    class ThermalEventListener : public AidlBnThermalEventListener {
    public:
        ThermalEventListener(ThermalAidlImpl& parent) : _parent(parent) {}

        ::android::binder::Status onThermalStateChange(
            const AidlActionEvent& event) override
        {
            std::lock_guard<std::mutex> lock(_parent._cacheMutex);
            _parent._cachedState = event.state;
            if (event.temperatureReading.has_value()) {
                _parent._cachedSocTemp = static_cast<float>(event.temperatureReading->temperatureCelsius);
            }
            LOGINFO("Thermal state change event: state=%d, temp=%0.1f",
                static_cast<int>(event.state), _parent._cachedSocTemp);
            return ::android::binder::Status::ok();
        }

        int32_t getInterfaceVersion() override { return AidlIThermalEventListener::VERSION; }
        std::string getInterfaceHash() override { return "notfrozen"; }

    private:
        ThermalAidlImpl& _parent;
    };

    // ── AIDL service access ──
    android::sp<AidlIThermalSensor> getService()
    {
        std::lock_guard<std::mutex> lock(_serviceMutex);
        if (_service == nullptr) {
            android::sp<android::IServiceManager> sm = android::defaultServiceManager();
            if (sm == nullptr) {
                LOGERR("Failed to get Android service manager");
                return nullptr;
            }
            android::sp<android::IBinder> binder =
                sm->getService(android::String16(AidlIThermalSensor::serviceName().c_str()));
            if (binder == nullptr) {
                LOGERR("Failed to get IThermalSensor AIDL service");
                return nullptr;
            }
            _service = android::interface_cast<AidlIThermalSensor>(binder);
            if (_service == nullptr) {
                LOGERR("Failed to cast binder to IThermalSensor interface");
            }
        }
        return _service;
    }

    // ── Member state ──
    mutable std::mutex _serviceMutex;
    android::sp<AidlIThermalSensor> _service;
    android::sp<ThermalEventListener> _listener;

    mutable std::mutex _cacheMutex;
    AidlState _cachedState;
    float _cachedSocTemp;
    float _cachedWifiTemp;

public:
    ThermalAidlImpl()
        : _service(nullptr)
        , _listener(nullptr)
        , _cachedState(AidlState::NORMAL)
        , _cachedSocTemp(0.0f)
        , _cachedWifiTemp(0.0f)
    {
        android::ProcessState::self()->startThreadPool();

        auto svc = getService();
        if (svc != nullptr) {
            // Register event listener for async thermal state changes
            _listener = new ThermalEventListener(*this);
            bool registered = false;
            android::binder::Status bs = svc->registerEventListener(_listener, &registered);
            if (bs.isOk() && registered) {
                LOGINFO("ThermalAidlImpl: event listener registered");
            } else {
                LOGERR("ThermalAidlImpl: failed to register event listener: %s",
                    bs.isOk() ? "already registered" : bs.toString8().c_str());
            }
        } else {
            LOGERR("IThermalSensor AIDL service not available at construction time");
        }
    }

    virtual ~ThermalAidlImpl()
    {
        auto svc = getService();
        if (svc != nullptr && _listener != nullptr) {
            bool unregistered = false;
            svc->unregisterEventListener(_listener, &unregistered);
        }
        std::lock_guard<std::mutex> lock(_serviceMutex);
        _service = nullptr;
        _listener = nullptr;
    }

    virtual uint32_t GetTemperatureThresholds(float& tempHigh, float& tempCritical) const override
    {
        // The AIDL IThermalSensor interface does not expose threshold configuration.
        // The thermal policy (thresholds) is entirely managed by the HAL service.
        // Return NOT_SUPPORTED so the caller knows this is not available via AIDL.
        LOGINFO("GetTemperatureThresholds: not available via AIDL thermal interface (policy is HAL-managed)");
        return WPEFramework::Core::ERROR_NOT_SUPPORTED;
    }

    virtual uint32_t SetTemperatureThresholds(float tempHigh, float tempCritical) override
    {
        // The AIDL IThermalSensor interface does not expose threshold configuration.
        LOGINFO("SetTemperatureThresholds: not available via AIDL thermal interface (policy is HAL-managed)");
        return WPEFramework::Core::ERROR_NOT_SUPPORTED;
    }

    virtual uint32_t GetClockSpeed(uint32_t& speed) const override
    {
        // The AIDL thermal interface does not manage clock speeds.
        // Clock speed management is handled internally by the HAL thermal policy.
        LOGINFO("GetClockSpeed: not available via AIDL thermal interface (managed by HAL policy)");
        speed = 0;
        return WPEFramework::Core::ERROR_NOT_SUPPORTED;
    }

    virtual uint32_t SetClockSpeed(uint32_t speed) override
    {
        // The AIDL thermal interface does not manage clock speeds.
        LOGINFO("SetClockSpeed: not available via AIDL thermal interface (managed by HAL policy)");
        return WPEFramework::Core::ERROR_NOT_SUPPORTED;
    }

    virtual uint32_t DetemineClockSpeeds(uint32_t& cpu_rate_Normal, uint32_t& cpu_rate_Scaled, uint32_t& cpu_rate_Minimal) override
    {
        // The AIDL thermal interface does not expose clock speed discovery.
        LOGINFO("DetemineClockSpeeds: not available via AIDL thermal interface (managed by HAL policy)");
        cpu_rate_Normal = 0;
        cpu_rate_Scaled = 0;
        cpu_rate_Minimal = 0;
        return WPEFramework::Core::ERROR_NOT_SUPPORTED;
    }

    virtual uint32_t GetTemperature(ThermalTemperature& curState, float& curTemperature, float& wifiTemperature) const override
    {
        ThermalAidlImpl* self = const_cast<ThermalAidlImpl*>(this);
        auto svc = self->getService();
        if (svc == nullptr) {
            LOGERR("IThermalSensor AIDL service unavailable");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        // Query current thermal state
        AidlState aidlState;
        android::binder::Status bs = svc->getCurrentThermalState(&aidlState);
        if (!bs.isOk()) {
            LOGERR("AIDL getCurrentThermalState binder error: %s", bs.toString8().c_str());
            return WPEFramework::Core::ERROR_GENERAL;
        }

        curState = conv(aidlState);

        // Query current temperature readings
        std::vector<AidlTemperatureReading> readings;
        bs = svc->getCurrentTemperatures(&readings);
        if (!bs.isOk()) {
            LOGERR("AIDL getCurrentTemperatures binder error: %s", bs.toString8().c_str());
            // State was retrieved successfully, use cached temps
            std::lock_guard<std::mutex> lock(_cacheMutex);
            curTemperature = _cachedSocTemp;
            wifiTemperature = _cachedWifiTemp;
            return WPEFramework::Core::ERROR_NONE;
        }

        // Extract SoC and WiFi temperatures from readings
        float socTemp = 0.0f;
        float wifiTemp = 0.0f;
        for (const auto& reading : readings) {
            std::string name(android::String8(reading.sensorName).c_str());
            std::string loc(android::String8(reading.location).c_str());
            if (loc == "CPU" || name.find("SoC") != std::string::npos || name.find("soc") != std::string::npos) {
                socTemp = static_cast<float>(reading.temperatureCelsius);
            } else if (name.find("WiFi") != std::string::npos || name.find("wifi") != std::string::npos ||
                       name.find("WLAN") != std::string::npos) {
                wifiTemp = static_cast<float>(reading.temperatureCelsius);
            }
        }

        // If no specific sensor found, use first reading as SoC temp
        if (socTemp == 0.0f && !readings.empty()) {
            socTemp = static_cast<float>(readings[0].temperatureCelsius);
        }

        curTemperature = socTemp;
        wifiTemperature = wifiTemp;

        // Update cache
        {
            std::lock_guard<std::mutex> lock(_cacheMutex);
            _cachedState = aidlState;
            _cachedSocTemp = socTemp;
            _cachedWifiTemp = wifiTemp;
        }

        LOGINFO("SoC Temperature: %d, Wifi Temperature: %d",
            static_cast<int>(curTemperature), static_cast<int>(wifiTemperature));
        return WPEFramework::Core::ERROR_NONE;
    }
};
