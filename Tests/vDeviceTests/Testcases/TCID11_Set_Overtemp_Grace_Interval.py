"""
/**
 * @file TCID11_Set_Overtemp_Grace_Interval.py
 * @brief L2 PowerManager functional testcase for setOvertempGraceInterval.
 *
 * @testcase TCID11_Set_Overtemp_Grace_Interval
 * @details Writes a grace interval via setOvertempGraceInterval and verifies it
 *          persists by reading it back with getOvertempGraceInterval. Restores
 *          the original value afterwards.
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
 *  - setOvertempGraceInterval succeeds and getOvertempGraceInterval returns the value.
 *
 * @pass_criteria
 *  - Setter succeeds (result present, no error) and read-back matches.
 *
 * @failure_criteria
 *  - Setter error, read-back mismatch, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


TEST_INTERVAL = 120


def run_test():
    start_time = time.perf_counter()

    original = parse_result(send_curl_command(PowerManagerApis.get_overtemp_grace_interval))

    log_info("Executing setOvertempGraceInterval")
    set_resp = send_curl_command(PowerManagerApis.set_overtemp_grace_interval(TEST_INTERVAL))
    log_warning(f"Response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID11_Set_Overtemp_Grace_Interval Failed ❌ (setter did not succeed)")
        return False
    log_success("✔ curl command sent")

    grace = parse_result(send_curl_command(PowerManagerApis.get_overtemp_grace_interval))
    log_warning(f"Read-back graceInterval: {grace}")
    if grace != TEST_INTERVAL:
        log_error("TCID11_Set_Overtemp_Grace_Interval Failed ❌ (read-back mismatch)")
        return False

    # Best-effort restore of the original value (non-fatal).
    if isinstance(original, int) and not isinstance(original, bool):
        send_curl_command(PowerManagerApis.set_overtemp_grace_interval(original))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID11_Set_Overtemp_Grace_Interval Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
