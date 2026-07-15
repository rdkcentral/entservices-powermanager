"""
/**
 * @file TCID05_Get_Last_Wakeup_Reason.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID05_Get_Last_Wakeup_Reason
 * @details Validates getLastWakeupReason returns a valid result with wakeupReason.
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
 *  - Response contains a result dict with a wakeupReason field.
 *
 * @pass_criteria
 *  - Result present, wakeupReason present and success not False, run_test() True.
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

    log_info("Executing getLastWakeupReason")
    response = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC returns the WakeupReason enum unwrapped as its string
    # name (e.g. "UNKNOWN"), not a dict.
    wakeup_reason = parse_result(response)
    if not isinstance(wakeup_reason, str) or not wakeup_reason:
        log_error("TCID05_Get_Last_Wakeup_Reason Failed ❌ (wakeupReason missing/not a string)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID05_Get_Last_Wakeup_Reason Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
