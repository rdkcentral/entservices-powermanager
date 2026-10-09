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
#include <cstdint>
#include <errno.h>    // for errno
#include <fstream>    // for ifstream
#include <functional> // for function
#include <memory>

#include <stdio.h>    // for fclose, fopen, fscanf, FILE, ferror
#include <stdlib.h>   // for system, rand, srand
#include <string.h>   // for strerror, strlen
#include <sys/stat.h> // for stat
#include <time.h>     // for tm, time, difftime, mktime, NULL, loca...
#include <unistd.h>   // for sleep

#include <core/IAction.h>     // for IDispatch
#include <core/Portability.h> // for ErrorCodes
#include <core/Time.h>        // for Time
#include <core/WorkerPool.h>  // for IWorkerPool, WorkerPool

#include "DeepSleepController.h"

#include <condition_variable>
#include <mutex>
#include "LambdaJob.h"      // for LambdaJob
#include "PowerUtils.h"     // for WakeupReason string
#include "UtilsLogging.h"   // for LOGINFO, LOGERR
#include "libIARM.h"        // for _IARM_Result_t, IARM_Result_t
#include "libIBus.h"        // for IARM_Bus_Call
#include "secure_wrapper.h" // for v_secure_system
#include "sysMgr.h"         // for IARM_BUS_SYSMGR_API_GetSystemStates
#include "rfcapi.h"

#define FIXEDSTARTS "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.FixedStarts"
#define RANDOMDELAY "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.RandomDelay"
#define INACTIVITYTIMEOUT "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.InactivityTimeout"

using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;
using PowerState   = WPEFramework::Exchange::IPowerManager::PowerState;
using IPlatform    = hal::deepsleep::IPlatform;
using util         = PowerUtils;

std::map<std::string, DeepSleepWakeupSettings::tzValue> DeepSleepWakeupSettings::_maptzValues;

static bool parseFixedStarts(const std::string& extractedParam, std::vector<int>& list_fixedStarts)
{
    std::vector<int> parsed;
    std::stringstream ss(extractedParam);
    std::string token;

    if (!extractedParam.empty() && extractedParam.back() == ',')
    {
        LOGERR("fixedStarts RFC value '%s' has a trailing separator - rejecting", extractedParam.c_str());
        return false;
    }

    while (std::getline(ss, token, ',')) {
        try {
            size_t pos = 0;
            int value = std::stoi(token, &pos);
            if ((value < 0) || value >= (24 * 60))
            {
                /* Reject this RFC values which could be negative or greater than 24 hours */
                LOGERR("Invalid fixedStarts entry '%s' in RFC value '%s' - rejecting whole list",
                       token.c_str(), extractedParam.c_str());
                return false;
            }

            /* Validate the value is integer */
            if (pos != token.size()) {
                LOGERR("Invalid fixedStarts entry '%s' in RFC value '%s' - rejecting whole list",
                       token.c_str(), extractedParam.c_str());
                return false;
            }
            parsed.push_back(value);
        } catch (...) {
            LOGERR("Invalid fixedStarts entry '%s' in RFC value '%s' - rejecting whole list",
                   token.c_str(), extractedParam.c_str());
            return false;
        }
    }

    if (parsed.empty()) {
        LOGERR("fixedStarts RFC value '%s' produced no entries - rejecting", extractedParam.c_str());
        return false;
    }

    list_fixedStarts = std::move(parsed);
    return true;
}

uint32_t DeepSleepWakeupSettings::getTZDiffInSec() const
{
    uint32_t _TZDiffTime  = 6 * 3600;
    IARM_Result_t iResult = IARM_RESULT_SUCCESS;
    tzValue value         = tzCST06;

    /* Get the Time Zone Pay Load from SysMgr */
    IARM_Bus_SYSMgr_GetSystemStates_Param_t param = {0};
    iResult = IARM_Bus_Call(IARM_BUS_SYSMGR_NAME, IARM_BUS_SYSMGR_API_GetSystemStates, (void*)&param, sizeof(param));
    if (iResult == IARM_RESULT_SUCCESS) {
        if (param.time_zone_available.error) {
            LOGINFO("Failed to get the Time Zone Information from SysMgr");
        } else if (param.time_zone_available.state == 2) {
            if (strlen(param.time_zone_available.payload) > 1) {
                LOGINFO("TZ Payload - %s", param.time_zone_available.payload);
                value       = _maptzValues[param.time_zone_available.payload];
                _TZDiffTime = value * 3600;

                LOGINFO("TZ value = %d", value);
                LOGINFO("Time Zone in Sec = %d", _TZDiffTime);
            }
        }
    }
    return _TZDiffTime;
}

