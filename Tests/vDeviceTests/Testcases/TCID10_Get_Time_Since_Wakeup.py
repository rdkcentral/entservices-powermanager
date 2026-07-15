"""
/**
 * @file TCID10_Get_Time_Since_Wakeup.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID10_Get_Time_Since_Wakeup
 * @details Validates getTimeSinceWakeup returns a valid result.
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

    log_info("Executing getTimeSinceWakeup")
    response = send_curl_command(PowerManagerApis.get_time_since_wakeup)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    result = parse_result(response)
    if not isinstance(result, dict) or result.get("success") is False:
        log_error("TCID10_Get_Time_Since_Wakeup Failed ❌ (no valid result)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID10_Get_Time_Since_Wakeup Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
