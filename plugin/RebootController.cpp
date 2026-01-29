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
#include <chrono>

#include <core/Time.h>
#include <core/WorkerPool.h>

#include "rfcapi.h"
#include "secure_wrapper.h"

#include "LambdaJob.h"
#include "RebootController.h"
#include "UtilsLogging.h"

#define MAX_RFC_LEN 15

#define STANDBY_REBOOT_ENABLE "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.StandbyReboot.Enable"
#define STANDBY_REBOOT "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.StandbyReboot.StandbyAutoReboot"
#define FORCE_REBOOT "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.StandbyReboot.ForceAutoReboot"

using Timestamp = std::chrono::steady_clock::time_point;
using TimestampSec = std::chrono::time_point<std::chrono::steady_clock, std::chrono::seconds>;
static constexpr const int HEARTBEAT_INTERVAL_SEC = 300;

RebootController::RebootController(const Settings& settings)
    : _workerPool(WPEFramework::Core::WorkerPool::Instance())
    , _settings(settings)
    , _standbyRebootThreshold(86400 * 3, 300)
    , _forcedRebootThreshold(172800 * 3)
    , _rfcUpdated(false)
{

    _heartbeatJob = LambdaJob::Create([this]() {
        heartbeatMsg();
    });

    scheduleHeartbeat();
}

RebootController::~RebootController()
{
    if (_heartbeatJob.IsValid()) {
        _workerPool.Revoke(_heartbeatJob);
        _heartbeatJob.Release();
    }
}

void RebootController::scheduleHeartbeat()
{
    _workerPool.Schedule(
        WPEFramework::Core::Time::Now().Add(HEARTBEAT_INTERVAL_SEC * 1000),
        _heartbeatJob);
}

void RebootController::heartbeatMsg()
{
    time_t curr = 0;
    time(&curr);

    LOGINFO("PowerManager plugin: HeartBeat at %s", ctime(&curr));

    if (!_rfcUpdated) {
        bool enabled = isStandbyRebootEnabled();

        int val = fetchRFCValueInt(STANDBY_REBOOT);

        if (-1 != val) {
            _standbyRebootThreshold.SetThreshold(val);
        }

        val = fetchRFCValueInt(FORCE_REBOOT);
        if (-1 != val) {
            _forcedRebootThreshold.SetThreshold(val);
        }

        LOGINFO("Reboot thresolds updated: Enabled: %d, StandbyReboot = %d, ForcedReboot = %d\n",
            enabled, _standbyRebootThreshold.threshold(), _forcedRebootThreshold.threshold());
        _rfcUpdated = true;
    }

    if (isStandbyRebootEnabled()) {
        auto uptime = now<std::chrono::seconds>();
        if (_standbyRebootThreshold.IsThresholdExceeded(uptime)) {
            if (_standbyRebootThreshold.IsGraceIntervalExceeded(_settings.InactiveDuration())) {
                LOGINFO("Going to reboot after %lld\n", uptime);
                v_secure_system("sh /rebootNow.sh -s PwrMgr -o 'Standby Maintenance reboot'");
            }

            if (_forcedRebootThreshold.IsThresholdExceeded(uptime)) {
                LOGINFO("Going to force reboot after %lld\n", uptime);
                v_secure_system("sh /rebootNow.sh -s PwrMgr -o 'Forced Maintenance reboot'");
            }
        }
    }

    scheduleHeartbeat();
}

int RebootController::fetchRFCValueInt(const char* key)
{
    RFC_ParamData_t param = {0};
    char rfcVal[MAX_RFC_LEN + 1] = { 0 };
    int len = 0;

    if (WDMP_SUCCESS == getRFCParameter((char*)"PwrMgr", key, &param)) {
        len = strlen(param.value);
        if (len > MAX_RFC_LEN) {
            len = MAX_RFC_LEN;
        }

        if ((param.value[0] == '"') && (param.value[len] == '"')) {
            strncpy(rfcVal, &param.value[1], len - 1);
            rfcVal[len] = '\0';
        } else {
            strncpy(rfcVal, param.value, MAX_RFC_LEN - 1);
            rfcVal[len] = '\0';
        }
        return atoi(rfcVal);
    }

    LOGERR("Failed to get RFC parameter %s", key);

    return -1;
}

bool RebootController::isStandbyRebootEnabled()
{
    RFC_ParamData_t rfcParam = {0};
    const char* key = STANDBY_REBOOT_ENABLE;
    if (WDMP_SUCCESS == getRFCParameter((char*)"PwrMgr", key, &rfcParam)) {
        return (strncasecmp(rfcParam.value, "true", 4) == 0);
    }

    return false;
}
