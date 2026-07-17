"""
/**
 * @file TCID24_PluginInactiveApis.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID24_PluginInactiveApis
 * @details Validates service-inactive handling after deactivating the plugin.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error


def run_test():
    start_time = time.perf_counter()

    deactivate_resp = send_curl_command(PowerManagerApis.controller_deactivate())
    log_warning(f"Deactivate response: {deactivate_resp}")
    if not is_ok(deactivate_resp):
        log_error("TCID24_PluginInactiveApis Failed ❌ (plugin deactivation failed)")
        return False

    failed = False
    for label, command in (
        ("getTimeSinceWakeup", PowerManagerApis.get_time_since_wakeup),
        ("getPowerState", PowerManagerApis.get_power_state),
        ("getLastWakeupReason", PowerManagerApis.get_last_wakeup_reason),
    ):
        response = send_curl_command(command)
        log_warning(f"{label} inactive response: {response}")
        if not isinstance(parse_error(response), dict):
            failed = True
            break

    activate_resp = send_curl_command(PowerManagerApis.controller_activate())
    log_warning(f"Activate response: {activate_resp}")
    if not is_ok(activate_resp):
        log_error("TCID24_PluginInactiveApis Failed ❌ (plugin reactivation failed)")
        return False

    time.sleep(6)

    if failed:
        log_error("TCID24_PluginInactiveApis Failed ❌ (inactive API did not return an error)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID24_PluginInactiveApis Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True