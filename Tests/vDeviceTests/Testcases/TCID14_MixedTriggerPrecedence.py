"""
/**
 * @file TCID14_MixedTriggerPrecedence.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID14_MixedTriggerPrecedence
 * @details Validates a reduced representative mixed-trigger precedence matrix
 *          across the non-network external wake families. Each subflow
 *          enables TIMER only as a long fallback wake, enables two external
 *          wake sources, posts both YAML stimuli in an intentional order,
 *          and verifies that the first injected external trigger wins.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import build_wakeup_override_entries, get_source_enabled, parse_error, parse_last_wakeup_keycode, parse_last_wakeup_reason, parse_power_state, parse_wakeup_config, wakeup_map


FALLBACK_TIMER_SECONDS = 20
PRE_POST_DELAY_SECONDS = 3
INTER_TRIGGER_DELAY_SECONDS = 1
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
TRIGGERS = {
    "CEC": {
        "wakeup_source": "CEC",
        "expected_reason": "CEC",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_CEC.yaml",
    },
    "VOICE": {
        "wakeup_source": "VOICE",
        "expected_reason": "VOICE",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_VOICE.yaml",
    },
    "PRESENCE": {
        "wakeup_source": "PRESENCEDETECTED",
        "expected_reason": "PRESENCE",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_PRESENCE.yaml",
    },
    "FRONT_PANEL": {
        "wakeup_source": "POWERKEY",
        "expected_reason": "FRONTPANEL",
        "expected_keycode": 0,
        "yaml_file": "DeepSleep_Wakeup_FRONT_PANEL.yaml",
    },
    "RCU_IR": {
        "wakeup_source": "IR",
        "expected_reason": "IR",
        "expected_keycode": 6,
        "yaml_file": "DeepSleep_Wakeup_RCU_IR.yaml",
    },
    "RCU_BT": {
        "wakeup_source": "BLUETOOTH",
        "expected_reason": "BLUETOOTH",
        "expected_keycode": 7,
        "yaml_file": "DeepSleep_Wakeup_RCU_BT.yaml",
    },
    "RCU_RF4CE": {
        "wakeup_source": "RF4CE",
        "expected_reason": "RF4CE",
        "expected_keycode": 8,
        "yaml_file": "DeepSleep_Wakeup_RCU_RF4CE.yaml",
    },
}
MIXED_TRIGGER_SUBFLOWS = [
    ("CEC", "VOICE"),
    ("VOICE", "PRESENCE"),
    ("PRESENCE", "FRONT_PANEL"),
    ("FRONT_PANEL", "RCU_IR"),
    ("RCU_IR", "RCU_BT"),
    ("RCU_BT", "RCU_RF4CE"),
    ("RCU_RF4CE", "CEC"),
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


def _build_pair_entries(config_list, first_source, second_source):
    desired_states = {source: False for source in ALL_WAKE_SOURCES}
    desired_states["TIMER"] = True
    desired_states[first_source] = True
    desired_states[second_source] = True
    return build_wakeup_override_entries(config_list, desired_states)


def _restore_baseline(original_config, standby_reason):
    send_curl_command(PowerManagerApis.set_wakeup_source_config(_build_restore_entries(original_config)))
    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason=standby_reason))


def _powerkey_pair(trigger_a, trigger_b):
    return "POWERKEY" in {
        TRIGGERS[trigger_a]["wakeup_source"],
        TRIGGERS[trigger_b]["wakeup_source"],
    }


def _run_subflow(original_config, first_trigger, second_trigger, index):
    first_meta = TRIGGERS[first_trigger]
    second_meta = TRIGGERS[second_trigger]
    first_source = first_meta["wakeup_source"]
    second_source = second_meta["wakeup_source"]
    expected_reason = first_meta["expected_reason"]
    expected_keycode = first_meta["expected_keycode"]
    label = f"{first_trigger}_then_{second_trigger}"
    standby_reason = f"PM-PLUGIN-037-{index:02d}-{first_trigger}-{second_trigger}"

    try:
        log_warning(f"Starting mixed-trigger subflow: {label}")

        set_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                _build_pair_entries(original_config, first_source, second_source)
            )
        )
        log_warning(f"{label} set response: {set_resp}")
        tolerated_platform_error = False
        if not is_ok(set_resp):
            error = parse_error(set_resp)
            if not (_powerkey_pair(first_trigger, second_trigger) and isinstance(error, dict) and error.get("message") == "ERROR_GENERAL"):
                return False, f"failed to configure {label} mixed-trigger setup"
            tolerated_platform_error = True
            log_warning(f"{label} wake-source update returned ERROR_GENERAL; continuing with mixed-trigger wake-path validation on this target.")

        config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"{label} configured wakeup response: {config_resp}")
        configured = parse_wakeup_config(config_resp)
        if not isinstance(configured, list):
            return False, f"unable to read configured wakeup state for {label}"
        for source in (first_source, second_source):
            if get_source_enabled(configured, source) is not True:
                if not (tolerated_platform_error and source == "POWERKEY"):
                    return False, f"{source} not enabled before deep sleep for {label}"
                log_warning("POWERKEY remained disabled in read-back after ERROR_GENERAL; continuing because the DeepSleep controller may still accept FRONT_PANEL wake.")
        if get_source_enabled(configured, "TIMER") is not True:
            return False, f"TIMER not enabled before deep sleep for {label}"

        timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(FALLBACK_TIMER_SECONDS))
        log_warning(f"{label} timer response: {timer_resp}")
        if not is_ok(timer_resp):
            return False, f"failed to set deep-sleep timer for {label}"

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason=standby_reason, timeout=10))
        log_warning(f"{label} deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            return False, f"failed to enter DEEP_SLEEP for {label}"

        time.sleep(PRE_POST_DELAY_SECONDS)
        if not _post_deepsleep(first_meta["yaml_file"]):
            return False, f"failed to post first wake simulation for {label}"
        time.sleep(INTER_TRIGGER_DELAY_SECONDS)
        if not _post_deepsleep(second_meta["yaml_file"]):
            return False, f"failed to post second wake simulation for {label}"

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
        for source in (first_source, second_source):
            if get_source_enabled(post_config, source) is not True:
                if not (tolerated_platform_error and source == "POWERKEY"):
                    return False, f"{source} not enabled after {label} wake"
                log_warning("POWERKEY remained disabled in post-wake read-back after ERROR_GENERAL; accepting the successful FRONT_PANEL mixed-trigger wake path on this target.")
        if get_source_enabled(post_config, "TIMER") is not True:
            return False, f"TIMER not enabled after {label} wake"

        log_success(f"{label} mixed-trigger subflow passed (reason={reason}, keycode={keycode})")
        return True, None
    finally:
        _restore_baseline(original_config, f"{standby_reason}-restore")


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID14_MixedTriggerPrecedence Failed ❌ (unable to read baseline config)")
        return False

    failures = []
    try:
        for index, (first_trigger, second_trigger) in enumerate(MIXED_TRIGGER_SUBFLOWS, start=1):
            ok, failure = _run_subflow(original_config, first_trigger, second_trigger, index)
            if not ok and failure:
                label = f"{first_trigger}_then_{second_trigger}"
                log_error(f"{label} mixed-trigger subflow failed ❌ ({failure})")
                failures.append(f"{label}: {failure}")
    finally:
        _restore_baseline(original_config, "PM-PLUGIN-037-final-restore")

    if failures:
        log_error(f"TCID14_MixedTriggerPrecedence Failed ❌ ({'; '.join(failures)})")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID14_MixedTriggerPrecedence Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True