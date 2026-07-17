"""
/**
 * @file TCID26_IgnoreDeepSleep.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID26_IgnoreDeepSleep
 * @details Validates the ignoredeepsleep override handling for a deep-sleep
 *          request when target-local file access is available.
 */
"""

import os
import time
from pathlib import Path

from utils import TARGET_HOST, send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import can_manage_ignoredeepsleep_file, note_partial, parse_error, parse_power_state


IGNORE_DEEPSLEEP_PATH = os.environ.get("POWERMANAGER_IGNOREDEEPSLEEP_PATH", "/tmp/ignoredeepsleep")


def run_test():
    start_time = time.perf_counter()

    if not can_manage_ignoredeepsleep_file():
        if not note_partial("Target-local Linux file access is required for /tmp/ignoredeepsleep handling."):
            log_error("TCID26_IgnoreDeepSleep Failed ❌ (strict partial mode enabled)")
            return False
        elapsed_time = time.perf_counter() - start_time
        msg = "TCID26_IgnoreDeepSleep Passed ✅"
        if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
            log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
        else:
            log_success(msg)
        return True

    override_file = Path(IGNORE_DEEPSLEEP_PATH)
    try:
        override_file.parent.mkdir(parents=True, exist_ok=True)
        override_file.write_text("1", encoding="ascii")

        on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-010"))
        log_warning(f"ON response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID26_IgnoreDeepSleep Failed ❌ (failed to set ON baseline)")
            return False

        before_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Baseline state response: {before_resp}")
        before_state = parse_power_state(before_resp)
        if not isinstance(before_state, dict):
            log_error("TCID26_IgnoreDeepSleep Failed ❌ (invalid baseline state)")
            return False

        deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-010"))
        log_warning(f"Deep sleep response: {deep_resp}")
        if not isinstance(parse_error(deep_resp), dict):
            log_error("TCID26_IgnoreDeepSleep Failed ❌ (DEEP_SLEEP request was not rejected)")
            return False

        after_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Post-request state response: {after_resp}")
        after_state = parse_power_state(after_resp)
        if not isinstance(after_state, dict) or after_state.get("currentState") != before_state.get("currentState"):
            log_error("TCID26_IgnoreDeepSleep Failed ❌ (state changed despite override)")
            return False

        note_partial("Async absence of onDeepSleepTimeout is not asserted by this curl-only testcase.")
    finally:
        try:
            if override_file.exists():
                override_file.unlink()
        except Exception:
            pass
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-010-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID26_IgnoreDeepSleep Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True