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

#include <map>         // for map
#include <memory>      // for unique_ptr, operator!=
#include <stdint.h>    // for uint32_t
#include <string>      // for string
#include <type_traits> // for is_base_of
#include <utility>     // for forward, move

#include <core/Proxy.h>               // for ProxyType
#include <core/Trace.h>               // for ASSERT
#include <interfaces/IPowerManager.h> // for IPowerManager

#include "Settings.h"          // for Settings
#include "hal/DeepSleep.h"     // for IPlatform
#include "hal/DeepSleepImpl.h" // for DeepSleepImpl

// forward declarations
namespace WPEFramework {
namespace Core {
    struct IDispatch;
    struct IWorkerPool;
}
}

class DeepSleepWakeupSettings {
    // enum with Time Zone hours
    typedef enum _tzValue {
        tzHST11      = 11,
        tzHST11HDT   = 10,
        tzAKST       = 9,
        tzAKST09AKDT = 8,
        tzPST08      = 8,
        tzPST08PDT   = 8,
        tzMST07      = 7,
        tzMST07MDT   = 6,
        tzCST06      = 6,
        tzCST06CDT   = 5,
        tzEST05      = 5,
        tzEST05EDT   = 4
    } tzValue;

public:
    DeepSleepWakeupSettings(Settings& settings)
        : _settings(settings)
        , _isDeepSleepTimeoutSet(false)
    {
        initializeTimeZone();
    }

    void SetTimeout(uint32_t timeout)
    {
        _isDeepSleepTimeoutSet = true;
        _settings.SetDeepSleepTimeout(timeout);

        _settings.Save();
    }

    uint32_t timeout() const
    {
        if (_isDeepSleepTimeoutSet) {
            return _settings.deepSleepTimeout();
        }

        return getWakeupTime();
    }

private:
    uint32_t getTZDiffInSec() const;

    /*  Get TZ diff
        Have Record of All US TZ as of now.
    */
    void initializeTimeZone();

    /*  Get Wakeup timeout.
        Wakeup the box to do Maintenance related activities.
    */
    uint32_t getWakeupTime() const;

    /* Secure 32-bit random number from /dev/urandom. */
    uint32_t secure_random() const;

private:
    Settings& _settings;
    bool _isDeepSleepTimeoutSet;
    static std::map<std::string, tzValue> _maptzValues;
};

class DeepSleepController {

    using MonotonicClock = std::chrono::steady_clock;
    using Timestamp      = std::chrono::time_point<MonotonicClock>;
    using WakeupReason   = WPEFramework::Exchange::IPowerManager::WakeupReason;
    using PowerState     = WPEFramework::Exchange::IPowerManager::PowerState;
    using IPlatform      = hal::deepsleep::IPlatform;
    using DefaultImpl    = DeepSleepImpl;

    typedef enum {
        Failed     = -1, /*!< Deepsleep operation failed */
        NotStarted = 0,  /*!< Deepsleep operation not started*/
        InProgress,      /*!< Deepsleep operation in progress */
        Completed,       /*!< Deepsleep operation completed */
    } DeepSleepState;

public:
    ~DeepSleepController();
    class INotification {
    public:
        virtual ~INotification() = default;

        virtual void onDeepSleepTimerWakeup(const int wakeupTimeout) = 0;
        virtual void onDeepSleepUserWakeup(const bool userWakeup)    = 0;
        virtual void onDeepSleepFailed()                             = 0;
    };

private:
    DeepSleepController(INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        ASSERT(_platform != nullptr);
        return *_platform;
    }

public:
    template <typename IMPL = DefaultImpl, typename... Args>
    static DeepSleepController Create(INotification& parent, Args&&... args)
    {
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::deepsleep::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        return DeepSleepController(parent, std::move(impl));
    }

    uint32_t GetLastWakeupReason(WakeupReason& wakeupReason) const;

    uint32_t GetLastWakeupKeyCode(int& keyCode) const;

    // activate deep sleep mode
    uint32_t Activate(uint32_t timeOut, bool nwStandbyMode);

    // deactivate deep sleep mode
    uint32_t Deactivate();

    // perform maintenance reboot
    void MaintenanceReboot();

    inline bool IsDeepSleepInProgress() const
    {
        return (DeepSleepState::InProgress == _deepSleepState);
    }

    inline std::chrono::steady_clock::duration Elapsed()
    {
        if (_deepsleepStartTime.time_since_epoch() == std::chrono::steady_clock::duration::zero()) {
            return std::chrono::steady_clock::duration::zero();
        }
        return MonotonicClock::now() - _deepsleepStartTime;
    }

private:
    bool read_integer_conf(const char* file_name, uint32_t& val);
    void enterDeepSleepDelayed();
    void enterDeepSleepNow();
    void deepSleepTimerWakeup();
    void performActivate(uint32_t timeOut, bool nwStandbyMode);

private:
    INotification& _parent;
    WPEFramework::Core::IWorkerPool& _workerPool;
    Timestamp _deepsleepStartTime;
    std::shared_ptr<IPlatform> _platform;
    DeepSleepState _deepSleepState;
    uint32_t _deepSleepDelaySec;         // Duration to wait before entering deep sleep mode
    uint32_t _deepSleepWakeupTimeoutSec; // Total duration for which the system remains in deep sleep mode

    WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch> _deepSleepDelayJob; // Job to handle delay before entering deepsleep

    bool _nwStandbyMode; // Flag to indicate if network standby mode is enabled
};