/*  Get TZ diff
    Have Record of All US TZ as of now.
*/
void DeepSleepWakeupSettings::initializeTimeZone()
{
    _maptzValues["HST11"]                     = tzHST11;
    _maptzValues["HST11HDT,M3.2.0,M11.1.0"]   = tzHST11HDT;
    _maptzValues["AKST"]                      = tzAKST;
    _maptzValues["AKST09AKDT,M3.2.0,M11.1.0"] = tzAKST09AKDT;
    _maptzValues["PST08"]                     = tzPST08;
    _maptzValues["PST08PDT,M3.2.0,M11.1.0"]   = tzPST08PDT;
    _maptzValues["MST07"]                     = tzMST07;
    _maptzValues["MST07MDT,M3.2.0,M11.1.0"]   = tzMST07MDT;
    _maptzValues["CST06"]                     = tzCST06;
    _maptzValues["CST06CDT,M3.2.0,M11.1.0"]   = tzCST06CDT;
    _maptzValues["EST05"]                     = tzEST05;
    _maptzValues["EST05EDT,M3.2.0,M11.1.0"]   = tzEST05EDT;
}

uint32_t DeepSleepWakeupSettings::secure_random() const
{
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    uint32_t value;
    urandom.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

/*  Get Wakeup timeout.
    Wakeup the box to do Maintenance related activities.
*/
uint32_t DeepSleepWakeupSettings::getWakeupTime() const
{
    time_t now = 0, wakeup = 0;
    struct tm wakeupTime     = { 0 };
    uint32_t wakeupTimeInSec = 0;
    uint32_t getTZDiffTime   = 0;
    uint32_t wakeupTimeInMin = 5;

    const uint32_t limit = 6 * 24 * 60 * 60; // 6 days limit in seconds

    /* Read the wakeup Time in Seconds from /tmp override
       else calculate the Wakeup time till 2AM */
    FILE* fp = fopen("/tmp/deepSleepWakeupTimer", "r");
    if (NULL != fp) {
        if (0 > fscanf(fp, "%d", &wakeupTimeInMin)) {
            LOGINFO("Error: fscanf on wakeupTimeInSec failed");
        } else {
            wakeupTimeInSec = wakeupTimeInMin * 60 > limit ? limit : wakeupTimeInMin * 60;
            fclose(fp);
            LOGINFO("/tmp/ override Deep Sleep Wakeup Time is %" PRIu32, wakeupTimeInSec);

            return wakeupTimeInSec;
        }
        fclose(fp);
    }

    /* curr time */
    time(&now);

    /* wakeup time */
    time(&wakeup);
    auto* res = localtime(&wakeup);

    if (nullptr != res) {
        wakeupTime = *res;
        if (wakeupTime.tm_hour >= 0 && wakeupTime.tm_hour < 2) {
            /*Calculate the wakeup time till 2 AM..*/
            wakeupTime.tm_hour = 2;
            wakeupTime.tm_min  = 0;
            wakeupTime.tm_sec  = 0;
            wakeupTimeInSec    = difftime(mktime(&wakeupTime), now);

        } else {
            /*Calculate the wakeup time till midnight + 2 hours for 2 AM..*/
            wakeupTime.tm_hour = 23;
            wakeupTime.tm_min  = 59;
            wakeupTime.tm_sec  = 60;
            wakeupTimeInSec    = difftime(mktime(&wakeupTime), now);
            wakeupTimeInSec    = wakeupTimeInSec + 7200; // 7200sec for 2 hours
        }

        /* Add randomness to calculated value i.e between 2AM - 3AM
            for 1 hour window
        */
        uint32_t randTimeInSec = secure_random() % 3600; // for 1 hour window
        wakeupTimeInSec        = wakeupTimeInSec + randTimeInSec;
        LOGINFO("Calculated Deep Sleep Wakeup Time Before TZ setting is %" PRIu32 "Sec", wakeupTimeInSec);

        getTZDiffTime   = getTZDiffInSec();
        wakeupTimeInSec = wakeupTimeInSec + getTZDiffTime;

        LOGINFO("Calculated Deep Sleep Wakeup Time After TZ setting is %" PRIu32 "Sec", wakeupTimeInSec);
        return wakeupTimeInSec;
    }

    LOGERR("Failed to get local time");

    return 0;
}

#ifdef CUSTOM_LGI
/*  Get Wakeup timeout.
    Wakeup the box to do Maintenance related activities.
*/
uint32_t DeepSleepWakeupSettings::getCustomMaintenanceWakeupTime() const
{
    time_t now = 0, wakeup = 0;
    time_t wakeupTime     = 0;
    uint32_t wakeupTimeInSec = 0;
    uint32_t minWakeupTime = 5 * 60 ;

    /* curr time */
    time(&now);

    bool hasFixedStartAfterWakeup = false;
    LOGINFO("Current time: %ld, Inactivity timeout: %d minutes", (long)now, _inactivityTimeout);
    wakeup = now + ( _inactivityTimeout * 60 ) ;
    auto* res = localtime(&wakeup); /*current time plus inactivity timeout */
    if (nullptr != res)
    {
        wakeupTime = wakeup ;
        struct tm localTime = *res;
        LOGINFO("Wakeup time: %ld", (long)wakeupTime);
        localTime.tm_hour = 0;
        localTime.tm_min  = 0;
        localTime.tm_sec  = 0;
        LOGINFO("localTime: %04d-%02d-%02d %02d:%02d:%02d gmtoff=%ld isdst=%d",
                    localTime.tm_year + 1900,localTime.tm_mon + 1,localTime.tm_mday,
                        localTime.tm_hour,localTime.tm_min,localTime.tm_sec,
                            (long)localTime.tm_gmtoff,localTime.tm_isdst);

        if(!_fixedStarts.empty())
        {
            time_t midnightBeforeWakeup = mktime(&localTime);
            for (const auto& fixedStart : _fixedStarts)
            {
                time_t candidateWakeupTime = midnightBeforeWakeup + (fixedStart * 60);
                LOGINFO("Midnight timestamp: %ld", (long)midnightBeforeWakeup);
                LOGINFO("fixedStart: %d", fixedStart);
                LOGINFO("Candidate wakeup time: %ld", (long)candidateWakeupTime);
                if(candidateWakeupTime > wakeupTime)
                {
                    wakeupTime = candidateWakeupTime ;
                    LOGINFO("Got fixed start after wakeupTime: %ld", (long)wakeupTime);
                    hasFixedStartAfterWakeup = true ;
                    break ;
                }
            }
        }
        if( !_fixedStarts.empty() && !hasFixedStartAfterWakeup)
        {
            LOGINFO("No fixed start after wakeupTime, so move to tomorrow.");
            /* Adding a day to the current time to ensure we are scheduling for the next day */
            localTime.tm_mday += 1;
            localTime.tm_hour = 0;
            localTime.tm_min = 0;
            localTime.tm_sec = 0;
            localTime.tm_isdst = -1 ;

            time_t midnightAfterWakeup = mktime(&localTime);
            LOGINFO("Midnight after wakeup timestamp: %ld", (long)midnightAfterWakeup);

            struct tm *check = localtime(&midnightAfterWakeup);

            LOGINFO("AFTER mktime/localtime: %04d-%02d-%02d %02d:%02d:%02d",
                check->tm_year + 1900,
                check->tm_mon + 1,
                check->tm_mday,
                check->tm_hour,
                check->tm_min,
                check->tm_sec);


            LOGINFO("fixedStart: %d", _fixedStarts[0]);
            wakeupTime = midnightAfterWakeup + (_fixedStarts[0] * 60 );
            LOGINFO("Wakeup time after midnight calculation: %ld", (long)wakeupTime);
        }

        if(_randomDelay)
        {
            double randomFactor = static_cast<double>(secure_random()) /
                                    (static_cast<double>(UINT32_MAX) + 1.0);
            wakeupTime += static_cast<time_t>(randomFactor * _randomDelay * 60);
            LOGINFO("Random factor: %f", randomFactor);
            LOGINFO("After adding random delay of %d seconds to wakeup time:%ld", _randomDelay,(long)wakeupTime);
        }

        const time_t minimumWakeup = now + static_cast<time_t>(minWakeupTime);
        wakeupTimeInSec = static_cast<uint32_t>(difftime(std::max(wakeupTime, minimumWakeup), now));
        LOGINFO("wakeupTime in sec :%d, now:%ld, minimumWakeup:%ld", wakeupTimeInSec, (long)now, (long)minimumWakeup);
        return wakeupTimeInSec;
    }

    LOGERR("Failed to get local time");

    return 0;
}
#endif

struct DeepSleepController::InFlightEntryState {
    std::mutex mutex;
    std::condition_variable condition;
    bool active { false };
};

DeepSleepController::DeepSleepController(INotification& parent, std::shared_ptr<IPlatform> platform)
    : _parent(parent)
    , _workerPool(WPEFramework::Core::WorkerPool::Instance())
    , _platform(std::move(platform))
    , _deepSleepState(DeepSleepState::NotStarted)
    , _deepSleepDelaySec(0)
    , _deepSleepWakeupTimeoutSec(0)
    , _jobLock(std::make_shared<WPEFramework::Core::CriticalSection>())
    , _inFlightEntryState(std::make_shared<InFlightEntryState>())
    , _nwStandbyMode(false)
    , _abortState(std::make_shared<AbortState>())
    , _timingMutex(std::make_shared<std::mutex>())
{
    LOGINFO(">> CTOR <<");
}

DeepSleepController::~DeepSleepController()
{
    LOGINFO(">> DTOR");
    Shutdown();
    LOGINFO("<< DTOR");
}

void DeepSleepController::Shutdown()
{
    // Revoke queued jobs and wait for any in-flight dispatch to finish before
    // dependent owner members are destroyed, since both jobs capture `this`
    // and can callback through _parent.
    cancelPendingWorkerJobs();
    waitForInFlightEntry();
    // An activation can schedule delayed entry before completing. Re-check after
    // the in-flight activation has finished so that job cannot outlive teardown.
    cancelPendingWorkerJobs();
    waitForInFlightEntry();
}

uint32_t DeepSleepController::GetLastWakeupReason(WakeupReason& wakeupReason) const
{
    const uint32_t errorCode = platform().GetLastWakeupReason(wakeupReason);
#ifdef CUSTOM_LGI
    /* When maintenance schedule is set and platform reports a timer wakeup,
       override to maintenance wakeup reason*/
    if (WPEFramework::Core::ERROR_NONE == errorCode
        && _maintenanceWakeupScheduled->load()
        && wakeupReason == WakeupReason::WAKEUP_REASON_TIMER) {
        wakeupReason = WakeupReason::WAKEUP_REASON_MAINTENANCE;
    }
#endif
    return errorCode;
}

uint32_t DeepSleepController::GetLastWakeupKeyCode(int& keyCode) const
{
    return platform().GetLastWakeupKeyCode(keyCode);
}

// activate deep sleep mode
#ifdef CUSTOM_LGI
uint32_t DeepSleepController::Activate(uint32_t timeOut, bool nwStandbyMode, bool isMaintenanceWakeupScheduled)
{
    LOGINFO("timeOut: %u, nwStandbyMode: %s, isMaintenanceWakeupScheduled: %s",
            timeOut,(nwStandbyMode ? "Enabled" : "Disabled"),
                (isMaintenanceWakeupScheduled ? "true" : "false"));
    _maintenanceWakeupScheduled->store(isMaintenanceWakeupScheduled);
    return submitActivation(timeOut, nwStandbyMode);
}
#else
uint32_t DeepSleepController::Activate(uint32_t timeOut, bool nwStandbyMode)
{
    LOGINFO("timeOut: %u, nwStandbyMode: %s", timeOut, (nwStandbyMode ? "Enabled" : "Disabled"));

    return submitActivation(timeOut, nwStandbyMode);
}
#endif

uint32_t DeepSleepController::submitActivation(uint32_t timeOut, bool nwStandbyMode)
{
    // Replace any previous queued Activate job so teardown can always Revoke the latest,
    // and so a stale queued activation (from a prior call) doesn't race this new one.
    cancelPendingWorkerJobs();

    {
        std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
        if (_inFlightEntryState->active) {
            LOGERR("Deep sleep activation is already in progress");
            return WPEFramework::Core::ERROR_ILLEGAL_STATE;
        }
    }

    _jobLock->Lock();
    _activateJob = LambdaJob::Create([this, timeOut, nwStandbyMode]() {
        _jobLock->Lock();
        {
            std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
            _inFlightEntryState->active = true;
        }
        _jobLock->Unlock();

        LOGINFO("timeOut: %u, nwStandbyMode: %s", timeOut, (nwStandbyMode ? "Enabled" : "Disabled"));
        performActivate(timeOut, nwStandbyMode);

        {
            std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
            _inFlightEntryState->active = false;
        }
        _inFlightEntryState->condition.notify_all();
    });
    _workerPool.Submit(_activateJob);

    _jobLock->Unlock();
    return WPEFramework::Core::ERROR_NONE;
}

// deactivate deep sleep mode
uint32_t DeepSleepController::Deactivate()
{
    cancelPendingWorkerJobs();

    if (_deepSleepDelayJob.IsValid()) {
        // Cancel the delay timer if it is still active
        _workerPool.Revoke(_deepSleepDelayJob);
        _deepSleepDelayJob.Release();
        LOGINFO("Deepsleep delayed job cancelled");
    }

    uint32_t errorCode = platform().DeepSleepWakeup();

    _deepSleepState = DeepSleepState::NotStarted;

    LOGINFO("Deepsleep wakeup completed, errorCode: %u", errorCode);

    return errorCode;
}

void DeepSleepController::cancelPendingWorkerJobs()
{
    WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch> activateJob;

    _jobLock->Lock();

    if (_activateJob.IsValid()) {
        std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
        // Deep-sleep wakeup calls Deactivate() synchronously from the activation
        // worker. Revoking that same in-flight dispatch would wait on itself.
        if (!_inFlightEntryState->active) {
            activateJob = _activateJob;
            _activateJob.Release();
        }
    }

    _jobLock->Unlock();

    // Revoke outside _jobLock because Revoke may wait for the dispatch. Wait for
    // activation first; it can create a delayed-entry job while it is in flight.
    if (activateJob.IsValid()) {
        _workerPool.Revoke(activateJob);
        LOGINFO("Deepsleep activate job cancelled");
    }

    WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch> deepSleepDelayJob;
    _jobLock->Lock();
    if (_deepSleepDelayJob.IsValid()) {
        deepSleepDelayJob = _deepSleepDelayJob;
        _deepSleepDelayJob.Release();
    }
    _jobLock->Unlock();

    if (deepSleepDelayJob.IsValid()) {
        _workerPool.Revoke(deepSleepDelayJob);
        LOGINFO("Deepsleep delayed job cancelled");
    }
}

bool DeepSleepController::read_integer_conf(const char* file_name, uint32_t& val)
{
    bool ok         = false;
    FILE* file      = fopen(file_name, "r");
    const char* err = nullptr;

    if (nullptr != file) {
        int res = fscanf(file, "%u", &val);
        if (1 == res) {
            ok = true;
        } else {
            err = ferror(file) ? strerror(errno) : "fscanf EOF";
        }
        fclose(file);
    } else {
        err = strerror(errno);
    }

    if (!ok) {
        LOGERR("file %s, error: %s", file_name, err);
    }

    return ok;
}

void DeepSleepController::enterDeepSleepDelayed()
{
    _jobLock->Lock();
    {
        std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
        _inFlightEntryState->active = true;
    }
    _deepSleepDelayJob.Release();
    _jobLock->Unlock();

    LOGINFO("Deep Sleep timer expired: entering deep sleep mode");
    enterDeepSleepNow();

    {
        std::lock_guard<std::mutex> lock(_inFlightEntryState->mutex);
        _inFlightEntryState->active = false;
    }
    _inFlightEntryState->condition.notify_all();
}

void DeepSleepController::enterDeepSleepNow()
{
    LOGINFO("Enter to Deep sleep Mode..stop Receiver with sleep 1 before DS");
    if (waitForAbortableDelay(std::chrono::seconds(1))) {
        _deepSleepState = DeepSleepState::NotStarted;
        LOGINFO("Deep sleep entry aborted during pre-delay");
        return;
    }

    uint32_t errorCode = WPEFramework::Core::ERROR_NONE;
    bool failed           = true;
    int retryCount        = 5;
    bool userWakeup       = 0;
    bool abortRequested   = false;
    LOGINFO("Device entering Deep sleep with nwStandbyMode: %s", (_nwStandbyMode ? "Enabled" : "Disabled"));

    while (retryCount && failed) {
        if (isAbortDeepSleepRequested()) {
            abortRequested = true;
            break;
        }

        errorCode = platform().SetDeepSleep(_deepSleepWakeupTimeoutSec, userWakeup, _nwStandbyMode);

        failed = WPEFramework::Core::ERROR_NONE != errorCode;

        if (failed) {
            _deepSleepState = DeepSleepState::Failed;
            retryCount--;

            if ((errorCode == WPEFramework::Core::ERROR_ABORTED) && (retryCount > 0)) {
                LOGINFO("Failed to enter deep sleep mode: %u, retry after 5s", errorCode);
                if (waitForAbortableDelay(std::chrono::seconds(5))) {
                    abortRequested = true;
                    break;
                }
            } else {
                LOGINFO("No retry for deep sleep error code: %u", errorCode);
                break;
            }
        } else {
            _deepSleepState = DeepSleepState::Completed;
            LOGINFO("Device entered to Deep sleep Mode..");
        }
    }

    if (abortRequested) {
        _deepSleepState = DeepSleepState::NotStarted;
        LOGINFO("Deep sleep retry loop aborted due to deactivate/wakeup request");
        return;
    }

    if (failed) {
#ifdef CUSTOM_LGI
        _maintenanceWakeupScheduled->store(false);
#endif
        LOGERR("Failed to enter deep sleep mode error code: %u", errorCode);
        _parent.onDeepSleepFailed();
        return;
    }
    LOGINFO("DeepSleep success; performing wakeup action");

    if (userWakeup) {
#ifdef CUSTOM_LGI
        _maintenanceWakeupScheduled->store(false);
#endif
        LOGINFO("DeepSleep wakeupReason: user action");
        _parent.onDeepSleepUserWakeup(userWakeup);
    } else {
        deepSleepTimerWakeup();
    }
}

void DeepSleepController::deepSleepTimerWakeup()
{
    const auto elapsed = Elapsed();
    const auto timeout = std::chrono::seconds(_deepSleepWakeupTimeoutSec);
    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
    const uint32_t errorCode = GetLastWakeupReason(wakeupReason);

    if (WPEFramework::Core::ERROR_NONE != errorCode) {
        // If the platform cannot identify the source, elapsed time is the only safe
        // fallback for identifying a generic timer wake. It cannot confirm that
        // a maintenance timer caused the wake.
        wakeupReason = elapsed >= timeout
            ? WakeupReason::WAKEUP_REASON_TIMER
            : WakeupReason::WAKEUP_REASON_UNKNOWN;
    }

    const auto pending = elapsed < timeout
        ? std::chrono::duration_cast<std::chrono::milliseconds>(timeout - elapsed).count()
        : 0;
    LOGINFO("DeepSleep wakeupReason: %s, timeout: %ds, elapsed: %llds, pending: %lldms, status: %u",
        util::str(wakeupReason), _deepSleepWakeupTimeoutSec,
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), pending, errorCode);

    // irrespective of wakeup reason / status / elapsed duration always notify deepsleep wakeup
    _parent.onDeepSleepTimerWakeup(_deepSleepWakeupTimeoutSec, wakeupReason);
}

