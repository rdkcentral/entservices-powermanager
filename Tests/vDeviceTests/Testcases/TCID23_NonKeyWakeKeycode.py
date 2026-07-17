"""
/**
 * @file TCID23_NonKeyWakeKeycode.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID23_NonKeyWakeKeycode
 * @details Validates that a non-key wake flow does not leave a stale non-zero
 *          wake keycode behind.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_last_wakeup_keycode, parse_last_wakeup_reason


def run_test():
    start_time = time.perf_counter()

    baseline_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
    log_warning(f"Baseline keycode response: {baseline_resp}")
    baseline_keycode = parse_last_wakeup_keycode(baseline_resp)
    if baseline_keycode != 0:
        log_error("TCID23_NonKeyWakeKeycode Failed ❌ (baseline keycode not zero)")
        return False

    timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
    log_warning(f"Timer response: {timer_resp}")
    if not is_ok(timer_resp):
        log_error("TCID23_NonKeyWakeKeycode Failed ❌ (failed to set deep-sleep timer)")
        return False

    deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-007", timeout=10))
    log_warning(f"Deep sleep response: {deep_resp}")
    if not is_ok(deep_resp):
        log_error("TCID23_NonKeyWakeKeycode Failed ❌ (failed to enter DEEP_SLEEP)")
        return False

    time.sleep(7)

    reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
    log_warning(f"Wakeup reason response: {reason_resp}")
    reason = parse_last_wakeup_reason(reason_resp)
    if not isinstance(reason, str) or not reason:
        log_error("TCID23_NonKeyWakeKeycode Failed ❌ (invalid wakeup reason)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-007-restore"))
        return False

    keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
    log_warning(f"Post-wake keycode response: {keycode_resp}")
    keycode = parse_last_wakeup_keycode(keycode_resp)
    if keycode != 0:
        log_error("TCID23_NonKeyWakeKeycode Failed ❌ (non-key wake left a stale keycode)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-007-restore"))
        return False

    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-007-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_NonKeyWakeKeycode Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True