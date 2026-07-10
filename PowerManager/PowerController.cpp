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

#include <functional> // for function
#include <unistd.h>   // for access, F_OK

#include <core/IAction.h>    // for IDispatch
#include <core/Time.h>       // for Time
#include <core/WorkerPool.h> // for IWorkerPool, WorkerPool

#include "Module.h"
#include "LambdaJob.h"      // for LambdaJob
#include "UtilsLogging.h"   // for LOGINFO, LOGERR
#include "secure_wrapper.h" // for v_secure_system

#include "PowerController.h"
#include "PowerUtils.h"

using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;
using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
using IPlatform = hal::power::IPlatform;
using DefaultImpl = PowerImpl;
using util = PowerUtils;

PowerController::PowerController(DeepSleepController& deepSleep, std::unique_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    , _powerStateBeforeReboot(PowerState::POWER_STATE_UNKNOWN)
    , _lastKnownPowerState(PowerState::POWER_STATE_ON)
    , _settings(Settings::Load(m_settingsFile))
    , _deepSleepWakeupSettings(_settings)
    , _workerPool(WPEFramework::Core::WorkerPool::Instance())
    , _deepSleep(deepSleep)
#ifdef OFFLINE_MAINT_REBOOT
    , _rebootController(_settings)
#endif
{
    SYSLOG(WPEFramework::Logging::Startup, (_T("%s >>"), __FUNCTION__));
    ASSERT(nullptr != _platform);

    // Settings initialization will never fail
    // It will either be deserialized from file or initialized to default values
    int wakeupSrcConfig = WakeupSrcType::WAKEUP_SRC_WIFI | WakeupSrcType::WAKEUP_SRC_LAN;
    int wakeupSrcValue = _settings.nwStandbyMode() ? wakeupSrcConfig : 0;
    SYSLOG(WPEFramework::Logging::Startup, (_T("%s before SetWakeupSourceConfig"), __FUNCTION__));
    this->platform().SetWakeupSrcConfig(0, wakeupSrcConfig, wakeupSrcValue);

    SYSLOG(WPEFramework::Logging::Startup, (_T("%s before init"), __FUNCTION__));
    init();
    SYSLOG(WPEFramework::Logging::Startup, (_T("%s <<"), __FUNCTION__));
}

void PowerController::init()
{
    // settings already loaded in constructor
    _lastKnownPowerState = _settings.powerState();

    do {
        // Sync power state with platform
        PowerState currentState = PowerState::POWER_STATE_UNKNOWN;

        uint32_t errorCode = platform().GetPowerState(currentState);

        if (WPEFramework::Core::ERROR_NONE != errorCode) {
            LOGINFO("Failed to get current power state from platform: %u", errorCode);
            break;
        }

        if (_settings.powerState() == currentState) {
            LOGINFO("Bootup powerState is already in sync with hardware: %s", util::str(currentState));
            break;
        }

        errorCode = SetPowerState(0, _settings.powerState(), "Initialization");

        if (WPEFramework::Core::ERROR_NONE == errorCode) {
            LOGINFO("Successfull at syncing powerState %s with hardware", util::str(_settings.powerState()));
            break;
        }

        LOGINFO("Bootup failed to sync powerState with hardware, update settings file to powerState: %s",
            util::str(currentState));

        _settings.SetPowerState(currentState);
        _settings.Save(m_settingsFile);
    } while (false);
}

uint32_t PowerController::SetPowerState(const int keyCode, const PowerState powerState, const std::string& reason)
{
    if (access("/tmp/ignoredeepsleep", F_OK) == 0) {
        if (PowerState::POWER_STATE_STANDBY_DEEP_SLEEP == powerState) {
            LOGINFO("Ignoring DEEPSLEEP state due to tmp override /tmp/ignoredeepsleep");
            return WPEFramework::Core::ERROR_ILLEGAL_STATE;
        }
    }

    PowerState curState = _settings.powerState();

    /* Independent of Deep sleep */
    uint32_t errCode = platform().SetPowerState(powerState);
    if (WPEFramework::Core::ERROR_NONE != errCode) {
        LOGERR("Failed to set power state: %u", errCode);
    } else {
        _settings.SetPowerState(powerState);
        _settings.Save(m_settingsFile);
        _lastKnownPowerState = curState;
    }

    return errCode;
}

uint32_t PowerController::ActivateDeepSleep()
{
    return _deepSleep.Activate(_deepSleepWakeupSettings.timeout(), _settings.nwStandbyMode());
}

uint32_t PowerController::SetNetworkStandbyMode(const bool standbyMode)
{
    if (standbyMode == _settings.nwStandbyMode()) {
        LOGINFO("Network Standby mode is already set to %s", standbyMode ? "Enabled" : "Disabled");
        return WPEFramework::Core::ERROR_NONE;
    }

    int wakeupSrcConfig = WakeupSrcType::WAKEUP_SRC_WIFI | WakeupSrcType::WAKEUP_SRC_LAN;
    int wakeupSrcValue = standbyMode ? wakeupSrcConfig : 0;

    this->platform().SetWakeupSrcConfig(0, wakeupSrcConfig, wakeupSrcValue);

    _settings.SetNwStandbyMode(standbyMode);

    bool ok = _settings.Save(m_settingsFile);

    if (!ok) {
        LOGERR("Failed to save settings");
        return WPEFramework::Core::ERROR_GENERAL;
    }

    return WPEFramework::Core::ERROR_NONE;
}

uint32_t PowerController::GetNetworkStandbyMode(bool& standbyMode) const
{
    standbyMode = _settings.nwStandbyMode();
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t PowerController::SetWakeupSrcConfig(const int powerMode, const int srcType, int config)
{
    return platform().SetWakeupSrcConfig(powerMode, srcType, config);
}

uint32_t PowerController::GetWakeupSrcConfig(int& powerMode, int& srcType, int& config) const
{
    return platform().GetWakeupSrcConfig(powerMode, srcType, config);
}

uint32_t PowerController::Reboot(const string& requestor, const string& reasonCustom, const string& reasonOther)
{
    _workerPool.Submit(LambdaJob::Create([requestor, reasonCustom, reasonOther]() {
        v_secure_system("echo 0 > /opt/.rebootFlag");

        LOGINFO("------------FINAL REBOOT NOTICE----------\n\tRebooting device requestor: %s, reasonCustom: %s, reasonOther: %s",
            requestor.c_str(), reasonCustom.c_str(), reasonOther.c_str());
        if (0 == access("/rebootNow.sh", F_OK)) {
            v_secure_system("/rebootNow.sh -s '%s' -r '%s' -o '%s'", requestor.c_str(), reasonCustom.c_str(), reasonOther.c_str());
        } else {
            v_secure_system("/lib/rdk/rebootNow.sh -s '%s' -r '%s' -o '%s'", requestor.c_str(), reasonCustom.c_str(), reasonOther.c_str());
        }
    }));

    return WPEFramework::Core::ERROR_NONE;
}

uint32_t PowerController::SetDeepSleepTimer(const int timeOut)
{
    _deepSleepWakeupSettings.SetTimeout(timeOut);
    _settings.Save(m_settingsFile);
    return WPEFramework::Core::ERROR_NONE;
}
