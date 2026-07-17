"""
/**
 * @file TCID33_EnableNetworkStandby.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID33_EnableNetworkStandby
 * @details Validates that enabling network standby enables WIFI and LAN in the
 *          wakeup-source configuration.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, non_network_map, parse_network_standby, parse_wakeup_config


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
        log_error("TCID33_EnableNetworkStandby Failed ❌ (unable to read baseline network standby mode)")
        return False

    baseline_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Baseline config response: {baseline_resp}")
    baseline_config = parse_wakeup_config(baseline_resp)
    if not isinstance(baseline_config, list):
        log_error("TCID33_EnableNetworkStandby Failed ❌ (unable to read baseline config)")
        return False

    try:
        set_resp = _set_network_standby(True)
        log_warning(f"Set response: {set_resp}")
        if not is_ok(set_resp):
            log_error("TCID33_EnableNetworkStandby Failed ❌ (failed to enable network standby)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if mode is not True:
            log_error("TCID33_EnableNetworkStandby Failed ❌ (network standby not enabled)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID33_EnableNetworkStandby Failed ❌ (invalid wakeup config)")
            return False

        if get_source_enabled(config, "WIFI") is not True or get_source_enabled(config, "LAN") is not True:
            log_error("TCID33_EnableNetworkStandby Failed ❌ (WIFI/LAN not enabled)")
            return False

        if non_network_map(config) != non_network_map(baseline_config):
            log_error("TCID33_EnableNetworkStandby Failed ❌ (non-network wake sources changed unexpectedly)")
            return False
    finally:
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID33_EnableNetworkStandby Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True