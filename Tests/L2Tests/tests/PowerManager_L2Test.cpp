/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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
**/

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "L2Tests.h"
#include "L2TestsMock.h"
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <interfaces/IPowerManager.h>
#include "deepSleepMgr.h"
#include "PowerManagerMock.h"
#include "PowerManagerHalMock.h"
#include "MfrMock.h"

#define JSON_TIMEOUT   (1000)
#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);
#define SYSTEM_CALLSIGN  _T("org.rdk.PowerManager.1")
#define L2TEST_CALLSIGN _T("L2tests.1")
#define KED_FP_POWER 10

#define POWERMANAGER_MOCK (*p_powerManagerHalMock)

using ::testing::NiceMock;
using namespace WPEFramework;
using testing::StrictMock;
using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;
using WakeupReason = WPEFramework::Exchange::IPowerManager::WakeupReason;

typedef enum : uint32_t {
    POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED = 0x00000001,
    POWERMANAGERL2TEST_THERMALSTATE_CHANGED=0x00000002,
    POWERMANAGERL2TEST_LOGUPLOADSTATE_CHANGED=0x00000004,
    POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE = 0x00000005,
    POWERMANAGERL2TEST_EVENT_REBOOTING = 0x00000006,
    POWERMANAGERL2TEST_NETWORK_STANDBYMODECHANGED = 0x00000007,
    POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT = 0x00000008,
    POWERMANAGERL2TEST_NW_STANDBYMODECHANGED = 0x00000009,
    POWERMANAGERL2TEST_STATE_INVALID = 0x00000000
}PowerManagerL2test_async_events_t;

class PwrMgr_Notification : public Exchange::IPowerManager::IRebootNotification,
                             public Exchange::IPowerManager::IModePreChangeNotification,
                             public Exchange::IPowerManager::IModeChangedNotification,
                             public Exchange::IPowerManager::IDeepSleepTimeoutNotification,
                             public Exchange::IPowerManager::INetworkStandbyModeChangedNotification,
                             public Exchange::IPowerManager::IThermalModeChangedNotification {
    private:
        /** @brief Mutex */
        std::mutex m_mutex;

        /** @brief Condition variable */
        std::condition_variable m_condition_variable;

        /** @brief Event signalled flag */
        uint32_t m_event_signalled;

        BEGIN_INTERFACE_MAP(PwrMgr_Notification)
        INTERFACE_ENTRY(Exchange::IPowerManager::IRebootNotification)
        INTERFACE_ENTRY(Exchange::IPowerManager::IModePreChangeNotification)
        INTERFACE_ENTRY(Exchange::IPowerManager::IModeChangedNotification)
        INTERFACE_ENTRY(Exchange::IPowerManager::IDeepSleepTimeoutNotification)
        INTERFACE_ENTRY(Exchange::IPowerManager::INetworkStandbyModeChangedNotification)
        INTERFACE_ENTRY(Exchange::IPowerManager::IThermalModeChangedNotification)
        END_INTERFACE_MAP

    public:
        PwrMgr_Notification(){}
        ~PwrMgr_Notification(){}

       template <typename T>
       T* baseInterface()
       {
           static_assert(std::is_base_of<T, PwrMgr_Notification>(), "base type mismatch");
           return static_cast<T*>(this);
       }

        void OnPowerModeChanged(const PowerState currentState, const PowerState newState) override
        {
            TEST_LOG("OnPowerModeChanged event triggered ***\n");
            std::unique_lock<std::mutex> lock(m_mutex);

            TEST_LOG("OnPowerModeChanged currentState: %u, newState: %u\n", currentState, newState);
            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED;
            m_condition_variable.notify_one();
        }

        void OnPowerModePreChange(const PowerState currentState, const PowerState newState, const int trxnId, const int stateChangeAfter) override
        {
            TEST_LOG("OnPowerModePreChange event triggered ***\n");
            std::unique_lock<std::mutex> lock(m_mutex);

            TEST_LOG("OnPowerModePreChange currentState: %u, newState: %u\n", currentState, newState);

            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE;
            m_condition_variable.notify_one();
        }

        void OnDeepSleepTimeout(const int wakeupTimeout) override
        {
            TEST_LOG("OnDeepSleepTimeout: wakeupTimeout %d\n", wakeupTimeout);
            std::unique_lock<std::mutex> lock(m_mutex);

            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT;
            m_condition_variable.notify_one();
        }

        void OnNetworkStandbyModeChanged(const bool enabled) override
        {
            TEST_LOG("OnNetworkStandbyModeChanged: enabled %d\n", enabled);
            std::unique_lock<std::mutex> lock(m_mutex);

            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_NW_STANDBYMODECHANGED;
            m_condition_variable.notify_one();
        }

        void OnThermalModeChanged(const ThermalTemperature currentThermalLevel, const ThermalTemperature newThermalLevel, const float currentTemperature) override
        {
            TEST_LOG("OnThermalModeChanged event triggered ***\n");
            std::unique_lock<std::mutex> lock(m_mutex);
            LOGINFO("OnThermalModeChanged received: currentThermalLevel %u, newThermalLevel %u, currentTemperature %f\n", currentThermalLevel, newThermalLevel, currentTemperature);

            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_THERMALSTATE_CHANGED;
            m_condition_variable.notify_one();
        }

        void OnRebootBegin(const string &rebootReasonCustom, const string &rebootReasonOther, const string &rebootRequestor) override
        {
            TEST_LOG("OnRebootBegin event triggered ***\n");
            std::unique_lock<std::mutex> lock(m_mutex);

            LOGINFO("OnRebootBegin: rebootReasonCustom %s, rebootReasonOther %s, rebootRequestor %s\n", rebootReasonCustom.c_str(), rebootReasonOther.c_str(), rebootRequestor.c_str());
            /* Notify the requester thread. */
            m_event_signalled |= POWERMANAGERL2TEST_EVENT_REBOOTING;
            m_condition_variable.notify_one();
        }

        uint32_t WaitForRequestStatus(uint32_t timeout_ms, PowerManagerL2test_async_events_t expected_status)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto now = std::chrono::system_clock::now();
            std::chrono::milliseconds timeout(timeout_ms);
            uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

            while (!(expected_status & m_event_signalled))
            {
              if (m_condition_variable.wait_until(lock, now + timeout) == std::cv_status::timeout)
              {
                 TEST_LOG("Timeout waiting for request status event");
                 break;
              }
            }

            signalled = m_event_signalled;
            // Clear only the expected flags that were waited for, not all flags
            m_event_signalled &= ~expected_status;
            return signalled;
        }

};

