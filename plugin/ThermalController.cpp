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

#include "secure_wrapper.h"
#include "ThermalController.h"
#include "rfcapi.h"

ThermalController::ThermalController (INotification& parent, std::shared_ptr<IPlatform> platform)
    : _platform(std::move(platform))
    ,_therm_mutex(std::make_shared<std::mutex>())
    ,_grace_interval_mutex(std::make_shared<std::mutex>())
    , m_cur_Thermal_Level(ThermalTemperature::THERMAL_TEMPERATURE_NORMAL)
    , _parent(parent)
    , _stopThread(false)
{
    initializeThermalProtection();
    LOGINFO(">> CTOR <<");
}

ThermalController::~ThermalController()
{
    LOGINFO(">> DTOR");
    _stopThread = true;
    if ( nullptr != thermalThreadId )
    {
        if (thermalThreadId->joinable())
        {
            thermalThreadId->join();
        }
        delete thermalThreadId;
        thermalThreadId = nullptr;
    }
    LOGINFO("<< DTOR");
}

uint32_t ThermalController::GetThermalState(ThermalTemperature &curLevel, float &curTemperature) const
{
    _therm_mutex->lock();

    curLevel = m_cur_Thermal_Level;
    curTemperature = m_cur_Thermal_Value;

    _therm_mutex->unlock();
    LOGINFO("curTemperature: %d, curLevel %d", m_cur_Thermal_Value, int(m_cur_Thermal_Level));

    return WPEFramework::Core::ERROR_NONE;
}

uint32_t ThermalController::GetTemperatureThresholds(float &tempHigh,float &tempCritical) const
{
    uint32_t retCode = WPEFramework::Core::ERROR_GENERAL;

    retCode = platform().GetTemperatureThresholds(tempHigh, tempCritical);

    if (retCode == WPEFramework::Core::ERROR_NONE) {
        LOGINFO("Current thermal threshold : %f , %f ", tempHigh, tempCritical);
    } else {
        LOGERR("Failed to get thermal thresholds. Error code: %u", retCode);
    }
    return retCode;
}

uint32_t ThermalController::SetTemperatureThresholds(float tempHigh,float tempCritical)
{
    uint32_t retCode = WPEFramework::Core::ERROR_NONE;

    LOGINFO("Setting thermal threshold : %f , %f ", tempHigh,tempCritical);  //CID:127982 ,127475,103705 - Print_args

    retCode = platform().SetTemperatureThresholds(tempHigh,tempCritical);

    return retCode;
}

uint32_t ThermalController::GetOvertempGraceInterval(int &graceInterval) const
{
    uint32_t retCode = WPEFramework::Core::ERROR_NONE;

    graceInterval = rebootThreshold.graceInterval;
    retCode = WPEFramework::Core::ERROR_NONE;
    LOGINFO("Current over temparature grace interval : %d", graceInterval);

    return retCode;
}

uint32_t ThermalController::SetOvertempGraceInterval(int graceInterval)
{
    uint32_t retCode = WPEFramework::Core::ERROR_NONE;

    if(graceInterval >= 0 )
    {
        LOGINFO("Setting over temparature grace interval : %d", graceInterval);
        _grace_interval_mutex->lock();

        rebootThreshold.graceInterval = graceInterval;
        deepsleepThreshold.graceInterval = graceInterval;

        _grace_interval_mutex->unlock();

        retCode = WPEFramework::Core::ERROR_NONE;
    }
    else
    {
        retCode = WPEFramework::Core::ERROR_INVALID_PARAMETER;
    }

    return retCode;
}

