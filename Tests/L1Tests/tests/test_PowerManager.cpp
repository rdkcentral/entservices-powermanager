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
#include <thread>
#include <bitset>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Services.h>
#include <gmock/gmock.h>

#include <interfaces/IPowerManager.h>

#include "PowerManagerHalMock.h"
#include "PowerManagerImplementation.h"
#include "WorkerPoolImplementation.h"

#include "IarmBusMock.h"
#include "MfrMock.h"
#include "RfcApiMock.h"
#include "TelemetryMock.h"
#include "WrapsMock.h"

// utils
#include "WaitGroup.h"

using namespace WPEFramework;
using ::testing::NiceMock;

using WakeupReason  = WPEFramework::Exchange::IPowerManager::WakeupReason;
using WakeupSrcType = WPEFramework::Exchange::IPowerManager::WakeupSrcType;

#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);

template <typename Enum, int N = ((sizeof(int) * 8)-1)>
class EnumSet {
public:
    EnumSet() : bits(0) {}
    EnumSet(int val) : bits(val) {}

    void set(Enum e) {
        bits.set(static_cast<int>(e));
    }

    void set(Enum e, bool value) {
        if (value) {
            set(e);
        } else {
            reset(e);
        }
    }

    void reset(Enum e) {
        bits.reset(static_cast<int>(e));
    }


    bool test(Enum e) const {
        return bits.test(static_cast<int>(e));
    }

    std::string str() const {
        std::string result;
        std::stringstream ss;
        for (int i = N; i >= 0; --i) {
            if (bits.test(i)) {
                ss << "1";
            } else {
                ss << "0";
            }
            if (i % 8 == 0  && i != N && i != 0) {
                ss << "_";
            }
        }
        return ss.str();
    }

private:
    std::bitset<N+1> bits;
};

class TestPowerManager : public ::testing::Test {

protected:
    WrapsImplMock* p_wrapsImplMock     = nullptr;
    RfcApiImplMock* p_rfcApiImplMock   = nullptr;
    PowerManagerHalMock* p_powerManagerHalMock = nullptr;
    mfrMock *p_mfrMock = nullptr;
    IarmBusImplMock* p_iarmBusMock = nullptr;
    TelemetryApiImplMock* p_telemetryMock = nullptr;

public:
    bool wait_call = true;
    std::mutex m_mutex;
    Core::ProxyType<Plugin::PowerManagerImplementation> powerManagerImpl;
    EnumSet<PWRMGR_WakeupSrcType_t, PWRMGR_WAKEUPSRC_MAX> _wakeupSources;
    WaitGroup setupWg; // wait group created specifically for setup / init (SetUpMocks)

    struct PowerModePreChangeEvent : public WPEFramework::Exchange::IPowerManager::IModePreChangeNotification {
        MOCK_METHOD(void, OnPowerModePreChange, (const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter), (override));

        BEGIN_INTERFACE_MAP(PowerModePreChangeEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::IModePreChangeNotification)
        END_INTERFACE_MAP
    };

    struct PowerModeChangeAcknowledgementEvent : public WPEFramework::Exchange::IPowerManager::IPowerModeChangeAcknowledgementRequested {
        MOCK_METHOD(void, OnPowerModeChangeAcknowledgementRequested, (const PowerState currentState, const PowerState newState, const int transactionId, const string& reason), (override));

        BEGIN_INTERFACE_MAP(PowerModeChangeAcknowledgementEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::IPowerModeChangeAcknowledgementRequested)
        END_INTERFACE_MAP
    };

    struct PowerModeChangedEvent : public WPEFramework::Exchange::IPowerManager::IModeChangedNotification {
        MOCK_METHOD(void, OnPowerModeChanged, (const PowerState, const PowerState), (override));

        BEGIN_INTERFACE_MAP(PowerModeChangedEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::IModeChangedNotification)
        END_INTERFACE_MAP
    };

    struct DeepSleepWakeupEvent : public WPEFramework::Exchange::IPowerManager::IDeepSleepTimeoutNotification {
        MOCK_METHOD(void, OnDeepSleepTimeout, (const int), (override));

        BEGIN_INTERFACE_MAP(DeepSleepWakeupEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::IDeepSleepTimeoutNotification)
        END_INTERFACE_MAP
    };

    struct RebootEvent : public WPEFramework::Exchange::IPowerManager::IRebootNotification {
        MOCK_METHOD(void, OnRebootBegin, (const string&, const string&, const string&), (override));

        BEGIN_INTERFACE_MAP(RebootEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::IRebootNotification)
        END_INTERFACE_MAP
    };

    struct NetworkStandbyChangedEvent : public WPEFramework::Exchange::IPowerManager::INetworkStandbyModeChangedNotification {
        MOCK_METHOD(void, OnNetworkStandbyModeChanged, (const bool), (override));

        BEGIN_INTERFACE_MAP(NetworkStandbyChangedEvent)
        INTERFACE_ENTRY(Exchange::IPowerManager::INetworkStandbyModeChangedNotification)
        END_INTERFACE_MAP
    };

    TestPowerManager()
        : _wakeupSources(0xFF)
    {
        SetUpMocks();

        EXPECT_EQ(0, system("mkdir -p /mnt/secure_storage/pwrmgr && rm -f /mnt/secure_storage/pwrmgr/schedules.stg"))
            << "failed to prepare wakeup schedule storage";

        setupWg.Add(1);
        powerManagerImpl = Core::ProxyType<Plugin::PowerManagerImplementation>::Create();

        TEST_LOG("MIL: Await mfrGetTemperature to start testCase");
        setupWg.Wait();

        // Default Wake-On-LAN is disabled
        EXPECT_FALSE(_wakeupSources.test(PWRMGR_WAKEUPSRC_WIFI));
        EXPECT_FALSE(_wakeupSources.test(PWRMGR_WAKEUPSRC_LAN));

        TEST_LOG("MIL: >> Exec test now testCase");
    }

