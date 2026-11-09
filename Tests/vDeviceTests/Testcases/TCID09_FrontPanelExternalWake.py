"""
/**
 * @file TCID09_FrontPanelExternalWake.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID09_FrontPanelExternalWake
 * @details Validates that enabling FRONT_PANEL as a wake source allows a
 *          simulated FRONT_PANEL DeepSleep wake and reports the correct
 *          non-key wake metadata.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_explicit_wakeup_entries, parse_last_wakeup_keycode, parse_last_wakeup_reason, parse_power_state, parse_wakeup_config, wakeup_map


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


def _build_powerkey_only_entries(config_list):
    return build_explicit_wakeup_entries(config_list, ["POWERKEY"])


def _matches_expected_config(config_list, expected_map):
    return wakeup_map(config_list) == expected_map


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID09_FrontPanelExternalWake Failed ❌ (unable to read baseline config)")
        return False

    try:
        expected_entries = _build_powerkey_only_entries(original_config)
        expected_config = wakeup_map(expected_entries)
        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                expected_entries
            )
        )
        log_warning(f"Set response: {set_resp}")
        if not is_ok(set_resp):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (failed to configure POWERKEY-only wake source state)")
            return False

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {config_resp}")
        configured = parse_wakeup_config(config_resp)
        if not isinstance(configured, list):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (unable to read configured wakeup state)")
            return False
        if not _matches_expected_config(configured, expected_config):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (configured wakeup state was not POWERKEY=true with all other sources false)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-033", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(3)
        if not _post_deepsleep("DeepSleep_Wakeup_FRONT_PANEL.yaml"):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (failed to post FRONT_PANEL wake simulation)")
            return False

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (device did not report a post-wake power state)")
            return False

        reason = _wait_for_wakeup_reason("FRONTPANEL")
        if reason != "FRONTPANEL":
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (last wakeup reason was not FRONTPANEL)")
            return False

        keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"Wakeup keycode response: {keycode_resp}")
        keycode = parse_last_wakeup_keycode(keycode_resp)
        if keycode != 0:
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (FRONT_PANEL wake should not report a non-zero keycode)")
            return False

        post_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Post-wake config response: {post_config_resp}")
        post_config = parse_wakeup_config(post_config_resp)
        if not isinstance(post_config, list):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (invalid wakeup config after FRONT_PANEL wake)")
            return False
        if not _matches_expected_config(post_config, expected_config):
            log_error("TCID09_FrontPanelExternalWake Failed ❌ (post-wake config was not POWERKEY=true with all other sources false)")
            return False
    finally:
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-033-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID09_FrontPanelExternalWake Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True

