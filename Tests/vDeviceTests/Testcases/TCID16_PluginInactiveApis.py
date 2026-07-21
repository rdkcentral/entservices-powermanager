"""
/**
 * @file TCID16_PluginInactiveApis.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID16_PluginInactiveApis
 * @details Validates service-inactive handling after deactivating the plugin.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error, parse_power_state


DEACTIVATE_SETTLE_SECONDS = int(os.environ.get("POWERMANAGER_DEACTIVATE_SETTLE_SECONDS", "20"))


def _reactivate_plugin(max_attempts=6, sleep_seconds=2):
    last_response = ""
    for attempt in range(1, max_attempts + 1):
        activate_resp = send_curl_command(PowerManagerApis.controller_activate(timeout=12))
        last_response = activate_resp
        log_warning(f"Activate attempt {attempt} response: {activate_resp}")

        if is_ok(activate_resp):
            time.sleep(6)
            health_resp = send_curl_command(PowerManagerApis.get_power_state)
            log_warning(f"Post-activate health response: {health_resp}")
            if isinstance(parse_power_state(health_resp), dict):
                return True, activate_resp

        error = parse_error(activate_resp)
        if isinstance(error, dict) and error.get("code") == 5:
            time.sleep(sleep_seconds)
            continue

        if activate_resp.startswith("< No response"):
            time.sleep(sleep_seconds)
            continue

        break

    return False, last_response


def run_test():
    start_time = time.perf_counter()

    failed = False
    observed_inactive_error = False
    reactivation_failed = False

    try:
        deactivate_resp = send_curl_command(PowerManagerApis.controller_deactivate())
        log_warning(f"Deactivate response: {deactivate_resp}")

        if not is_ok(deactivate_resp) and not deactivate_resp.startswith("< No response"):
            log_error("TCID16_PluginInactiveApis Failed ❌ (plugin deactivation failed)")
            return False

        log_warning(f"Waiting {DEACTIVATE_SETTLE_SECONDS}s for plugin deactivation to settle")
        time.sleep(DEACTIVATE_SETTLE_SECONDS)

        for label, command in (
            ("getTimeSinceWakeup", PowerManagerApis.get_time_since_wakeup),
            ("getPowerState", PowerManagerApis.get_power_state),
            ("getLastWakeupReason", PowerManagerApis.get_last_wakeup_reason),
        ):
            response = send_curl_command(command)
            log_warning(f"{label} inactive response: {response}")
            error = parse_error(response)
            if isinstance(error, dict) and error.get("message") == "Service is not active":
                observed_inactive_error = True
                continue
            failed = True
            break
    finally:
        reactivated, activate_resp = _reactivate_plugin()
        if not reactivated:
            reactivation_failed = True

    if not observed_inactive_error:
        log_error("TCID16_PluginInactiveApis Failed ❌ (plugin never entered inactive state)")
        return False

    if reactivation_failed:
        log_error("TCID16_PluginInactiveApis Failed ❌ (plugin reactivation failed)")
        return False

    if failed:
        log_error("TCID16_PluginInactiveApis Failed ❌ (inactive API did not return an error)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID16_PluginInactiveApis Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True