    void SetUpMocks()
    {
        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        p_rfcApiImplMock = new NiceMock<RfcApiImplMock>;
        RfcApi::setImpl(p_rfcApiImplMock);

        p_iarmBusMock = new testing::NiceMock<IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusMock);

        p_powerManagerHalMock = new NiceMock<PowerManagerHalMock>;
        PowerManagerAPI::setImpl(p_powerManagerHalMock);

        p_mfrMock = new NiceMock<mfrMock>;
        mfr::setImpl(p_mfrMock);

        p_telemetryMock = new NiceMock<TelemetryApiImplMock>;
        TelemetryApi::setImpl(p_telemetryMock);
        ON_CALL(*p_telemetryMock, t2_event_s(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(T2ERROR_SUCCESS));
        ON_CALL(*p_telemetryMock, t2_event_d(::testing::_, ::testing::_))
            .WillByDefault(::testing::Return(T2ERROR_SUCCESS));

        EXPECT_CALL( *p_powerManagerHalMock, PLAT_INIT())
            .WillOnce(::testing::Return(PWRMGR_SUCCESS));

        EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_INIT())
            .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

        ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    if (strcmp("RFC_DATA_ThermalProtection_POLL_INTERVAL", pcParameterName) == 0) {
                        strcpy(pstParamData->value, "1");
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

        // called from ThermalController constructor in initializeThermalProtection
        EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](int high, int critical) {
                    EXPECT_EQ(high, 100);
                    EXPECT_EQ(critical, 110);
                    return mfrERR_NONE;
                }));

        // called from pollThermalLevels
        EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke(
                [this](mfrTemperatureState_t* state, int* temperatureValue, int* wifiTemp) {
                    *state            = mfrTEMPERATURE_NORMAL;
                    *temperatureValue = 40;
                    *wifiTemp         = 35;
                    TEST_LOG("signal mfrGetTemperature from testCase");
                    setupWg.Done();
                    return mfrERR_NONE;
                }));

        // called from PowerController::init (constructor)
        EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetPowerState(::testing::_))
            .WillRepeatedly(::testing::Invoke(
                [](PWRMgr_PowerState_t* powerState) {
                    *powerState = PWRMGR_POWERSTATE_OFF; // by default over boot up, return PowerState OFF
                    return PWRMGR_SUCCESS;
                }));

        // called from PowerController::init (constructor)
        EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
            .WillRepeatedly(::testing::Invoke(
                [](PWRMgr_PowerState_t powerState) {
                    // All tests are run without settings file
#ifdef PLATCO_BOOTTO_STANDBY
                    // If BOOTTO_STANDBY is enabled, device boots in STANDBY by default.
                    EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY);
#else
                    // default expected power state is ON
                    EXPECT_EQ(powerState, PWRMGR_POWERSTATE_ON);
#endif
                    return PWRMGR_SUCCESS;
                }));

        EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke(
                [this](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                    _wakeupSources.set(wakeupSrc, enabled);
                    return PWRMGR_SUCCESS;
                }));

        EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetWakeupSrc(::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke(
                [this](PWRMGR_WakeupSrcType_t wakeupSrc, bool *enabled) {
                    EXPECT_TRUE(nullptr != enabled);
                    *enabled = _wakeupSources.test(wakeupSrc);
                    return PWRMGR_SUCCESS;
                }));
    }

    void TearDownMocks()
    {
    }

    ~TestPowerManager() override
    {
        TEST_LOG("MIL: << Done Exec testCase cleanup now");
        TEST_LOG("DTOR is called, %p", this);
        WaitGroup wg;
        wg.Add();

        EXPECT_CALL(*p_powerManagerHalMock, PLAT_TERM())
            .WillOnce(::testing::Invoke([]() {
                return PWRMGR_SUCCESS;
            }));

        EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_TERM())
            .WillOnce(::testing::Invoke([&]() {
                wg.Done();
                return DEEPSLEEPMGR_SUCCESS;
            }));

        EXPECT_EQ(powerManagerImpl.IsValid(), true);
        TEST_LOG(">> Release powerManagerImpl %p", &(*powerManagerImpl));
        powerManagerImpl.Release();
        EXPECT_EQ(powerManagerImpl.IsValid(), false);
        TEST_LOG("<< Released powerManagerImpl");

        wg.Wait();

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr) {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }

        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr) {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }

        IarmBus::setImpl(nullptr);
        if (p_iarmBusMock != nullptr) {
            delete p_iarmBusMock;
            p_iarmBusMock = nullptr;
        }

        PowerManagerAPI::setImpl(nullptr);
        if (p_powerManagerHalMock != nullptr) {
            delete p_powerManagerHalMock;
            p_powerManagerHalMock = nullptr;
        }

        mfr::setImpl(nullptr);
        if (p_mfrMock != nullptr) {
            delete p_mfrMock;
            p_mfrMock = nullptr;
        }

        TelemetryApi::setImpl(nullptr);
        if (p_telemetryMock != nullptr) {
            delete p_telemetryMock;
            p_telemetryMock = nullptr;
        }

        TearDownMocks();

        if (0 != system("rm /opt/uimgr_settings.bin")) { /* do nothing */
        }

        // Although this file is not created always
        // delete to avoid dependency among test cases
        if (0 != system("rm -f /tmp/deepSleepDelayTimer")) { /* do nothing */
        }
        if (0 != system("rm -f /tmp/deepSleepWakeupTimer")) { /* do nothing */
        }
        if (0 != system("rm -f /tmp/ignoredeepsleep")) { /* do nothing */
        }
        if (0 != system("rm -f /mnt/secure_storage/pwrmgr/schedules.stg")) { /* do nothing */
        }

        // in some rare cases we saw settings file being reused from
        // old testcase, fs sync would resolve such issues
        if (0 != system("sync")) {
            // do nothing
        }
    }

    static void SetUpTestSuite()
    {
        // static WorkerPoolImplementation workerPool(4, WPEFramework::Core::Thread::DefaultStackSize(), 16);
        static WorkerPoolImplementation workerPool(4, 64 * 1024, 16);
        WPEFramework::Core::WorkerPool::Assign(&workerPool);
        workerPool.Run();
    }

    PowerState initialPowerState()
    {
#ifdef PLATCO_BOOTTO_STANDBY
        // If BOOTTO_STANDBY is enabled, device boots in STANDBY by default.
        return PowerState::POWER_STATE_STANDBY;
#else
        // default expected power state is ON
        return PowerState::POWER_STATE_ON;
#endif
    }
};