void DeepSleepController::waitForInFlightEntry()
{
    std::unique_lock<std::mutex> lock(_inFlightEntryState->mutex);
    _inFlightEntryState->condition.wait(lock, [this]() {
        return !_inFlightEntryState->active;
    });
}

void DeepSleepController::performActivate(uint32_t timeOut, bool nwStandbyMode)
{
    LOGINFO("timeOut: %u, nwStandbyMode: %s", timeOut, (nwStandbyMode ? "Enabled" : "Disabled"));
    if (!IsDeepSleepInProgress()) {

        clearAbortDeepSleep();

        // latch
        _deepSleepState = DeepSleepState::InProgress;
        {
            std::lock_guard<std::mutex> lock(*_timingMutex);
            _deepsleepStartTime = MonotonicClock::now();
        }

        // Perform the deep sleep operation
        _nwStandbyMode             = nwStandbyMode;
        _deepSleepWakeupTimeoutSec = timeOut;

        _deepSleepDelaySec = 0; // reset before reading override; prevents stale value persisting across activations
        uint32_t delayTimeOut = 0;
        if (read_integer_conf("/tmp/deepSleepDelayTimer", delayTimeOut) && delayTimeOut) {
            _deepSleepDelaySec = delayTimeOut;
            LOGINFO("/tmp/deepSleepDelayTimer override Deep Sleep timeOut value: %u", delayTimeOut);
        }

        uint32_t wakeupTimer = 0;
        if (read_integer_conf("/tmp/deepSleepWakeupTimer", wakeupTimer) && wakeupTimer) {
            _deepSleepWakeupTimeoutSec = wakeupTimer;
            LOGINFO("/tmp/deepSleepWakeupTimer override Deep Sleep wakeup timer value: %u", wakeupTimer);
        }

        if (_deepSleepDelaySec) {
            _jobLock->Lock();
            _deepSleepDelayJob = LambdaJob::Create([this]() {
                enterDeepSleepDelayed();
            });

            WPEFramework::Core::WorkerPool::Instance().Schedule(
                WPEFramework::Core::Time(
                    WPEFramework::Core::Time::Now().Add(_deepSleepDelaySec * 1000)),
                _deepSleepDelayJob);
            _jobLock->Unlock();
        } else {
            enterDeepSleepNow();
        }
    } else {
        LOGERR("Deep sleep operation is already in progress");
    }
}

