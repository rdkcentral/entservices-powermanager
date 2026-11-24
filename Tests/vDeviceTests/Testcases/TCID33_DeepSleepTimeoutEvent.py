"""
/**
 * @file TCID33_DeepSleepTimeoutEvent.py
 * @brief Validates onDeepSleepTimeout delivery over a persistent WebSocket.
 *
 * @testcase TCID33_DeepSleepTimeoutEvent
 * @details Registers for onDeepSleepTimeout, enters timed deep sleep, validates
 *          wakeupTimeout=5, and verifies the transition to LIGHT_SLEEP.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import (
    build_wakeup_override_entries,
    get_source_enabled,
    parse_power_state,
    parse_time_since_wakeup,
    parse_wakeup_config,
    wakeup_map,
)
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
)


DEEP_SLEEP_TIMEOUT = 5
EVENT_TIMEOUT = float(os.environ.get("POWERMANAGER_DEEP_SLEEP_EVENT_TIMEOUT", "42"))


def run_test():
    start_time = time.perf_counter()
    listener = None

    original_config_resp = send_curl_command(
        PowerManagerApis.get_wakeup_source_config
    )
    original_config = parse_wakeup_config(original_config_resp)
    if not isinstance(original_config, list):
        log_error("TCID33_DeepSleepTimeoutEvent Failed (unable to read wakeup config)")
        return False

    try:
        if get_source_enabled(original_config, "TIMER") is not True:
            timer_config_resp = send_curl_command(
                PowerManagerApis.set_wakeup_source_config(
                    build_wakeup_override_entries(
                        original_config, {"TIMER": True}
                    )
                )
            )
            if not is_ok(timer_config_resp):
                log_error(
                    "TCID33_DeepSleepTimeoutEvent Failed "
                    "(unable to enable TIMER wake source)"
                )
                return False

        baseline_resp = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID33-event-baseline"
            )
        )
        if not is_ok(baseline_resp):
            log_error("TCID33_DeepSleepTimeoutEvent Failed (unable to establish ON baseline)")
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onDeepSleepTimeout",
            "TCID33_onDeepSleepTimeout",
            timeout=EVENT_TIMEOUT,
        )
        log_info("Registering onDeepSleepTimeout over WebSocket")
        if not listener.connect():
            log_error("TCID33_DeepSleepTimeoutEvent Failed (event registration rejected)")
            return False
        log_success("Registered onDeepSleepTimeout event listener")

        log_info(f"Executing setDeepSleepTimer({DEEP_SLEEP_TIMEOUT})")
        timer_resp = send_curl_command(
            PowerManagerApis.set_deep_sleep_timer(DEEP_SLEEP_TIMEOUT)
        )
        log_warning(f"Deep sleep timer response: {timer_resp}")
        if not is_ok(timer_resp):
            log_error("TCID33_DeepSleepTimeoutEvent Failed (setDeepSleepTimer failed)")
            return False
        log_success(
            f"setDeepSleepTimer accepted with timeout={DEEP_SLEEP_TIMEOUT}"
        )

        log_info("Executing setPowerState(DEEP_SLEEP)")
        deep_sleep_resp = send_curl_command(
            PowerManagerApis.set_power_state(
                "DEEP_SLEEP", standby_reason="APIUnitTest", timeout=10
            )
        )
        log_warning(f"DEEP_SLEEP response: {deep_sleep_resp}")
        if not is_ok(deep_sleep_resp):
            log_error("TCID33_DeepSleepTimeoutEvent Failed (setPowerState failed)")
            return False
        log_success("setPowerState(DEEP_SLEEP) accepted")

        log_info("Executing getTimeSinceWakeup in DEEP_SLEEP")
        wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"DEEP_SLEEP wake time response: {wake_resp}")
        wake_age = parse_time_since_wakeup(wake_resp)
        if wake_age != 0:
            log_error("TCID33_DeepSleepTimeoutEvent Failed (wake time was not reset)")
            return False
        log_success("Validated secondsSinceWakeup=0 in DEEP_SLEEP")

        log_info(
            f"Waiting up to {EVENT_TIMEOUT:.0f}s for onDeepSleepTimeout"
        )
        notification = listener.wait_for_event(
            lambda params: params.get("wakeupTimeout") == DEEP_SLEEP_TIMEOUT,
            timeout=EVENT_TIMEOUT,
        )
        if notification is None:
            log_error(
                "TCID33_DeepSleepTimeoutEvent Failed "
                "(onDeepSleepTimeout with wakeupTimeout=5 was not received)"
            )
            return False
        log_success(f"Captured onDeepSleepTimeout event: {notification}")

        log_info("Executing getPowerState after deep-sleep timeout")
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Post-timeout power state response: {state_resp}")
        state = parse_power_state(state_resp)
        if not isinstance(state, dict) or state.get("currentState") != "LIGHT_SLEEP":
            log_error(
                "TCID33_DeepSleepTimeoutEvent Failed "
                f"(expected LIGHT_SLEEP after timeout, got {state})"
            )
            return False
        log_success(
            "Validated DEEP_SLEEP to LIGHT_SLEEP transition after timer timeout"
        )

        log_info("Executing setPowerState(ON) from LIGHT_SLEEP")
        on_resp = send_curl_command(
            PowerManagerApis.set_power_state("ON", standby_reason="APIUnitTest")
        )
        log_warning(f"ON response: {on_resp}")
        if not is_ok(on_resp):
            log_error("TCID33_DeepSleepTimeoutEvent Failed (unable to return to ON)")
            return False
        log_success("setPowerState(ON) accepted")

        time.sleep(1)
        log_info("Executing getTimeSinceWakeup in ON")
        on_wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"ON wake time response: {on_wake_resp}")
        on_wake_age = parse_time_since_wakeup(on_wake_resp)
        if not isinstance(on_wake_age, int):
            log_error(
                "TCID33_DeepSleepTimeoutEvent Failed "
                "(secondsSinceWakeup was not returned after entering ON)"
            )
            return False
        log_success(
            f"Validated secondsSinceWakeup={on_wake_age} after entering ON"
        )
    finally:
        if listener is not None:
            listener.close()
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(
            PowerManagerApis.set_wakeup_source_config(restore_entries)
        )
        send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID33-event-restore"
            )
        )

    elapsed_time = time.perf_counter() - start_time
    message = "TCID33_DeepSleepTimeoutEvent Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
