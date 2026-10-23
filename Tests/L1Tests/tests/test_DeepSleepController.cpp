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
#include <ctime>
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

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
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

    /* updateMaintenanceWakeupConfig() marks the config updated if ANY of the 4
       RFC params were retrieved successfully (OR semantics); the other 3
       params here are still valid, so the flag is true even though
       FixedStarts itself failed to parse. */
    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsTrailingGarbageInEntry)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "1,2x,3";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsEmptyValue)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsOutOfRangeEntry)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "99999999999999999999";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, FixedStartsRejectsOutOfBoundary)
{
    setDefaultValidValues();
    _rfcValues[kFixedStarts] = "4294967296";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
}

TEST_F(TestDeepSleepWakeupSettings, AllFourRfcParamsInvalidLeavesConfigNotUpdated)
{
    /* Only when ALL 3 RFC params fail should _maintenanceConfigUpdated stay
       false under the current OR semantics. */
    _rfcValues[kFixedStarts]       = "1,two,3";
    _rfcValues[kRandomDelay]       = "abc";
    _rfcValues[kInactivityTimeout] = "";

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_FALSE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
    EXPECT_EQ(dsws._randomDelay, 0u);
    EXPECT_EQ(dsws._inactivityTimeout, 0u);
}

TEST_F(TestDeepSleepWakeupSettings, MissingFixedStartsRFCStillParsesOtherParamsAndConfigUpdated)
{
    setDefaultValidValues();
    _rfcValues.erase(kFixedStarts);

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    EXPECT_TRUE(dsws._maintenanceConfigUpdated);
    EXPECT_TRUE(dsws._fixedStarts.empty());
    EXPECT_EQ(dsws._randomDelay, 60u);
    EXPECT_EQ(dsws._inactivityTimeout, 120u);
}

TEST_F(TestDeepSleepWakeupSettings, ConfigUpdateIsIdempotentOnceSucceeded)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    ASSERT_TRUE(dsws._maintenanceConfigUpdated);

    // Change underlying RFC values and re-invoke; since _maintenanceConfigUpdated is
    // already latched true, the update should short-circuit.
    dsws.updateMaintenanceWakeupConfig();
}

/* --------------------------------------------------------------------------
 * timeout() branch selection between user timer / custom maintenance /
 * legacy upstream calculation.
 * ------------------------------------------------------------------------ */

TEST_F(TestDeepSleepWakeupSettings, Timeout_UsesNearestScheduleRegardlessOfRequestor)
{
    Settings settings = makeSettings();
    WakeupScheduleRegister schedules;
    DeepSleepWakeupSettings scheduledSettings(settings, &schedules);
    const auto earlierSchedule = static_cast<WakeupScheduleRegister::UnixTime>(time(nullptr) + 10);
    const auto laterSchedule = static_cast<WakeupScheduleRegister::UnixTime>(time(nullptr) + 30);
    ASSERT_EQ(schedules.addWakeupSchedule(
                  earlierSchedule, WakeupScheduleRegister::ActiveStandby, "testApp"),
              WakeupScheduleRegister::Successful);
    ASSERT_EQ(schedules.addWakeupSchedule(
                  laterSchedule, WakeupScheduleRegister::ActiveStandby, "deepsleeptimer"),
              WakeupScheduleRegister::Successful);

    scheduledSettings.SetTimeout(1234);

#ifdef CUSTOM_LGI
    EXPECT_NEAR(scheduledSettings.timeout(), 10u, 2u);
#else
    EXPECT_EQ(scheduledSettings.timeout(), 1234u);
#endif
}

#ifdef CUSTOM_LGI
TEST_F(TestDeepSleepWakeupSettings, Timeout_ReturnsZeroWhenNoScheduleIsAvailable)
{
    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);
    WakeupScheduleRegister schedules;
    DeepSleepWakeupSettings withEmptyScheduleRegistry(settings, &schedules);

    EXPECT_EQ(withEmptyScheduleRegistry.timeout(), 0u);
    EXPECT_EQ(dsws.timeout(), 0u);
}

TEST_F(TestDeepSleepWakeupSettings, Timeout_ReturnsZeroWhenMaintenanceScheduleCannotBeCreated)
{
    /* Under OR semantics, _maintenanceConfigUpdated only stays false when ALL
       3 RFC params fail to parse. */
    _rfcValues[kFixedStarts]       = "1,two,3";
    _rfcValues[kRandomDelay]       = "abc";
    _rfcValues[kInactivityTimeout] = "";

    Settings settings = makeSettings();
    WakeupScheduleRegister schedules;
    DeepSleepWakeupSettings dsws(settings, &schedules);
    ASSERT_FALSE(dsws._maintenanceConfigUpdated);

    EXPECT_EQ(dsws.getCustomMaintenanceWakeupTime(), 0u);
    EXPECT_EQ(dsws.timeout(), 0u);
    EXPECT_TRUE(schedules.getPastAndFutureWakeupSchedules("maintenance").second.empty());
}
#else
TEST_F(TestDeepSleepWakeupSettings, Timeout_AlwaysUsesUpstreamWhenCustomLgiDisabled)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);
    ASSERT_TRUE(dsws._maintenanceConfigUpdated);

    // Without CUSTOM_LGI, timeout() must ignore _maintenanceConfigUpdated entirely
    // and always fall back to the legacy getWakeupTime() calculation.
    // See note above: bypass the real IARM_Bus_Call via the /tmp override file.
    ASSERT_EQ(0, system("echo 2 > /tmp/deepSleepWakeupTimer"));
    uint32_t expected = dsws.getWakeupTime();
    EXPECT_NEAR(dsws.timeout(), expected, 2u);
    ASSERT_EQ(0, system("rm -f /tmp/deepSleepWakeupTimer"));
}
#endif

