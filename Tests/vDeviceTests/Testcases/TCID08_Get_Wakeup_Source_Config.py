"""
/**
 * @file TCID08_Get_Wakeup_Source_Config.py
 * @brief L2 PowerManager functional testcase.
 *
 * @testcase TCID08_Get_Wakeup_Source_Config
 * @details Validates getWakeupSourceConfig returns a valid result.
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

    log_info("Executing getWakeupSourceConfig")
    response = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    if not response:
        log_error("✖ curl command not sent")
        return False
    log_success("✔ curl command sent")
    log_warning(f"Response: {response}")

    # Thunder JSON-RPC serializes the iterator output as a JSON array of
    # {wakeupSource, enabled} entries, not a dict.
    result = parse_result(response)
    if not isinstance(result, list) or not result:
        log_error("TCID08_Get_Wakeup_Source_Config Failed ❌ (no valid result)")
        return False

    for entry in result:
        if (
            not isinstance(entry, dict)
            or "wakeupSource" not in entry
            or not isinstance(entry.get("enabled"), bool)
        ):
            log_error("TCID08_Get_Wakeup_Source_Config Failed ❌ (malformed entry)")
            return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID08_Get_Wakeup_Source_Config Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
