"""
/**
 * @file TCID22_KeycodeZeroDeepSleep.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID22_KeycodeZeroDeepSleep
 * @details Validates stable zero-valued wake keycode before and after a timer
 *          driven deep-sleep cycle.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_last_wakeup_keycode


def run_test():
    start_time = time.perf_counter()

    first_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
    log_warning(f"Initial keycode response: {first_resp}")
    first_keycode = parse_last_wakeup_keycode(first_resp)
    if first_keycode != 0:
        log_error("TCID22_KeycodeZeroDeepSleep Failed ❌ (initial keycode not zero)")
        return False

    timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
    log_warning(f"Timer response: {timer_resp}")
    if not is_ok(timer_resp):
        log_error("TCID22_KeycodeZeroDeepSleep Failed ❌ (failed to set deep-sleep timer)")
        return False

    deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-006", timeout=10))
    log_warning(f"Deep sleep response: {deep_resp}")
    if not is_ok(deep_resp):
        log_error("TCID22_KeycodeZeroDeepSleep Failed ❌ (failed to enter DEEP_SLEEP)")
        return False

    time.sleep(7)
    second_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
    log_warning(f"Post-wake keycode response: {second_resp}")
    second_keycode = parse_last_wakeup_keycode(second_resp)
    if second_keycode != 0:
        log_error("TCID22_KeycodeZeroDeepSleep Failed ❌ (post-wake keycode not zero)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-006-restore"))
        return False

    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-006-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID22_KeycodeZeroDeepSleep Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True