TEST_F(TestPowerManager, GetLastWakeupReason)
{
    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_IR;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    uint32_t status = powerManagerImpl->GetLastWakeupReason(wakeupReason);

    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupReason, WakeupReason::WAKEUP_REASON_IR);
}

TEST_F(TestPowerManager, GetLastWakeupKeyCode)
{
    int wakeupKeyCode = 0;

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupKeyCode(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleepMgr_WakeupKeyCode_Param_t* param) {
                // ASSERT_TRUE(param != nullptr);
                EXPECT_TRUE(param != nullptr);
                param->keyCode = 1234;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    uint32_t status = powerManagerImpl->GetLastWakeupKeyCode(wakeupKeyCode);

    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupKeyCode, 1234);
}

TEST_F(TestPowerManager, GetTimeSinceWakeup_NoWakeupOccurred)
{
    // Test case: Device has not woken up yet (in standby or initial state)
    // Expected: secondsSinceWakeup should be 0

    WPEFramework::Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup;
    timeSinceWakeup.secondsSinceWakeup = 999; // Initialize with non-zero value

    uint32_t status = powerManagerImpl->GetTimeSinceWakeup(timeSinceWakeup);

    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(timeSinceWakeup.secondsSinceWakeup, 0);
}

TEST_F(TestPowerManager, GetTimeSinceWakeup_AfterWakeup)
{
    // Test case: Transition to ON state to trigger wakeup timestamp, then measure elapsed time

    // Set up mock expectations for state transitions
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_ON);
                return PWRMGR_SUCCESS;
            }));

    // Now transition back to ON - this will trigger UpdateWakeupTime()
    uint32_t status = powerManagerImpl->SetPowerState(0, PowerState::POWER_STATE_ON, "test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    // Sleep for a short duration to allow time to elapse since wakeup
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Now get the time since wakeup
    WPEFramework::Exchange::IPowerManager::TimeSinceWakeup timeSinceWakeup;
    status = powerManagerImpl->GetTimeSinceWakeup(timeSinceWakeup);

    EXPECT_EQ(status, Core::ERROR_NONE);
    // The elapsed time should be at least 2 seconds
    EXPECT_GE(timeSinceWakeup.secondsSinceWakeup, 2);
    EXPECT_LE(timeSinceWakeup.secondsSinceWakeup, 5);
}

using WakeupSourceConfigIteratorImpl = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>>;

TEST_F(TestPowerManager, SetWakeupSourceConfig)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool enabled) {
                EXPECT_EQ(wakeupSrc, PWRMGR_WAKEUPSRC_WIFI);
                EXPECT_EQ(enabled, true);
                return PWRMGR_SUCCESS;
            }));

    std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_WIFI, true}};
    auto iterator = WakeupSourceConfigIteratorImpl::Create<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>(configs);

    uint32_t status = powerManagerImpl->SetWakeupSourceConfig(iterator);

    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, GetWakeupSourceConfig)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetWakeupSrc(::testing::_, ::testing::_))
        .Times(10)
        .WillRepeatedly(::testing::Invoke(
            [](PWRMGR_WakeupSrcType_t wakeupSrc, bool* enabled) {
                EXPECT_TRUE(enabled != nullptr);

                if (PWRMGR_WAKEUPSRC_WIFI == wakeupSrc) {
                    *enabled = true;
                } else {
                    *enabled = false;
                }
                return PWRMGR_SUCCESS;
            }));

    WPEFramework::RPC::IIteratorType<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig, WPEFramework::Exchange::IDS::ID_POWER_MANAGER_WAKEUP_SRC_ITERATOR>* _wakeupSources{};

    uint32_t status = powerManagerImpl->GetWakeupSourceConfig(_wakeupSources);
    EXPECT_EQ(status, Core::ERROR_NONE);

    WPEFramework::Exchange::IPowerManager::WakeupSourceConfig config{WakeupSrcType::WAKEUP_SRC_UNKNOWN, false};

    EXPECT_EQ(_wakeupSources->Count(), 10U);

    while (_wakeupSources->Next(config)) {
        if (WakeupSrcType::WAKEUP_SRC_WIFI == config.wakeupSource) {
            EXPECT_EQ(config.enabled, true);
        } else {
            EXPECT_EQ(config.enabled, false);
        }
    }
}

