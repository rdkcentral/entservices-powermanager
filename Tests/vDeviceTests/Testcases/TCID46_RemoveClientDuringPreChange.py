"""
/**
 * @file TCID46_RemoveClientDuringPreChange.py
 * @brief Verifies client removal self-acknowledges an active transaction.
 *
 * @testcase TCID46_RemoveClientDuringPreChange
 * @details Removes the only pending pre-change client during a real active
 *          transaction, verifies the transition completes, then verifies a
 *          late acknowledgement and second removal are rejected.
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
CLIENT_NAME = "TCID46_remove_during_prechange"
TARGET_STATE = "STANDBY"


def run_test():
    start_time = time.perf_counter()
    listener = None
    client_id = None
    removed_client_id = None

    try:
        baseline = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON", standby_reason="TCID46-baseline"
            )
        )
        baseline_state = wait_for_power_state("ON")
        if (
            not is_ok(baseline)
            or not isinstance(baseline_state, dict)
            or baseline_state.get("currentState") != "ON"
        ):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(unable to establish ON baseline)"
            )
            return False

        add_response = send_curl_command(
            PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME)
        )
        client_id = extract_client_id(parse_result(add_response))
        if not is_ok(add_response) or client_id is None:
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(unable to register pre-change client)"
            )
            return False

        listener = JsonRpcEventListener(
            PowerManagerApis.CALLSIGN,
            "onPowerModePreChange",
            "TCID46_onPowerModePreChange",
            timeout=EVENT_TIMEOUT,
        )
        if not listener.connect():
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(event registration rejected)"
            )
            return False

        transition = send_curl_command(
            PowerManagerApis.set_power_state(
                TARGET_STATE,
                standby_reason="TCID46-remove-client",
            )
        )
        if not is_ok(transition):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
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
                "TCID46_RemoveClientDuringPreChange Failed "
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
                "TCID46_RemoveClientDuringPreChange Failed "
                "(unable to extend active transaction)"
            )
            return False

        removed_client_id = client_id
        remove_response = send_curl_command(
            PowerManagerApis.remove_power_mode_prechange_client(
                removed_client_id
            )
        )
        if not is_ok(remove_response):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(active client removal was rejected)"
            )
            return False
        client_id = None

        reached = wait_for_power_state(TARGET_STATE)
        if (
            not isinstance(reached, dict)
            or reached.get("currentState") != TARGET_STATE
        ):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(client removal did not complete transition)"
            )
            return False

        late_ack = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                removed_client_id, transaction_id
            )
        )
        log_warning(f"Late acknowledgement response: {late_ack}")
        if not isinstance(parse_error(late_ack), dict):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(removed client acknowledgement was not rejected)"
            )
            return False

        second_remove = send_curl_command(
            PowerManagerApis.remove_power_mode_prechange_client(
                removed_client_id
            )
        )
        log_warning(f"Second removal response: {second_remove}")
        if not isinstance(parse_error(second_remove), dict):
            log_error(
                "TCID46_RemoveClientDuringPreChange Failed "
                "(second client removal was not rejected)"
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
                "ON", standby_reason="TCID46-restore"
            )
        )

    elapsed = time.perf_counter() - start_time
    message = "TCID46_RemoveClientDuringPreChange Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed:.3f}s")
    else:
        log_success(message)
    return True