void ThermalController::initializeThermalProtection()
{
    if (isThermalProtectionEnabled())
    {
        LOGINFO("Thermal Monitor [REBOOT] Enabled: %s", (rebootThreshold.graceInterval > 0) ? "TRUE" : "FALSE");
        if (rebootThreshold.graceInterval > 0) {
            LOGINFO("Thermal Monitor [REBOOT] Thresholds -- Critical:%d Concern:%d Safe:%d Grace Interval:%d",
                rebootThreshold.critical, rebootThreshold.concern, rebootThreshold.safe, rebootThreshold.graceInterval);
        }

        LOGINFO("Thermal Monitor [DEEP SLEEP] Enabled: %s", (deepsleepThreshold.graceInterval > 0) ? "TRUE" : "FALSE");
        if (deepsleepThreshold.graceInterval > 0) {
            LOGINFO("Thermal Monitor [DEEP SLEEP] Thresholds -- Critical:%d Concern:%d Safe:%d Grace Interval:%d",
                deepsleepThreshold.critical, deepsleepThreshold.concern, deepsleepThreshold.safe, deepsleepThreshold.graceInterval);
        }

#ifndef DISABLE_DECLOCKING_LOGIC
        LOGINFO("Thermal Monitor [DECLOCK] Enabled: %s", (declockThreshold.graceInterval > 0) ? "TRUE" : "FALSE");
        if (declockThreshold.graceInterval > 0) {
            LOGINFO("Thermal Monitor [DECLOCK] Thresholds -- Critical:%d Concern:%d Safe:%d Grace Interval:%d",
                declockThreshold.critical, declockThreshold.concern, declockThreshold.safe, declockThreshold.graceInterval);
            /* Discover clock rate for this system. Only discover if the rate is 0 */
            platform().DetemineClockSpeeds( PLAT_CPU_SPEED_NORMAL, PLAT_CPU_SPEED_SCALED, PLAT_CPU_SPEED_MINIMAL);
            LOGINFO("Thermal Monitor [DECLOCK] Frequencies -- Normal:%d Scaled:%d Minimal:%d", PLAT_CPU_SPEED_NORMAL, PLAT_CPU_SPEED_SCALED, PLAT_CPU_SPEED_MINIMAL);
            if (PLAT_CPU_SPEED_NORMAL == 0 || PLAT_CPU_SPEED_SCALED == 0 || PLAT_CPU_SPEED_MINIMAL == 0) {
                LOGINFO("Thermal Monitor [DECLOCK] **ERROR** At least one clock speed is 0. Disabling declocking!");
                declockThreshold.graceInterval = 0;
            }
	    cur_Cpu_Speed = PLAT_CPU_SPEED_NORMAL;
	    LOGINFO("Thermal Monitor [DECLOCK] Default Frequency during Bootup [%d]", cur_Cpu_Speed);
        }
#endif

        if(WPEFramework::Core::ERROR_NONE != platform().SetTemperatureThresholds(declockThreshold.concern, declockThreshold.critical))
        {
            LOGINFO("*****Critical*** Fails to set temperature thresholds.. ");
        }

        thermalThreadId = new std::thread(&ThermalController::pollThermalLevels, this);

        if (nullptr == thermalThreadId )
        {
            LOGINFO("*****Critical*** Fails to Create temperature monitor thread ");
        }
    }
    else
    {
        LOGINFO("Thermal protection is disabled from RFC ");
    }
}

bool ThermalController::isThermalProtectionEnabled()
{
    if (!read_config_param)
    {
        if (updateRFCStatus())
        {
            read_ConfigProps();
        }

        read_config_param= TRUE;
    }

    return isFeatureEnabled;
}

void ThermalController::logThermalShutdownReason()
{
    //command is echo THERMAL_SHUTDOWN_REASON > STANDBY_REASON_FILE
    //int cmdSize =  strlen(THERMAL_SHUTDOWN_REASON) + strlen(STANDBY_REASON_FILE) + 10;
    //char logCommand[cmdSize]= {'0'};
    //snprintf(logCommand,cmdSize,"echo %s > %s",THERMAL_SHUTDOWN_REASON, STANDBY_REASON_FILE);
    v_secure_system("echo %s > %s",THERMAL_SHUTDOWN_REASON, STANDBY_REASON_FILE);
}

