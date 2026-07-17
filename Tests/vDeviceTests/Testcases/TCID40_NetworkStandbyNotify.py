"""
/**
 * @file TCID40_NetworkStandbyNotify.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID40_NetworkStandbyNotify
 * @details Validates the network-standby event registration path and the
 *          related mode transitions.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import note_partial, parse_network_standby


def _set_network_standby(enabled):
    response = send_curl_command(PowerManagerApis.set_network_standby_mode(enabled))
    if is_ok(response):
        return response
    response = send_curl_command(PowerManagerApis.set_network_standby_mode_nw(enabled))
    return response


def run_test():
    start_time = time.perf_counter()

    original_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
    log_warning(f"Original mode response: {original_mode_resp}")
    original_mode = parse_network_standby(original_mode_resp)
    if not isinstance(original_mode, bool):
        log_error("TCID40_NetworkStandbyNotify Failed ❌ (unable to read baseline network standby mode)")
        return False

    try:
        register_resp = send_curl_command(PowerManagerApis.register_event("onNetworkStandbyModeChanged", "pm_plugin_024_nwstandby"))
        log_warning(f"Event registration response: {register_resp}")
        if not is_ok(register_resp):
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (network-standby event registration failed)")
            return False

        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (failed to enable network standby)")
            return False

        enabled_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Enabled mode response: {enabled_mode_resp}")
        enabled_mode = parse_network_standby(enabled_mode_resp)
        if enabled_mode is not True:
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (network standby not enabled after setter)")
            return False

        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (failed to disable network standby)")
            return False

        disabled_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Disabled mode response: {disabled_mode_resp}")
        disabled_mode = parse_network_standby(disabled_mode_resp)
        if disabled_mode is not False:
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (network standby not disabled after setter)")
            return False

        if not note_partial("Async onNetworkStandbyModeChanged delivery is not asserted by this framework."):
            log_error("TCID40_NetworkStandbyNotify Failed ❌ (strict partial mode enabled)")
            return False
    finally:
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID40_NetworkStandbyNotify Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True