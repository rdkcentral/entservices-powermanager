"""
/**
 * @file TCID14_Set_Wakeup_Source_Config.py
 * @brief L2 PowerManager functional testcase for setWakeupSourceConfig.
 *
 * @testcase TCID14_Set_Wakeup_Source_Config
 * @details Enables the POWERKEY wakeup source via setWakeupSourceConfig and
 *          verifies the change is reflected by getWakeupSourceConfig.
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
 *  - getWakeupSourceConfig lists POWERKEY with enabled == True.
 *
 * @pass_criteria
 *  - Setter succeeds and the POWERKEY entry reports enabled True on read-back.
 *
 * @failure_criteria
 *  - Setter error, entry missing/not enabled, JSON parse error, or run_test() returns False.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


WAKEUP_SOURCE = "POWERKEY"


def run_test():
    start_time = time.perf_counter()

    log_info("Executing setWakeupSourceConfig")
    set_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config(
            [{"wakeupSource": WAKEUP_SOURCE, "enabled": True}]
        )
    )
    log_warning(f"Response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID14_Set_Wakeup_Source_Config Failed ❌ (setter did not succeed)")
        return False
    log_success("✔ curl command sent")

    result = parse_result(send_curl_command(PowerManagerApis.get_wakeup_source_config))
    log_warning(f"Read-back wakeup config: {result}")
    if not isinstance(result, list):
        log_error("TCID14_Set_Wakeup_Source_Config Failed ❌ (no valid result)")
        return False

    entry = next(
        (e for e in result if isinstance(e, dict) and e.get("wakeupSource") == WAKEUP_SOURCE),
        None,
    )
    if entry is None or entry.get("enabled") is not True:
        log_error("TCID14_Set_Wakeup_Source_Config Failed ❌ (POWERKEY not enabled)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID14_Set_Wakeup_Source_Config Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
