"""
/**
 * @file TCID27_AsymmetricNetworkConfig.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID27_AsymmetricNetworkConfig
 * @details Validates asymmetric WIFI/LAN wakeup configuration while the
 *          network-standby baseline remains disabled.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_network_standby, parse_wakeup_config, wakeup_map


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
        log_error("TCID27_NetworkDeepSleepEnabled Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID27_NetworkDeepSleepEnabled Failed ❌ (unable to read baseline config)")
        return False

    try:
        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (failed to disable network standby)")
            return False

        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config([
                {"wakeupSource": "WIFI", "enabled": True},
                {"wakeupSource": "LAN", "enabled": False},
            ])
        )
        log_warning(f"Set response: {set_resp}")
        if not is_ok(set_resp):
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (failed to set asymmetric WIFI/LAN config)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (invalid wakeup config)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if get_source_enabled(config, "WIFI") is not True:
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (WIFI not enabled)")
            return False
        if get_source_enabled(config, "LAN") is not False:
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (LAN not disabled)")
            return False
        if mode is not False:
            log_error("TCID27_AsymmetricNetworkConfig Failed ❌ (network standby baseline did not remain disabled)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID27_AsymmetricNetworkConfig Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True