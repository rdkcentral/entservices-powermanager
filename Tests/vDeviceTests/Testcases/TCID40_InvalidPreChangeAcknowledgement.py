"""
/**
 * @file TCID40_InvalidPreChangeAcknowledgement.py
 * @brief Rejects mismatched power-mode pre-change acknowledgements.
 *
 * @testcase TCID40_InvalidPreChangeAcknowledgement
 * @details Captures a real onPowerModePreChange transaction, verifies stale
 *          transaction and unknown client acknowledgements are rejected, then
 *          completes the transition using the valid identifiers.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error, parse_power_state
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    parse_result,
    send_curl_command,
)


EVENT_TIMEOUT = float(os.environ.get("POWERMANAGER_EVENT_TIMEOUT", "8"))
CLIENT_NAME = "TCID40_invalid_ack_client"
TARGET_STATE = "LIGHT_SLEEP"


def _extract_client_id(result):
    if isinstance(result, dict):
        value = result.get("clientId")
    else:
        value = result
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return None


def _extract_transaction_id(params):
    for key in ("transactionId", "trxnId"):
        value = params.get(key)
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def _wait_for_state(expected, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = parse_power_state(
            send_curl_command(PowerManagerApis.get_power_state)
        )
        if isinstance(state, dict) and state.get("currentState") == expected:
            return True
        time.sleep(0.25)
    return False


def run_test():
    start_time = time.perf_counter()
    listener = None
    client_id = None

    try:
        baseline = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID40-baseline"
            )
        )
        if not is_ok(baseline):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(unable to establish ON baseline)"
            )
            return False
        if not _wait_for_state("ON"):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(ON baseline transition did not complete)"
            )
            return False

        add_response = send_curl_command(
            PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME)
        )
        client_id = _extract_client_id(parse_result(add_response))
        if not is_ok(add_response) or client_id is None:
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(unable to register pre-change client)"
            )
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onPowerModePreChange",
            "TCID40_onPowerModePreChange",
            timeout=EVENT_TIMEOUT,
        )
        if not listener.connect():
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(event registration rejected)"
            )
            return False

        transition = send_curl_command(
            PowerManagerApis.set_power_state(
                TARGET_STATE,
                standby_reason="TCID40-negative-ack",
            )
        )
        if not is_ok(transition):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(transition request was rejected)"
            )
            return False

        event = listener.wait_for_event(
            lambda params: params.get("newState") == TARGET_STATE,
            timeout=EVENT_TIMEOUT,
        )
        log_warning(f"Pre-change event: {event}")
        if not isinstance(event, dict):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(pre-change event was not received)"
            )
            return False

        transaction_id = _extract_transaction_id(event)
        if transaction_id is None:
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(event did not contain a transaction ID)"
            )
            return False

        keep_active = send_curl_command(
            PowerManagerApis.delay_power_mode_change_by(
                client_id, transaction_id, 10
            )
        )
        if not is_ok(keep_active):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(unable to extend active transaction)"
            )
            return False

        stale_response = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, transaction_id + 1000000
            )
        )
        log_warning(f"Stale transaction response: {stale_response}")
        if not isinstance(parse_error(stale_response), dict):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(stale transaction ID was not rejected)"
            )
            return False

        unknown_client_response = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id + 1000000, transaction_id
            )
        )
        log_warning(
            f"Unknown client response: {unknown_client_response}"
        )
        if not isinstance(parse_error(unknown_client_response), dict):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(unknown client ID was not rejected)"
            )
            return False

        valid_response = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, transaction_id
            )
        )
        if not is_ok(valid_response):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(valid acknowledgement was rejected)"
            )
            return False

        if not _wait_for_state(TARGET_STATE):
            log_error(
                "TCID40_InvalidPreChangeAcknowledgement Failed "
                "(valid acknowledgement did not complete transition)"
            )
            return False
    finally:
        if listener is not None:
            listener.close()
        if client_id is not None:
            send_curl_command(
                PowerManagerApis.remove_power_mode_prechange_client(client_id)
            )
        send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID40-restore"
            )
        )

    elapsed_time = time.perf_counter() - start_time
    message = "TCID40_InvalidPreChangeAcknowledgement Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
