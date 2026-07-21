"""
/**
 * @file TCID13_WakeupAgeReset.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID13_WakeupAgeReset
 * @details Validates getTimeSinceWakeup monotonicity in ON and reset behavior
 *          after leaving ON.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_time_since_wakeup


def run_test():
    start_time = time.perf_counter()

    on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-002"))
    log_warning(f"Response: {on_resp}")
    if not is_ok(on_resp):
        log_error("TCID13_WakeupAgeReset Failed ❌ (failed to set ON baseline)")
        return False

    time.sleep(2)
    first_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    log_warning(f"First wakeup age response: {first_resp}")
    first_value = parse_time_since_wakeup(first_resp)
    if not isinstance(first_value, int):
        log_error("TCID13_WakeupAgeReset Failed ❌ (invalid first wakeup age)")
        return False

    time.sleep(2)
    second_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    log_warning(f"Second wakeup age response: {second_resp}")
    second_value = parse_time_since_wakeup(second_resp)
    if not isinstance(second_value, int) or second_value < first_value:
        log_error("TCID13_WakeupAgeReset Failed ❌ (wakeup age not monotonic)")
        return False

    standby_resp = send_curl_command(PowerManagerApis.set_power_state("STANDBY", standby_reason="PM-PLUGIN-002"))
    log_warning(f"Standby response: {standby_resp}")
    if not is_ok(standby_resp):
        log_error("TCID13_WakeupAgeReset Failed ❌ (failed to set STANDBY)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-002-restore"))
        return False

    reset_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    log_warning(f"Reset wakeup age response: {reset_resp}")
    reset_value = parse_time_since_wakeup(reset_resp)
    if reset_value != 0:
        log_error("TCID13_WakeupAgeReset Failed ❌ (wake age did not reset outside ON)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-002-restore"))
        return False

    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-002-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID13_WakeupAgeReset Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True