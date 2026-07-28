"""
/**
 * @file TCID04_AsymmetricDeepSleep.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID04_AsymmetricDeepSleep
 * @details Validates that with network standby disabled and asymmetric
 *          WIFI/LAN wake configuration, LAN does not wake the device from
 *          deep sleep while WLAN does.
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
        log_error("TCID04_AsymmetricDeepSleep Failed ❌ (unable to read baseline network standby mode)")
        return False

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID04_AsymmetricDeepSleep Failed ❌ (unable to read baseline config)")
        return False

    try:
        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (failed to disable network standby)")
            return False

        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                build_wakeup_override_entries(original_config, {
                    "WIFI": True,
                    "LAN": False,
                    "TIMER": True,
                })
            )
        )
        log_warning(f"Set response: {set_resp}")
        if not is_ok(set_resp):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (failed to set asymmetric WIFI/LAN config with TIMER enabled)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {config_resp}")
        configured = parse_wakeup_config(config_resp)
        if not isinstance(configured, list):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (unable to read asymmetric config)")
            return False
        if get_source_enabled(configured, "WIFI") is not True:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (WIFI not enabled before deep sleep)")
            return False
        if get_source_enabled(configured, "LAN") is not False:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (LAN not disabled before deep sleep)")
            return False
        if get_source_enabled(configured, "TIMER") is not True:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (TIMER not enabled before deep sleep)")
            return False

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(60))
        log_warning(f"Timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (failed to set deep-sleep timer)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-028", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (deep-sleep request failed)")
            return False

        time.sleep(3)
        if not _post_deepsleep("DeepSleep_Wakeup_LAN.yaml"):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (failed to post LAN wake simulation)")
            return False

        time.sleep(4)
        lan_state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Post-LAN state response: {lan_state_resp}")
        lan_state = parse_power_state(lan_state_resp)
        if not isinstance(lan_state, dict) or lan_state.get("currentState") != "DEEP_SLEEP":
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (LAN wake should not exit DEEP_SLEEP)")
            return False

        if not _post_deepsleep("DeepSleep_Wakeup_WLAN.yaml"):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (failed to post WLAN wake simulation)")
            return False

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (device did not report a post-wake power state)")
            return False

        reason = _wait_for_wakeup_reason("WIFI")
        if reason != "WIFI":
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (last wakeup reason was not WIFI after WLAN wake)")
            return False

        keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Wakeup keycode response: {keycode_resp}")
        keycode = parse_last_wakeup_keycode(keycode_resp)
        if keycode != 0:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (WLAN wake should not report a non-zero keycode)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Config response: {config_resp}")
        config = parse_wakeup_config(config_resp)
        if not isinstance(config, list):
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (invalid wakeup config after cycle)")
            return False

        mode_resp = send_curl_command(PowerManagerApis.get_network_standby_mode)
        log_warning(f"Mode response: {mode_resp}")
        mode = parse_network_standby(mode_resp)
        if get_source_enabled(config, "WIFI") is not True:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (WIFI not enabled after cycle)")
            return False
        if get_source_enabled(config, "LAN") is not False:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (LAN not disabled after cycle)")
            return False
        if get_source_enabled(config, "TIMER") is not True:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (TIMER not enabled after cycle)")
            return False
        if mode is not False:
            log_error("TCID04_AsymmetricDeepSleep Failed ❌ (network standby baseline did not remain disabled)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        _set_network_standby(original_mode)
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-028-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID04_AsymmetricDeepSleep Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True


