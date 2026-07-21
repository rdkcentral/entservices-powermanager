"""
/**
 * @file TCID11_ExternalWakePrecedence.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID11_ExternalWakePrecedence
 * @details Validates that when TIMER and one non-network external wake source
 *          are both enabled with a long deep-sleep timer, the injected
 *          external wake trigger wins over timer expiry for every supported
 *          non-network wake family.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, parse_last_wakeup_keycode, parse_last_wakeup_reason, parse_power_state, parse_wakeup_config, wakeup_map


PRECEDENCE_TIMER_SECONDS = 20
PRECEDENCE_WAKE_DELAY_SECONDS = 3
ALL_WAKE_SOURCES = [
    "VOICE",
    "PRESENCEDETECTED",
    "BLUETOOTH",
    "WIFI",
    "IR",
    "POWERKEY",
    "TIMER",
    "CEC",
    "LAN",
    "RF4CE",
]
PRECEDENCE_VARIANTS = [
    {
        "label": "CEC",
        "wakeup_source": "CEC",
        "expected_reason": "CEC",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_CEC.yaml",
        "standby_reason": "PM-PLUGIN-011-CEC",
    },
    {
        "label": "VOICE",
        "wakeup_source": "VOICE",
        "expected_reason": "VOICE",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_VOICE.yaml",
        "standby_reason": "PM-PLUGIN-011-VOICE",
    },
    {
        "label": "PRESENCE",
        "wakeup_source": "PRESENCEDETECTED",
        "expected_reason": "PRESENCE",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_PRESENCE.yaml",
        "standby_reason": "PM-PLUGIN-011-PRESENCE",
    },
    {
        "label": "FRONT_PANEL",
        "wakeup_source": "POWERKEY",
        "expected_reason": "FRONTPANEL",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_FRONT_PANEL.yaml",
        "standby_reason": "PM-PLUGIN-011-FRONTPANEL",
    },
    {
        "label": "RCU_IR",
        "wakeup_source": "IR",
        "expected_reason": "IR",
        "expected_keycode": 6,
        "yaml_file": "DeepSleep_Wakeup_RCU_IR.yaml",
        "standby_reason": "PM-PLUGIN-011-IR",
    },
    {
        "label": "RCU_BT",
        "wakeup_source": "BLUETOOTH",
        "expected_reason": "BLUETOOTH",
        "expected_keycode": 7,
        "yaml_file": "DeepSleep_Wakeup_RCU_BT.yaml",
        "standby_reason": "PM-PLUGIN-011-BT",
    },
    {
        "label": "RCU_RF4CE",
        "wakeup_source": "RF4CE",
        "expected_reason": "RF4CE",
        "expected_keycode": 8,
        "yaml_file": "DeepSleep_Wakeup_RCU_RF4CE.yaml",
        "standby_reason": "PM-PLUGIN-011-RF4CE",
    },
]


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


def _build_restore_entries(config_list):
    return [
        {"wakeupSource": source, "enabled": enabled}
        for source, enabled in wakeup_map(config_list).items()
    ]


def _build_variant_entries(config_list, enabled_source):
    desired_states = {source: False for source in ALL_WAKE_SOURCES}
    desired_states["TIMER"] = True
    desired_states[enabled_source] = True
    return build_wakeup_override_entries(config_list, desired_states)


def _restore_baseline(original_config, standby_reason):
    send_curl_command(PowerManagerApis.set_wakeup_source_config(_build_restore_entries(original_config)))
    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason=standby_reason))


def _run_variant(original_config, variant):
    label = variant["label"]
    wakeup_source = variant["wakeup_source"]
    expected_reason = variant["expected_reason"]
    expected_keycode = variant["expected_keycode"]
    yaml_file = variant["yaml_file"]
    standby_reason = variant["standby_reason"]

    try:
        log_warning(f"Starting precedence subflow: {label}")

        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(_build_variant_entries(original_config, wakeup_source))
        )
        log_warning(f"{label} set response: {set_resp}")
        if not is_ok(set_resp):
            return False, f"failed to configure {label} precedence setup"

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"{label} configured wakeup response: {config_resp}")
        configured = parse_wakeup_config(config_resp)
        if not isinstance(configured, list):
            return False, f"unable to read configured wakeup state for {label}"
        if get_source_enabled(configured, wakeup_source) is not True:
            return False, f"{wakeup_source} not enabled before deep sleep for {label}"
        if get_source_enabled(configured, "TIMER") is not True:
            return False, f"TIMER not enabled before deep sleep for {label}"

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(PRECEDENCE_TIMER_SECONDS))
        log_warning(f"{label} timer response: {timer_resp}")
        if not is_ok(timer_resp):
            return False, f"failed to set deep-sleep timer for {label}"

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason=standby_reason, timeout=10))
        log_warning(f"{label} deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            return False, f"failed to enter DEEP_SLEEP for {label}"

        time.sleep(PRECEDENCE_WAKE_DELAY_SECONDS)
        if not _post_deepsleep(yaml_file):
            return False, f"failed to post {label} wake simulation"

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            return False, f"device did not report a post-wake power state for {label}"

        reason = _wait_for_wakeup_reason(expected_reason)
        if reason != expected_reason:
            return False, f"last wakeup reason for {label} was {reason} instead of {expected_reason}"

        keycode_resp = send_curl_command(PowerManagerApis.get_last_wakeup_keycode)
        log_warning(f"{label} wakeup keycode response: {keycode_resp}")
        keycode = parse_last_wakeup_keycode(keycode_resp)
        if keycode != expected_keycode:
            return False, f"last wakeup keycode for {label} was {keycode} instead of {expected_keycode}"

        post_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"{label} post-wake config response: {post_config_resp}")
        post_config = parse_wakeup_config(post_config_resp)
        if not isinstance(post_config, list):
            return False, f"invalid wakeup config after {label} wake"
        if get_source_enabled(post_config, wakeup_source) is not True:
            return False, f"{wakeup_source} not enabled after {label} wake"
        if get_source_enabled(post_config, "TIMER") is not True:
            return False, f"TIMER not enabled after {label} wake"

        log_success(f"{label} precedence subflow passed (reason={expected_reason}, keycode={expected_keycode})")
        return True, None
    finally:
        _restore_baseline(original_config, f"{standby_reason}-restore")


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID11_ExternalWakePrecedence Failed ❌ (unable to read baseline config)")
        return False

    failures = []
    try:
        for variant in PRECEDENCE_VARIANTS:
            ok, failure = _run_variant(original_config, variant)
            if not ok and failure:
                log_error(f"{variant['label']} precedence subflow failed ❌ ({failure})")
                failures.append(f"{variant['label']}: {failure}")
    finally:
        _restore_baseline(original_config, "PM-PLUGIN-011-final-restore")

    if failures:
        log_error(f"TCID11_ExternalWakePrecedence Failed ❌ ({'; '.join(failures)})")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID11_ExternalWakePrecedence Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True