"""
/**
 * @file TCID02_Get_Temperature_Thresholds.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID02_Get_Temperature_Thresholds
 * @details Validates getTemperatureThresholds returns high and critical thresholds.
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
 *  - Response contains a result dict with numeric high and critical fields.
 *
 * @pass_criteria
 *  - Result present with numeric threshold fields and run_test() returns True.
 *
 * @failure_criteria
 *  - Missing result/fields, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing getTemperatureThresholds")
    response = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    result = parse_result(response)
    if not isinstance(result, dict):
        log_error("TCID02_Get_Temperature_Thresholds Failed ❌ (no result)")
        return False

    high = result.get("high")
    critical = result.get("critical")
    if not isinstance(high, (int, float)) or not isinstance(critical, (int, float)):
        log_error("TCID02_Get_Temperature_Thresholds Failed ❌ (thresholds missing/not numeric)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID02_Get_Temperature_Thresholds Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