bool DeepSleepWakeupSettings::fetchMaintenanceWakeupRFCValueInt(const char *key, uint32_t& val)
{
    RFC_ParamData_t param = {0};
    const uint32_t MAX_MAINTENANCE_RFC =  20;
    char rfcVal[MAX_MAINTENANCE_RFC + 1] = { 0 };
    uint32_t len = 0;

    if (WDMP_SUCCESS == getRFCParameter((char*)"MaintenanceWakeup_Config", key, &param))
    {
        len = strlen(param.value);        	
        const char* raw = param.value;

        if (len >= 2 && raw[0] == '"' && raw[len - 1] == '"') 
        {
            raw++;
            len -= 2;
        }

        if (len > MAX_MAINTENANCE_RFC) 
        {
            LOGERR("RFC value for %s too long (%u > %u): '%s'", key, len, MAX_MAINTENANCE_RFC, param.value);
            return false ;
        }

        memcpy(rfcVal, raw, len);
        rfcVal[len] = '\0';
        char* end = nullptr;
        errno = 0;
        long parsed = strtol(rfcVal, &end, 10);

        if (errno != 0 || end == rfcVal || *end != '\0' || parsed < 0 || static_cast<unsigned long>(parsed) > UINT32_MAX) 
        {
            LOGERR("Invalid integer RFC value for %s: '%s'", key, rfcVal);
            return false;
        }
        val = static_cast<uint32_t>(parsed);
        LOGINFO("name=%s, type=%d, value=%s, parsed=%u", param.name, param.type, param.value, val);
        return true;
    }

    LOGERR("Failed to get RFC parameter %s", key);
    return false;
}

