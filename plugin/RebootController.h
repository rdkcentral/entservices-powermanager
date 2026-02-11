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

#include "UtilsLogging.h"
#include <core/WorkerPool.h>

#include "Settings.h"

class RebootController {

    template <typename T>
    static inline typename T::rep now()
    {
        return std::chrono::duration_cast<T>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    class Threshold {
    public:
        Threshold(int threshold, int graceInterval = 0)
            : _threshold(threshold)
            , _graceInterval(graceInterval)
        {
        }

        inline void SetThreshold(int threshold)
        {
            if (threshold > 0) {
                _threshold = threshold;
            } else {
                LOGERR("Attempted to set negative threshold %d, ignore", threshold);
            }
        }

        inline int threshold() const
        {
            return _threshold;
        }

        inline int graceInterval() const
        {
            return _graceInterval;
        }

        inline void SetGraceInterval(int graceInterval)
        {
            if (graceInterval > 0) {
                _graceInterval = graceInterval;
            }
        }

        bool IsThresholdExceeded(int uptime = now<std::chrono::seconds>()) const
        {
            return (uptime >= _threshold);
        }

        bool IsGraceIntervalExceeded(int64_t uptime = now<std::chrono::seconds>()) const
        {
            return (uptime >= _graceInterval);
        }

    private:
        int _threshold;
        int _graceInterval;
    };

public:
    RebootController(const Settings& settings);
    ~RebootController();

private:
    void scheduleHeartbeat();
    void heartbeatMsg();
    int fetchRFCValueInt(const char* key);
    bool isStandbyRebootEnabled();

private:
    WPEFramework::Core::IWorkerPool& _workerPool;
    const Settings& _settings;
    Threshold _standbyRebootThreshold;
    Threshold _forcedRebootThreshold;
    WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch> _heartbeatJob;
    bool _rfcUpdated;
};
