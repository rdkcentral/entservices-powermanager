"""
/**
 * @file TCID12_Set_Network_Standby_Mode.py
 * @brief L2 PowerManager functional testcase for setNetworkStandbyMode.
 *
 * @testcase TCID12_Set_Network_Standby_Mode
 * @details Toggles the network standby mode via setNetworkStandbyMode and
 *          verifies the change with getNetworkStandbyMode. Restores the original
 *          value afterwards.
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
 *  - getNetworkStandbyMode reflects the value written by setNetworkStandbyMode.
 *
 * @pass_criteria
 *  - Setter succeeds and the getter returns the toggled value.
 *
 * @failure_criteria
 *  - Setter error, read-back mismatch, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def run_test():
    start_time = time.perf_counter()

    original = parse_result(send_curl_command(PowerManagerApis.get_network_standby_mode))
    if not isinstance(original, bool):
        log_error("TCID12_Set_Network_Standby_Mode Failed ❌ (could not read current mode)")
        return False

    target = not original

    log_info("Executing setNetworkStandbyMode")
    set_resp = send_curl_command(PowerManagerApis.set_network_standby_mode(target))
    log_warning(f"Response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID12_Set_Network_Standby_Mode Failed ❌ (setter did not succeed)")
        return False
    log_success("✔ curl command sent")

    standby = parse_result(send_curl_command(PowerManagerApis.get_network_standby_mode))
    log_warning(f"Read-back standbyMode: {standby}")
    if standby is not target:
        log_error("TCID12_Set_Network_Standby_Mode Failed ❌ (read-back mismatch)")
        send_curl_command(PowerManagerApis.set_network_standby_mode(original))
        return False

    # Restore original value (non-fatal).
    send_curl_command(PowerManagerApis.set_network_standby_mode(original))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID12_Set_Network_Standby_Mode Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
