"""
/**
 * @file TCID45_DuplicatePreChangeAcknowledgement.py
 * @brief Verifies a completed transaction cannot be acknowledged twice.
 *
 * @testcase TCID45_DuplicatePreChangeAcknowledgement
 * @details Captures a real pre-change transaction, completes it with a valid
 *          acknowledgement, then verifies an identical second acknowledgement
 *          is rejected and does not trigger another state transition.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import (
    extract_client_id,
    extract_transaction_id,
    parse_error,
    wait_for_power_state,
)
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_success,
    log_warning,
    parse_result,
    send_curl_command,
)


EVENT_TIMEOUT = float(os.environ.get("POWERMANAGER_EVENT_TIMEOUT", "8"))
CLIENT_NAME = "TCID45_duplicate_ack"
TARGET_STATE = "LIGHT_SLEEP"


def run_test():
    start_time = time.perf_counter()
    listener = None
    client_id = None

    try:
        baseline = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID45-baseline"
            )
        )
        baseline_state = wait_for_power_state("ON")
        if (
            not is_ok(baseline)
            or not isinstance(baseline_state, dict)
            or baseline_state.get("currentState") != "ON"
        ):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(unable to establish ON baseline)"
            )
            return False

        add_response = send_curl_command(
            PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME)
        )
        client_id = extract_client_id(parse_result(add_response))
        if not is_ok(add_response) or client_id is None:
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(unable to register pre-change client)"
            )
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onPowerModePreChange",
            "TCID45_onPowerModePreChange",
            timeout=EVENT_TIMEOUT,
        )
        if not listener.connect():
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(event registration rejected)"
            )
            return False

        transition = send_curl_command(
            PowerManagerApis.set_power_state(
                TARGET_STATE,
                standby_reason="TCID45-duplicate-ack",
            )
        )
        if not is_ok(transition):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(transition request was rejected)"
            )
            return False

        event = listener.wait_for_event(
            lambda params: params.get("newState") == TARGET_STATE,
            timeout=EVENT_TIMEOUT,
        )
        log_warning(f"Pre-change event: {event}")
        transaction_id = extract_transaction_id(event)
        if transaction_id is None:
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(event did not contain a transaction ID)"
            )
            return False

        delay_response = send_curl_command(
            PowerManagerApis.delay_power_mode_change_by(
                client_id, transaction_id, 10
            )
        )
        if not is_ok(delay_response):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(unable to extend active transaction)"
            )
            return False

        first_ack = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, transaction_id
            )
        )
        if not is_ok(first_ack):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(valid acknowledgement was rejected)"
            )
            return False

        reached = wait_for_power_state(TARGET_STATE)
        if (
            not isinstance(reached, dict)
            or reached.get("currentState") != TARGET_STATE
        ):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(valid acknowledgement did not complete transition)"
            )
            return False

        duplicate_ack = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, transaction_id
            )
        )
        log_warning(f"Duplicate acknowledgement response: {duplicate_ack}")
        if not isinstance(parse_error(duplicate_ack), dict):
            log_error(
                "TCID45_DuplicatePreChangeAcknowledgement Failed "
                "(duplicate acknowledgement was not rejected)"
            )
            return False
    finally:
        if listener is not None:
            listener.close()
        if client_id is not None:
            send_curl_command(
                PowerManagerApis.remove_power_mode_prechange_client(
                    client_id
                )
            )
        send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID45-restore"
            )
        )

    elapsed = time.perf_counter() - start_time
    message = "TCID45_DuplicatePreChangeAcknowledgement Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed:.3f}s")
    else:
        log_success(message)
    return True
