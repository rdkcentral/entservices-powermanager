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

#include <errno.h>  // for errno
#include <stdint.h> // for uint32_t
#include <stdio.h>  // for fclose, ferror, fopen, fscanf
#include <stdlib.h> // for system
#include <string.h> // for strerror
#include <unistd.h> // for sleep

#include <sys/stat.h> // for stat

#include <functional> // for function
#include <memory>     // for unique_ptr, default_delete
#include <utility>    // for move, forward
#include <mutex>

#include <core/IAction.h>             // for IDispatch
#include <core/Portability.h>         // for ErrorCodes, EXTERNAL
#include <core/Proxy.h>               // for ProxyType
#include <core/Time.h>                // for Time
#include <core/Trace.h>               // for ASSERT
#include <interfaces/IPowerManager.h> // for IPowerManager

#include "UtilsLogging.h" // for LOGINFO, LOGERR
#include "mfr_temperature.h"
#include "mfrMgr.h"

#ifdef MFR_TEMP_CLOCK_READ
#include "hal/ThermalMfrImpl.h"
#else
#include "hal/ThermalImpl.h"
#endif

#define STANDBY_REASON_FILE       "/opt/standbyReason.txt"
#define THERMAL_PROTECTION_GROUP  (char*)"Thermal_Config"
#define THERMAL_SHUTDOWN_REASON   (char*)"THERMAL_SHUTDOWN"


class ThermalController {

#ifndef MFR_TEMP_CLOCK_READ
/* Temperature (in celcus) at which box will ALWAYS be rebooted */
static constexpr int REBOOT_CRITICAL    = 120;
/* Temperature (in celcus) at which box will be rebooted after grace_interval has passed
   Timer is started 2 minutes late to give deepsleep logic a chance to work */
static constexpr int REBOOT_CONCERN = 120;
/* Temperature (in celcus) at which box is considered safe and will stop reboot consideration */
static constexpr int REBOOT_SAFE        = 110;
/* Temperature (in celcus) at which box will go to deepsleep/reboot after grace_interval has passed */
static constexpr int GRACE_INTERVAL = 600;
/* Temperature (in celcus) at which box will ALWAYS go to deep sleep */
static constexpr int DEEPSLEEP_CRITICAL = 115;
/* Temperature (in celcus) at which box will ALWAYS be switched to the middle clock mode */
static constexpr int DEEPSLEEP_CONCERN = 110;
/* Temperature (in celcus) at which box is considered safe and will stop deep sleep consideration */
static constexpr int DEEPSLEEP_SAFE = 100;
/* Temperature (in celcus) at which box will ALWAYS be switched to the LOWEST clock mode */
static constexpr int DECLOCK_CRITICAL = 110;
/* Temperature (in celcus) at which box will ALWAYS be switched to the middle clock mode */
static constexpr int DECLOCK_CONCERN = 100;
/* Temperature (in celcus) at which box will be switched back to highest clock mode after 'thermal_declock_grace_interval' has passed */
static constexpr int DECLOCK_SAFE = 90;
/* The amount of time (in seconds) that must pass after to switch from a lower clock mode to a higher clock mode

    ***NOTE: All temperature based declock logic will be disabled if 'thermal_declock_grace_interval' is set to 0 ***   */
static constexpr int DECLOCK_GRACE_INTERVAL = 60;
// the interval at which temperature will be polled from lower layers
// the interval after which reboot will happen if the temperature goes above reboot threshold
static constexpr int POLL_INTERVAL = 30;

#else //MFR_TEMP_CLOCK_READ
/* Temperature (in celcus) at which box will ALWAYS be rebooted */
static constexpr int REBOOT_CRITICAL    = 110;
/* Temperature (in celcus) at which box will be rebooted after grace_interval has passed
   Timer is started 2 minutes late to give deepsleep logic a chance to work */
static constexpr int REBOOT_CONCERN     = 102;
/* Temperature (in celcus) at which box is considered safe and will stop reboot consideration */
static constexpr int REBOOT_SAFE        = 100;

/* Temperature (in celcus) at which box will ALWAYS go to deep sleep */
static constexpr int DEEPSLEEP_CRITICAL     = 105;

/* Temperature (in celcus) at which box will ALWAYS be switched to the middle clock mode */
static constexpr int DEEPSLEEP_CONCERN      = 100;

/* Temperature (in celcus) at which box is considered safe and will stop deep sleep consideration */
static constexpr int DEEPSLEEP_SAFE         = 90;

/* Temperature (in celcus) at which box will ALWAYS be switched to the LOWEST clock mode */
static constexpr int DECLOCK_CRITICAL   = 100;
/* Temperature (in celcus) at which box will ALWAYS be switched to the middle clock mode */
static constexpr int DECLOCK_CONCERN    = 90;

/* Temperature (in celcus) at which box will be switched back to highest clock mode after 'thermal_declock_grace_interval' has passed */
static constexpr int DECLOCK_SAFE       = 80;

/* Temperature (in celcus) at which box will go to deepsleep/reboot after grace_interval has passed */
static constexpr int GRACE_INTERVAL     = 600;

// the interval at which temperature will be polled from lower layers
// the interval after which reboot will happen if the temperature goes above reboot threshold
static constexpr int POLL_INTERVAL      = 30;

/* The amount of time (in seconds) that must pass after to switch from a lower clock mode to a higher clock mode

    ***NOTE: All temperature based declock logic will be disabled if 'thermal_declock_grace_interval' is set to 0 ***   */
static constexpr int DECLOCK_GRACE_INTERVAL = 60;

#endif //MFR_TEMP_CLOCK_READ

