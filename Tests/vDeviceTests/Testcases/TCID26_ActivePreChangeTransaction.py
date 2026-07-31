"""
/**
 * @file TCID26_ActivePreChangeTransaction.py
 * @brief L3 PowerManager functional testcase for the in-progress power-mode change path.
 *
 * @testcase TCID26_ActivePreChangeTransaction
 * @details Registers a pre-change client and then triggers a power-state
 *          transition. Because a registered client has not acknowledged, the
 *          PreModeChangeController stays active, allowing this test to exercise
 *          the "transaction in progress" branches that TCID23/TCID26 cannot:
 *            - DelayPowerModeChangeBy  -> _modeChangeController->Reschedule(...)
 *            - PowerModePreChangeComplete -> _modeChangeController->Ack(...)
 *          plus the pre-change notification dispatch with a registered client.
 *
 * @precondition
 *  - org.rdk.PowerManager plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py
 *  - PowerManager_Curl.py
 *  - SuiteManager.py
 *
 * @expected_result
 *  - Client registration succeeds, the transition is accepted, the delay/complete
 *    calls are handled while a transaction is active, and the plugin remains
 *    responsive afterwards.
 *
 * @pass_criteria
 *  - Registration and the post-transition health check succeed; the plugin does
 *    not become unresponsive.
 *
 * @failure_criteria
 *  - Registration fails, the plugin stops responding, JSON parse error, or
 *    run_test() returns False.
 *
 * @note The transactionId assigned internally is delivered via the async
 *       onPowerModePreChange event, which this curl-only framework does not
 *       capture. The delay/complete calls therefore use a best-effort
 *       transactionId; they still execute the in-progress controller branches.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


CLIENT_NAME = "l3_active_txn_client"
BEST_EFFORT_TRANSACTION_ID = 0
DELAY_PERIOD = 1


def _extract_client_id(result):
    if isinstance(result, dict):
        cid = result.get("clientId")
        if isinstance(cid, int) and not isinstance(cid, bool):
            return cid
    if isinstance(result, int) and not isinstance(result, bool):
        return result
    return None


def run_test():
    start_time = time.perf_counter()

    # Start from a known state so the following transition actually changes state.
    send_curl_command(PowerManagerApis.set_power_state(PowerManagerApis.POWER_STATE_ON, standby_reason="TCID28-setup"))
    time.sleep(2)

    # Register a pre-change client. With an outstanding (un-acked) client, the next
    # setPowerState keeps the PreModeChangeController active until timeout.
    log_info("Executing addPowerModePreChangeClient")
    resp = send_curl_command(PowerManagerApis.add_power_mode_prechange_client(CLIENT_NAME))
    log_warning(f"Response: {resp}")
    if not is_ok(resp):
        log_error("TCID26_ActivePreChangeTransaction Failed ❌ (add client did not succeed)")
        return False
    client_id = _extract_client_id(parse_result(resp))
    if client_id is None:
        log_error("TCID26_ActivePreChangeTransaction Failed ❌ (no clientId returned)")
        return False
    log_success(f"✅ Registered client id={client_id}")

    try:
        # Trigger an async transition; the controller now waits for our client's ack.
        log_info("Triggering setPowerState(LIGHT_SLEEP) to start an in-progress transaction")
        send_curl_command(
            PowerManagerApis.set_power_state(
                PowerManagerApis.POWER_STATE_STANDBY_LIGHT_SLEEP,
                standby_reason="TCID28-active-txn",
            )
        )

        # While the transaction is active, exercise the in-progress controller branches.
        log_info("Executing delayPowerModeChangeBy during active transaction")
        send_curl_command(
            PowerManagerApis.delay_power_mode_change_by(client_id, BEST_EFFORT_TRANSACTION_ID, DELAY_PERIOD)
        )

        log_info("Executing powerModePreChangeComplete during active transaction")
        send_curl_command(
            PowerManagerApis.power_mode_prechange_complete(client_id, BEST_EFFORT_TRANSACTION_ID)
        )

        # Allow the completion handler to run (ack-driven or timeout-driven).
        time.sleep(2)

        # Health check: the plugin must remain responsive.
        log_info("Verifying plugin is still responsive via getPowerState")
        health = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Response: {health}")
        if not is_ok(health):
            log_error("TCID26_ActivePreChangeTransaction Failed ❌ (plugin unresponsive after active transaction)")
            return False
        log_success("✅ Plugin responsive after in-progress transaction handling")
    finally:
        # Cleanup: drop the client and restore a powered-on state (best effort).
        send_curl_command(PowerManagerApis.remove_power_mode_prechange_client(client_id))
        send_curl_command(
            PowerManagerApis.set_power_state(PowerManagerApis.POWER_STATE_ON, standby_reason="TCID28-restore")
        )

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID26_ActivePreChangeTransaction Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
