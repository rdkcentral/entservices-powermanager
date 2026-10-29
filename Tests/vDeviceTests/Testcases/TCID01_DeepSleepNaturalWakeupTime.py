"""
/**
 * @file TCID01_DeepSleepNaturalWakeupTime.py
 * @brief L3 PowerManager coverage testcase for the natural deep-sleep wakeup-time path.
 *
 * @testcase TCID01_DeepSleepNaturalWakeupTime
 * @details Enters DEEP_SLEEP WITHOUT first calling setDeepSleepTimer so that the
 *          plugin computes the wakeup timeout via DeepSleepWakeupSettings::
 *          getWakeupTime() (and its helpers getTZDiffInSec() / secure_random())
 *          instead of returning a stored/overridden value.
 *
 *          Rationale / ordering requirement:
 *            DeepSleepWakeupSettings::timeout() only calls getWakeupTime() while
 *            the internal _isDeepSleepTimeoutSet flag is false. That flag is set
 *            permanently (for the plugin lifetime) the first time setDeepSleepTimer
 *            is invoked. Every other deep-sleep testcase calls setDeepSleepTimer
 *            before sleeping, which is why getWakeupTime() is otherwise never
 *            exercised. THIS TEST MUST THEREFORE RUN FIRST in the suite (before
 *            any setDeepSleepTimer call) - see SuiteManager.SUITES ordering.
 *
 *          Because the natural wakeup time resolves to hours (wake-till-~2-3AM +
 *          timezone offset), the configured timer will not expire within the test
 *          window, so an external CEC wake stimulus is injected to bring the
 *          device back - identical to the external-wake pattern used by TCID01.
 *
 * @precondition
 *  - org.rdk.PowerManager plugin is active and reachable via JSON-RPC endpoint.
 *  - No setDeepSleepTimer has been issued yet in this plugin lifetime.
 *
 * @dependencies
 *  - utils.py
 *  - PowerManager_Curl.py
 *  - PowerManager_CombinationHelpers.py
 *  - SuiteManager.py
 *  - vcomponent_configurations/commands/DeepSleep_Wakeup_CEC.yaml
 *
 * @expected_result
 *  - DEEP_SLEEP is entered using the computed natural wakeup time, and the
 *    injected CEC stimulus wakes the device with wakeup reason CEC.
 *
 * @pass_criteria
 *  - DEEP_SLEEP entry succeeds, the device wakes on CEC, and configuration is
 *    restored.
 *
 * @failure_criteria
 *  - DEEP_SLEEP entry fails, the device does not wake, or the wakeup reason is
 *    not CEC.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, parse_last_wakeup_reason, parse_power_state, parse_wakeup_config, wakeup_map


def _post_deepsleep(yaml_file):
    http_code, body = send_vcomponent_command(f"{POWERMANAGER_CMD_BASE}/{yaml_file}")
    log_warning(f"vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def _wait_for_awake_state(timeout_seconds=20):
    deadline = time.time() + timeout_seconds
    last_state = None
    while time.time() < deadline:
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        last_state = parse_power_state(state_resp)
        if isinstance(last_state, dict) and last_state.get("currentState") != "DEEP_SLEEP":
            return last_state
        time.sleep(1)
    return last_state


def _wait_for_wakeup_reason(expected_reason, timeout_seconds=20):
    deadline = time.time() + timeout_seconds
    last_reason = None
    while time.time() < deadline:
        reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"Wakeup reason response: {reason_resp}")
        last_reason = parse_last_wakeup_reason(reason_resp)
        if last_reason == expected_reason:
            return last_reason
        time.sleep(1)
    return last_reason


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (unable to read baseline config)")
        return False

    try:
        # Enable CEC as the external wake source (TIMER left enabled so the
        # natural computed wakeup timeout governs the timer path). No
        # setDeepSleepTimer is issued anywhere in this test on purpose.
        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                build_wakeup_override_entries(original_config, {
                    "TIMER": True,
                    "CEC": True,
                })
            )
        )
        log_warning(f"Set response: {set_resp}")
        if not is_ok(set_resp):
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (failed to enable CEC wake source)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {config_resp}")
        configured = parse_wakeup_config(config_resp)
        if not isinstance(configured, list) or get_source_enabled(configured, "CEC") is not True:
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (CEC not enabled before deep sleep)")
            return False

        # Baseline ON before sleeping.
        on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-031"))
        log_warning(f"ON baseline response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (failed to set ON baseline)")
            return False

        # Enter DEEP_SLEEP WITHOUT setDeepSleepTimer. This forces
        # DeepSleepWakeupSettings::timeout() into the getWakeupTime() branch,
        # exercising getWakeupTime(), getTZDiffInSec() and secure_random().
        log_info("Executing setPowerState(DEEP_SLEEP) using natural computed wakeup time (no setDeepSleepTimer)")
        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-031", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        # The natural wakeup time resolves to hours, so it will not expire during
        # the test - inject an external CEC wake to bring the device back.
        log_warning("Allowing the device to settle in DEEP_SLEEP before injecting the external CEC wake.")
        time.sleep(3)

        if not _post_deepsleep("DeepSleep_Wakeup_CEC.yaml"):
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (failed to post CEC wake simulation)")
            return False

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            log_error("TCID01_DeepSleepNaturalWakeupTime Failed ❌ (device did not report a post-wake power state)")
            return False

        reason = _wait_for_wakeup_reason("CEC")
        if reason != "CEC":
            log_error(
                "TCID01_DeepSleepNaturalWakeupTime Failed ❌ "
                f"(last wakeup reason was not CEC after natural-wakeup deep sleep). Returned: {reason}"
            )
            return False
        log_success("✅ Device woke on CEC after entering DEEP_SLEEP with the natural computed wakeup time")
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-031-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID01_DeepSleepNaturalWakeupTime Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
