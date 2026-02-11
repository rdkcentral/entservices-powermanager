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

#include "LambdaJob.h"      // for LambdaJob
#include "UtilsLogging.h"   // for LOGINFO, LOGERR
#include "secure_wrapper.h" // for v_secure_system

#include "PowerController.h"
#include "PowerUtils.h"

using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;
using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
using WakeupSourceConfig = WPEFramework::Exchange::IPowerManager::WakeupSourceConfig;
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
    ASSERT(nullptr != _platform);

    // Settings initialization will never fail
    // It will either be deserialized from file or initialized to default values
    bool wakeupSrcValue = _settings.nwStandbyMode();

    std::list<WakeupSourceConfig> configs = {
        { WakeupSrcType::WAKEUP_SRC_WIFI, wakeupSrcValue },
        { WakeupSrcType::WAKEUP_SRC_LAN, wakeupSrcValue }
    };

    SetWakeupSourceConfig(configs);

    init();
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

    std::list<WakeupSourceConfig> configs = {
        { WakeupSrcType::WAKEUP_SRC_WIFI, standbyMode },
        { WakeupSrcType::WAKEUP_SRC_LAN, standbyMode }
    };

    uint32_t errorCode = SetWakeupSourceConfig(configs);

    if (WPEFramework::Core::ERROR_NONE == errorCode) {

        _settings.SetNwStandbyMode(standbyMode);

        bool ok = _settings.Save(m_settingsFile);

        if (!ok) {
            LOGERR("Failed to save settings");
            errorCode = WPEFramework::Core::ERROR_GENERAL;
        }
    }

    return errorCode;
}

uint32_t PowerController::GetNetworkStandbyMode(bool& standbyMode) const
{
    standbyMode = _settings.nwStandbyMode();
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t PowerController::SetWakeupSourceConfig(const std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig>& configs)
{
    bool failed = false;

    for (auto& config : configs) {
        bool supported = false;
        int result = platform().SetWakeupSrc(config.wakeupSource, config.enabled, supported);
        if (WPEFramework::Core::ERROR_NONE != result && supported) {
            // latch failed status
            failed = true;
        }
    }

    uint32_t errorCode = failed ? WPEFramework::Core::ERROR_GENERAL : WPEFramework::Core::ERROR_NONE;

    LOGINFO("errorCode: %d", errorCode);

    return errorCode;
}

uint32_t PowerController::GetWakeupSourceConfig(std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig>& configs) const
{
    bool failed = false;

    for (int src = WakeupSrcType::WAKEUP_SRC_VOICE; src <= WakeupSrcType::WAKEUP_SRC_RF4CE; src++) {
        WakeupSrcType wakeupSrc = static_cast<WakeupSrcType>(src);

        bool supported = false, enabled = false;

        uint32_t result = platform().GetWakeupSrc(wakeupSrc, enabled, supported);

        if (WPEFramework::Core::ERROR_NONE == result) {
            configs.push_back({ wakeupSrc, enabled });
        } else if (!supported) {
            // Not supported, won't append to config list
            // platform API already has logs so not logging here
        } else {
            // failed, latch failed status
            failed = true;
        }
    }
    uint32_t errorCode = failed ? WPEFramework::Core::ERROR_GENERAL : WPEFramework::Core::ERROR_NONE;

    LOGINFO("errorCode: %d", errorCode);

    return errorCode;
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
