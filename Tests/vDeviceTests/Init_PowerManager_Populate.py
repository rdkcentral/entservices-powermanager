"""
/**
 * @file Init_PowerManager_Populate.py
 * @brief Suite initialization / precondition for the PowerManager L2 vDevice suite.
 *
 * @testcase Init_PowerManager_Populate
 * @details Establishes a known baseline before the test cases run: verifies the
 *          plugin is reachable and responds to getPowerState, and drives the
 *          device to a known POWER_STATE_ON baseline when possible.
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
 *  - getPowerState returns a valid result and baseline is established.
 *
 * @pass_criteria
 *  - Baseline verification succeeds and run_test() returns True.
 *
 * @failure_criteria
 *  - Endpoint unreachable, JSON parse failure, or run_test() returns False.
 */
"""

import time

from utils import (
    send_curl_command,
    parse_result,
    log_info,
    log_success,
    log_warning,
    log_error,
)
import PowerManager_Curl as PowerManagerApis


def _get_power_state():
    """Return (currentState, prevState) or None on error."""
    result = parse_result(send_curl_command(PowerManagerApis.get_power_state))
    if not isinstance(result, dict):
        return None
    return result


def run_test():
    log_info("PowerManager suite initialization: verifying plugin reachability")

    result = _get_power_state()
    if result is None:
        log_error("Init failed: getPowerState did not return a valid result")
        return False

    current = result.get("currentState")
    prev = result.get("prevState", result.get("previousState"))
    log_info(f"  Baseline power state: current={current} prev={prev}")

    # Best-effort: drive to POWER_STATE_ON as a known baseline. This is a
    # non-destructive transition and is not fatal if it fails on some targets.
    on_resp = send_curl_command(
        PowerManagerApis.set_power_state(PowerManagerApis.POWER_STATE_ON)
    )
    on_result = parse_result(on_resp)
    if isinstance(on_result, dict) and on_result.get("success") is True:
        log_info("  Baseline set to POWER_STATE_ON")
    else:
        log_warning("  Could not set baseline POWER_STATE_ON (non-fatal)")

    time.sleep(1)
    log_success("Suite initialization completed successfully")
    return True