void ThermalController::rebootIfNeeded()
{
    static struct timeval monitorTime;
    struct timeval tv;
    long difftime = 0;

    if (rebootThreshold.graceInterval == 0) {
        /* This check is disable */
        return;
    }

    if (m_cur_Thermal_Value >= rebootThreshold.critical)
    {
        LOGINFO("Rebooting is being forced!");
        v_secure_system("/rebootNow.sh -s Power_Thermmgr -o 'Rebooting the box due to stb temperature greater than rebootThreshold critical...'");
    }
    else if (!_rebootZone && m_cur_Thermal_Value >= rebootThreshold.concern)
    {
        LOGINFO("Temperature threshold crossed (%d) ENTERING reboot zone", m_cur_Thermal_Value );
        gettimeofday(&monitorTime, NULL);
        _rebootZone = true;
    }
    else if (_rebootZone && m_cur_Thermal_Value < rebootThreshold.safe) {
        LOGINFO("Temperature threshold crossed (%d) EXITING reboot zone", m_cur_Thermal_Value );
        _rebootZone = false;
    }

    if (_rebootZone) {
        /* We are in the deep sleep zone. After 'rebootThreshold graceInterval' passes we will go to deep sleep */
        gettimeofday(&tv, NULL);
        difftime = tv.tv_sec - monitorTime.tv_sec;

        if (difftime >= rebootThreshold.graceInterval)
        {
            LOGINFO("Rebooting since the temperature is still above critical level after %d seconds !! :  ",
            rebootThreshold.graceInterval);
            v_secure_system("/rebootNow.sh -s Power_Thermmgr -o 'Rebooting the box as the stb temperature is still above critical level after 20 seconds...'");
        }
        else {
            LOGINFO("Still in the reboot zone! Will go for reboot in %lu seconds unless the temperature falls below %u!", rebootThreshold.graceInterval-difftime, rebootThreshold.safe );
        }
    }
}


void ThermalController::deepSleepIfNeeded()
{
    struct timeval tv;
    long difftime = 0;
    static struct timeval monitorTime;
    static bool deepSleepZone = false;

    if (deepsleepThreshold.graceInterval == 0) {
        /* This check is disable */
        return;
    }

    if (m_cur_Thermal_Value >= deepsleepThreshold.critical)
    {
        logThermalShutdownReason();
        LOGINFO("Going to deepsleep since the temperature is above %d", deepsleepThreshold.critical);
        _parent.onDeepSleepForThermalChange();
    }
    else if (!deepSleepZone && m_cur_Thermal_Value >= deepsleepThreshold.concern)
    {
        LOGINFO("Temperature threshold crossed (%d) ENTERING deepsleep zone", m_cur_Thermal_Value );
        gettimeofday(&monitorTime, NULL);
        deepSleepZone = true;
    }
    else if (deepSleepZone && m_cur_Thermal_Value < deepsleepThreshold.safe) {
        LOGINFO("Temperature threshold crossed (%d) EXITING deepsleep zone", m_cur_Thermal_Value );
        deepSleepZone = false;
    }

    if (deepSleepZone) {
        /* We are in the deep sleep zone. After 'deepsleepThreshold graceInterval' passes we will go to deep sleep */
        gettimeofday(&tv, NULL);
        difftime = tv.tv_sec - monitorTime.tv_sec;

        if (difftime >= deepsleepThreshold.graceInterval)
        {
            logThermalShutdownReason();
            LOGINFO("Going to deepsleep since the temperature reached %d and stayed above %d for %d seconds",
                deepsleepThreshold.concern, deepsleepThreshold.safe, deepsleepThreshold.graceInterval);
            _parent.onDeepSleepForThermalChange();
        }
        else {
            LOGINFO("Still in the deep sleep zone! Entering deep sleep in %lu seconds unless the temperature falls below %u!", deepsleepThreshold.graceInterval-difftime, deepsleepThreshold.safe );
        }
    }
}


