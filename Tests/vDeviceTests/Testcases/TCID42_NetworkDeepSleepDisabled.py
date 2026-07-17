"""
/**
 * @file TCID42_NetworkDeepSleepDisabled.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID42_NetworkDeepSleepDisabled
 * @details Validates that network standby remains disabled across a deep-sleep
 *          timer wake cycle.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_network_standby, parse_power_state, parse_wakeup_config, wakeup_map


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
        log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (unable to read baseline config)")
        return False

    try:
        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (failed to disable network standby)")
            return False

        disabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Disabled config response: {disabled_config_resp}")
        disabled_config = parse_wakeup_config(disabled_config_resp)
        if not isinstance(disabled_config, list):
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (invalid disabled config)")
            return False
        if get_source_enabled(disabled_config, "WIFI") is not False or get_source_enabled(disabled_config, "LAN") is not False:
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (WIFI/LAN not disabled before deep sleep)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-026", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (deep-sleep request failed)")
            return False

        time.sleep(7)
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        state = parse_power_state(state_resp)
        if not isinstance(state, dict) or state.get("currentState") != "LIGHT_SLEEP":
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (expected LIGHT_SLEEP after wake)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if mode is not False:
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (network standby not disabled after cycle)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (invalid wakeup config after cycle)")
            return False
        if get_source_enabled(config, "WIFI") is not False or get_source_enabled(config, "LAN") is not False:
            log_error("TCID42_NetworkDeepSleepDisabled Failed ❌ (WIFI/LAN not disabled after cycle)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-026-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID42_NetworkDeepSleepDisabled Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True