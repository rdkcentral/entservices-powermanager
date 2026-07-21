"""
/**
 * @file TCID15_NonKeyWakeKeycode.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID15_NonKeyWakeKeycode
 * @details Validates that a non-key wake flow does not leave a stale non-zero
 *          wake keycode behind.
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
        log_error("TCID15_NonKeyWakeKeycode Failed ❌ (unable to read baseline wakeup config)")
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
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (failed to enable TIMER wake source)")
            return False

        configured_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {configured_resp}")
        configured = parse_wakeup_config(configured_resp)
        if not isinstance(configured, list) or get_source_enabled(configured, "TIMER") is not True:
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (TIMER not enabled before timer wake flow)")
            return False

        baseline_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Baseline keycode response: {baseline_resp}")
        baseline_keycode = parse_last_wakeup_keycode(baseline_resp)
        if baseline_keycode != 0:
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (baseline keycode not zero)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-007", timeout=10))
[O        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(7)

        reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"Wakeup reason response: {reason_resp}")
        reason = parse_last_wakeup_reason(reason_resp)
        if not is_timer_wakeup_reason(reason):
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (expected timer-backed wakeup reason)")
            return False
        if reason != "TIMER":
            log_warning(f"Observed timer-backed wakeup reason alias: {reason}")

        keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Post-wake keycode response: {keycode_resp}")
        keycode = parse_last_wakeup_keycode(keycode_resp)
        if keycode != 0:
            log_error("TCID15_NonKeyWakeKeycode Failed ❌ (non-key wake left a stale keycode)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
[I        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-007-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID15_NonKeyWakeKeycode Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
