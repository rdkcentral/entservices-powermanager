/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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
#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Services.h>
#include <interfaces/IPowerManager.h>
#include <map>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "RfcApiMock.h"

#define private public
#define protected public
#include "DeepSleepController.h"
#undef private
#undef protected

using namespace WPEFramework;

namespace {
/* Mirrors the RFC parameter names used internally by DeepSleepController.cpp */
const char* kWakeupDuration    = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.WakeupDuration";
const char* kFixedStarts       = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.FixedStarts";
const char* kRandomDelay       = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.RandomDelay";
const char* kInactivityTimeout = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.MaintenanceWakeup.InactivityTimeout";
}

class TestDeepSleepWakeupSettings : public ::testing::Test {
public:
    TestDeepSleepWakeupSettings()
    {
        p_rfcApiImplMock = new testing::NiceMock<RfcApiImplMock>;
        RfcApi::setImpl(p_rfcApiImplMock);

        ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [this](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    strncpy(pstParamData->name, pcParameterName, sizeof(pstParamData->name) - 1);
                    pstParamData->name[sizeof(pstParamData->name) - 1] = '\0';

                    auto it = _rfcValues.find(pcParameterName);
                    if (it == _rfcValues.end()) {
                        return WDMP_FAILURE;
                    }

                    strncpy(pstParamData->value, it->second.c_str(), sizeof(pstParamData->value) - 1);
                    pstParamData->value[sizeof(pstParamData->value) - 1] = '\0';
                    return WDMP_SUCCESS;
                }));
    }

    ~TestDeepSleepWakeupSettings() override
    {
        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr) {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }
    }

protected:
    void setDefaultValidValues()
    {
        _rfcValues[kWakeupDuration]    = "30";
        _rfcValues[kFixedStarts]       = "1,2,3";
        _rfcValues[kRandomDelay]       = "60";
        _rfcValues[kInactivityTimeout] = "120";
    }

    Settings makeSettings()
    {
        return Settings::Load("/tmp/test_deepsleep_wakeup_settings.bin");
    }

    RfcApiImplMock* p_rfcApiImplMock = nullptr;
    std::map<std::string, std::string> _rfcValues;
};

TEST_F(TestDeepSleepWakeupSettings, ValidConfigIsParsedAndApplied)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceRfcUpdated);
    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 30u);
    EXPECT_EQ(dsws._randomDelay, 60u);
    EXPECT_EQ(dsws._inactivityTimeout, 120u);
    ASSERT_EQ(dsws._fixedStarts.size(), 3u);
    EXPECT_EQ(dsws._fixedStarts[0], 1);
    EXPECT_EQ(dsws._fixedStarts[1], 2);
    EXPECT_EQ(dsws._fixedStarts[2], 3);
}

TEST_F(TestDeepSleepWakeupSettings, QuotedIntegerRFCValueIsAccepted)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "\"120\"";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    ASSERT_EQ(dsws._fixedStarts.size(), 1u);
    EXPECT_EQ(dsws._fixedStarts[0], 120);
}

TEST_F(TestDeepSleepWakeupSettings, QuotedFixedStartsListIsAccepted)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "\"1,2,3\"";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    ASSERT_EQ(dsws._fixedStarts.size(), 3u);
    EXPECT_EQ(dsws._fixedStarts[0], 1);
    EXPECT_EQ(dsws._fixedStarts[1], 2);
    EXPECT_EQ(dsws._fixedStarts[2], 3);
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsNonNumericEntry)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "1,two,3";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_FALSE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsTrailingGarbageInEntry)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "1,2x,3";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsEmptyValue)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsOutOfRangeEntry)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "99999999999999999999";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsOutOfBoundary)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "4294967296";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, WakeupDurationRejectsOverflow)
{
    setDefaultValidValues();
    _rfcValues[kWakeupDuration] = "99999999999999999999";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_FALSE(dsws._maintenanceConfigUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, WakeupDurationRejectsNegativeValue)
{
    setDefaultValidValues();
    _rfcValues[kWakeupDuration] = "-5";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, WakeupDurationRejectsTrailingGarbage)
{
    setDefaultValidValues();
    _rfcValues[kWakeupDuration] = "123abc";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, WakeupDurationRejectsValueTooLong)
{
    setDefaultValidValues();
    /* MAX_MAINTENANCE_RFC is 20 chars; this value is 21 chars. */
    _rfcValues[kWakeupDuration] = "123456789012345678901";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, MissingWakeupDurationRFCStopsConfigUpdate)
{
    setDefaultValidValues();
    _rfcValues.erase(kWakeupDuration);

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_FALSE(dsws._maintenanceConfigUpdated);
    /* Nothing beyond WakeupDuration should have been consulted. */
    EXPECT_TRUE(dsws._fixedStarts.empty());
    EXPECT_EQ(dsws._randomDelay, 0u);
    EXPECT_EQ(dsws._inactivityTimeout, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, MissingFixedStartsRFCStopsConfigUpdateAfterWakeupDuration)
{
    setDefaultValidValues();
    _rfcValues.erase(kFixedStarts);

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceRfcUpdated);
    EXPECT_FALSE(dsws._maintenanceConfigUpdated);
    EXPECT_EQ(dsws._wakeupDurationSec, 30u); /* already parsed before failure */
    EXPECT_TRUE(dsws._fixedStarts.empty());
    EXPECT_EQ(dsws._randomDelay, 0u);
    EXPECT_EQ(dsws._inactivityTimeout, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, ConfigUpdateIsIdempotentOnceSucceeded)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    ASSERT_TRUE(dsws._maintenanceConfigUpdated);

    // Change underlying RFC values and re-invoke; since _maintenanceConfigUpdated is
    // already latched true, the update should short-circuit and the
    // previously parsed values must remain unchanged.
    _rfcValues[kWakeupDuration] = "999";
    dsws.updateMaintenanceWakeupConfig();

    EXPECT_EQ(dsws._wakeupDurationSec, 30u);
}