void ThermalController::declockIfNeeded()
{
#ifndef DISABLE_DECLOCKING_LOGIC
    struct timeval tv;
    long difftime = 0;
    static struct timeval monitorTime;

    if (declockThreshold.graceInterval == 0) {
        /* This check is disable */
        return;
    }

    if (m_cur_Thermal_Value >= declockThreshold.critical)
    {
        if (cur_Cpu_Speed != PLAT_CPU_SPEED_MINIMAL) {
            LOGINFO("Temperature threshold crossed (%d) !!!! Switching to minimal mode !!", m_cur_Thermal_Value);
            if ( WPEFramework::Core::ERROR_NONE != platform().SetClockSpeed(PLAT_CPU_SPEED_MINIMAL))
            {
                LOGERR("SetClockSpeed Failed");
            }

            cur_Cpu_Speed = PLAT_CPU_SPEED_MINIMAL;
        }
        /* Always reset the monitor time */
        gettimeofday(&monitorTime, NULL);
    }
    else if (m_cur_Thermal_Value >= declockThreshold.concern)
    {
        if (cur_Cpu_Speed == PLAT_CPU_SPEED_NORMAL) {
            /* Switching from normal to scaled */
            LOGINFO("CPU Scaling threshold crossed (%d) !!!! Switching to scaled mode (%d) from normal mode(%d) !!",
                m_cur_Thermal_Value,PLAT_CPU_SPEED_SCALED,PLAT_CPU_SPEED_NORMAL );
            if ( WPEFramework::Core::ERROR_NONE != platform().SetClockSpeed(PLAT_CPU_SPEED_SCALED))
            {
                LOGERR("SetClockSpeed Failed");
            }
            cur_Cpu_Speed = PLAT_CPU_SPEED_SCALED;
        }
        gettimeofday(&monitorTime, NULL);
    }
    else if (m_cur_Thermal_Value > declockThreshold.safe)
        //Between declockThreshold concern and declockThreshold safe
    {
        if (cur_Cpu_Speed == PLAT_CPU_SPEED_MINIMAL) {
            /* We are already declocked. If we stay in this state for 'declockThreshold graceInterval' we will change to */
            gettimeofday(&tv, NULL);
            difftime = tv.tv_sec - monitorTime.tv_sec;

            if (difftime >= declockThreshold.graceInterval)
            {
                LOGINFO("CPU Scaling threshold crossed (%d) !!!! Switching to scaled mode (%d) from minimal mode(%d) !!",
                    m_cur_Thermal_Value,PLAT_CPU_SPEED_SCALED,PLAT_CPU_SPEED_MINIMAL );
                if ( WPEFramework::Core::ERROR_NONE != platform().SetClockSpeed(PLAT_CPU_SPEED_SCALED))
                {
                    LOGERR("SetClockSpeed Failed");
                }
                cur_Cpu_Speed = PLAT_CPU_SPEED_SCALED;
                gettimeofday(&monitorTime, NULL);
            }
        }
        else {
            /*Still in the correct mode. Always reset the monitor time */
            gettimeofday(&monitorTime, NULL);
        }
    }
    else //m_cur_Thermal_Value <= declockThreshold.safe
    {
        if (cur_Cpu_Speed != PLAT_CPU_SPEED_NORMAL) {
            /* We are in the declock zone. After 'declockThreshold graceInterval' passes we will go back to normal mode */
            gettimeofday(&tv, NULL);
            difftime = tv.tv_sec - monitorTime.tv_sec;

            if (difftime >= declockThreshold.graceInterval)
            {
                LOGINFO(" CPU rescaling threshold crossed (%d) !!!! Switching to normal mode !!",
                    m_cur_Thermal_Value );
                if ( WPEFramework::Core::ERROR_NONE != platform().SetClockSpeed(PLAT_CPU_SPEED_NORMAL))
                {
                    LOGERR("SetClockSpeed Failed");
                }
                cur_Cpu_Speed = PLAT_CPU_SPEED_NORMAL;
            }
        }

    }
#endif

}

