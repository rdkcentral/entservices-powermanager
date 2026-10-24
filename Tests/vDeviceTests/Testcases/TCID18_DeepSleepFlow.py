"""
/**
 * @file TCID18_DeepSleepFlow.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID18_DeepSleepFlow
 * @details Validates the deep-sleep timeout flow across setDeepSleepTimer,
 *          setPowerState, getPowerState, getTimeSinceWakeup, and the
 *          registration handling for the deep-sleep timeout and power-mode
 *          events while covering the main ON/LIGHT_SLEEP/DEEP_SLEEP/STANDBY
 *          transitions in one testcase.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, note_partial, parse_power_state, parse_time_since_wakeup, parse_wakeup_config, wakeup_map


def _assert_state(label, response, expected_current, expected_previous=None):
    state = parse_power_state(response)
    if not isinstance(state, dict):
        return False, f"{label} returned an invalid power-state payload"
    if state.get("currentState") != expected_current:
        return False, f"{label} currentState mismatch"
    if expected_previous is not None:
        previous = state.get("previousState", state.get("prevState"))
        if previous != expected_previous:
            return False, f"{label} previousState mismatch"
    return True, None


def run_test():
    start_time = time.perf_counter()

    original_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_config_resp}")
    original_config = parse_wakeup_config(original_config_resp)
    if not isinstance(original_config, list):
        log_error("TCID18_DeepSleepFlow Failed ❌ (unable to read baseline wakeup config)")
        return False

    try:
        timer_enable_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                build_wakeup_override_entries(original_config, {
                    "IR": False,
                    "TIMER": True,
                })
            )
        )
        log_warning(f"Timer enable response: {timer_enable_resp}")
        if not is_ok(timer_enable_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to enable TIMER wake source)")
            return False

        configured_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {configured_resp}")
        configured = parse_wakeup_config(configured_resp)
        if not isinstance(configured, list) or get_source_enabled(configured, "TIMER") is not True:
            log_error("TCID18_DeepSleepFlow Failed ❌ (TIMER not enabled before timer wake flow)")
            return False

        timeout_register_resp = send_curl_command(PowerManagerApis.register_event("onDeepSleepTimeout", "pm_plugin_001_timeout"))
        log_warning(f"Deep sleep register response: {timeout_register_resp}")
        if not is_ok(timeout_register_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (deep-sleep-timeout registration failed)")
            return False

        power_register_resp = send_curl_command(PowerManagerApis.register_event("onPowerModeChanged", "pm_plugin_001_powermode"))
        log_warning(f"Power mode register response: {power_register_resp}")
        if not is_ok(power_register_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (power-mode registration failed)")
            return False

        note_partial("Async onDeepSleepTimeout delivery is not asserted by this curl-only testcase.")
        note_partial("Async onPowerModeChanged delivery is not asserted by this curl-only testcase.")

        log_info("Executing setPowerState(ON)")
        on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001"))
        log_warning(f"ON response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to set ON baseline)")
            return False

        time.sleep(1)
        on_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"ON state response: {on_state_resp}")
        ok, failure = _assert_state("ON baseline", on_state_resp, "ON")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        log_info("Executing setPowerState(LIGHT_SLEEP)")
        light_resp = send_curl_command(PowerManagerApis.set_power_state("LIGHT_SLEEP", standby_reason="PM-PLUGIN-001"))
        log_warning(f"LIGHT_SLEEP response: {light_resp}")
        if not is_ok(light_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to enter LIGHT_SLEEP)")
            return False

        time.sleep(1)
        light_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"LIGHT_SLEEP state response: {light_state_resp}")
        ok, failure = _assert_state("LIGHT_SLEEP transition", light_state_resp, "LIGHT_SLEEP", "ON")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        log_info("Executing setDeepSleepTimer(5)")
        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to set deep-sleep timer)")
            return False

        log_info("Executing setPowerState(DEEP_SLEEP)")
        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-001", timeout=10))
        log_warning(f"DEEP_SLEEP response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(7)

        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Read-back power state: {state_resp}")
        ok, failure = _assert_state("post-timeout wake", state_resp, "LIGHT_SLEEP", "DEEP_SLEEP")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"Wakeup age response: {wake_resp}")
        wake_age = parse_time_since_wakeup(wake_resp)
        if wake_age != 0:
            log_error("TCID18_DeepSleepFlow Failed ❌ (wake age should be 0 in LIGHT_SLEEP)")
            return False

        restore_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001"))
        log_warning(f"Restore ON response: {restore_resp}")
        if not is_ok(restore_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to restore ON)")
            return False

        time.sleep(1)
        restored_on_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Restored ON state response: {restored_on_state_resp}")
        ok, failure = _assert_state("restore ON transition", restored_on_state_resp, "ON", "LIGHT_SLEEP")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        standby_resp = send_curl_command(PowerManagerApis.set_power_state("STANDBY", standby_reason="PM-PLUGIN-001"))
        log_warning(f"STANDBY response: {standby_resp}")
        if not is_ok(standby_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to enter STANDBY)")
            return False

        time.sleep(1)
        standby_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"STANDBY state response: {standby_state_resp}")
        ok, failure = _assert_state("STANDBY transition", standby_state_resp, "STANDBY", "ON")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        final_on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001"))
        log_warning(f"Final ON response: {final_on_resp}")
        if not is_ok(final_on_resp):
            log_error("TCID18_DeepSleepFlow Failed ❌ (failed to return to ON from STANDBY)")
            return False

        time.sleep(2)

        final_on_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Final ON state response: {final_on_state_resp}")
        ok, failure = _assert_state("final ON transition", final_on_state_resp, "ON", "STANDBY")
        if not ok:
            log_error(f"TCID18_DeepSleepFlow Failed ❌ ({failure})")
            return False

        on_wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"ON wakeup age response: {on_wake_resp}")
        on_wake_age = parse_time_since_wakeup(on_wake_resp)
        if not isinstance(on_wake_age, int) or on_wake_age <= 0:
            log_error("TCID18_DeepSleepFlow Failed ❌ (expected non-zero wake age in ON)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID18_DeepSleepFlow Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True