/* Systemservice L2 test class declaration */
class PowerManager_L2Test : public L2TestMocks {

    public:

    PowerManager_L2Test();
    virtual ~PowerManager_L2Test() override;

    public:
        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnPowerModeChanged(const PowerState currentState, const PowerState newState);

        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnPowerModePreChange(const PowerState currentState, const PowerState newState, const int trxnId, const int stateChangeAfter);

        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnDeepSleepTimeout(const int wakeupTimeout);

        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnNetworkStandbyModeChanged(const bool enabled);

        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnThermalModeChanged(const ThermalTemperature currentThermalLevel, const ThermalTemperature newThermalLevel, const float currentTemperature);

        /**
         * @brief called when System state
         * changed notification received from IARM
         */
        void OnRebootBegin(const string &rebootReasonCustom, const string &rebootReasonOther, const string &rebootRequestor);

        /**
         * @brief waits for various status change on asynchronous calls
         */
      uint32_t WaitForRequestStatus(uint32_t timeout_ms,PowerManagerL2test_async_events_t expected_status);
      void Test_PowerStateChange( Exchange::IPowerManager* PowerManagerPlugin);
      void Test_TemperatureThresholds( Exchange::IPowerManager* PowerManagerPlugin);
      void Test_OvertempGraceInterval( Exchange::IPowerManager* PowerManagerPlugin);
      void Test_WakeupSrcConfig( Exchange::IPowerManager* PowerManagerPlugin);
      void Test_PerformReboot( Exchange::IPowerManager* PowerManagerPlugin);
      void Test_NetworkStandbyMode( Exchange::IPowerManager* PowerManagerPlugin);
      Core::Sink<PwrMgr_Notification> mNotification;

    private:
        /** @brief Mutex */
        std::mutex m_mutex;

        /** @brief Condition variable */
        std::condition_variable m_condition_variable;

        /** @brief Event signalled flag */
        uint32_t m_event_signalled;
};


/**
 * @brief Constructor for PowerManagers L2 test class
 */
