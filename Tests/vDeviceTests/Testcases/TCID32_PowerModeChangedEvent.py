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
from PowerManager_CombinationHelpers import parse_time_since_wakeup
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


def run_test():
    start_time = time.perf_counter()
    listener = None

    try:
        baseline_resp = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID32-event-baseline"
            )
        )
        if not is_ok(baseline_resp):
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

        wake_resp = send_curl_command(PowerManagerApis.get_time_since_wakeup)
        log_warning(f"Wake time response: {wake_resp}")
        if parse_time_since_wakeup(wake_resp) != 0:
            log_error("TCID32_PowerModeChangedEvent Failed (wake time was not reset)")
            return False

        notification = listener.wait_for_event(
            lambda params: params.get("newState") == "STANDBY",
            timeout=EVENT_TIMEOUT,
        )
        if notification is None:
            log_error(
                "TCID32_PowerModeChangedEvent Failed "
                "(onPowerModeChanged with newState=STANDBY was not received)"
            )
            return False
        log_success(f"Captured onPowerModeChanged event: {notification}")
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
