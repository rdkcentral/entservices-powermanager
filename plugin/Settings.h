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

#include <chrono>
#include <cstdint>

#include <core/Time.h>
#include <interfaces/IPowerManager.h>
#include <limits>

class SettingsV1;

class Settings {
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
    using MonotonicClock = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<MonotonicClock>;

    static constexpr const char* kSettingsFilePath = "/opt/uimgr_settings.bin";
    static constexpr const char* kRamSettingsFilePath = "/tmp/uimgr_settings.bin";
    static constexpr const int kDeepSleepTimeoutSec = 8 * 60 * 60; // 8 hours

    // Common header across all settings versions
    typedef struct _header_t {
        uint32_t magic;
        uint32_t version;
        uint32_t length;
    } Header;

    // Note constructor is private to prevent instantiation without loading from file
    Settings()
        : _magic(0)
        , _version(std::numeric_limits<uint32_t>::max())
        , _powerState(PowerState::POWER_STATE_ON)
        , _powerStateBeforeReboot(PowerState::POWER_STATE_ON)
        , _deepSleepTimeout(kDeepSleepTimeoutSec)
        , _nwStandbyMode(false)
        , _lastUpdateTime(MonotonicClock::now())
    {
    }

public:
    enum Version {
        V1 = 1, // Current version
    };
    static Settings Load(const std::string& path = kSettingsFilePath);
    bool Save(const std::string& path = kSettingsFilePath);

    inline void setMagic(const uint32_t magic) { _magic = magic; }
    inline void setVersion(const uint32_t version) { _version = version; }
    inline void SetPowerState(const PowerState powerState)
    {
        if (_powerState != powerState) {
            _powerState = powerState;
            _lastUpdateTime = MonotonicClock::now();
        }
    }

    inline int64_t InactiveDuration() const
    {
        if (_powerState != PowerState::POWER_STATE_ON) {
            return std::chrono::duration_cast<std::chrono::seconds>(MonotonicClock::now() - _lastUpdateTime).count();
        }
        return 0;
    }

    inline void SetDeepSleepTimeout(uint32_t timeout) { _deepSleepTimeout = timeout; }
    inline void SetNwStandbyMode(bool mode) { _nwStandbyMode = mode; }

    inline uint32_t magic() const { return _magic; }
    inline uint32_t version() const { return _version; }
    inline PowerState powerState() const { return _powerState; }
    inline PowerState powerStateBeforeReboot() const { return _powerStateBeforeReboot; }
    inline uint32_t deepSleepTimeout() const { return _deepSleepTimeout; }
    inline bool nwStandbyMode() const { return _nwStandbyMode; }

    void printDetails(const std::string& prefix) const;

private:
    void initDefaults();
    bool save(int fd);

private:
    uint32_t _magic;
    uint32_t _version;

    PowerState _powerState;             // Current power state
    PowerState _powerStateBeforeReboot; // Power state before reboot, NOTE this field is not serialized to file
    uint32_t _deepSleepTimeout;         // Deep sleep timeout in seconds
    bool _nwStandbyMode;                // Network standby mode, true if enabled, false if disabled
    Timestamp _lastUpdateTime;          // Timestamp when power state was updated, used for calculating inactive duration

    friend class SettingsV1;
};