PowerManager_L2Test::PowerManager_L2Test()
        : L2TestMocks()
        , mNotification()
        , m_mutex()
        , m_condition_variable()
        , m_event_signalled(POWERMANAGERL2TEST_STATE_INVALID)
{
        uint32_t status = Core::ERROR_GENERAL;

         EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_INIT())
         .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

         EXPECT_CALL(POWERMANAGER_MOCK, PLAT_INIT())
         .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

         EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
         .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

         ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
         .WillByDefault(::testing::Invoke(
             [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                 if (strcmp("RFC_DATA_ThermalProtection_POLL_INTERVAL", pcParameterName) == 0) {
                     strcpy(pstParamData->value, "2");
                     return WDMP_SUCCESS;
                 } else if (strcmp("RFC_ENABLE_ThermalProtection", pcParameterName) == 0) {
                     strcpy(pstParamData->value, "true");
                     return WDMP_SUCCESS;
                 } else if (strcmp("RFC_DATA_ThermalProtection_DEEPSLEEP_GRACE_INTERVAL", pcParameterName) == 0) {
                     strcpy(pstParamData->value, "6");
                     return WDMP_SUCCESS;
                 } else {
                     /* The default threshold values will assign, if RFC call failed */
                     return WDMP_FAILURE;
                 }
             }));

        EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
         .WillOnce(::testing::Invoke(
             [](int high, int critical) {
                 EXPECT_EQ(high, 100);
                 EXPECT_EQ(critical, 110);
                 return mfrERR_NONE;
             }));

        EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_GetPowerState(::testing::_))
         .WillRepeatedly(::testing::Invoke(
             [](PWRMgr_PowerState_t* powerState) {
                 *powerState = PWRMGR_POWERSTATE_OFF; // by default over boot up, return PowerState OFF
                 return PWRMGR_SUCCESS;
             }));

        EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetPowerState(::testing::_))
         .WillRepeatedly(::testing::Invoke(
             [](PWRMgr_PowerState_t powerState) {
                 // All tests are run without settings file
                 // so default expected power state is ON
                 powerState = (PWRMgr_PowerState_t)PowerState::POWER_STATE_ON;
                 return PWRMGR_SUCCESS;
             }));

          ON_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
              .WillByDefault(::testing::Invoke(
                  [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                      *curTemperature  = 60; // safe temperature
                      *curState        = (mfrTemperatureState_t)0;
                      *wifiTemperature = 25;
                      return mfrERR_NONE;
                          }));

         /* Activate plugin in constructor */
         status = ActivateService("org.rdk.PowerManager");
         EXPECT_EQ(Core::ERROR_NONE, status);

}

/**
 * @brief Destructor for PowerManagers L2 test class
 */
