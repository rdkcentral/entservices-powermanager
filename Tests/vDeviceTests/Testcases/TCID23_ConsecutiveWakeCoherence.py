"""
/**
 * @file TCID23_ConsecutiveWakeCoherence.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID23_ConsecutiveWakeCoherence
 * @details Validates coherent wake metadata across two consecutive deep-sleep
 *          timer wake cycles.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, is_timer_wakeup_reason, parse_last_wakeup_keycode, parse_last_wakeup_reason, parse_wakeup_config, wakeup_map


def run_test():
    start_time = time.perf_counter()

    original_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_config_resp}")
    original_config = parse_wakeup_config(original_config_resp)
    if not isinstance(original_config, list):
        log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (unable to read baseline wakeup config)")
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
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (failed to enable TIMER wake source)")
            return False

        configured_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {configured_resp}")
        configured = parse_wakeup_config(configured_resp)
        if not isinstance(configured, list) or get_source_enabled(configured, "TIMER") is not True:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (TIMER not enabled before timer wake cycles)")
            return False

        first_timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"First timer response: {first_timer_resp}")
        if not is_ok(first_timer_resp):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (failed to set first deep-sleep timer)")
            return False

        first_deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-016-A", timeout=10))
        log_warning(f"First deep sleep response: {first_deep_resp}")
        if not is_ok(first_deep_resp):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (first deep-sleep request failed)")
            return False

        time.sleep(7)
        first_reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"First wakeup reason response: {first_reason_resp}")
        first_reason = parse_last_wakeup_reason(first_reason_resp)
        if not isinstance(first_reason, str) or not first_reason:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (invalid first wakeup reason)")
            return False
        if not is_timer_wakeup_reason(first_reason):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (expected first timer-backed wakeup reason)")
            return False
        if first_reason != "TIMER":
            log_warning(f"Observed first timer-backed wakeup reason alias: {first_reason}")

        first_key_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"First wakeup keycode response: {first_key_resp}")
        first_key = parse_last_wakeup_keycode(first_key_resp)
        if first_key != 0:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (expected first wakeup keycode 0)")
            return False

        second_timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Second timer response: {second_timer_resp}")
        if not is_ok(second_timer_resp):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (failed to set second deep-sleep timer)")
            return False

        second_deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-016-B", timeout=10))
        log_warning(f"Second deep sleep response: {second_deep_resp}")
        if not is_ok(second_deep_resp):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (second deep-sleep request failed)")
            return False

        time.sleep(7)
        second_reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"Second wakeup reason response: {second_reason_resp}")
        second_reason = parse_last_wakeup_reason(second_reason_resp)
        if not isinstance(second_reason, str) or not second_reason:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (invalid second wakeup reason)")
            return False
        if not is_timer_wakeup_reason(second_reason):
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (expected second timer-backed wakeup reason)")
            return False
        if second_reason != first_reason:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (wakeup reason changed across consecutive timer cycles)")
            return False

        second_key_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Second wakeup keycode response: {second_key_resp}")
        second_key = parse_last_wakeup_keycode(second_key_resp)
        if second_key != 0:
            log_error("TCID23_ConsecutiveWakeCoherence Failed ❌ (expected second wakeup keycode 0)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-016-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_ConsecutiveWakeCoherence Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