    using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;
    using IPlatform = hal::Thermal::IPlatform;
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
#ifdef MFR_TEMP_CLOCK_READ
    using DefaultImpl = ThermalMfrImpl;
#else
    using DefaultImpl = ThermalImpl;
#endif

private:
    std::shared_ptr<IPlatform> _platform;
    std::shared_ptr<std::mutex> _therm_mutex;
    std::shared_ptr<std::mutex> _grace_interval_mutex;

    bool _rebootZone = false;

    class Thresholds {
        public:
            int critical;
            int concern;
            int safe;
            int graceInterval;
    };

    Thresholds rebootThreshold = {REBOOT_CRITICAL,REBOOT_CONCERN,REBOOT_SAFE,GRACE_INTERVAL};
    Thresholds deepsleepThreshold = {DEEPSLEEP_CRITICAL,DEEPSLEEP_CONCERN,DEEPSLEEP_SAFE,GRACE_INTERVAL};
    Thresholds declockThreshold = {DECLOCK_CRITICAL,DECLOCK_CONCERN,DECLOCK_SAFE,DECLOCK_GRACE_INTERVAL};

    // the interval at which temperature will be polled from lower layers
    int thermal_poll_interval        = POLL_INTERVAL; //in seconds
    // the interval after which reboot will happen if the temperature goes above reboot threshold

    //Did we already read config params once ?
    bool read_config_param           = FALSE;

    // Is feature enabled ?
    bool isFeatureEnabled            = TRUE;
    //Current temperature level
    volatile ThermalTemperature m_cur_Thermal_Level;
    ///Current temperature reading in celcius
    volatile int m_cur_Thermal_Value =0;
    //Current CPU clocking mode.
    uint32_t cur_Cpu_Speed = 0;

    // These are the clock rates that will actually be used when declocking. 0 is uninitialized and we'll attempt to auto discover
    uint32_t PLAT_CPU_SPEED_NORMAL = 0;
    uint32_t PLAT_CPU_SPEED_SCALED = 0;
    uint32_t PLAT_CPU_SPEED_MINIMAL = 0;

    // Thread id of polling thread
    std::thread *thermalThreadId = nullptr;

public:
    class INotification {

        public:
            virtual ~INotification() = default;

            virtual void onThermalTemperatureChanged(const ThermalTemperature cur_Thermal_Level,const ThermalTemperature new_Thermal_Level, const float current_Temp) = 0;
            virtual void onDeepSleepForThermalChange() = 0;
    };

private:
    ThermalController (INotification& parent, std::shared_ptr<IPlatform> platform);

    inline IPlatform& platform() const
    {
        ASSERT(_platform != nullptr);
        return *_platform;
    }

    INotification& _parent;
    volatile bool _stopThread;

    void initializeThermalProtection();
    bool isThermalProtectionEnabled();
    void logThermalShutdownReason();
    void rebootIfNeeded();
    void deepSleepIfNeeded();
    void declockIfNeeded();

    bool updateRFCStatus();
    char * read_ConfigProperty(const char* key);
    bool read_ConfigProps();

    //Thread entry function to monitor thermal levels of the device.
    void pollThermalLevels();

    static const char* str(ThermalTemperature mode);

public:
    template <typename IMPL = DefaultImpl, typename... Args>
    static ThermalController Create(INotification& parent, Args&&... args)
    {
        static_assert(std::is_base_of<IPlatform, IMPL>::value, "Impl must derive from hal::Thermal::IPlatform");
        auto impl = std::shared_ptr<IMPL>(new IMPL(std::forward<Args>(args)...));
        ASSERT(impl != nullptr);
        return ThermalController(parent, std::move(impl));
    }

    uint32_t GetThermalState(ThermalTemperature &curLevel, float &curTemperature) const;
    uint32_t GetTemperatureThresholds(float &tempHigh,float &tempCritical) const;
    uint32_t SetTemperatureThresholds(float tempHigh,float tempCritical);
    uint32_t GetOvertempGraceInterval(int &graceInterval) const;
    uint32_t SetOvertempGraceInterval(int graceInterval);

    ~ThermalController();

};
