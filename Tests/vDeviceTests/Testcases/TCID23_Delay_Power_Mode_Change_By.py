"""
/**
 * @file TCID23_Delay_Power_Mode_Change_By.py
 * @brief L3 PowerManager functional testcase for delayPowerModeChangeBy.
 *
 * @testcase TCID23_Delay_Power_Mode_Change_By
 * @details Invokes delayPowerModeChangeBy and verifies the call is accepted by
 *          the framework (returns a JSON-RPC result with no error).
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
 *  - Response contains a result field and no error.
 *
 * @pass_criteria
 *  - delayPowerModeChangeBy succeeds (result present, no error).
 *
 * @failure_criteria
 *  - Error response, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


CLIENT_ID = 0
TRANSACTION_ID = 0
DELAY_PERIOD = 0


def run_test():
    start_time = time.perf_counter()

    log_info("Executing delayPowerModeChangeBy")
    response = send_curl_command(
        PowerManagerApis.delay_power_mode_change_by(CLIENT_ID, TRANSACTION_ID, DELAY_PERIOD)
    )
    if not response:
        log_error("❌ curl command not sent")
        return False
    log_success("✅ curl command sent")
    log_warning(f"Response: {response}")

    if not is_ok(response):
        log_error("TCID23_Delay_Power_Mode_Change_By Failed ❌ (call did not succeed)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_Delay_Power_Mode_Change_By Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True