TEST_F(TestPowerManager, GetPowerStateBeforeReboot)
{
    PowerState powerState = PowerState::POWER_STATE_UNKNOWN;
    auto status           = powerManagerImpl->GetPowerStateBeforeReboot(powerState);
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, GetCoreTemperature)
{
    float temp  = 0;
    auto status = powerManagerImpl->GetThermalState(temp);
    EXPECT_EQ(temp, 40.0); // 40 is set in SetUpMocks
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModePreChangeAck)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t clientId  = 0;
    int transaction_id = 0;
    uint32_t status    = powerManagerImpl->AddPowerModePreChangeClient("l1-test-client", clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent = Core::ProxyType<PowerModePreChangeEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    WaitGroup wg;
    wg.Add();
    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                transaction_id = transactionId;
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(stateChangeAfter, 1);
                // Test invalid parameters FIRST before modifying state
                // Acknowledge - Change Complete with invalid transactionId
                auto status = powerManagerImpl->PowerModePreChangeComplete(clientId, transactionId + 10);
                EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
                // Acknowledge - Change Complete with invalid clientId
                status = powerManagerImpl->PowerModePreChangeComplete(clientId + 10, transactionId);
                EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

                // Now set valid delays
                // Delay power mode change by 10 seconds
                status = powerManagerImpl->DelayPowerModeChangeBy(clientId, transactionId, 10);
                EXPECT_EQ(status, Core::ERROR_NONE);

                // delay by larger value (extends the timeout)
                status = powerManagerImpl->DelayPowerModeChangeBy(clientId, transactionId, 30);
                EXPECT_EQ(status, Core::ERROR_NONE);

                // valid PowerModePreChangeComplete
                status = powerManagerImpl->PowerModePreChangeComplete(clientId, transaction_id);
                EXPECT_EQ(status, Core::ERROR_NONE);

                wg.Done();
            }));

    // Even though same state is set multiple times only one pre change notification is invoked
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    // some delay to destroy AckController after IModeChanged notification
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->RemovePowerModePreChangeClient(clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModePreChangeAckTimeout)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t clientId = 0;
    uint32_t status   = powerManagerImpl->AddPowerModePreChangeClient("l1-test-client", clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent = Core::ProxyType<PowerModeChangedEvent>::Create();

    EXPECT_EQ(status, powerManagerImpl->Register(&(*prechangeEvent)));
    EXPECT_EQ(status, powerManagerImpl->Register(&(*modeChangedEvent)));

    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(stateChangeAfter, 1);
            }));

    WaitGroup wg;
    wg.Add(1);
    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();
    // some delay to destroy AckController after IModeChanged notification
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModePreChangeUnregisterBeforeAck)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t clientId = 0;
    uint32_t status   = powerManagerImpl->AddPowerModePreChangeClient("l1-test-client", clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent = Core::ProxyType<PowerModeChangedEvent>::Create();

    EXPECT_EQ(status, powerManagerImpl->Register(&(*prechangeEvent)));
    EXPECT_EQ(status, powerManagerImpl->Register(&(*modeChangedEvent)));

    WaitGroup wg;
    wg.Add();
    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(stateChangeAfter, 1);

                // Delay power mode change by 1 seconds
                auto status = powerManagerImpl->DelayPowerModeChangeBy(clientId, transactionId, 1);
                EXPECT_EQ(status, Core::ERROR_NONE);

                // Extend delay power mode change by 10 seconds
                status = powerManagerImpl->DelayPowerModeChangeBy(clientId, transactionId, 10);
                EXPECT_EQ(status, Core::ERROR_NONE);

                // acknowledge after a short delay
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                wg.Done();
            }));

    // Even though same state is set multiple times only one pre change notification is invoked
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    wg.Add();
    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    status = powerManagerImpl->RemovePowerModePreChangeClient(clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModeChangeAcknowledgement)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    // engage in phase 1 (pre-change) so we can complete it immediately and reach phase 2 (ack)
    uint32_t preChangeClientId = 0;
    uint32_t status = powerManagerImpl->AddPowerModePreChangeClient("l1-test-prechange-client", preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    uint32_t ackClientId = 0;
    status = powerManagerImpl->AddPowerModeChangeAcknowledgementClient("l1-test-ack-client", ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent               = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangeAcknowledgementEvent> ackEvent         = Core::ProxyType<PowerModeChangeAcknowledgementEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent               = Core::ProxyType<PowerModeChangedEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                // complete phase 1 immediately so we move into phase 2 (acknowledgement)
                auto status = powerManagerImpl->PowerModePreChangeComplete(preChangeClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }));

    WaitGroup wg;
    wg.Add(1);
    EXPECT_CALL(*ackEvent, OnPowerModeChangeAcknowledgementRequested(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const string& reason) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                // invalid transactionId - rejected, should not complete the round
                auto status = powerManagerImpl->PowerModeChangeAcknowledgement(ackClientId, transactionId + 10);
                EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

                // invalid acknowledgeClientId - rejected, should not complete the round
                status = powerManagerImpl->PowerModeChangeAcknowledgement(ackClientId + 10, transactionId);
                EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

                // valid acknowledgement - completes the round (only client registered)
                status = powerManagerImpl->PowerModeChangeAcknowledgement(ackClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);

                wg.Done();
            }));

    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
            }));

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    // some delay to destroy AckController after IModeChanged notification
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->RemovePowerModePreChangeClient(preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->RemovePowerModeChangeAcknowledgementClient(ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModeChangeAcknowledgementTimeout)
{
    // Architect decision: on timeout, PowerManager logs the unresponsive clients and still
    // proceeds with the power state change (resilient-but-bounded behavior).
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t preChangeClientId = 0;
    uint32_t status = powerManagerImpl->AddPowerModePreChangeClient("l1-test-prechange-client", preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    uint32_t ackClientId = 0;
    status = powerManagerImpl->AddPowerModeChangeAcknowledgementClient("l1-test-ack-client", ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent       = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangeAcknowledgementEvent> ackEvent = Core::ProxyType<PowerModeChangeAcknowledgementEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent       = Core::ProxyType<PowerModeChangedEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                auto status = powerManagerImpl->PowerModePreChangeComplete(preChangeClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }));

    // registered ack client never acknowledges - expect the round to time out (hardcoded 10s)
    // and PowerManager to proceed with the power state change anyway.
    EXPECT_CALL(*ackEvent, OnPowerModeChangeAcknowledgementRequested(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const string& reason) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                // intentionally do not acknowledge
            }));

    WaitGroup wg;
    wg.Add(1);
    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    // some delay to destroy AckController after IModeChanged notification
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->RemovePowerModePreChangeClient(preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->RemovePowerModeChangeAcknowledgementClient(ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModeChangeAcknowledgementUnregisterBeforeAck)
{
    // Removing an ack client while its acknowledgement round is in progress should
    // self-ack on its behalf (mirrors RemovePowerModePreChangeClient behavior for phase 1).
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t preChangeClientId = 0;
    uint32_t status = powerManagerImpl->AddPowerModePreChangeClient("l1-test-prechange-client", preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    uint32_t ackClientId = 0;
    status = powerManagerImpl->AddPowerModeChangeAcknowledgementClient("l1-test-ack-client", ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent       = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangeAcknowledgementEvent> ackEvent = Core::ProxyType<PowerModeChangeAcknowledgementEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent       = Core::ProxyType<PowerModeChangedEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                auto status = powerManagerImpl->PowerModePreChangeComplete(preChangeClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }));

    WaitGroup wg;
    wg.Add(1);
    EXPECT_CALL(*ackEvent, OnPowerModeChangeAcknowledgementRequested(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const string& reason) {
                // do not call PowerModeChangeAcknowledgement, instead disengage the client;
                // this must self-ack and allow the state change to proceed without waiting for timeout.
                auto status = powerManagerImpl->RemovePowerModeChangeAcknowledgementClient(ackClientId);
                EXPECT_EQ(status, Core::ERROR_NONE);
                wg.Done();
            }));

    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
            }));

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    // some delay for state change (self-ack) and AckController destruction to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->RemovePowerModePreChangeClient(preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, PowerModeChangeAcknowledgementOutOfStage)
{
    // Calling PowerModeChangeAcknowledgement while NOT in the acknowledgement negotiation
    // stage at all must be rejected (ERROR_INVALID_PARAMETER) and logged as a warning - it should
    // never happen and signifies a client-side issue.
    uint32_t ackClientId = 0;
    uint32_t status = powerManagerImpl->AddPowerModeChangeAcknowledgementClient("l1-test-ack-client", ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    // no power state change in progress - PowerModeChangeAcknowledgement should be rejected
    status = powerManagerImpl->PowerModeChangeAcknowledgement(ackClientId, 0);
    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

    status = powerManagerImpl->RemovePowerModeChangeAcknowledgementClient(ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, SetPowerStateRejectedDuringAcknowledgementNegotiation)
{
    // While the acknowledgement negotiation is in progress, any new SetPowerState
    // request must be rejected outright (ERROR_ILLEGAL_STATE) and must NOT restart / cancel the
    // 1st stage (pre-change) negotiation.
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }));

    int keyCode = 0;

    uint32_t preChangeClientId = 0;
    uint32_t status = powerManagerImpl->AddPowerModePreChangeClient("l1-test-prechange-client", preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    uint32_t ackClientId = 0;
    status = powerManagerImpl->AddPowerModeChangeAcknowledgementClient("l1-test-ack-client", ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent       = Core::ProxyType<PowerModePreChangeEvent>::Create();
    Core::ProxyType<PowerModeChangeAcknowledgementEvent> ackEvent = Core::ProxyType<PowerModeChangeAcknowledgementEvent>::Create();
    Core::ProxyType<PowerModeChangedEvent> modeChangedEvent       = Core::ProxyType<PowerModeChangedEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Register(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                auto status = powerManagerImpl->PowerModePreChangeComplete(preChangeClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }));

    WaitGroup wg;
    wg.Add(1);
    EXPECT_CALL(*ackEvent, OnPowerModeChangeAcknowledgementRequested(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const string& reason) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);

                // While the ack round is in progress (client has not acked yet), attempt a new
                // SetPowerState request - it must be rejected without disturbing this round.
                auto status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_ON, "l1-test-concurrent");
                EXPECT_EQ(status, Core::ERROR_ILLEGAL_STATE);

                // now acknowledge to let the original round complete normally
                status = powerManagerImpl->PowerModeChangeAcknowledgement(ackClientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);

                wg.Done();
            }));

    EXPECT_CALL(*modeChangedEvent, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
            }));

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    // some delay to destroy AckController after IModeChanged notification
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PowerState currentState = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState    = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(currentState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    // original request (STANDBY_LIGHT_SLEEP) completed; the rejected concurrent request (ON) never applied
    EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(prevState, initialPowerState());

    status = powerManagerImpl->RemovePowerModePreChangeClient(preChangeClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->RemovePowerModeChangeAcknowledgementClient(ackClientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*ackEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
    status = powerManagerImpl->Unregister(&(*modeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepIgnore)
{
    if (0 != system("touch /tmp/ignoredeepsleep")) {
        TEST_LOG("system() failed");
    }

    uint32_t clientId = 0;
    uint32_t status   = powerManagerImpl->AddPowerModePreChangeClient("l1-test-client", clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent = Core::ProxyType<PowerModePreChangeEvent>::Create();

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    WaitGroup wg;
    wg.Add();
    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(stateChangeAfter, 1);

                auto status = powerManagerImpl->PowerModePreChangeComplete(clientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);

                wg.Done();
            }));

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_NE(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepUserWakeup)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate user triggered wakeup
                *isGPIOWakeup = true;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout / 2));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_GPIO;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
    status                    = powerManagerImpl->GetLastWakeupReason(wakeupReason);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupReason, WakeupReason::WAKEUP_REASON_GPIO);

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

