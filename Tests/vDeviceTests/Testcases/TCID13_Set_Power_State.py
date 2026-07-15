"""
/**
 * @file TCID13_Set_Power_State.py
 * @brief L2 PowerManager functional testcase for setPowerState.
 *
 * @testcase TCID13_Set_Power_State
 * @details Drives the device to STANDBY via setPowerState and confirms the
 *          transition through getPowerState. Restores POWER_STATE_ON afterwards.
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
 *  - getPowerState reports currentState == "STANDBY" after the transition.
 *
 * @pass_criteria
 *  - Setter succeeds and getPowerState reflects the requested state.
 *
 * @failure_criteria
 *  - Setter error, state mismatch, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


TARGET_STATE = "STANDBY"


def run_test():
    start_time = time.perf_counter()

    log_info("Executing setPowerState")
    set_resp = send_curl_command(PowerManagerApis.set_power_state(TARGET_STATE))
    log_warning(f"Response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID13_Set_Power_State Failed ❌ (setter did not succeed)")
        return False
    log_success("✔ curl command sent")

    # Allow the asynchronous power transition to settle before reading back.
    time.sleep(2)

    result = parse_result(send_curl_command(PowerManagerApis.get_power_state))
    log_warning(f"Read-back power state: {result}")
    if not isinstance(result, dict) or result.get("currentState") != TARGET_STATE:
        log_error("TCID13_Set_Power_State Failed ❌ (currentState mismatch)")
        send_curl_command(PowerManagerApis.set_power_state("ON"))
        return False

    # Restore a sane baseline (non-fatal).
    send_curl_command(PowerManagerApis.set_power_state("ON"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID13_Set_Power_State Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
