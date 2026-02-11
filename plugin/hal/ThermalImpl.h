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

#include <stdint.h>
#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#include "PowerUtils.h"
#include "UtilsLogging.h"

#include "Thermal.h"

typedef enum _PWRMgr_ThermalState_t {
    PWRMGR_TEMPERATURE_NORMAL = 0,
    PWRMGR_TEMPERATURE_HIGH,
    PWRMGR_TEMPERATURE_CRITICAL
} PWRMgr_ThermalState_t;

class ThermalImpl : public hal::Thermal::IPlatform {
    using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

    // delete copy constructor and assignment operator
    ThermalImpl(const ThermalImpl&) = delete;
    ThermalImpl& operator=(const ThermalImpl&) = delete;

public:
    ThermalImpl() = default;
    ~ThermalImpl() = default;

    static int uint32_compare( const void* a, const void* b)
    {
        const uint32_t l = * ((const uint32_t*) a);
        const uint32_t r = * ((const uint32_t*) b);

        if ( l == r ) return 0;
        else if ( l < r ) return -1;
        else return 1;
    }

    ThermalTemperature conv(PWRMgr_ThermalState_t state) const
    {
        switch (state) {
        case PWRMGR_TEMPERATURE_NORMAL:
            return ThermalTemperature::THERMAL_TEMPERATURE_NORMAL;
        case PWRMGR_TEMPERATURE_HIGH:
            return ThermalTemperature::THERMAL_TEMPERATURE_HIGH;
        case PWRMGR_TEMPERATURE_CRITICAL:
            return ThermalTemperature::THERMAL_TEMPERATURE_CRITICAL;
        default:
            return ThermalTemperature::THERMAL_TEMPERATURE_UNKNOWN;
        }
    }

    PWRMgr_ThermalState_t conv(ThermalTemperature state) const
    {
        switch (state) {
        case ThermalTemperature::THERMAL_TEMPERATURE_NORMAL:
            return PWRMGR_TEMPERATURE_NORMAL;
        case ThermalTemperature::THERMAL_TEMPERATURE_HIGH:
            return PWRMGR_TEMPERATURE_HIGH;
        case ThermalTemperature::THERMAL_TEMPERATURE_CRITICAL:
            return PWRMGR_TEMPERATURE_CRITICAL;
        default:
            return PWRMGR_TEMPERATURE_NORMAL;
        }
    }

    virtual uint32_t GetTemperatureThresholds(float &tempHigh,float &tempCritical) const override
    {
        uint32_t result = WPEFramework::Core::ERROR_GENERAL;
        int high = 0;
        int critical = 0;

        mfrError_t response = mfrGetTempThresholds(&high, &critical);
        if(mfrERR_NONE == response)
        {
            result = WPEFramework::Core::ERROR_NONE;
            tempHigh = (float)high;
            tempCritical = (float)critical;
        }

        LOGINFO("High Temperature: %0.6f, [%d] and Critical Temperature: %0.6f, [%d], response: %u",tempHigh, high, tempCritical, critical,response);

        return result;
    }

    virtual uint32_t SetTemperatureThresholds(float tempHigh,float tempCritical) override
    {
        LOGINFO("Setting High Temperature Threshold as : %0.6f and Critical Temperature Threshold as : %0.6f",tempHigh, tempCritical);
        mfrError_t response = mfrSetTempThresholds(tempHigh,tempCritical);
        uint32_t result = (response == mfrERR_NONE) ?WPEFramework::Core::ERROR_NONE:WPEFramework::Core::ERROR_GENERAL;

        return result;
    }