// Only difference from above test-case is a user trigger for SetPowerState ON
TEST_F(TestPowerManager, DeepSleepUserWakeupRaceCondition)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP);
                return PWRMGR_SUCCESS;
            }))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_LIGHT_SLEEP);
                return PWRMGR_SUCCESS;
            }))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_ON);
                return PWRMGR_SUCCESS;
            }));

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_ON);
                wg.Done();
            }));

    uint32_t clientId = 0;
    uint32_t status   = powerManagerImpl->AddPowerModePreChangeClient("l1-test-client", clientId);
    EXPECT_EQ(status, Core::ERROR_NONE);

    Core::ProxyType<PowerModePreChangeEvent> prechangeEvent = Core::ProxyType<PowerModePreChangeEvent>::Create();
    EXPECT_CALL(*prechangeEvent, OnPowerModePreChange(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(stateChangeAfter, 1);

                // valid PowerModePreChangeComplete
                auto status = powerManagerImpl->PowerModePreChangeComplete(clientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(stateChangeAfter, 0);

                // trigger new state change now
                wg.Done();

                // simulate a small delay (for new state change i,e ON)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // valid PowerModePreChangeComplete
                auto status = powerManagerImpl->PowerModePreChangeComplete(clientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) {
                EXPECT_EQ(currentState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_ON);
                EXPECT_EQ(stateChangeAfter, 1);

                // valid PowerModePreChangeComplete
                auto status = powerManagerImpl->PowerModePreChangeComplete(clientId, transactionId);
                EXPECT_EQ(status, Core::ERROR_NONE);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate user triggered wakeup
                *isGPIOWakeup = true;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout / 2));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_GPIO;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
    status                    = powerManagerImpl->GetLastWakeupReason(wakeupReason);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupReason, WakeupReason::WAKEUP_REASON_GPIO);

    // ON
    wg.Add(1);
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_ON, "IR-KeyPress-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg.Wait();

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    EXPECT_EQ(newState, PowerState::POWER_STATE_ON);

    // TODO: identify whu delay is required
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    status = powerManagerImpl->Unregister(&(*prechangeEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepTimerWakeup)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 10);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_TIMER;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
    status                    = powerManagerImpl->GetLastWakeupReason(wakeupReason);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupReason, WakeupReason::WAKEUP_REASON_TIMER);

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepDelayedTimerWakeup)
{
    if (0 != system("echo 1 > /tmp/deepSleepDelayTimer")) {
        TEST_LOG("system() failed");
    }
    if (0 != system("echo 2 > /tmp/deepSleepWakeupTimer")) {
        TEST_LOG("system() failed");
    }

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 2);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 2U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_TIMER;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    WakeupReason wakeupReason = WakeupReason::WAKEUP_REASON_UNKNOWN;
    status                    = powerManagerImpl->GetLastWakeupReason(wakeupReason);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(wakeupReason, WakeupReason::WAKEUP_REASON_TIMER);

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

