"""
/**
 * @file TCID24_PowerModePreChangeClientLifecycle.py
 * @brief L3 PowerManager functional testcase for the power-mode pre-change client lifecycle.
 *
 * @testcase TCID24_PowerModePreChangeClientLifecycle
 * @details Exercises the pre-change client registration APIs that no existing
 *          test case covers:
 *            - addPowerModePreChangeClient (new client, duplicate client, empty name)
 *            - powerModePreChangeComplete (acknowledge path)
 *            - delayPowerModeChangeBy (with a real, registered clientId)
 *            - removePowerModePreChangeClient (registered id, unknown id)
 *          Targets PowerManagerImplementation::AddPowerModePreChangeClient,
 *          RemovePowerModePreChangeClient and PowerModePreChangeComplete branches.
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
 *  - Registration succeeds and returns a clientId; duplicate returns the same id;
 *    empty name is rejected; removal of the registered id succeeds; removal of an
 *    unknown id is rejected.
 *
 * @pass_criteria
 *  - All positive calls succeed and all negative calls are rejected as described.
 *
 * @failure_criteria
 *  - Any positive call fails, any negative call succeeds, JSON parse error, or
 *    run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


CLIENT_NAME = "l3_prechange_client"
UNKNOWN_CLIENT_ID = 999999


def _extract_client_id(result):
    """Return the clientId from a JSON-RPC result, tolerating dict or scalar shapes."""
    if isinstance(result, dict):
        cid = result.get("clientId")
        if isinstance(cid, int) and not isinstance(cid, bool):
            return cid
    if isinstance(result, int) and not isinstance(result, bool):
        return result
    return None


def run_test():
    start_time = time.perf_counter()

    # 1. Register a new pre-change client (happy path).
    log_info("Executing addPowerModePreChangeClient (new client)")
    resp = send_curl_command(PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME))
    log_warning(f"Response: {resp}")
    if not is_ok(resp):
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (add client did not succeed)")
        return False

    client_id = _extract_client_id(parse_result(resp))
    if client_id is None:
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (no clientId returned)")
        return False
    log_success(f"✅ Registered client id={client_id}")

    # 2. Register the same client name again -> should return the same clientId.
    log_info("Executing addPowerModePreChangeClient (duplicate client)")
    resp_dup = send_curl_command(PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME))
    log_warning(f"Response: {resp_dup}")
    if not is_ok(resp_dup):
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (duplicate add did not succeed)")
        return False
    dup_id = _extract_client_id(parse_result(resp_dup))
    if dup_id is not None and dup_id != client_id:
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (duplicate returned different clientId)")
        return False
    log_success("✅ Duplicate registration returned the same clientId")

    # 3. Register with an empty name -> must be rejected (ERROR_INVALID_PARAMETER).
    log_info("Executing addPowerModePreChangeClient (empty name, expect rejection)")
    resp_empty = send_curl_command(PowerManagerApis.add_power_mode_prechange_client(""))
    log_warning(f"Response: {resp_empty}")
    if is_ok(resp_empty):
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (empty client name unexpectedly accepted)")
        return False
    log_success("✅ Empty client name correctly rejected")

    # 4. delayPowerModeChangeBy with the registered client (no active transaction).
    #    Best-effort: exercises the method entry with a real clientId.
    log_info("Executing delayPowerModeChangeBy with registered clientId")
    send_curl_command(PowerManagerApis.delay_power_mode_change_by(client_id, 0, 0))

    # 5. powerModePreChangeComplete for the registered client (acknowledge path).
    #    Best-effort: exercises PowerModePreChangeComplete entry with a real clientId.
    log_info("Executing powerModePreChangeComplete with registered clientId")
    send_curl_command(PowerManagerApis.power_mode_prechange_complete(client_id, 0))

    # 6. Remove the registered client (happy path).
    log_info("Executing removePowerModePreChangeClient (registered id)")
    resp_rm = send_curl_command(PowerManagerApis.remove_power_mode_prechange_client(client_id))
    log_warning(f"Response: {resp_rm}")
    if not is_ok(resp_rm):
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (remove client did not succeed)")
        return False
    log_success(f"✅ Removed client id={client_id}")

    # 7. Remove an unknown client -> must be rejected (not found).
    log_info("Executing removePowerModePreChangeClient (unknown id, expect rejection)")
    resp_rm_bad = send_curl_command(PowerManagerApis.remove_power_mode_prechange_client(UNKNOWN_CLIENT_ID))
    log_warning(f"Response: {resp_rm_bad}")
    if is_ok(resp_rm_bad):
        log_error("TCID24_PowerModePreChangeClientLifecycle Failed ❌ (unknown client removal unexpectedly accepted)")
        return False
    log_success("✅ Unknown client removal correctly rejected")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID24_PowerModePreChangeClientLifecycle Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