PowerManager_L2Test::~PowerManager_L2Test()
{
    uint32_t status = Core::ERROR_GENERAL;

    EXPECT_CALL(POWERMANAGER_MOCK, PLAT_TERM())
        .WillOnce(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_TERM())
        .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    status = DeactivateService("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

void PowerManager_L2Test::OnPowerModeChanged(const PowerState currentState, const PowerState newState)
{
    TEST_LOG("OnPowerModeChanged event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    TEST_LOG("OnPowerModeChanged currentState: %u, newState: %u\n", currentState, newState);
    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED;
    m_condition_variable.notify_one();
}

void PowerManager_L2Test::OnPowerModePreChange(const PowerState currentState, const PowerState newState, const int trxnId, const int stateChangeAfter)
{
    TEST_LOG("OnPowerModePreChange event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    TEST_LOG("OnPowerModePreChange currentState: %u, newState: %u\n", currentState, newState);
    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE;
    m_condition_variable.notify_one();
}

void PowerManager_L2Test::OnDeepSleepTimeout(const int wakeupTimeout)
{
    TEST_LOG("OnDeepSleepTimeout: wakeupTimeout %d\n", wakeupTimeout);
    std::unique_lock<std::mutex> lock(m_mutex);

    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT;
    m_condition_variable.notify_one();
}

void PowerManager_L2Test::OnNetworkStandbyModeChanged(const bool enabled)
{
    TEST_LOG("OnNetworkStandbyModeChanged: enabled %d\n", enabled);
    std::unique_lock<std::mutex> lock(m_mutex);

    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_NW_STANDBYMODECHANGED;
    m_condition_variable.notify_one();
}

void PowerManager_L2Test::OnThermalModeChanged(const ThermalTemperature currentThermalLevel, const ThermalTemperature newThermalLevel, const float currentTemperature)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    LOGINFO("OnThermalModeChanged received: currentThermalLevel %u, newThermalLevel %u, currentTemperature %f\n", currentThermalLevel, newThermalLevel, currentTemperature);

    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_THERMALSTATE_CHANGED;
    m_condition_variable.notify_one();
}

void PowerManager_L2Test::OnRebootBegin(const string &rebootReasonCustom, const string &rebootReasonOther, const string &rebootRequestor)
{
    TEST_LOG("OnRebootBegin event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    LOGINFO("OnRebootBegin: rebootReasonCustom %s, rebootReasonOther %s, rebootRequestor %s\n", rebootReasonCustom.c_str(), rebootReasonOther.c_str(), rebootRequestor.c_str());
    /* Notify the requester thread. */
    m_event_signalled |= POWERMANAGERL2TEST_EVENT_REBOOTING;
    m_condition_variable.notify_one();
}
/**
 * @brief waits for various status change on asynchronous calls
 *
 * @param[in] timeout_ms timeout for waiting
 */
uint32_t PowerManager_L2Test::WaitForRequestStatus(uint32_t timeout_ms,PowerManagerL2test_async_events_t expected_status)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto now = std::chrono::system_clock::now();
    std::chrono::milliseconds timeout(timeout_ms);
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

   while (!(expected_status & m_event_signalled))
   {
      if (m_condition_variable.wait_until(lock, now + timeout) == std::cv_status::timeout)
      {
         TEST_LOG("Timeout waiting for request status event");
         break;
      }
   }

    signalled = m_event_signalled;
    m_event_signalled = POWERMANAGERL2TEST_STATE_INVALID;

    return signalled;
}


/**
 * @brief Compare two request status objects
 *
 * @param[in] data Expected value
 * @return true if the argument and data match, false otherwise
 */
MATCHER_P(MatchRequestStatus, data, "")
{
    bool match = true;
    std::string expected;
    std::string actual;

    data.ToString(expected);
    arg.ToString(actual);
    TEST_LOG(" rec = %s, arg = %s",expected.c_str(),actual.c_str());
    EXPECT_STREQ(expected.c_str(),actual.c_str());

    return match;
}

/* COM-RPC tests */
void PowerManager_L2Test::Test_OvertempGraceInterval( Exchange::IPowerManager* PowerManagerPlugin )
{
    uint32_t status = Core::ERROR_GENERAL;

    int graceInterval = 100;
    TEST_LOG("\n##############  Running Test_OvertempGraceInterval Test ###################\n");

    status = PowerManagerPlugin->SetOvertempGraceInterval(graceInterval);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    int graceInterval1 = 0;

    status = PowerManagerPlugin->GetOvertempGraceInterval(graceInterval1);
    EXPECT_EQ(graceInterval1, graceInterval);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/* COM-RPC tests */
void PowerManager_L2Test::Test_TemperatureThresholds( Exchange::IPowerManager* PowerManagerPlugin )
{
    uint32_t status = Core::ERROR_GENERAL;
    float high = 100, critical = 110;
    TEST_LOG("\n################## Running Test_TemperatureThresholds Test #################\n");

    EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
     .WillOnce(::testing::Invoke(
         [](int high, int critical) {
             EXPECT_EQ(high, 100);
             EXPECT_EQ(critical, 110);
             return mfrERR_NONE;
    }));

    status = PowerManagerPlugin->SetTemperatureThresholds(high, critical);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    float high1 = 0, critical1 = 0;

    EXPECT_CALL(*p_mfrMock, mfrGetTempThresholds(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](int* high, int* critical) {
                *high     = 100;
                *critical = 110;
                return mfrERR_NONE;
    }));

    status = PowerManagerPlugin->GetTemperatureThresholds(high1, critical1);
    EXPECT_EQ(high1, high);
    EXPECT_EQ(critical1, critical);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    float temperature = 0.0;
    status = PowerManagerPlugin->GetThermalState(temperature);
    EXPECT_EQ(temperature, 60);
    EXPECT_EQ(status,Core::ERROR_NONE);

    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

}

/* COM-RPC tests */
void PowerManager_L2Test::Test_PowerStateChange( Exchange::IPowerManager* PowerManagerPlugin )
{
    std::condition_variable cond_var;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;
    uint32_t status = Core::ERROR_GENERAL;

    PowerState currentState = PowerState::POWER_STATE_STANDBY;
    const string  standbyReason = "";
    int keyCode = KED_FP_POWER;
    TEST_LOG("\n####################### Running Test_PowerStateChange Test #########################\n");

    status = PowerManagerPlugin->SetPowerState(keyCode, currentState, standbyReason);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT, POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);
    //EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);

    PowerState currentState1 = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState1 = PowerState::POWER_STATE_UNKNOWN;

    status = PowerManagerPlugin->GetPowerState(currentState1, prevState1);
    EXPECT_EQ(currentState1, currentState);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

using IWakeupSourceConfigIterator  = WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator;
using WakeupSourceConfigIteratorImpl = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IWakeupSourceConfigIterator>>;
using WakeupSrcConfig           = WPEFramework::Exchange::IPowerManager::WakeupSourceConfig;
using WakeupSrcType             = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

/* COM-RPC tests */
void PowerManager_L2Test::Test_WakeupSrcConfig( Exchange::IPowerManager* PowerManagerPlugin )
{
    uint32_t status = Core::ERROR_GENERAL;

    TEST_LOG("\n################### Running Test_WakeupSrcConfig Test ########################\n");

    EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                return PWRMGR_SUCCESS;
            }));

    std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_VOICE, true}};
    auto wakeupsrcsSetIter = WakeupSourceConfigIteratorImpl::Create<IWakeupSourceConfigIterator>(configs);

    status = PowerManagerPlugin->SetWakeupSourceConfig(wakeupsrcsSetIter);
    EXPECT_EQ(status,Core::ERROR_NONE);

    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_GetWakeupSrc(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool* enabled) {
                if(wakeupSrc == PWRMGR_WAKEUPSRC_VOICE) {
                    *enabled = true;
                    return PWRMGR_SUCCESS;
                }
                else {
                    *enabled = false;
                    return PWRMGR_GET_FAILURE;
                }
            }));

    WPEFramework::RPC::IIteratorType<WakeupSrcConfig, WPEFramework::Exchange::IDS::ID_POWER_MANAGER_WAKEUP_SRC_ITERATOR>* wakeupsrcsGetIter;

    status = PowerManagerPlugin->GetWakeupSourceConfig(wakeupsrcsGetIter);
    EXPECT_EQ(status, Core::ERROR_NONE);

    bool ok = false;

    WPEFramework::Exchange::IPowerManager::WakeupSourceConfig config{WakeupSrcType::WAKEUP_SRC_UNKNOWN, false};
    while (wakeupsrcsGetIter->Next(config)) {
        if (WakeupSrcType::WAKEUP_SRC_VOICE == config.wakeupSource) {
            EXPECT_TRUE(config.enabled);
            ok = true;
        }
    }

    EXPECT_TRUE(ok);

    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/* COM-RPC tests */
