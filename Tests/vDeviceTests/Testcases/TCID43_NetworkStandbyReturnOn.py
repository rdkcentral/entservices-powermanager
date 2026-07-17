"""
/**
 * @file TCID43_NetworkStandbyReturnOn.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID43_NetworkStandbyReturnOn
 * @details Validates that a deep-sleep cycle followed by an explicit ON
 *          transition preserves network-standby behavior and wakeup age.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_network_standby, parse_power_state, parse_time_since_wakeup, parse_wakeup_config, wakeup_map


def _set_network_standby(enabled):
    response = send_curl_command(PowerManagerApis.set_network_standby_mode(enabled))
    if is_ok(response):
        return response
    response = send_curl_command(PowerManagerApis.set_network_standby_mode_nw(enabled))
    return response


def _wait_for_on_state(timeout_seconds=5):
    deadline = time.monotonic() + timeout_seconds
    last_state = None
    while time.monotonic() < deadline:
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Polled power state response: {state_resp}")
        state = parse_power_state(state_resp)
        if isinstance(state, dict):
            last_state = state
            if state.get("currentState") == "ON":
                return True
        time.sleep(1)
    return isinstance(last_state, dict) and last_state.get("currentState") == "ON"


def run_test():
    start_time = time.perf_counter()

    original_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
    log_warning(f"Original mode response: {original_mode_resp}")
    original_mode = parse_network_standby(original_mode_resp)
    if not isinstance(original_mode, bool):
        log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (unable to read baseline config)")
        return False

    try:
        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (failed to enable network standby)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-027", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (deep-sleep request failed)")
            return False

        time.sleep(7)
        on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-027"))
        log_warning(f"ON response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (failed to return to ON)")
            return False

        if not _wait_for_on_state(5):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (power state did not reach ON)")
            return False

        time.sleep(2)
        wake_age_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"Wakeup age response: {wake_age_resp}")
        wake_age = parse_time_since_wakeup(wake_age_resp)
        if not isinstance(wake_age, int) or wake_age <= 0:
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (expected non-zero wakeup age after ON)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if mode is not True:
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (network standby not enabled after cycle)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (invalid wakeup config after cycle)")
            return False
        if get_source_enabled(config, "WIFI") is not True or get_source_enabled(config, "LAN") is not True:
            log_error("TCID43_NetworkStandbyReturnOn Failed ❌ (WIFI/LAN not enabled after cycle)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-027-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID43_NetworkStandbyReturnOn Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True