//Thread entry function to monitor thermal levels of the device.
void ThermalController::pollThermalLevels()
{
    ThermalTemperature state;
    float current_Temp     = 0;
    float current_WifiTemp = 0;

    unsigned int pollCount = 0;
    int thermalLogInterval = 300/thermal_poll_interval;

    //PACEXI5-2127 //print current temperature levels every 15 mins
    int fifteenMinInterval = 900/thermal_poll_interval; //15 *60 seconds/interval


    LOGINFO(">> Start monitoring temeperature every %d seconds log interval: %d", thermal_poll_interval, thermalLogInterval);

    while(!_stopThread)
    {
        _therm_mutex->lock();
        uint32_t result = platform().GetTemperature(state, current_Temp, current_WifiTemp);//m_cur_Thermal_Level
        if(WPEFramework::Core::ERROR_NONE == result)
        {
            if(m_cur_Thermal_Level != state)//State changed, need to broadcast
            {
                LOGINFO("Temeperature levels changed %s -> %s", str(m_cur_Thermal_Level), str(state));

                _parent.onThermalTemperatureChanged(m_cur_Thermal_Level,state,current_Temp);

                m_cur_Thermal_Level = state;
            }
            //PACEXI5-2127 - BEGIN
            if(0 == (pollCount % fifteenMinInterval))
            {
                LOGINFO("CURRENT_CPU_SCALE_MODE:%s ",
                    (cur_Cpu_Speed == PLAT_CPU_SPEED_NORMAL)?"Normal":
                    ((cur_Cpu_Speed == PLAT_CPU_SPEED_SCALED)?"Scaled":"Minimal"));
            }
            //PACEXI5-2127 - END
            if (0 == pollCount % thermalLogInterval)
            {
                LOGINFO("Current Temperature %d", (int)current_Temp );
            }
            m_cur_Thermal_Value = (int)current_Temp;

            if (_stopThread) 
            {
                LOGINFO("pollThermalLevels thread is signalled to be destroyed");
                break;
            }
        }

        _therm_mutex->unlock();

        if(WPEFramework::Core::ERROR_NONE == result)
        {
            _grace_interval_mutex->lock();
            /* Check if we should enter deepsleep based on the current temperature */
            deepSleepIfNeeded();

            /* Check if we should reboot based on the current temperature */
            rebootIfNeeded();
            _grace_interval_mutex->unlock();

            /* Check if we should declock based on the current temperature */
            declockIfNeeded();
        }
        else
        {
            LOGINFO("Warning - Failed to retrieve temperature from OEM");
        }
        sleep(thermal_poll_interval);
        pollCount++;
    }
    LOGINFO(">> Stop monitoring temeperature");
}