void PowerManager_L2Test::Test_PerformReboot( Exchange::IPowerManager* PowerManagerPlugin )
{
    uint32_t status = Core::ERROR_GENERAL;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;
    string requestor = "PowerManager_L2Test";
    string customReason = "MAINTENANCE_REBOOT";
    string otherReason = "MAINTENANCE_REBOOT";
    TEST_LOG("\n######################### Running Test_PerformReboot Test ###########################\n");

    status = PowerManagerPlugin->Reboot(requestor,customReason,otherReason);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT,POWERMANAGERL2TEST_EVENT_REBOOTING);
    EXPECT_TRUE(signalled & POWERMANAGERL2TEST_EVENT_REBOOTING);
}

/* COM-RPC tests */
void PowerManager_L2Test::Test_NetworkStandbyMode( Exchange::IPowerManager* PowerManagerPlugin )
{
    uint32_t status = Core::ERROR_GENERAL;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;
    bool standbyMode = true;
    TEST_LOG("\n############################ Running Test_NetworkStandbyMode Test #####################\n");

    status = PowerManagerPlugin->SetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }

    signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT,POWERMANAGERL2TEST_NETWORK_STANDBYMODECHANGED);
    EXPECT_TRUE(signalled & POWERMANAGERL2TEST_NETWORK_STANDBYMODECHANGED);

    bool standbyMode1;

    status = PowerManagerPlugin->GetNetworkStandbyMode(standbyMode1);
    EXPECT_EQ(standbyMode, standbyMode1);
    EXPECT_EQ(status,Core::ERROR_NONE);
    if (status != Core::ERROR_NONE)
    {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

TEST_F(PowerManager_L2Test, deepSleepOnThermalChange)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {
                uint32_t status = PowerManagerPlugin->SetDeepSleepTimer(10);
                EXPECT_EQ(status, Core::ERROR_NONE);

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetPowerState(::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP);
                            return PWRMGR_SUCCESS;
                     }))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                            return PWRMGR_SUCCESS;
                     }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                            return DEEPSLEEPMGR_SUCCESS;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_GetLastWakeupReason(::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](DeepSleep_WakeupReason_t* wakeupReason) {
                            *wakeupReason = DEEPSLEEP_WAKEUPREASON_GPIO;
                            return DEEPSLEEPMGR_SUCCESS;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_DeepSleepWakeup())
                    .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

                EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
                    .WillOnce(::testing::Invoke(
                        [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                            *curTemperature  = 120; // high temperature
                            *curState        = (mfrTemperatureState_t)mfrTEMPERATURE_HIGH;
                            *wifiTemperature = 25;
                            return mfrERR_NONE;
                        }))
                    .WillRepeatedly(::testing::Invoke(
                        [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                            *curTemperature  = 60; // Return to SAFE temperature after first reading
                            *curState        = (mfrTemperatureState_t)mfrTEMPERATURE_NORMAL;
                            *wifiTemperature = 25;
                            return mfrERR_NONE;
                }));

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3,POWERMANAGERL2TEST_THERMALSTATE_CHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_THERMALSTATE_CHANGED);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 15, POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);

                // Add delay to allow all pending state transitions to complete
                std::this_thread::sleep_for(std::chrono::seconds(2));

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

/********************************************************
************Test case Details **************************
** 1. Get temperature from systemservice
** 2. Set temperature threshold
** 3. Temperature threshold change event triggered from IARM
** 4. Verify that threshold change event is notified
*******************************************************/

