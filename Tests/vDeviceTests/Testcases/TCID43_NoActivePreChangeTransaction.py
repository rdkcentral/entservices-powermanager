"""
/**
 * @file TCID43_NoActivePreChangeTransaction.py
 * @brief Rejects pre-change operations when no transaction is active.
 *
 * @testcase TCID43_NoActivePreChangeTransaction
 * @details Registers a valid pre-change client without starting a power-state
 *          transition, then verifies acknowledgement and delay APIs reject the
 *          request because no PreModeChangeController exists.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import (
    extract_client_id,
    parse_error,
    parse_power_state,
)
from utils import (
    is_ok,
    log_error,
    log_success,
    log_warning,
    parse_result,
    send_curl_command,
)


CLIENT_NAME = "TCID43_no_active_transaction"
TRANSACTION_ID = 1


def run_test():
    start_time = time.perf_counter()
    client_id = None
    failures = []

    try:
        add_response = send_curl_command(
            PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME)
        )
        client_id = extract_client_id(parse_result(add_response))
        if not is_ok(add_response) or client_id is None:
            log_error(
                "TCID43_NoActivePreChangeTransaction Failed "
                "(unable to register pre-change client)"
            )
            return False

        ack_response = send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(
                client_id, TRANSACTION_ID
            )
        )
        log_warning(f"No-active acknowledgement response: {ack_response}")
        if not isinstance(parse_error(ack_response), dict):
            failures.append(
                "powerModePreChangeComplete was not rejected"
            )

        delay_response = send_curl_command(
            PowerManagerApis.delay_power_mode_change_by(
                client_id, TRANSACTION_ID, 1
            )
        )
        log_warning(f"No-active delay response: {delay_response}")
        if not isinstance(parse_error(delay_response), dict):
            failures.append("delayPowerModeChangeBy was not rejected")

        health = parse_power_state(
            send_curl_command(PowerManagerApis.get_power_state)
        )
        if not isinstance(health, dict):
            failures.append("plugin became unresponsive")
    finally:
        if client_id is not None:
            send_curl_command(
                PowerManagerApis.remove_power_mode_prechange_client(
                    client_id
                )
            )

    if failures:
        log_error(
            "TCID43_NoActivePreChangeTransaction Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed = time.perf_counter() - start_time
    message = "TCID43_NoActivePreChangeTransaction Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed:.3f}s")
    else:
        log_success(message)
    return True
