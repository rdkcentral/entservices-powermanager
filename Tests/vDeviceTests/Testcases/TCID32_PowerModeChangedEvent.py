"""
/**
 * @file TCID32_PowerModeChangedEvent.py
 * @brief Validates onPowerModeChanged delivery over a persistent WebSocket.
 *
 * @testcase TCID32_PowerModeChangedEvent
 * @details Registers for onPowerModeChanged, enters STANDBY, validates the
 *          asynchronous event payload, and verifies wake time is reset.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_power_state, parse_time_since_wakeup
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
)


EVENT_TIMEOUT = float(os.environ.get("POWERMANAGER_EVENT_TIMEOUT", "8"))


def _wait_for_state(expected_state, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        response = send_curl_command(PowerManagerApis.get_power_state)
        state = parse_power_state(response)
        if isinstance(state, dict) and state.get("currentState") == expected_state:
            return state
        time.sleep(0.25)
    return None


def run_test():
    start_time = time.perf_counter()
    listener = None

    try:
        baseline_resp = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID32-event-baseline"
            )
        )
        if not is_ok(baseline_resp) or _wait_for_state("ON") is None:
            log_error("TCID32_PowerModeChangedEvent Failed (unable to establish ON baseline)")
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onPowerModeChanged",
            "TCID32_onPowerModeChanged",
            timeout=EVENT_TIMEOUT,
        )
        if not listener.connect():
            log_error("TCID32_PowerModeChangedEvent Failed (event registration rejected)")
            return False

        log_info("Executing setPowerState(STANDBY)")
        standby_resp = send_curl_command(
            PowerManagerApis.set_power_state(
                "STANDBY", standby_reason="APIUnitTest"
            )
        )
        log_warning(f"STANDBY response: {standby_resp}")
        if not is_ok(standby_resp):
            log_error("TCID32_PowerModeChangedEvent Failed (setPowerState failed)")
            return False

        notification = listener.wait_for_event(
            lambda params: (
                params.get("currentState") == "ON"
                and params.get("newState") == "STANDBY"
            ),
            timeout=EVENT_TIMEOUT,
        )
        if notification is None:
            log_error(
                "TCID32_PowerModeChangedEvent Failed "
                "(onPowerModeChanged for ON to STANDBY was not received)"
            )
            return False
        log_success(f"Captured onPowerModeChanged event: {notification}")

        final_state = _wait_for_state("STANDBY")
        if final_state is None:
            log_error(
                "TCID32_PowerModeChangedEvent Failed "
                "(device did not reach STANDBY after the event)"
            )
            return False
        log_success(f"Validated completed power-state transition: {final_state}")

        wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"Wake time response: {wake_resp}")
        if parse_time_since_wakeup(wake_resp) != 0:
            log_error("TCID32_PowerModeChangedEvent Failed (wake time was not reset)")
            return False
        log_success("Validated secondsSinceWakeup=0 in STANDBY")
    finally:
        if listener is not None:
            listener.close()
        send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID32-event-restore"
            )
        )

    elapsed_time = time.perf_counter() - start_time
    message = "TCID32_PowerModeChangedEvent Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
