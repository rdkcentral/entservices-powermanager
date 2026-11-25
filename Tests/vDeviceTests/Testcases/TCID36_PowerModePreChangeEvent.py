"""
/**
 * @file TCID36_PowerModePreChangeEvent.py
 * @brief Validates onPowerModePreChange delivery over a persistent WebSocket.
 *
 * @testcase TCID36_PowerModePreChangeEvent
 * @details Registers a pre-change client and an onPowerModePreChange listener,
 *          requests an ON to LIGHT_SLEEP transition, validates the complete
 *          event payload, acknowledges its real transaction ID, and verifies
 *          that the transition completes.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_power_state
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
CLIENT_NAME = "TCID36_prechange_event_client"
CURRENT_STATE = "ON"
TARGET_STATE = "LIGHT_SLEEP"
EXPECTED_STATE_CHANGE_AFTER = 1


def _extract_client_id(result):
    value = result.get("clientId") if isinstance(result, dict) else result
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return None


def _extract_transaction_id(params):
    for key in ("transactionId", "trxnId"):
        value = params.get(key)
        if isinstance(value, int) and not isinstance(value, bool) and value > 0:
            return value
    return None


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
    client_id = None

    try:
        log_info(f"Establishing {CURRENT_STATE} power-state baseline")
        baseline_response = send_curl_command(
            PowerManagerApis.set_power_state(
                CURRENT_STATE, standby_reason="TCID36-baseline"
            )
        )
        log_warning(f"Baseline response: {baseline_response}")
        if not is_ok(baseline_response) or _wait_for_state(CURRENT_STATE) is None:
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(unable to establish ON baseline)"
            )
            return False

        log_info("Registering PowerManager pre-change client")
        client_response = send_curl_command(
            PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME)
        )
        log_warning(f"Pre-change client response: {client_response}")
        client_id = _extract_client_id(parse_result(client_response))
        if not is_ok(client_response) or client_id is None:
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(unable to register pre-change client)"
            )
            return False
        log_success(f"Registered pre-change client with clientId={client_id}")

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onPowerModePreChange",
            "TCID36_onPowerModePreChange",
            timeout=EVENT_TIMEOUT,
        )
        log_info("Registering onPowerModePreChange over WebSocket")
        if not listener.connect():
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(event registration rejected)"
            )
            return False
        log_success("Registered onPowerModePreChange event listener")

        log_info(f"Executing setPowerState({TARGET_STATE})")
        transition_response = send_curl_command(
            PowerManagerApis.set_power_state(
                TARGET_STATE, standby_reason="TCID36-prechange-event"
            )
        )
        log_warning(f"Transition response: {transition_response}")
        if not is_ok(transition_response):
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(transition request was rejected)"
            )
            return False

        log_info(f"Waiting up to {EVENT_TIMEOUT:.0f}s for onPowerModePreChange")
        event = listener.wait_for_event(
            lambda params: (
                params.get("currentState") == CURRENT_STATE
                and params.get("newState") == TARGET_STATE
            ),
            timeout=EVENT_TIMEOUT,
        )
        log_warning(f"onPowerModePreChange event: {event}")
        if not isinstance(event, dict):
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(expected pre-change event was not received)"
            )
            return False

        transaction_id = _extract_transaction_id(event)
        state_change_after = event.get("stateChangeAfter")
        if transaction_id is None:
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(event did not contain a valid transactionId)"
            )
            return False
        if state_change_after != EXPECTED_STATE_CHANGE_AFTER:
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                f"(expected stateChangeAfter={EXPECTED_STATE_CHANGE_AFTER}, "
                f"got {state_change_after})"
            )
            return False
        log_success(
            "Validated onPowerModePreChange payload: "
            f"currentState={CURRENT_STATE}, newState={TARGET_STATE}, "
            f"transactionId={transaction_id}, "
            f"stateChangeAfter={state_change_after}"
        )

        log_info(
            "Acknowledging power-mode pre-change transaction "
            f"{transaction_id}"
        )
        completion_response = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, transaction_id
            )
        )
        log_warning(f"Pre-change completion response: {completion_response}")
        if not is_ok(completion_response):
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(valid pre-change acknowledgement was rejected)"
            )
            return False

        final_state = _wait_for_state(TARGET_STATE)
        if final_state is None:
            log_error(
                "TCID36_PowerModePreChangeEvent Failed "
                "(power-state transition did not complete)"
            )
            return False
        log_success(f"Validated completed power-state transition: {final_state}")
    finally:
        if listener is not None:
            listener.close()
        if client_id is not None:
            send_curl_command(
                PowerManagerApis.remove_power_mode_prechange_client(client_id)
            )
        send_curl_command(
            PowerManagerApis.set_power_state(
                CURRENT_STATE, standby_reason="TCID36-restore"
            )
        )

    elapsed_time = time.perf_counter() - start_time
    message = "TCID36_PowerModePreChangeEvent Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