// Verify that _deepSleepDelaySec is reset across activations: a second activation without
// the delay override file must not inherit the delay value from a previous activation.
TEST_F(TestPowerManager, DeepSleepDelayNotPersistedAfterFileRemoved)
{
    if (0 != system("echo 1 > /tmp/deepSleepDelayTimer")) {
        TEST_LOG("system() failed");
    }
    if (0 != system("echo 1 > /tmp/deepSleepWakeupTimer")) {
        TEST_LOG("system() failed");
    }

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .Times(4)
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    WaitGroup wg1, wg2;
    wg1.Add();
    wg2.Add();

    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&wg1](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg1.Done();
            }))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&wg2](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg2.Done();
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 1); // cycle 1: wakeup timer from file
            }))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 1); // cycle 2: wakeup timer from SetDeepSleepTimer
            }));

    {
        ::testing::InSequence seq;
        // Cycle 1: delay file present; PLAT_DS_SetDeepSleep called after 1s scheduling delay
        EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                    EXPECT_EQ(deep_sleep_timeout, 1U);
                    EXPECT_TRUE(nullptr != isGPIOWakeup);
                    *isGPIOWakeup = false;
                    std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout));
                    return DEEPSLEEPMGR_SUCCESS;
                }));
        // Cycle 2: delay file absent; _deepSleepDelaySec must be reset to 0, so
        // PLAT_DS_SetDeepSleep is called immediately (no scheduling delay)
        EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                    EXPECT_EQ(deep_sleep_timeout, 1U);
                    EXPECT_TRUE(nullptr != isGPIOWakeup);
                    *isGPIOWakeup = false;
                    std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout));
                    return DEEPSLEEPMGR_SUCCESS;
                }));
    }

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_TIMER;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .Times(2)
        .WillRepeatedly(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    // --- First activation: delay file present ---
    status = powerManagerImpl->SetDeepSleepTimer(1);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg1.Wait(); // wait for first cycle to fully complete (power state back to light sleep)

    // Remove delay and wakeup override files before second activation
    if (0 != system("rm -f /tmp/deepSleepDelayTimer")) { /* do nothing */ }
    if (0 != system("rm -f /tmp/deepSleepWakeupTimer")) { /* do nothing */ }

    // --- Second activation: no delay file ---
    // Without the fix, _deepSleepDelaySec would still be 1 and deep sleep would be
    // scheduled with a 1s delay instead of running immediately.
    status = powerManagerImpl->SetDeepSleepTimer(1);
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    wg2.Wait(); // wait for second cycle to complete

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

// TODO: This testcase will need some rework
TEST_F(TestPowerManager, DeepSleepInvalidWakeup)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 10);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout / 2));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                // Invalid wakeup reason
                return DeepSleep_Return_Status_t(-1);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    std::this_thread::sleep_for(std::chrono::seconds(20));

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepEarlyWakeup)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 10);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout / 2));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillOnce(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                // Invalid wakeup reason
                return DeepSleep_Return_Status_t(-1);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, DeepSleepFailure)
{
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
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

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
                wg.Done();
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .Times(5)
        .WillRepeatedly(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 10U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                return DEEPSLEEPMGR_SET_FAILURE; // ERROR_ABORTED -> triggers retry loop
            }));

    // TODO: this is incorrect, ideally if SetDeepSleep fails, we should not call DeepSleepWakeup
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->SetDeepSleepTimer(10);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, Reboot)
{
    Core::ProxyType<RebootEvent> rebootEvent = Core::ProxyType<RebootEvent>::Create();
    EXPECT_CALL(*rebootEvent, OnRebootBegin(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const std::string& reasonCustom, const std::string& reasonOther, const std::string& requestor) {
                EXPECT_EQ("L1Test", requestor);
                EXPECT_EQ("L1Test-custom", reasonCustom);
                EXPECT_EQ("Unknown", reasonOther);
            }));

    WaitGroup wg;
    wg.Add(2);
    EXPECT_CALL(*p_wrapsImplMock, v_secure_system(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const char* command, va_list args) {
                EXPECT_EQ(string(command), string("echo 0 > /opt/.rebootFlag"));
                wg.Done();
                return Core::ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](const char* command, va_list args) {
                EXPECT_EQ(string(command), "/lib/rdk/rebootNow.sh -s '%s' -r '%s' -o '%s'");
                wg.Done();
                return Core::ERROR_NONE;
            }));

    uint32_t status = powerManagerImpl->Register(&(*rebootEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    powerManagerImpl->Reboot("L1Test", "L1Test-custom", "");

    wg.Wait();

    status = powerManagerImpl->Unregister(&(*rebootEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
}

TEST_F(TestPowerManager, NetworkStandby)
{
    WaitGroup wg;
    wg.Add(1);

    Core::ProxyType<NetworkStandbyChangedEvent> nwstandbyModeChangedEvent = Core::ProxyType<NetworkStandbyChangedEvent>::Create();
    EXPECT_CALL(*nwstandbyModeChangedEvent, OnNetworkStandbyModeChanged(::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const bool enabled) {
                EXPECT_EQ(enabled, true);
                wg.Done();
            }));

    auto status = powerManagerImpl->Register(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
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

    powerManagerImpl->SetNetworkStandbyMode(true);

    wg.Wait();

    bool standbyMode = false;

    status = powerManagerImpl->GetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(standbyMode, true);

    status = powerManagerImpl->Unregister(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
};

TEST_F(TestPowerManager, DisableWakeOnLAN)
{
    WaitGroup wg;
    wg.Add(1);

    Core::ProxyType<NetworkStandbyChangedEvent> nwstandbyModeChangedEvent = Core::ProxyType<NetworkStandbyChangedEvent>::Create();
    EXPECT_CALL(*nwstandbyModeChangedEvent, OnNetworkStandbyModeChanged(::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const bool enabled) {
                EXPECT_EQ(enabled, true);
                wg.Done();
            }))
        .WillOnce(::testing::Invoke(
            [&](const bool enabled) {
                EXPECT_EQ(enabled, false);
                wg.Done();
            }));

    auto status = powerManagerImpl->Register(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    // Enable WakeOnLAN
    powerManagerImpl->SetNetworkStandbyMode(true);
    wg.Wait();

    wg.Add(1);

    {
        std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_WIFI, false}};
        auto iterator = WakeupSourceConfigIteratorImpl::Create<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>(configs);

        powerManagerImpl->SetWakeupSourceConfig(iterator);
        EXPECT_EQ(status, Core::ERROR_NONE);
    }

    bool standbyMode = false;

    status = powerManagerImpl->GetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(standbyMode, true);

    {

        std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_LAN, false}};
        auto iterator = WakeupSourceConfigIteratorImpl::Create<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>(configs);

        // only after both WIFI and LAN wakeupSrc is enabled nwStandbyMode gets disabled
        powerManagerImpl->SetWakeupSourceConfig(iterator);
        EXPECT_EQ(status, Core::ERROR_NONE);
    }

    wg.Wait();

    status = powerManagerImpl->GetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(standbyMode, false);

    status = powerManagerImpl->Unregister(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
};

TEST_F(TestPowerManager, EnableWakeOnLAN)
{
    WaitGroup wg;
    wg.Add(1);

    Core::ProxyType<NetworkStandbyChangedEvent> nwstandbyModeChangedEvent = Core::ProxyType<NetworkStandbyChangedEvent>::Create();
    EXPECT_CALL(*nwstandbyModeChangedEvent, OnNetworkStandbyModeChanged(::testing::_))
        .WillOnce(::testing::Invoke(
            [&](const bool enabled) {
                EXPECT_EQ(enabled, true);
                wg.Done();
            }));

    auto status = powerManagerImpl->Register(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);

    {
        std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_WIFI, true}};
        auto iterator = WakeupSourceConfigIteratorImpl::Create<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>(configs);

        powerManagerImpl->SetWakeupSourceConfig(iterator);
        EXPECT_EQ(status, Core::ERROR_NONE);
    }

    bool standbyMode = false;

    status = powerManagerImpl->GetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(standbyMode, false);

    {
        std::list<WPEFramework::Exchange::IPowerManager::WakeupSourceConfig> configs = {{WakeupSrcType::WAKEUP_SRC_LAN, true}};
        auto iterator = WakeupSourceConfigIteratorImpl::Create<WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator>(configs);

        // only after both WIFI and LAN wakeupSrc is enabled nwStandbyMode gets enabled
        powerManagerImpl->SetWakeupSourceConfig(iterator);
        EXPECT_EQ(status, Core::ERROR_NONE);
    }

    wg.Wait();

    status = powerManagerImpl->GetNetworkStandbyMode(standbyMode);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(standbyMode, true);

    status = powerManagerImpl->Unregister(&(*nwstandbyModeChangedEvent));
    EXPECT_EQ(status, Core::ERROR_NONE);
};

