"""
/**
 * @file TCID34_NetworkStandbyModeChangedEvent.py
 * @brief Validates onNetworkStandbyModeChanged delivery over a WebSocket.
 *
 * @testcase TCID34_NetworkStandbyModeChangedEvent
 * @details Registers for onNetworkStandbyModeChanged, disables network standby,
 *          and validates that the event contains enabled=false.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_network_standby
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


def _set_network_standby(enabled):
    response = send_curl_command(
        PowerManagerApis.set_network_standby_mode(enabled)
    )
    if is_ok(response):
        return response
    return send_curl_command(
        PowerManagerApis.set_network_standby_mode_nw(enabled)
    )


def run_test():
    start_time = time.perf_counter()
    listener = None

    original_mode_resp = send_curl_command(
        PowerManagerApis.get_network_standby_mode
    )
    original_mode = parse_network_standby(original_mode_resp)
    if not isinstance(original_mode, bool):
        log_error(
            "TCID34_NetworkStandbyModeChangedEvent Failed "
            "(unable to read network standby mode)"
        )
        return False

    try:
        baseline_resp = _set_network_standby(True)
        if not is_ok(baseline_resp):
            log_error(
                "TCID34_NetworkStandbyModeChangedEvent Failed "
                "(unable to establish enabled baseline)"
            )
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onNetworkStandbyModeChanged",
            "TCID34_onNetworkStandbyModeChanged",
            timeout=EVENT_TIMEOUT,
        )
        if not listener.connect():
            log_error(
                "TCID34_NetworkStandbyModeChangedEvent Failed "
                "(event registration rejected)"
            )
            return False

        log_info("Executing setNetworkStandbyMode(false)")
        disable_resp = _set_network_standby(False)
        log_warning(f"Disable response: {disable_resp}")
        if not is_ok(disable_resp):
            log_error(
                "TCID34_NetworkStandbyModeChangedEvent Failed "
                "(setNetworkStandbyMode failed)"
            )
            return False

        notification = listener.wait_for_event(
            lambda params: params.get("enabled") is False,
            timeout=EVENT_TIMEOUT,
        )
        if notification is None:
            log_error(
                "TCID34_NetworkStandbyModeChangedEvent Failed "
                "(onNetworkStandbyModeChanged with enabled=false was not received)"
            )
            return False
        log_success(f"Captured onNetworkStandbyModeChanged event: {notification}")

        current_mode_resp = send_curl_command(
            PowerManagerApis.get_network_standby_mode
        )
        if parse_network_standby(current_mode_resp) is not False:
            log_error(
                "TCID34_NetworkStandbyModeChangedEvent Failed "
                "(network standby did not remain disabled)"
            )
            return False
    finally:
        if listener is not None:
            listener.close()
        _set_network_standby(original_mode)

    elapsed_time = time.perf_counter() - start_time
    message = "TCID34_NetworkStandbyModeChangedEvent Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
