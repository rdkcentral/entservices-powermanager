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

#include "gmock/gmock.h"
#include <algorithm>
#include <condition_variable>
#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Services.h>
#include <cstring>
#include <interfaces/IPowerManager.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mutex>

#include "ThermalController.h"

// mocks
#include "IarmBusMock.h"
#include "MfrMock.h"
#include "RfcApiMock.h"
#include "WrapsMock.h"
#include "hal/ThermalImpl.h"
#include "PowerManagerHalMock.h"
#include "MfrMock.h"

using namespace WPEFramework;
using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

class WaitGroup {
public:
    WaitGroup()
        : _count(0)
    {
    }

    void Add(int count = 1)
    {
        _count += count;

        if (_count <= 0)
            _cv.notify_all();
    }

    void Done() { Add(-1); }

    void Wait()
    {
        if (_count <= 0)
            return;

        std::unique_lock<std::mutex> _lock { _m };
        _cv.wait(_lock, [&]() {
            return _count == 0;
        });
    }

private:
    std::atomic<int> _count;
    std::mutex _m;
    std::condition_variable _cv;
};

class TestThermalController : public ::testing::Test, public ThermalController::INotification {

public:
    MOCK_METHOD(void, onThermalTemperatureChanged, (const ThermalTemperature, const ThermalTemperature, const float current_Temp), (override));
    MOCK_METHOD(void, onDeepSleepForThermalChange, (), (override));

    TestThermalController()
    {
        p_wrapsImplMock = new testing::NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        p_rfcApiImplMock = new testing::NiceMock<RfcApiImplMock>;
        RfcApi::setImpl(p_rfcApiImplMock);

        p_powerManagerHalMock = new testing::NiceMock<PowerManagerHalMock>;
        PowerManagerAPI::setImpl(p_powerManagerHalMock);

        p_mfrMock = new testing::NiceMock<mfrMock>;
        mfr::setImpl(p_mfrMock);

        p_iarmBusMock = new testing::NiceMock<IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusMock);

        setupDefaultMocks();
    }

    void setupDefaultMocks()
    {

        ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    strncpy(pstParamData->name, pcParameterName, sizeof(pstParamData->name) - 1);
                    pstParamData->name[sizeof(pstParamData->name) - 1] = '\0'; // Ensure null termination

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

        // called from ThermalController constructor in initializeThermalProtection
        EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](int high, int critical) {
                    EXPECT_EQ(high, 100);
                    EXPECT_EQ(critical, 110);
                    return mfrERR_NONE;
                }));
    }

    ~TestThermalController() override
    {
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

        IarmBus::setImpl(nullptr);
        if (p_iarmBusMock != nullptr) {
            delete p_iarmBusMock;
            p_iarmBusMock = nullptr;
        }
    }

protected:
    WrapsImplMock* p_wrapsImplMock     = nullptr;
    RfcApiImplMock* p_rfcApiImplMock   = nullptr;
    PowerManagerHalMock* p_powerManagerHalMock = nullptr;
    mfrMock *p_mfrMock = nullptr;
    IarmBusImplMock* p_iarmBusMock = nullptr;
};

TEST_F(TestThermalController, temperatureThresholds)
{
    WaitGroup wg;


    wg.Add();

    EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                *curTemperature  = 60; // safe temperature
                *curState        = (mfrTemperatureState_t)PWRMGR_TEMPERATURE_NORMAL;
                *wifiTemperature = 25;
                wg.Done();
                return mfrERR_NONE;
            }));

    auto controller = ThermalController::Create(*this);

    // Set
    {
        EXPECT_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](int high, int critical) {
                    EXPECT_EQ((int)high, 90);
                    EXPECT_EQ((int)critical, 100);
                    return mfrERR_NONE;
                }));

        uint32_t res = controller.SetTemperatureThresholds(90, 100);
        EXPECT_EQ(res, Core::ERROR_NONE);
    }

    // Get
    {
        EXPECT_CALL(*p_mfrMock, mfrGetTempThresholds(::testing::_, ::testing::_))
            .WillOnce(::testing::Invoke(
                [](int* high, int* critical) {
                    *high     = 90;
                    *critical = 100;
                    return mfrERR_NONE;
                }));

        float high, critical;

        uint32_t res = controller.GetTemperatureThresholds(high, critical);

        EXPECT_EQ(res, Core::ERROR_NONE);
        EXPECT_EQ(high, 90);
        EXPECT_EQ(critical, 100);
    }

    // wait for test to end
    wg.Wait();
}

TEST_F(TestThermalController, modeChangeHigh)
{
    WaitGroup wg;

    EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                *curTemperature  = 100; // high temperature
                *curState        = (mfrTemperatureState_t)mfrTEMPERATURE_HIGH;
                *wifiTemperature = 25;
                return mfrERR_NONE;
            }));

    wg.Add();
    EXPECT_CALL(*this, onThermalTemperatureChanged(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&](const ThermalTemperature curr_mode, const ThermalTemperature new_mode, const float temp) {
            EXPECT_EQ(curr_mode, ThermalTemperature::THERMAL_TEMPERATURE_NORMAL);
            EXPECT_EQ(new_mode, ThermalTemperature::THERMAL_TEMPERATURE_HIGH);
            EXPECT_EQ(int(temp), 100);
            wg.Done();
        }));

    auto controller = ThermalController::Create(*this);

    wg.Wait();
}

TEST_F(TestThermalController, modeChangeCritical)
{
    WaitGroup wg;

    EXPECT_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                *curTemperature  = 115; // critical temperature
                *curState        = (mfrTemperatureState_t)mfrTEMPERATURE_CRITICAL;
                *wifiTemperature = 25;
                return mfrERR_NONE;
            }));

    wg.Add();
    EXPECT_CALL(*this, onThermalTemperatureChanged(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&](const ThermalTemperature curr_mode, const ThermalTemperature new_mode, const float temp) {
            EXPECT_EQ(curr_mode, ThermalTemperature::THERMAL_TEMPERATURE_NORMAL);
            EXPECT_EQ(new_mode, ThermalTemperature::THERMAL_TEMPERATURE_CRITICAL);
            EXPECT_EQ(int(temp), 115);
            wg.Done();
        }));

    wg.Add();
    EXPECT_CALL(*this, onDeepSleepForThermalChange())
        .WillOnce(::testing::Invoke([&]() {
            wg.Done();
        }));

    auto controller = ThermalController::Create(*this);

    wg.Wait();
}
