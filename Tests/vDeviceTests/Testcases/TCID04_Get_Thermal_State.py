"""
/**
 * @file TCID04_Get_Thermal_State.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID04_Get_Thermal_State
 * @details Validates getThermalState returns the current temperature.
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
 *  - Response contains a result dict with a numeric temperature field.
 *
 * @pass_criteria
 *  - Result present with numeric temperature and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing result/field, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getThermalState")
    response = send_curl_command(PowerManagerApis.get_thermal_state)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC returns the single output parameter unwrapped, so the
    # result is a bare numeric thermal value (e.g. 0), not a dict.
    temperature = parse_result(response)
    if isinstance(temperature, bool) or not isinstance(temperature, (int, float)):
        log_error("TCID04_Get_Thermal_State Failed ❌ (temperature missing/not numeric)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID04_Get_Thermal_State Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
