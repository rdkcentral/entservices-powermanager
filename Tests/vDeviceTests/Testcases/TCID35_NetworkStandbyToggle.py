"""
/**
 * @file TCID35_NetworkStandbyToggle.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID35_NetworkStandbyToggle
 * @details Validates that toggling network standby only affects the network
 *          wakeup sources.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, non_network_map, parse_wakeup_config


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
        log_error("TCID35_NetworkStandbyToggle Failed ❌ (unable to read baseline network standby mode)")
        return False

    baseline_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Baseline config response: {baseline_resp}")
    baseline_config = parse_wakeup_config(baseline_resp)
    if not isinstance(baseline_config, list):
        log_error("TCID35_NetworkStandbyToggle Failed ❌ (unable to read baseline config)")
        return False

    try:
        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (enable request failed)")
            return False

        enabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Enabled config response: {enabled_config_resp}")
        enabled_config = parse_wakeup_config(enabled_config_resp)
        if not isinstance(enabled_config, list):
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (invalid enabled config)")
            return False

        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (disable request failed)")
            return False

        disabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Disabled config response: {disabled_config_resp}")
        disabled_config = parse_wakeup_config(disabled_config_resp)
        if not isinstance(disabled_config, list):
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (invalid disabled config)")
            return False

        if get_source_enabled(enabled_config, "WIFI") is not True or get_source_enabled(enabled_config, "LAN") is not True:
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (enable flow did not enable WIFI/LAN)")
            return False

        if get_source_enabled(disabled_config, "WIFI") is not False or get_source_enabled(disabled_config, "LAN") is not False:
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (disable flow did not disable WIFI/LAN)")
            return False

        if non_network_map(enabled_config) != non_network_map(baseline_config) or non_network_map(disabled_config) != non_network_map(baseline_config):
            log_error("TCID35_NetworkStandbyToggle Failed ❌ (non-network wake sources changed unexpectedly)")
            return False
    finally:
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID35_NetworkStandbyToggle Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True