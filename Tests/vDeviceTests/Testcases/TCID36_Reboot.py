"""
/**
 * @file TCID36_Reboot.py
 * @brief L2 PowerManager functional testcase for reboot.
 *
 * @testcase TCID36_Reboot
 * @details Invokes the reboot API and verifies the request is accepted by the
 *          framework (returns a JSON-RPC result with no error).
 *
 * @note On the x86 vDevice the reboot API is a stub that acknowledges the
 *       request without actually restarting the device, so the suite continues.
 *       On real hardware this API triggers a device reboot.
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
 *  - reboot succeeds (result present, no error).
 *
 * @failure_criteria
 *  - Error response, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing reboot")
    response = send_curl_command(PowerManagerApis.reboot())
    if not response:
        log_error("âœ– curl command not sent")
        return False
    log_success("âœ” curl command sent")
    log_warning(f"Response: {response}")

    if not is_ok(response):
        log_error("TCID36_Reboot Failed ❌ (call did not succeed)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID36_Reboot Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True