TEST_F(PowerManager_L2Test,PowerManagerComRpc)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {

                EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
                    .WillRepeatedly(::testing::Invoke(
                        [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                            *curTemperature  = 60; // safe temperature
                            *curState        = (mfrTemperatureState_t)0;
                            *wifiTemperature = 25;
                            return mfrERR_NONE;
                                }));

                Test_PowerStateChange(PowerManagerPlugin);
                Test_TemperatureThresholds(PowerManagerPlugin);
                Test_OvertempGraceInterval(PowerManagerPlugin);
                Test_WakeupSrcConfig(PowerManagerPlugin);
                Test_PerformReboot(PowerManagerPlugin);
                Test_NetworkStandbyMode(PowerManagerPlugin);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

TEST_F(PowerManager_L2Test,DeepSleepFailure)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t deepSleepTimeout = 10;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {
                uint32_t status = PowerManagerPlugin->SetDeepSleepTimer(deepSleepTimeout);
                EXPECT_EQ(status, Core::ERROR_NONE);

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetPowerState(::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP);
                            return PWRMGR_SUCCESS;
                        }))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                            return PWRMGR_SUCCESS;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
                    .Times(5)
                    .WillRepeatedly(::testing::Invoke(
                        [&](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                            EXPECT_EQ(deep_sleep_timeout, deepSleepTimeout);
                            EXPECT_TRUE(nullptr != isGPIOWakeup);
                            EXPECT_EQ(networkStandby, false);
                            // Simulate timer wakeup
                            *isGPIOWakeup = false;
                            return DEEPSLEEPMGR_INVALID_ARGUMENT;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_DeepSleepWakeup())
                    .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

                int keyCode = 0;
                status      = PowerManagerPlugin->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l2-test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                sleep(25);

                PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
                PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

                status = PowerManagerPlugin->GetPowerState(newState, prevState);
                EXPECT_EQ(status, Core::ERROR_NONE);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

TEST_F(PowerManager_L2Test, DeepSleepIgnore)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            std::ofstream file("/tmp/ignoredeepsleep");
            file.close();

            if (PowerManagerPlugin)
            {
                uint32_t status;
                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);

                status = PowerManagerPlugin->SetDeepSleepTimer(10);
                EXPECT_EQ(status, Core::ERROR_NONE);

                int keyCode = 0;
                status      = PowerManagerPlugin->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l2-test-client");
                EXPECT_EQ(status, Core::ERROR_NONE);

                PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
                PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

                status = PowerManagerPlugin->GetPowerState(newState, prevState);
                EXPECT_EQ(status, Core::ERROR_NONE);
                EXPECT_NE(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
            std::remove("/tmp/ignoredeepsleep");
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

TEST_F(PowerManager_L2Test, NetworkStandby)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {
                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                            EXPECT_EQ(wakeupSrc, PWRMGR_WAKEUPSRC_WIFI);
                            EXPECT_EQ(enabled, false);
                            return PWRMGR_SUCCESS;
                        }))
                    .WillOnce(::testing::Invoke(
                        [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                            EXPECT_EQ(wakeupSrc, PWRMGR_WAKEUPSRC_LAN);
                            EXPECT_EQ(enabled, false);
                            return PWRMGR_SUCCESS;
                        }));

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_NW_STANDBYMODECHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_NW_STANDBYMODECHANGED);

                PowerManagerPlugin->SetNetworkStandbyMode(false);

                bool standbyMode = false;

                uint32_t status = PowerManagerPlugin->GetNetworkStandbyMode(standbyMode);
                EXPECT_EQ(status, Core::ERROR_NONE);
                EXPECT_EQ(standbyMode, false);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

TEST_F(PowerManager_L2Test,DeepSleepInvalidWakeup)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;
    uint32_t deepSleepTimeout = 10;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {
                uint32_t status = PowerManagerPlugin->SetDeepSleepTimer(deepSleepTimeout);
                EXPECT_EQ(status, Core::ERROR_NONE);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_PRECHANGE);

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetPowerState(::testing::_))
                    .Times(2)
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP);
                            return PWRMGR_SUCCESS;
                        }))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                            return PWRMGR_SUCCESS;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
                    .WillOnce(::testing::Invoke(
                        [&](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                            EXPECT_EQ(deep_sleep_timeout, deepSleepTimeout);
                            EXPECT_TRUE(nullptr != isGPIOWakeup);
                            EXPECT_EQ(networkStandby, false);
                            // Simulate timer wakeup
                            *isGPIOWakeup = false;
                            return DEEPSLEEPMGR_SUCCESS;
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_GetLastWakeupReason(::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](DeepSleep_WakeupReason_t* wakeupReason) {
                            // Invalid wakeup reason
                            return DeepSleep_Return_Status_t(-1);
                        }));

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_DS_DeepSleepWakeup())
                    .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

                int keyCode = 0;
                status      = PowerManagerPlugin->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l2-test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 15, POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_DEEP_SLEEP_TIMEOUT);

                sleep(3);

                PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
                PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

                status = PowerManagerPlugin->GetPowerState(newState, prevState);
                EXPECT_EQ(status, Core::ERROR_NONE);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