    virtual uint32_t GetClockSpeed(uint32_t &speed) const override
    {
        FILE* fp = fopen ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
        if (nullptr == fp) {
            LOGERR("Unable to open '/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq' for writing");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        if(0 >= fscanf(fp, "%u", &speed)) {
            LOGERR("Unable to get the speed");
        }
        fclose(fp);  //CID:103784 - checked return

        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t SetClockSpeed(uint32_t speed) override
    {
        FILE* fp = fopen ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "w");
        uint32_t cur_speed = 0;
        LOGINFO("Setting clock speed to %d", speed );
        //Opening the clock speed adjusting

        if (nullptr == fp) {
            LOGERR("Unable to open '/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor' for writing");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        /* Switch to 'userspace' mode */
        fprintf(fp, "userspace");
        fclose(fp);

        fp = fopen ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed", "w");
        if (nullptr == fp) {
            LOGERR("Unable to open '/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed' for writing" );
            return WPEFramework::Core::ERROR_GENERAL;
        }

        /* Set the desired speed */
        fprintf(fp, "%u", speed);
        fclose(fp);

        if (GetClockSpeed(cur_speed) != WPEFramework::Core::ERROR_NONE ) {
            LOGERR("Failed to read current CPU speed");
            return WPEFramework::Core::ERROR_NONE;
        }

        LOGINFO("Clock speed set to [%d]", cur_speed );

        return (speed == cur_speed) ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_GENERAL;
    }

    virtual uint32_t DetemineClockSpeeds(uint32_t &cpu_rate_Normal, uint32_t &cpu_rate_Scaled, uint32_t &cpu_rate_Minimal) override
    {
        FILE * fp;
        uint32_t normal = 0;
        uint32_t scaled = 0;
        uint32_t minimal = 0;
        std::array<uint32_t,32> freqList;
        uint32_t numFreqs = 0;
        uint32_t i;

        fp = fopen ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies", "r");
        if (nullptr == fp) {
            LOGERR("Unable to open '/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies' for reading");
            return WPEFramework::Core::ERROR_GENERAL;
        }

        /* Determine available frequencies */
        while (numFreqs < freqList.size() && (fscanf(fp, "%u", &freqList[numFreqs]) == 1))
            numFreqs++;

        if (numFreqs<=0) {
            LOGERR("**ERROR** Unable to read sacaling frequencies!");
            fclose(fp);  //CID:158617 - Resource leak
            return WPEFramework::Core::ERROR_GENERAL;
        }

        /* Ensure frequencies are sorted */
        qsort( (void*)freqList.data(), numFreqs, sizeof(freqList[0]), ThermalImpl::uint32_compare );
        LOGERR("Scaling Frequency List:");
        for(i=0; i < numFreqs; i++) LOGINFO ("    %uhz", freqList[i]);

        /* Select normal, scaled and minimal from the list */
        minimal=freqList[0];
        scaled=freqList[numFreqs/2];
        normal=freqList[numFreqs-1];
        LOGINFO("Using -- Normal:%u Scaled:%u Minimal:%u", normal, scaled, minimal);

        fclose(fp);

        if (!cpu_rate_Normal)  cpu_rate_Normal = normal;
        if (!cpu_rate_Scaled)  cpu_rate_Scaled = scaled;
        if (!cpu_rate_Minimal) cpu_rate_Minimal = minimal;
        return WPEFramework::Core::ERROR_NONE;
    }

    virtual uint32_t GetTemperature(ThermalTemperature &curState, float &curTemperature, float &wifiTemperature) const override
    {
        mfrTemperatureState_t state = mfrTEMPERATURE_NORMAL;
        int temperatureValue        = 0;
        int wifiTempValue           = 0;
        uint32_t retValue           = WPEFramework::Core::ERROR_GENERAL;

        mfrError_t result = mfrGetTemperature(&state, &temperatureValue, &wifiTempValue);

#if 0
    /* Leave this debug code here commented out (or otherwise disabled by default). This is used in testing to allow manually controlling the returned temperature.
       This helps test functionality without actually having to heat up the box */
    {
        FILE *fp;
        state = (mfrTemperatureState_t)PWRMGR_TEMPERATURE_NORMAL;
        temperatureValue=50.0;
        wifiTempValue=50.0;
        result = mfrERR_NONE;

        fp = fopen ("/opt/force_temp.soc", "r");
        if (fp) {
            fscanf(fp, "%d", &temperatureValue);
            fclose(fp);
        }

        fp = fopen ("/opt/force_temp.wifi", "r");
        if (fp) {
            fscanf(fp, "%d", &wifiTempValue);
            fclose(fp);
        }

        fp = fopen ("/opt/force_temp.state", "r");
        if (fp) {
            fscanf(fp, "%d", &state);
            fclose(fp);
        }
    }
#endif

        if (result == mfrERR_NONE)
        {
            LOGINFO("Got MFR Temperatures SoC:%d Wifi:%d", temperatureValue, wifiTempValue);
            curState = conv((PWRMgr_ThermalState_t)state);
            curTemperature = temperatureValue;
            wifiTemperature = wifiTempValue;
            retValue = WPEFramework::Core::ERROR_NONE;
        }

        return retValue;
    }

};

