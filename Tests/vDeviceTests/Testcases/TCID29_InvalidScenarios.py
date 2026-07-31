"""
/**
 * @file TCID29_InvalidScenarios.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID29_InvalidScenarios
 * @details Validates the negative-service and invalid-payload scenarios by
 *          combining the rejected wakeup-source payload checks with the
 *          service-inactive behavior observed after plugin deactivation.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error, parse_power_state, parse_wakeup_config, wakeup_map


DEACTIVATE_SETTLE_SECONDS = int(os.environ.get("POWERMANAGER_DEACTIVATE_SETTLE_SECONDS", "20"))


def _reactivate_plugin(max_attempts=6, sleep_seconds=2):
    last_response = ""
    for attempt in range(1, max_attempts + 1):
        activate_resp = send_curl_command(PowerManagerApis.controller_activate(timeout=12))
        last_response = activate_resp
        log_warning(f"Activate attempt {attempt} response: {activate_resp}")

        if is_ok(activate_resp):
            time.sleep(6)
            health_resp = send_curl_command(PowerManagerApis.get_power_state)
            log_warning(f"Post-activate health response: {health_resp}")
            if isinstance(parse_power_state(health_resp), dict):
                return True, activate_resp

        error = parse_error(activate_resp)
        if isinstance(error, dict) and error.get("code") == 5:
            time.sleep(sleep_seconds)
            continue

        if activate_resp.startswith("< No response"):
            time.sleep(sleep_seconds)
            continue

        break

    return False, last_response


def _run_invalid_payload_subflow():
    before_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Baseline config response: {before_resp}")
    before_config = parse_wakeup_config(before_resp)
    if not isinstance(before_config, list):
        return False, "unable to read baseline config before invalid payload"

    invalid_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "UNKNOWN", "enabled": True},
            {"wakeupSource": "IR", "enabled": False},
        ])
    )
    log_warning(f"Invalid set response: {invalid_resp}")
    if not isinstance(parse_error(invalid_resp), dict):
        return False, "invalid payload was not rejected"

    after_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Post-invalid config response: {after_resp}")
    after_config = parse_wakeup_config(after_resp)
    if not isinstance(after_config, list) or wakeup_map(before_config) != wakeup_map(after_config):
        return False, "config changed after invalid payload"

    return True, None


def _run_inactive_service_subflow():
    observed_inactive_error = False

    try:
        deactivate_resp = send_curl_command(PowerManagerApis.controller_deactivate())
        log_warning(f"Deactivate response: {deactivate_resp}")

        if not is_ok(deactivate_resp) and not deactivate_resp.startswith("< No response"):
            return False, "plugin deactivation failed"

        log_warning(f"Waiting {DEACTIVATE_SETTLE_SECONDS}s for plugin deactivation to settle")
        time.sleep(DEACTIVATE_SETTLE_SECONDS)

        for label, command in (
            ("getTimeSinceWakeup", PowerManagerApis.get_time_since_wakeup),
            ("getPowerState", PowerManagerApis.get_power_state),
            ("getLastWakeupReason", PowerManagerApis.get_last_wakeup_reason),
        ):
            response = send_curl_command(command)
            log_warning(f"{label} inactive response: {response}")
            error = parse_error(response)
            if isinstance(error, dict) and error.get("message") == "Service is not active":
                observed_inactive_error = True
                continue
            return False, f"{label} inactive response did not return service inactive"
    finally:
        reactivated, _ = _reactivate_plugin()
        if not reactivated:
            return False, "plugin reactivation failed"

    if not observed_inactive_error:
        return False, "plugin never entered inactive state"

    return True, None


def run_test():
    start_time = time.perf_counter()

    ok, failure = _run_invalid_payload_subflow()
    if not ok:
        log_error(f"TCID29_InvalidScenarios Failed ❌ ({failure})")
        return False

    ok, failure = _run_inactive_service_subflow()
    if not ok:
        log_error(f"TCID29_InvalidScenarios Failed ❌ ({failure})")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID29_InvalidScenarios Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True