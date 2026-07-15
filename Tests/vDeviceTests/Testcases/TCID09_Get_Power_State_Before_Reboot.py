"""
/**
 * @file TCID09_Get_Power_State_Before_Reboot.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID09_Get_Power_State_Before_Reboot
 * @details Validates getPowerStateBeforeReboot returns a valid result.
 *
 * @precondition
 *  - org.rdk.PowerManager plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py
 *  - PowerManager_Curl.py
 *  - SuiteManager.py
 *
 * @expected_result
 *  - Response contains a result dict and success is not False.
 *
 * @pass_criteria
 *  - Result present, success not False, and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing result, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getPowerStateBeforeReboot")
    response = send_curl_command(PowerManagerApis.get_power_state_before_reboot)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC returns the PowerState enum unwrapped as its string
    # name (e.g. "UNKNOWN"), not a dict.
    valid_states = {
        "UNKNOWN", "OFF", "STANDBY", "ON", "LIGHT_SLEEP", "DEEP_SLEEP",
    }
    result = parse_result(response)
    if not isinstance(result, str) or result not in valid_states:
        log_error("TCID09_Get_Power_State_Before_Reboot Failed ❌ (no valid result)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID09_Get_Power_State_Before_Reboot Passed ✅ "
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