const char* ThermalController::str(ThermalTemperature mode)
{
    switch (mode) {
    case ThermalTemperature::THERMAL_TEMPERATURE_NORMAL:
        return "NORMAL";
    case ThermalTemperature::THERMAL_TEMPERATURE_HIGH:
        return "HIGH";
    case ThermalTemperature::THERMAL_TEMPERATURE_CRITICAL:
        return "CRITICAL";
    case ThermalTemperature::THERMAL_TEMPERATURE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
    return "";
}

bool ThermalController::updateRFCStatus()
{
    bool result = false;
    RFC_ParamData_t param = {{0}};

    isFeatureEnabled = TRUE;

    WDMP_STATUS status = getRFCParameter(THERMAL_PROTECTION_GROUP, "RFC_ENABLE_ThermalProtection", &param);

    if (status == WDMP_SUCCESS)
    {
        LOGINFO("Key: RFC_ENABLE_ThermalProtection, Value: %s", param.value);

        if (0 == strncasecmp(param.value, "false", 5))
        {
            isFeatureEnabled = FALSE;
        }
        else
        {
            result = true;
        }
    }
    else
    {
        LOGINFO("Key: RFC_ENABLE_ThermalProtection is not configured, Status %d  ", status);
    }

    LOGINFO("result: %d", result);

    return result;
}

char* ThermalController::read_ConfigProperty(const char* key)
{
    char *value = nullptr;
    uint32_t dataLen = 0;
    RFC_ParamData_t param = {{0}};
    // RFC parameter storage
    const uint32_t MAX_THERMAL_RFC =  16;
    static char valueBuf[MAX_THERMAL_RFC] = { 0 };

    WDMP_STATUS status = getRFCParameter(THERMAL_PROTECTION_GROUP, key, &param);

    valueBuf[0] = '\0';

    if (status == WDMP_SUCCESS)
    {
        dataLen = strlen(param.value);
        if (dataLen > MAX_THERMAL_RFC-1)
        {
            dataLen = MAX_THERMAL_RFC-1;
        }

        if ( (param.value[0] == '"') && (param.value[dataLen-1] == '"'))
        {
            // remove quotes arround data
            strncpy (valueBuf, &param.value[1], dataLen-2);
            valueBuf[dataLen-2] = '\0';
        }
        else
        {
            strncpy (valueBuf, param.value, MAX_THERMAL_RFC-1);
            valueBuf[MAX_THERMAL_RFC-1] = '\0';
        }

        LOGINFO("name = %s, type = %d, value = %s, status = %d", param.name, param.type, param.value, status);
    }
    else
    {
        LOGINFO("Key: property %s is not configured, Status %d  ", key, status);
    }

    if (valueBuf[0])
    {
        value = valueBuf;
    }
    else
    {
        LOGINFO("Unable to find key %s in group %s", key, THERMAL_PROTECTION_GROUP);
    }

    return value;
}

bool ThermalController::read_ConfigProps()
{
    char *value = nullptr;

    //Now override with RFC values if any
    value = read_ConfigProperty("RFC_DATA_ThermalProtection_REBOOT_CRITICAL_THRESHOLD");
    if (NULL != value)
    {
        rebootThreshold.critical = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_REBOOT_CONCERN_THRESHOLD");
    if (NULL != value)
    {
        rebootThreshold.concern = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_REBOOT_SAFE_THRESHOLD");
    if (NULL != value)
    {
        rebootThreshold.safe = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_REBOOT_GRACE_INTERVAL");
    if (NULL != value)
    {
        rebootThreshold.graceInterval = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DECLOCK_CRITICAL_THRESHOLD");
    if (NULL != value)
    {
        declockThreshold.critical = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DECLOCK_CONCERN_THRESHOLD");
    if (NULL != value)
    {
        declockThreshold.concern = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DECLOCK_SAFE_THRESHOLD");
    if (NULL != value)
    {
        declockThreshold.safe = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DECLOCK_GRACE_INTERVAL");
    if (NULL != value)
    {
        declockThreshold.graceInterval = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DEEPSLEEP_CRITICAL_THRESHOLD");
    if (NULL != value)
    {
        deepsleepThreshold.critical =atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DEEPSLEEP_CONCERN_THRESHOLD");
    if (NULL != value)
    {
        deepsleepThreshold.concern = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DEEPSLEEP_SAFE_THRESHOLD");
    if (NULL != value)
    {
        deepsleepThreshold.safe = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_DEEPSLEEP_GRACE_INTERVAL");
    if (NULL != value)
    {
        deepsleepThreshold.graceInterval = atoi(value);
    }


    value = read_ConfigProperty("RFC_DATA_ThermalProtection_POLL_INTERVAL");
    if (NULL != value)
    {
        thermal_poll_interval = atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_PLAT_CPU_SPEED_NORMAL");
    if (NULL != value)
    {
        PLAT_CPU_SPEED_NORMAL =atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_PLAT_CPU_SPEED_SCALED");
    if (NULL != value)
    {
        PLAT_CPU_SPEED_SCALED =atoi(value);
    }

    value = read_ConfigProperty("RFC_DATA_ThermalProtection_PLAT_CPU_SPEED_MINIMAL");
    if (NULL != value)
    {
        PLAT_CPU_SPEED_MINIMAL =atoi(value);
    }

    return true;
}
