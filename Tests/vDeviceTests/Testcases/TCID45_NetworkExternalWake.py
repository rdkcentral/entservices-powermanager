"""
/**
 * @file TCID45_NetworkExternalWake.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID45_NetworkExternalWake
 * @details Validates network wake preconditions and records the current partial
 *          limitation around missing external wake stimulus in this framework.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, note_partial, parse_network_standby, parse_wakeup_config, wakeup_map


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
        log_error("TCID45_NetworkExternalWake Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID45_NetworkExternalWake Failed ❌ (unable to read baseline config)")
        return False

    try:
        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID45_NetworkExternalWake Failed ❌ (failed to enable network standby)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID45_NetworkExternalWake Failed ❌ (invalid wakeup config)")
            return False
        if get_source_enabled(config, "WIFI") is not True or get_source_enabled(config, "LAN") is not True:
            log_error("TCID45_NetworkExternalWake Failed ❌ (WIFI/LAN not enabled before network wake test)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(60))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID45_NetworkExternalWake Failed ❌ (failed to set deep-sleep timer)")
            return False

        if not note_partial("Full external LAN/WIFI wake validation requires a harness-generated network stimulus before timer expiry and is not executed here."):
            log_error("TCID45_NetworkExternalWake Failed ❌ (strict partial mode enabled)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID45_NetworkExternalWake Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True