bool DeepSleepWakeupSettings::fetchMaintenanceWakeupRFCValueString(const char *key, std::string& values)
{
    RFC_ParamData_t param = {0};
    WDMP_STATUS status = getRFCParameter((char*)"MaintenanceWakeup_Config", key, &param);

    if (status != WDMP_SUCCESS) {
        LOGINFO("RFC key %s not available, status=%d - keeping current value", key, status);
        return false;
    }

    std::string extracted_string(param.value);
    if (extracted_string.size() >= 2 && extracted_string.front() == '"' && extracted_string.back() == '"') {
        extracted_string = extracted_string.substr(1, extracted_string.size() - 2);
    }
    values = extracted_string;
    return true;
}

 bool DeepSleepWakeupSettings::retrieveConfigValueInt(const char* key, uint32_t& output_value)
{
    uint32_t value = 0;

    if (fetchMaintenanceWakeupRFCValueInt(key, value))
    {
        output_value = value;
        return true;
    }

    return false;
}

bool DeepSleepWakeupSettings::retrieveConfigFixedStarts()
{
    std::string value;

    if (!fetchMaintenanceWakeupRFCValueString(FIXEDSTARTS, value))
    {
        return false;
    }

    std::vector<int> parsed;
    if (!parseFixedStarts(value, parsed))
    {
        return false;
    }

    _fixedStarts = std::move(parsed);
    return true;
}

