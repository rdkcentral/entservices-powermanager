"""
/**
 * @file TCID17_InvalidWakeupUpdate.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID17_InvalidWakeupUpdate
 * @details Validates that a failed wakeup-source update is rejected, that an
 *          UNKNOWN deep-sleep wake stimulus does not exit deep sleep, and that
 *          a subsequent IR wake still follows the preserved valid config.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_error, parse_last_wakeup_reason, parse_power_state, parse_wakeup_config


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


def _wait_for_exact_state(expected_state, timeout_seconds=5):
    deadline = time.time() + timeout_seconds
    last_state = None
    while time.time() < deadline:
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        last_state = parse_power_state(state_resp)
        if isinstance(last_state, dict) and last_state.get("currentState") == expected_state:
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

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID17_InvalidWakeupUpdate Failed ❌ (unable to read baseline config)")
        return False

    restore_resp = None
    try:
        valid_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config([
                {"wakeupSource": "IR", "enabled": True},
            ])
        )
        log_warning(f"Valid config response: {valid_resp}")
        if not is_ok(valid_resp):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (failed to set valid wakeup config)")
            return False

        configured_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Configured wakeup response: {configured_resp}")
        configured = parse_wakeup_config(configured_resp)
        if not isinstance(configured, list):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (unable to read configured wakeup state)")
            return False
        if get_source_enabled(configured, "IR") is not True:
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (IR was not enabled before deep sleep)")
            return False

        invalid_resp = send_curl_command(
            PowerManagerApis.set_wakeup_source_config([
                {"wakeupSource": "UNKNOWN", "enabled": True}
            ])
        )
        log_warning(f"Invalid config response: {invalid_resp}")
        if not isinstance(parse_error(invalid_resp), dict):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (invalid update was not rejected)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-011", timeout=10))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not is_ok(deep_resp):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (failed to enter DEEP_SLEEP)")
            return False

        time.sleep(3)
        if not _post_deepsleep("DeepSleep_Wakeup_UNKNOWN.yaml"):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (failed to post UNKNOWN wake simulation)")
            return False

        unknown_state = _wait_for_exact_state("DEEP_SLEEP")
        if not isinstance(unknown_state, dict):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (device did not report power state after UNKNOWN wake)")
            return False
        if unknown_state.get("currentState") != "DEEP_SLEEP":
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (UNKNOWN wake unexpectedly exited DEEP_SLEEP)")
            return False

        time.sleep(1)
        if not _post_deepsleep("DeepSleep_Wakeup_RCU_IR.yaml"):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (failed to post IR wake simulation)")
            return False

        state = _wait_for_awake_state()
        if not isinstance(state, dict):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (device did not report a post-IR power state)")
            return False
        if state.get("currentState") == "DEEP_SLEEP":
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (device did not exit DEEP_SLEEP after IR wake)")
            return False

        reason = _wait_for_wakeup_reason("IR")
        if reason != "IR":
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (wake reason did not follow preserved IR config)")
            return False

        post_config_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
        log_warning(f"Post-wake config response: {post_config_resp}")
        post_config = parse_wakeup_config(post_config_resp)
        if not isinstance(post_config, list):
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (unable to read wakeup config after wake)")
            return False
        if get_source_enabled(post_config, "IR") is not True:
            log_error("TCID17_InvalidWakeupUpdate Failed ❌ (IR was not preserved after rejected update)")
            return False
    finally:
        restore_resp = send_curl_command(PowerManagerApis.set_wakeup_source_config(original_config))
        log_warning(f"Restore config response: {restore_resp}")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-011-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID17_InvalidWakeupUpdate Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True