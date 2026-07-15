"""
/**
 * @file TCID07_Get_Network_Standby_Mode.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID07_Get_Network_Standby_Mode
 * @details Validates getNetworkStandbyMode returns a boolean standbyMode.
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
 *  - Response contains a result dict with a boolean standbyMode field.
 *
 * @pass_criteria
 *  - Result present with boolean standbyMode and run_test() returns True.
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

    log_info("Executing getNetworkStandbyMode")
    response = send_curl_command(PowerManagerApis.get_network_standby_mode)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC returns the single output parameter unwrapped, so the
    # result is a bare boolean standby mode (e.g. true), not a dict.
    standby = parse_result(response)
    if not isinstance(standby, bool):
        log_error("TCID07_Get_Network_Standby_Mode Failed ❌ (standbyMode missing/not bool)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID07_Get_Network_Standby_Mode Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
