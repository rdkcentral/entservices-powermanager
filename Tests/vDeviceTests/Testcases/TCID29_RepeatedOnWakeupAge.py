"""
/**
 * @file TCID29_RepeatedOnWakeupAge.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID29_RepeatedOnWakeupAge
 * @details Validates that a repeated setPowerState(ON) call does not reset the
 *          wakeup-age timer.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_time_since_wakeup


def run_test():
    start_time = time.perf_counter()

    on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-013"))
    log_warning(f"Initial ON response: {on_resp}")
    if not is_ok(on_resp):
        log_error("TCID29_RepeatedOnWakeupAge Failed ❌ (failed to set ON baseline)")
        return False

    time.sleep(3)
    first_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    log_warning(f"First wakeup age response: {first_resp}")
    first_value = parse_time_since_wakeup(first_resp)
    if not isinstance(first_value, int):
        log_error("TCID29_RepeatedOnWakeupAge Failed ❌ (invalid first wakeup age)")
        return False

    repeat_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-013"))
    log_warning(f"Repeated ON response: {repeat_resp}")
    if not is_ok(repeat_resp):
        log_error("TCID29_RepeatedOnWakeupAge Failed ❌ (repeated ON request failed)")
        return False

    time.sleep(2)
    second_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    log_warning(f"Second wakeup age response: {second_resp}")
    second_value = parse_time_since_wakeup(second_resp)
    if not isinstance(second_value, int) or second_value < first_value:
        log_error("TCID29_RepeatedOnWakeupAge Failed ❌ (wakeup age reset after repeated ON)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID29_RepeatedOnWakeupAge Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True