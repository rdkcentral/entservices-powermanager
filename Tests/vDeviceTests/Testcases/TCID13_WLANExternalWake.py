"""
/**
 * @file TCID13_WLANExternalWake.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID13_WLANExternalWake
 * @details Validates that with network standby and a long deep-sleep timer
 *          enabled, a simulated WLAN wake exits deep sleep before timer expiry
 *          and reports WIFI as the wakeup reason.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, parse_last_wakeup_keycode, parse_last_wakeup_reason, parse_network_standby, parse_power_state, parse_wakeup_config, wakeup_map


def _set_network_standby(enabled):
    response = send_curl_command(PowerManagerApis.set_network_standby_mode(enabled))
    if is_ok(response):
        return response
    response = send_curl_command(PowerManagerApis.set_network_standby_mode_nw(enabled))
    return response


def _post_deepsleep(yaml_file):
    http_code, body = send_vcomponent_command(f"{POWERMANAGER_CMD_BASE}/{yaml_file}")
    log_warning(f"vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def _wait_for_awake_state(timeout_seconds=20):
    deadline = time.time() + timeout_seconds
    last_state = None
    while time.time() < deadline:
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        last_state = parse_power_state(state_resp)
        if isinstance(last_state, dict) and last_state.get("currentState") != "DEEP_SLEEP":
            return last_state
        time.sleep(1)
    return last_state


def _wait_for_wakeup_reason(expected_reason, timeout_seconds=20):
    deadline = time.time() + timeout_seconds
    last_reason = None
    while time.time() < deadline:
        reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"Wakeup reason response: {reason_resp}")
        last_reason = parse_last_wakeup_reason(reason_resp)
        if last_reason == expected_reason:
            return last_reason
        time.sleep(1)
    return last_reason


def run_test():
    start_time = time.perf_counter()

    original_mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
    log_warning(f"Original mode response: {original_mode_resp}")
    original_mode = parse_network_standby(original_mode_resp)
    if not isinstance(original_mode, bool):
        log_error("TCID13_WLANExternalWake Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID13_WLANExternalWake Failed ❌ (unable to read baseline config)")
        return False

    try:
        enable_resp = _set_network_standby(True)
        log_warning(f"Enable response: {enable_resp}")
        if not is_ok(enable_resp):
            log_error("TCID13_WLANExternalWake Failed ❌ (failed to enable network standby)")
            return False

        timer_enable_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                build_wakeup_override_entries(original_config, {
                    "WIFI": True,
                    "LAN": True,
                    "TIMER": True,
                })
            )
        )
        log_warning(f"Timer enable response: {timer_enable_resp}")
        if not is_ok(timer_enable_resp):
            log_error("TCID13_WLANExternalWake Failed ❌ (failed to enable timer-backed network wake configuration)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID13_WLANExternalWake Failed ❌ (invalid wakeup config)")
            return False
        if get_source_enabled(config, "WIFI") is not True or get_source_enabled(config, "LAN") is not True:
            log_error("TCID13_WLANExternalWake Failed ❌ (WIFI/LAN not enabled before network wake test)")
            return False
        if get_source_enabled(config, "TIMER") is not True:
            log_error("TCID13_WLANExternalWake Failed ❌ (TIMER not enabled before deep sleep)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(20))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID13_WLANExternalWake Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-010", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID13_WLANExternalWake Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(3)
        if not _post_deepsleep("DeepSleep_Wakeup_WLAN.yaml"):
            log_error("TCID13_WLANExternalWake Failed ❌ (failed to post WLAN wake simulation)")
            return False

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            log_error("TCID13_WLANExternalWake Failed ❌ (device did not report a post-wake power state)")
            return False

        reason = _wait_for_wakeup_reason("WIFI")
        if reason != "WIFI":
            log_error("TCID13_WLANExternalWake Failed ❌ (last wakeup reason was not WIFI)")
            return False

        keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Wakeup keycode response: {keycode_resp}")
        keycode = parse_last_wakeup_keycode(keycode_resp)
        if keycode != 0:
            log_error("TCID13_WLANExternalWake Failed ❌ (WLAN wake should not report a non-zero keycode)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if mode is not True:
            log_error("TCID13_WLANExternalWake Failed ❌ (network standby not enabled after WLAN wake)")
            return False

        post_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Post-wake config response: {post_config_resp}")
        post_config = parse_wakeup_config(post_config_resp)
        if not isinstance(post_config, list):
            log_error("TCID13_WLANExternalWake Failed ❌ (invalid wakeup config after WLAN wake)")
            return False
        if get_source_enabled(post_config, "WIFI") is not True or get_source_enabled(post_config, "LAN") is not True:
            log_error("TCID13_WLANExternalWake Failed ❌ (WIFI/LAN not enabled after WLAN wake)")
            return False
        if get_source_enabled(post_config, "TIMER") is not True:
            log_error("TCID13_WLANExternalWake Failed ❌ (TIMER not enabled after WLAN wake)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-010-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID13_WLANExternalWake Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
