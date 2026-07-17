"""
/**
 * @file TCID39_NetworkStandbyRepeat.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID39_NetworkStandbyRepeat
 * @details Validates idempotent repeated enable and disable operations for
 *          network standby.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_network_standby, parse_wakeup_config


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
        log_error("TCID39_NetworkStandbyRepeat Failed ❌ (unable to read baseline network standby mode)")
        return False

    try:
        first_enable_resp = _set_network_standby(True)
        log_warning(f"First enable response: {first_enable_resp}")
        if not is_ok(first_enable_resp):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (first enable request failed)")
            return False

        second_enable_resp = _set_network_standby(True)
        log_warning(f"Second enable response: {second_enable_resp}")
        if not is_ok(second_enable_resp):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (second enable request failed)")
            return False

        enabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Enabled config response: {enabled_config_resp}")
        enabled_config = parse_wakeup_config(enabled_config_resp)
        if not isinstance(enabled_config, list):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (invalid enabled config)")
            return False

        enabled_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Enabled mode response: {enabled_mode_resp}")
        enabled_mode = parse_network_standby(enabled_mode_resp)
        if enabled_mode is not True:
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (network standby not enabled after repeated enable)")
            return False
        if get_source_enabled(enabled_config, "WIFI") is not True or get_source_enabled(enabled_config, "LAN") is not True:
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (WIFI/LAN not enabled after repeated enable)")
            return False

        first_disable_resp = _set_network_standby(False)
        log_warning(f"First disable response: {first_disable_resp}")
        if not is_ok(first_disable_resp):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (first disable request failed)")
            return False

        second_disable_resp = _set_network_standby(False)
        log_warning(f"Second disable response: {second_disable_resp}")
        if not is_ok(second_disable_resp):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (second disable request failed)")
            return False

        disabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Disabled config response: {disabled_config_resp}")
        disabled_config = parse_wakeup_config(disabled_config_resp)
        if not isinstance(disabled_config, list):
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (invalid disabled config)")
            return False

        disabled_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Disabled mode response: {disabled_mode_resp}")
        disabled_mode = parse_network_standby(disabled_mode_resp)
        if disabled_mode is not False:
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (network standby not disabled after repeated disable)")
            return False
        if get_source_enabled(disabled_config, "WIFI") is not False or get_source_enabled(disabled_config, "LAN") is not False:
            log_error("TCID39_NetworkStandbyRepeat Failed ❌ (WIFI/LAN not disabled after repeated disable)")
            return False
    finally:
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID39_NetworkStandbyRepeat Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True