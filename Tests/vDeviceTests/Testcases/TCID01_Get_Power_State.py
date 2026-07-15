"""
/**
 * @file TCID01_Get_Power_State.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID01_Get_Power_State
 * @details Validates getPowerState returns a valid result containing currentState.
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
 *  - Response contains a result dict with an integer currentState.
 *
 * @pass_criteria
 *  - Result present, currentState is an int, and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing result, wrong types, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getPowerState")
    response = send_curl_command(PowerManagerApis.get_power_state)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    result = parse_result(response)
    if not isinstance(result, dict):
        log_error("TCID01_Get_Power_State Failed ❌ (no result)")
        return False

    # Thunder JSON-RPC serializes the PowerState enum as its string name
    # (e.g. "UNKNOWN", "STANDBY", "ON"), not as an integer.
    valid_states = {
        "UNKNOWN", "OFF", "STANDBY", "ON", "LIGHT_SLEEP", "DEEP_SLEEP",
    }
    current = result.get("currentState")
    if not isinstance(current, str) or current not in valid_states:
        log_error("TCID01_Get_Power_State Failed ❌ (currentState missing/not a valid PowerState)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID01_Get_Power_State Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