TEST_F(TestPowerManager, TemperatureThresholds)
{
    EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](int high, int critical) {
                EXPECT_EQ(high, 90);
                EXPECT_EQ(critical, 95);
                return mfrERR_NONE;
            }));

    auto status = powerManagerImpl->SetTemperatureThresholds(90, 95);
    EXPECT_EQ(status, Core::ERROR_NONE);

    EXPECT_CALL(*p_mfrMock, mfrGetTempThresholds(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](int* high, int* critical) {
                *high     = 90;
                *critical = 95;
                return mfrERR_NONE;
            }));

    float high = 0, critical = 0;

    status = powerManagerImpl->GetTemperatureThresholds(high, critical);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(high, 90.00);
    EXPECT_EQ(critical, 95.00);
}

TEST_F(TestPowerManager, OverTemperatureGraceInterval)
{
    auto status = powerManagerImpl->SetOvertempGraceInterval(60);
    EXPECT_EQ(status, Core::ERROR_NONE);

    int interval = 0;
    status       = powerManagerImpl->GetOvertempGraceInterval(interval);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(interval, 60);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

TEST_F(TestPowerManager, ScheduleDeepSleepWakeupValid)
{
    TEST_LOG(">> Test: Schedule deep sleep wakeup with valid requestor");

    time_t futureTime = time(nullptr) + 300;  // 5 minutes in future

    uint32_t status = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime, "testApp");

    EXPECT_EQ(status, Core::ERROR_NONE);
    TEST_LOG("<< Test passed");
}