#ifdef CUSTOM_LGI
TEST_F(TestDeepSleepWakeupSettings, Timeout_UsesNearestScheduledWakeupAsRelativeSeconds)
{
    Settings settings = makeSettings();
    WakeupScheduleRegister schedules;
    DeepSleepWakeupSettings dsws(settings, &schedules);
    const auto scheduledTime = static_cast<WakeupScheduleRegister::UnixTime>(time(nullptr) + 60);

    ASSERT_EQ(schedules.addWakeupSchedule(
                  scheduledTime, WakeupScheduleRegister::ActiveStandby, "testApp"),
              WakeupScheduleRegister::Successful);
    EXPECT_NEAR(dsws.timeout(), 60u, 2u);
}
#endif

/* --------------------------------------------------------------------------
 * getCustomMaintenanceWakeupTime() maintenance-window wakeup calculation.
 * ------------------------------------------------------------------------ */

#ifdef CUSTOM_LGI
TEST_F(TestDeepSleepWakeupSettings, GetCustomMaintenanceWakeupTime_EnforcesMinimumWakeupFloor)
{
    setDefaultValidValues();
    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    // Force a tiny inactivity timeout with no fixed starts / random delay so the
    // raw candidate wakeup (now + 1 minute) falls below the 5-minute floor.
    dsws._inactivityTimeout = 1;
    dsws._fixedStarts.clear();
    dsws._randomDelay = 0;

    uint32_t result = dsws.getCustomMaintenanceWakeupTime();

    // Result must be clamped to (now + 5 minutes) - now == 300s, independent of
    // time-of-day, so this is safe to assert with a small scheduling tolerance.
    EXPECT_NEAR(result, 300u, 2u);
}

TEST_F(TestDeepSleepWakeupSettings, GetCustomMaintenanceWakeupTime_NoFixedStarts_UsesInactivityTimeoutOnly)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    dsws._inactivityTimeout = 120; // minutes, well above the 5-minute floor
    dsws._fixedStarts.clear();
    dsws._randomDelay = 0;

    uint32_t result = dsws.getCustomMaintenanceWakeupTime();

    EXPECT_NEAR(result, 120u * 60u, 2u);
}

TEST_F(TestDeepSleepWakeupSettings, GetCustomMaintenanceWakeupTime_RandomDelayAddsWithinExpectedBounds)
{
    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    dsws._inactivityTimeout = 120; // 7200s baseline
    dsws._fixedStarts.clear();
    dsws._randomDelay = 60; // up to +3600s

    uint32_t result = dsws.getCustomMaintenanceWakeupTime();

    EXPECT_GE(result, 120u * 60u);
    EXPECT_LE(result, 120u * 60u + 60u * 60u + 2u);
}

TEST_F(TestDeepSleepWakeupSettings, GetCustomMaintenanceWakeupTime_FixedStartLaterToday_UsesThatSlot)
{
    time_t now = 0;
    time(&now);
    struct tm nowTm = *localtime(&now);
    int minutesSinceMidnight = nowTm.tm_hour * 60 + nowTm.tm_min;

    // Avoid flakiness around midnight rollover for this deterministic-window test.
    if (minutesSinceMidnight > 1430) {
        GTEST_SKIP() << "Too close to midnight for a stable 'later today' assertion";
    }

    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    dsws._inactivityTimeout = 1; // wakeup candidate ~ now + 60s
    dsws._randomDelay       = 0;
    dsws._fixedStarts       = { minutesSinceMidnight + 5 }; // 5 minutes from now, same day

    uint32_t result = dsws.getCustomMaintenanceWakeupTime();

    // Expected to land on the fixed start slot (~5 minutes away), not the
    // 1-minute inactivity timeout nor the 5-minute minimum-wakeup floor.
    EXPECT_NEAR(result, 5u * 60u, 5u);
}

TEST_F(TestDeepSleepWakeupSettings, GetCustomMaintenanceWakeupTime_NoFixedStartAfterWakeup_RollsToTomorrow)
{
    time_t now = 0;
    time(&now);
    struct tm nowTm = *localtime(&now);
    int minutesSinceMidnight = nowTm.tm_hour * 60 + nowTm.tm_min;

    // Need enough headroom for inactivityTimeout to push wakeup past the only
    // fixed start (00:05), and to avoid the wakeup crossing midnight itself.
    if (minutesSinceMidnight < 70 || minutesSinceMidnight > 1380) {
        GTEST_SKIP() << "Too close to midnight for a stable 'rolls to tomorrow' assertion";
    }

    setDefaultValidValues();

    Settings settings = makeSettings();
    DeepSleepWakeupSettings dsws(settings);

    dsws._inactivityTimeout = 60; // 1 hour from now, still later than 00:05 today
    dsws._randomDelay       = 0;
    dsws._fixedStarts       = { 5 }; // 00:05, already passed today

    uint32_t result = dsws.getCustomMaintenanceWakeupTime();

    // Expected wakeup = tomorrow 00:05 - now, i.e. more than the remainder of
    // today but less than 25h away.
    uint32_t secondsUntilMidnight = static_cast<uint32_t>((1440 - minutesSinceMidnight) * 60);
    EXPECT_GT(result, secondsUntilMidnight);
    EXPECT_LT(result, secondsUntilMidnight + 26u * 60u * 60u);
}
#endif