void DeepSleepWakeupSettings::updateMaintenanceWakeupConfig()
{
    bool configRetrieved = false;
    /* Configuration has already been successfully retrieved. */
    if (_maintenanceConfigUpdated)
        return;

    /* Fixed Starts */
    if (retrieveConfigFixedStarts())
    {
        configRetrieved = true;
        /* Log the fixedStarts values */
        std::string fixedStartsLog;
        for (size_t index = 0; index < _fixedStarts.size(); ++index)
        {
            if (index > 0)
            {
                fixedStartsLog += ",";
            }
            fixedStartsLog += std::to_string(_fixedStarts[index]);
        }
        LOGINFO("DeepSleep fixedStarts retrieved successfully: [%s]", fixedStartsLog.c_str());
    }
    else
    {
        LOGINFO("RFC fixedStarts not available");
    }

    /* Random Delay */
    if (retrieveConfigValueInt(RANDOMDELAY, _randomDelay))
    {
        configRetrieved = true;
        LOGINFO("DeepSleep randomDelaySec = %u", _randomDelay);
    }
    else
    {
        LOGINFO("RFC randomdelay not available");
    }

    /* Inactivity Timeout */
    if (retrieveConfigValueInt(INACTIVITYTIMEOUT, _inactivityTimeout))
    {
        configRetrieved = true;
        LOGINFO("DeepSleep inactivityTimeoutSec = %u", _inactivityTimeout);
    }
    else
    {
        LOGINFO("RFC inactivitytimeout not available");
    }
    /*
     * Mark configuration as updated only when at least one parameter
     * has been successfully retrieved.
     */
    if (configRetrieved)
    {
        _maintenanceConfigUpdated = true;
        LOGINFO("DeepSleep maintenance wakeup configuration updated successfully");
    }
    else
    {
        LOGINFO("DeepSleep maintenance wakeup configuration retrieval failed");
    }
}

bool DeepSleepController::waitForAbortableDelay(const std::chrono::seconds& delay)
{
    std::unique_lock<std::mutex> lock(_abortState->mutex);
    return _abortState->cv.wait_for(lock, delay, [this]() {
        return _abortState->requested;
    });
}

void DeepSleepController::requestAbortDeepSleep()
{
    {
        std::lock_guard<std::mutex> lock(_abortState->mutex);
        _abortState->requested = true;
    }
    _abortState->cv.notify_all();
}

void DeepSleepController::clearAbortDeepSleep()
{
    std::lock_guard<std::mutex> lock(_abortState->mutex);
    _abortState->requested = false;
}

bool DeepSleepController::isAbortDeepSleepRequested()
{
    std::lock_guard<std::mutex> lock(_abortState->mutex);
    return _abortState->requested;
}