TEST_F(TestPowerManager, ScheduleDeepSleepWakeupInvalidRequestorWithSpace)
{
    TEST_LOG(">> Test: Schedule deep sleep wakeup with invalid requestor (space)");

    time_t futureTime = time(nullptr) + 300;

    uint32_t status = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime, "test app");

    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
    TEST_LOG("<< Test passed");
}

TEST_F(TestPowerManager, ScheduleDeepSleepWakeupEmptyRequestor)
{
    TEST_LOG(">> Test: Schedule deep sleep wakeup with empty requestor");

    time_t futureTime = time(nullptr) + 300;

    uint32_t status = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime, "");

    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
    TEST_LOG("<< Test passed");
}

TEST_F(TestPowerManager, ScheduleDeepSleepWakeupPastTime)
{
    TEST_LOG(">> Test: Schedule deep sleep wakeup with past time");

    time_t pastTime = time(nullptr) - 300;  // 5 minutes ago

    uint32_t status = powerManagerImpl->ScheduleDeepSleepWakeup(pastTime, "testApp");

    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
    TEST_LOG("<< Test passed");
}

TEST_F(TestPowerManager, ScheduleDeepSleepWakeupMultipleSchedules)
{
    TEST_LOG(">> Test: Schedule multiple deep sleep wakeups");

    time_t futureTime1 = time(nullptr) + 300;
    time_t futureTime2 = time(nullptr) + 600;

    uint32_t status1 = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime1, "netflix");
    uint32_t status2 = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime2, "smartHome");

    EXPECT_EQ(status1, Core::ERROR_NONE);
    EXPECT_EQ(status2, Core::ERROR_NONE);
    TEST_LOG("<< Test passed");
}

// Verify that when the deep sleep timer expires because of a previously registered
// ScheduleDeepSleepWakeup() schedule, the device transitions to POWER_STATE_STANDBY
// (ActiveStandby) as required by ONEM-42970, rather than the generic LIGHT_SLEEP
// fallback used when no schedule was consumed.
TEST_F(TestPowerManager, ScheduleDeepSleepWakeupConsumedTransitionsToStandby)
{
    TEST_LOG(">> Test: Scheduled deep sleep wakeup transitions to STANDBY on timer expiry");

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY_DEEP_SLEEP);
                return PWRMGR_SUCCESS;
            }))
        .WillOnce(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                EXPECT_EQ(powerState, PWRMGR_POWERSTATE_STANDBY);
                return PWRMGR_SUCCESS;
            }));

    WaitGroup wg;
    wg.Add();
    Core::ProxyType<PowerModeChangedEvent> modeChanged = Core::ProxyType<PowerModeChangedEvent>::Create();
    EXPECT_CALL(*modeChanged, OnPowerModeChanged(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
            }))
        .WillOnce(::testing::Invoke(
            [&](const PowerState prevState, const PowerState newState) {
                EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
                EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY);
                wg.Done();
            }));

    Core::ProxyType<DeepSleepWakeupEvent> deepSleepTimeout = Core::ProxyType<DeepSleepWakeupEvent>::Create();
    EXPECT_CALL(*deepSleepTimeout, OnDeepSleepTimeout(::testing::_))
        .WillOnce(::testing::Invoke(
            [](const int timeout) {
                EXPECT_EQ(timeout, 2);
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_SetDeepSleep(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(
            [](uint32_t deep_sleep_timeout, bool* isGPIOWakeup, bool networkStandby) {
                EXPECT_EQ(deep_sleep_timeout, 2U);
                EXPECT_TRUE(nullptr != isGPIOWakeup);
                EXPECT_EQ(networkStandby, false);
                // Simulate timer wakeup
                *isGPIOWakeup = false;
                std::this_thread::sleep_for(std::chrono::seconds(deep_sleep_timeout));
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_GetLastWakeupReason(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](DeepSleep_WakeupReason_t* wakeupReason) {
                *wakeupReason = DEEPSLEEP_WAKEUPREASON_TIMER;
                return DEEPSLEEPMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_DeepSleepWakeup())
        .WillOnce(testing::Return(DEEPSLEEPMGR_SUCCESS));

    uint32_t status = powerManagerImpl->Register(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Register(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    // Schedule a wakeup 2 seconds in the future; ScheduleDeepSleepWakeup() internally
    // arms the deep sleep timer to match the nearest schedule.
    time_t futureTime = time(nullptr) + 2;
    status = powerManagerImpl->ScheduleDeepSleepWakeup(futureTime, "testApp");
    EXPECT_EQ(status, Core::ERROR_NONE);

    int keyCode = 0;
    status      = powerManagerImpl->SetPowerState(keyCode, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "l1-test");
    EXPECT_EQ(status, Core::ERROR_NONE);

    PowerState newState  = PowerState::POWER_STATE_UNKNOWN;
    PowerState prevState = PowerState::POWER_STATE_UNKNOWN;

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    wg.Wait();

    status = powerManagerImpl->GetPowerState(newState, prevState);
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_EQ(newState, PowerState::POWER_STATE_STANDBY);
    EXPECT_EQ(prevState, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);

    status = powerManagerImpl->Unregister(&(*modeChanged));
    EXPECT_EQ(status, Core::ERROR_NONE);

    status = powerManagerImpl->Unregister(&(*deepSleepTimeout));
    EXPECT_EQ(status, Core::ERROR_NONE);

    TEST_LOG("<< Test passed");
}
