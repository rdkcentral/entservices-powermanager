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

#include <core/Portability.h>
#include <core/Proxy.h>
#include <core/Services.h>
#include <interfaces/IPowerManager.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "PowerManagerImplementation.h"

using namespace WPEFramework;

#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);
struct PowerManagerSettingsParam {
    // test inputs
    PowerState powerState; // persisted power state
    uint32_t deepSleepTimeout;  // persisted deepsleep timeout
    bool nwStandbyMode;    // persisted network standby mode

    bool restart; // similate plugin restart

    // test outputs
    PowerState powerStateEx;             // expected power state after reboot
    PowerState powerStateBeforeRebootEx; // expected last power state before reboot
};

class TestPowerManagerSettings : public ::testing::TestWithParam<PowerManagerSettingsParam> {

public:
    TestPowerManagerSettings()
        : _settingsFile("/tmp/test_uimgr_settings.bin")
    {
    }

    ~TestPowerManagerSettings()
    {
        struct stat buf = {};
        // after every testcase run expect settings file to be populated
        EXPECT_EQ(stat(_settingsFile.c_str(), &buf), 0);

        std::string rmCmd = "rm -f " + _settingsFile;

        if(0!=system(rmCmd.c_str())){/* do nothig */}
        if(0!=system("rm -f /tmp/pwrmgr_restarted")){/* do nothig */}
        if(0!=system("rm -f /tmp/uimgr_settings.bin")){/* do nothig */}
    }

    void populateSettingsV1(PowerState prevState, uint32_t deepSleepTimeout, bool nwStandbyMode)
    {
        Settings settings = Settings::Load(_settingsFile);

        settings.SetPowerState(prevState);
        settings.SetDeepSleepTimeout(deepSleepTimeout);
        settings.SetNwStandbyMode(nwStandbyMode);

        settings.Save(_settingsFile);
    }

protected:
    std::string _settingsFile;
};

TEST_F(TestPowerManagerSettings, Empty)
{
    Settings settings = Settings::Load(_settingsFile);

#ifdef PLATCO_BOOTTO_STANDBY
    // If BOOTTO_STANDBY is enabled, device boots in STANDBY by default.
    EXPECT_EQ(settings.powerState(), PowerState::POWER_STATE_STANDBY);
#else
    // default expected power state is ON
    EXPECT_EQ(settings.powerState(), PowerState::POWER_STATE_ON);
#endif
    EXPECT_EQ(settings.powerStateBeforeReboot(), PowerState::POWER_STATE_ON);
    EXPECT_EQ(settings.deepSleepTimeout(), 8U * 60U * 60U); // 8 hours
    EXPECT_EQ(settings.nwStandbyMode(), false);

    int ret = access("/tmp/uimgr_settings.bin", F_OK);
    EXPECT_EQ(ret, 0); // file should be present

    settings.SetPowerState(PowerState::POWER_STATE_STANDBY);
    settings.Save(_settingsFile);

    Settings ramsettings = Settings::Load(_settingsFile);
    // Last PowerState is ON while boot
    EXPECT_EQ(ramsettings.powerStateBeforeReboot(), PowerState::POWER_STATE_ON);
}

TEST_P(TestPowerManagerSettings, AllTests)
{
    const auto& param = GetParam();

    if(0!=system("rm -f /tmp/uimgr_settings.bin")){/* do nothig */}
    populateSettingsV1(param.powerState, param.deepSleepTimeout, param.nwStandbyMode);

    if (param.restart) {
        if(0!=system("touch /tmp/pwrmgr_restarted"))
        {
            TEST_LOG("system() failed");
        }
    }

    if(0!=system("rm -f /tmp/uimgr_settings.bin")){/* do nothig */}
    Settings settings = Settings::Load(_settingsFile);

    EXPECT_EQ(settings.powerState(), param.powerStateEx);
    EXPECT_EQ(settings.powerStateBeforeReboot(), param.powerStateBeforeRebootEx);
    EXPECT_EQ(settings.deepSleepTimeout(), param.deepSleepTimeout);
    EXPECT_EQ(settings.nwStandbyMode(), param.nwStandbyMode);
}

INSTANTIATE_TEST_SUITE_P(
    PowerStateTests,
    TestPowerManagerSettings,
    ::testing::Values(
        /* ------------------------------ graceful restart (device reboot) --------------------- */
        // 0
        PowerManagerSettingsParam {
            .powerState       = PowerState::POWER_STATE_ON,
            .deepSleepTimeout = 2 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = false,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_ON }, // on device, APP moves to device state to ON

        // 1
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY,
            .deepSleepTimeout = 1 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = false,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY },

        // 2
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP,
            .deepSleepTimeout = 1 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = false,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP },

        // 3
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP,
            .deepSleepTimeout = 60,
            .nwStandbyMode    = false,
            .restart          = false,

            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP },

        /* ------------------------------ Power Manager plugin restart --------------------- */
        // 4
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_ON,
            .deepSleepTimeout = 2 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = true,
            // output
            .powerStateEx             = PowerState::POWER_STATE_ON,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_ON },

        // 5
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY,
            .deepSleepTimeout = 1 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = true,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY },

        // 6
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP,
            .deepSleepTimeout = 1 * 60 * 60,
            .nwStandbyMode    = true,
            .restart          = true,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP },

        // 7
        PowerManagerSettingsParam {
            // input
            .powerState       = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP,
            .deepSleepTimeout = 60,
            .nwStandbyMode    = false,
            .restart          = true,
            // output
            .powerStateEx             = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP,
            .powerStateBeforeRebootEx = PowerState::POWER_STATE_STANDBY_DEEP_SLEEP })
    // end
);
