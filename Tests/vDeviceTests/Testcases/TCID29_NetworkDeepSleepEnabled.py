"""
/**
 * @file TCID29_NetworkDeepSleepEnabled.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID29_NetworkDeepSleepEnabled
 * @details Validates that network standby remains enabled across a deep-sleep
 *          timer wake cycle.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, note_partial, parse_network_standby, parse_power_state, parse_wakeup_config, wakeup_map


def _set_network_standby(enabled):
    response = send_curl_command(PowerManagerApis.set_network_standby_mode(enabled))
    if is_ok(response):
        return response
    response = send_curl_command(PowerManagerApis.set_network_standby_mode_nw(enabled))
    return response


def _is_expected_post_wake_state(state):
    if not isinstance(state, dict):
        return False
    current_state = state.get("currentState")
    if current_state == "LIGHT_SLEEP":
        return True
    if current_state == "ON":
        note_partial("vdevice returned ON rather than LIGHT_SLEEP after the deep-sleep timer cycle.")
        return True
    return False


def run_test():
    start_time = time.perf_counter()

    original_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
    log_warning(f"Original mode response: {original_mode_resp}")
    original_mode = parse_network_standby(original_mode_resp)
    if not isinstance(original_mode, bool):
        log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (unable to read baseline config)")
        return False

    try:
        nw_register_resp = send_curl_command(PowerManagerApis.register_event("onNetworkStandbyModeChanged", "pm_plugin_024_nwstandby"))
        log_warning(f"Network standby event registration response: {nw_register_resp}")
        if not is_ok(nw_register_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (network-standby event registration failed)")
            return False

        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (failed to enable network standby)")
            return False

        enabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Enabled config response: {enabled_config_resp}")
        enabled_config = parse_wakeup_config(enabled_config_resp)
        if not isinstance(enabled_config, list):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (invalid enabled config)")
            return False
        if get_source_enabled(enabled_config, "WIFI") is not True or get_source_enabled(enabled_config, "LAN") is not True:
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (WIFI/LAN not enabled before deep sleep)")
            return False

        timer_enable_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                build_wakeup_override_entries(original_config, {
                    "IR": False,
                    "WIFI": True,
                    "LAN": True,
                    "TIMER": True,
                })
            )
        )
        log_warning(f"Timer enable response: {timer_enable_resp}")
        if not is_ok(timer_enable_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (failed to enable TIMER wake source)")
            return False

        timer_enabled_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Timer-enabled config response: {timer_enabled_config_resp}")
        timer_enabled_config = parse_wakeup_config(timer_enabled_config_resp)
        if not isinstance(timer_enabled_config, list):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (invalid timer-enabled config)")
            return False
        if get_source_enabled(timer_enabled_config, "TIMER") is not True:
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (TIMER not enabled before deep sleep)")
            return False
        if get_source_enabled(timer_enabled_config, "WIFI") is not True or get_source_enabled(timer_enabled_config, "LAN") is not True:
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (WIFI/LAN changed while enabling TIMER)")
            return False

        register_resp = send_curl_command(PowerManagerApis.register_event("onDeepSleepTimeout", "pm_plugin_025_timeout"))
        log_warning(f"Deep sleep event registration response: {register_resp}")
        if not is_ok(register_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (deep-sleep-timeout event registration failed)")
            return False

        if not note_partial("Async onNetworkStandbyModeChanged delivery is not asserted by this framework."):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (strict partial mode enabled)")
            return False

        if not note_partial("Async onDeepSleepTimeout delivery is not asserted by this framework."):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (strict partial mode enabled)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-025", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (deep-sleep request failed)")
            return False

        time.sleep(7)
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        state = parse_power_state(state_resp)
        if not _is_expected_post_wake_state(state):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (unexpected post-wake power state)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if mode is not True:
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (network standby not enabled after cycle)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (invalid wakeup config after cycle)")
            return False
        if get_source_enabled(config, "WIFI") is not True or get_source_enabled(config, "LAN") is not True:
            log_error("TCID29_NetworkDeepSleepEnabled Failed ❌ (WIFI/LAN not enabled after cycle)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-025-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID29_NetworkDeepSleepEnabled Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True