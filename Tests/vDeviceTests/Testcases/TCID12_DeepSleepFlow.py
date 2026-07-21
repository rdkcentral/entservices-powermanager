"""
/**
 * @file TCID12_DeepSleepFlow.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID12_DeepSleepFlow
 * @details Validates the deep-sleep timeout flow across setDeepSleepTimer,
 *          setPowerState, getPowerState, and getTimeSinceWakeup.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, note_partial, parse_power_state, parse_time_since_wakeup, parse_wakeup_config, wakeup_map


def run_test():
    start_time = time.perf_counter()

    original_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_config_resp}")
    original_config = parse_wakeup_config(original_config_resp)
    if not isinstance(original_config, list):
        log_error("TCID12_DeepSleepFlow Failed ❌ (unable to read baseline wakeup config)")
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
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to enable TIMER wake source)")
            return False

        configured_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {configured_resp}")
        configured = parse_wakeup_config(configured_resp)
        if not isinstance(configured, list) or get_source_enabled(configured, "TIMER") is not True:
            log_error("TCID12_DeepSleepFlow Failed ❌ (TIMER not enabled before timer wake flow)")
            return False

        log_info("Executing setPowerState(ON)")
        on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001"))
        log_warning(f"Response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to set ON baseline)")
            return False

        log_info("Executing setPowerState(LIGHT_SLEEP)")
        light_resp = send_curl_command(PowerManagerApis.set_power_state("LIGHT_SLEEP", standby_reason="PM-PLUGIN-001"))
        log_warning(f"Response: {light_resp}")
        if not is_ok(light_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to enter LIGHT_SLEEP)")
            return False

        register_resp = send_curl_command(PowerManagerApis.register_event("onDeepSleepTimeout", "pm_plugin_001_timeout"))
        log_warning(f"Register response: {register_resp}")
        if not is_ok(register_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (event registration failed)")
            return False
        note_partial("Async onDeepSleepTimeout delivery is not asserted by this curl-only testcase.")

        log_info("Executing setDeepSleepTimer(5)")
        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to set deep-sleep timer)")
            return False

        log_info("Executing setPowerState(DEEP_SLEEP)")
        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-001", timeout=10))
        log_warning(f"Response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(7)

        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Read-back power state: {state_resp}")
        state = parse_power_state(state_resp)
        if not isinstance(state, dict) or state.get("currentState") != "LIGHT_SLEEP":
            log_error("TCID12_DeepSleepFlow Failed ❌ (expected LIGHT_SLEEP after timeout)")
            return False

        previous = state.get("previousState", state.get("prevState"))
        if previous != "DEEP_SLEEP":
            log_error("TCID12_DeepSleepFlow Failed ❌ (previousState mismatch after wake)")
            return False

        wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"Wakeup age response: {wake_resp}")
        wake_age = parse_time_since_wakeup(wake_resp)
        if wake_age != 0:
            log_error("TCID12_DeepSleepFlow Failed ❌ (wake age should be 0 in LIGHT_SLEEP)")
            return False

        restore_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001"))
        log_warning(f"Restore response: {restore_resp}")
        if not is_ok(restore_resp):
            log_error("TCID12_DeepSleepFlow Failed ❌ (failed to restore ON)")
            return False

        time.sleep(2)

        on_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"ON state response: {on_state_resp}")
        on_state = parse_power_state(on_state_resp)
        if not isinstance(on_state, dict) or on_state.get("currentState") != "ON":
            log_error("TCID12_DeepSleepFlow Failed ❌ (expected ON after restore)")
            return False

        on_wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"ON wakeup age response: {on_wake_resp}")
        on_wake_age = parse_time_since_wakeup(on_wake_resp)
        if not isinstance(on_wake_age, int) or on_wake_age <= 0:
            log_error("TCID12_DeepSleepFlow Failed ❌ (expected non-zero wake age in ON)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-001-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID12_DeepSleepFlow Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True