TEST_F(PowerManager_L2Test, PowerModePreChangeAckTimeout)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;
    uint32_t signalled = POWERMANAGERL2TEST_STATE_INVALID;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
            PowerManagerPlugin->Register(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());

            if (PowerManagerPlugin)
            {
                int keyCode = 0;

                uint32_t clientId = 0;
                uint32_t status   = PowerManagerPlugin->AddPowerModePreChangeClient("l2-test-client", clientId);
                EXPECT_EQ(status, Core::ERROR_NONE);

                EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetPowerState(::testing::_))
                    .WillOnce(::testing::Invoke(
                        [](PWRMgr_PowerState_t powerState) {
                            EXPECT_EQ(powerState, PWRMGR_POWERSTATE_ON);
                            return PWRMGR_SUCCESS;
                        }));

                status = PowerManagerPlugin->SetPowerState(keyCode, PowerState::POWER_STATE_ON, "l2-test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                signalled = mNotification.WaitForRequestStatus(JSON_TIMEOUT * 3, POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);
                EXPECT_TRUE(signalled & POWERMANAGERL2TEST_SYSTEMSTATE_CHANGED);

                // some delay to destroy AckController after IModeChanged notification
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));

                PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
                PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

                status = PowerManagerPlugin->GetPowerState(currentState, prevState);
                EXPECT_EQ(status, Core::ERROR_NONE);
                EXPECT_EQ(currentState, PowerState::POWER_STATE_ON);
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY);

                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IRebootNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IDeepSleepTimeoutNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::INetworkStandbyModeChangedNotification>());
                PowerManagerPlugin->Unregister(mNotification.baseInterface<Exchange::IPowerManager::IThermalModeChangedNotification>());
                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}


TEST_F(PowerManager_L2Test, JsonRpcWakeupSourceChange)
{
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    JsonArray configs;

    {
        JsonObject source;
        source["wakeupSource"] = "WIFI";
        source["enabled"] = true;
        configs.Add(source);
    }
    {
        JsonObject source;
        source["wakeupSource"] = "LAN";
        source["enabled"] = true;
        configs.Add(source);
    }

    params["wakeupSources"] = configs;

    EXPECT_CALL(POWERMANAGER_MOCK, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                EXPECT_EQ(wakeupSrc, PWRMGR_WAKEUPSRC_WIFI);
                EXPECT_EQ(enabled, true);
                return PWRMGR_SUCCESS;
            }))
        .WillOnce(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                EXPECT_EQ(wakeupSrc, PWRMGR_WAKEUPSRC_LAN);
                EXPECT_EQ(enabled, true);
                return PWRMGR_SUCCESS;
            }));

    status = InvokeServiceMethod("org.rdk.PowerManager.1.", "setWakeupSourceConfig", params, result);

    EXPECT_EQ(status,Core::ERROR_NONE);
}

TEST_F(PowerManager_L2Test, JsonRpcWakeupSource_UNKNOWN)
{
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    JsonArray configs;

    {
        JsonObject source;
        source["wakeupSource"] = "UNKNOWN";
        source["enabled"] = true;
        configs.Add(source);
    }
    {
        JsonObject source;
        source["wakeupSource"] = "LAN";
        source["enabled"] = true;
        configs.Add(source);
    }

    params["wakeupSources"] = configs;

    status = InvokeServiceMethod("org.rdk.PowerManager.1.", "setWakeupSourceConfig", params, result);

    // EXPECT_EQ((status & 0x7F), Core::ERROR_INVALID_PARAMETER);
    EXPECT_NE((status & 0x7F), Core::ERROR_NONE);
}

