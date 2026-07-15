"""
/**
 * @file TCID06_Get_Last_Wakeup_Keycode.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID06_Get_Last_Wakeup_Keycode
 * @details Validates getLastWakeupKeyCode returns an integer keycode.
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
 *  - Response contains a result dict with an integer keycode field.
 *
 * @pass_criteria
 *  - Result present with integer keycode and run_test() returns True.
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

    log_info("Executing getLastWakeupKeyCode")
    response = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC returns the single output parameter unwrapped, so the
    # result is a bare integer keycode (e.g. 0), not a dict.
    keycode = parse_result(response)
    if isinstance(keycode, bool) or not isinstance(keycode, int):
        log_error("TCID06_Get_Last_Wakeup_Keycode Failed ❌ (keycode missing/not int)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID06_Get_Last_Wakeup_Keycode Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