/********************************************************
************Test case Details **************************
** Test GetTimeSinceWakeup when no wakeup has occurred
** Expected: secondsSinceWakeup should be 0
*******************************************************/
TEST_F(PowerManager_L2Test, GetTimeSinceWakeup_NoWakeup)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();
            if (PowerManagerPlugin)
            {
                Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup;
                timeSinceWakeup.secondsSinceWakeup = 999; // Initialize with non-zero value

                uint32_t status = PowerManagerPlugin->GetTimeSinceWakeup(timeSinceWakeup);

                EXPECT_EQ(status, Core::ERROR_NONE);
                // When no wakeup has occurred, it should return 0
                EXPECT_EQ(timeSinceWakeup.secondsSinceWakeup, 0);

                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

/********************************************************
************Test case Details **************************
** Test GetTimeSinceWakeup after transitioning to ON state
** 1. Transition to STANDBY
** 2. Transition back to ON (triggers wakeup timestamp)
** 3. Query GetTimeSinceWakeup and verify time elapsed
*******************************************************/
TEST_F(PowerManager_L2Test, GetTimeSinceWakeup_AfterDeepSleepWakeup)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            if (PowerManagerPlugin)
            {
                // Transition to STANDBY first to leave ON state
                uint32_t status = PowerManagerPlugin->SetPowerState(0, PowerState::POWER_STATE_STANDBY, "test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                // Transition to ON - this triggers UpdateWakeupTime()
                status = PowerManagerPlugin->SetPowerState(0, PowerState::POWER_STATE_ON, "test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                // Wait for 2 seconds to allow time to elapse since wakeup
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // Query GetTimeSinceWakeup
                Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup;
                status = PowerManagerPlugin->GetTimeSinceWakeup(timeSinceWakeup);

                EXPECT_EQ(status, Core::ERROR_NONE);
                // Verify that at least 2 seconds have elapsed since wakeup
                TEST_LOG("Time since wakeup: %u seconds", timeSinceWakeup.secondsSinceWakeup);
                EXPECT_GE(timeSinceWakeup.secondsSinceWakeup, 2);
                EXPECT_LE(timeSinceWakeup.secondsSinceWakeup, 5);

                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}

/********************************************************
************Test case Details **************************
** Test GetTimeSinceWakeup with multiple queries
** Verify that time increases between consecutive calls
*******************************************************/
TEST_F(PowerManager_L2Test, GetTimeSinceWakeup_MultipleQueries)
{
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> mEngine_PowerManager;
    Core::ProxyType<RPC::CommunicatorClient> mClient_PowerManager;
    PluginHost::IShell *mController_PowerManager;

    TEST_LOG("Creating mEngine_PowerManager");
    mEngine_PowerManager = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    mClient_PowerManager = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(mEngine_PowerManager));

    TEST_LOG("Creating mEngine_PowerManager Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    mEngine_PowerManager->Announcements(mClient_PowerManager->Announcement());
#endif

    if (!mClient_PowerManager.IsValid())
    {
        TEST_LOG("Invalid mClient_PowerManager");
    }
    else
    {
        mController_PowerManager = mClient_PowerManager->Open<PluginHost::IShell>(_T("org.rdk.PowerManager"), ~0, 3000);
        if (mController_PowerManager)
        {
            auto PowerManagerPlugin = mController_PowerManager->QueryInterface<Exchange::IPowerManager>();

            if (PowerManagerPlugin)
            {
                // Transition to STANDBY first
                uint32_t status = PowerManagerPlugin->SetPowerState(0, PowerState::POWER_STATE_STANDBY, "test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                // Transition to ON to trigger wakeup timestamp
                status = PowerManagerPlugin->SetPowerState(0, PowerState::POWER_STATE_ON, "test");
                EXPECT_EQ(status, Core::ERROR_NONE);

                // First query - get baseline
                Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup1;
                status = PowerManagerPlugin->GetTimeSinceWakeup(timeSinceWakeup1);
                EXPECT_EQ(status, Core::ERROR_NONE);
                TEST_LOG("First query - Time since wakeup: %u seconds", timeSinceWakeup1.secondsSinceWakeup);

                // Sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // Second query
                Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup2;
                status = PowerManagerPlugin->GetTimeSinceWakeup(timeSinceWakeup2);
                EXPECT_EQ(status, Core::ERROR_NONE);
                TEST_LOG("Second query - Time since wakeup: %u seconds", timeSinceWakeup2.secondsSinceWakeup);

                // Verify that the second query shows more elapsed time
                EXPECT_GT(timeSinceWakeup2.secondsSinceWakeup, timeSinceWakeup1.secondsSinceWakeup);
                EXPECT_GE(timeSinceWakeup2.secondsSinceWakeup - timeSinceWakeup1.secondsSinceWakeup, 1);

                // Sleep for another second
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // Third query
                Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup3;
                status = PowerManagerPlugin->GetTimeSinceWakeup(timeSinceWakeup3);
                EXPECT_EQ(status, Core::ERROR_NONE);
                TEST_LOG("Third query - Time since wakeup: %u seconds", timeSinceWakeup3.secondsSinceWakeup);

                // Verify that the third query shows even more elapsed time
                EXPECT_GT(timeSinceWakeup3.secondsSinceWakeup, timeSinceWakeup2.secondsSinceWakeup);

                PowerManagerPlugin->Release();
            }
            else
            {
                TEST_LOG("PowerManagerPlugin is NULL");
            }
            mController_PowerManager->Release();
        }
        else
        {
            TEST_LOG("mController_PowerManager is NULL");
        }
    }
}