"""
/**
 * @file TCID42_InvalidPowerStateHandling.py
 * @brief Rejects invalid power-state values without changing device state.
 *
 * @testcase TCID42_InvalidPowerStateHandling
 * @details Sends unsupported, empty, null, and incorrectly typed powerState
 *          values. Every request must return a JSON-RPC error, preserve the
 *          current power state, and leave the plugin responsive.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error, parse_power_state
from utils import (
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
)


INVALID_STATES = (
    ("unknown operational state", "UNKNOWN"),
    ("unsupported name", "INVALID_STATE"),
    ("incorrect case", "deep_sleep"),
    ("empty state", ""),
    ("null state", None),
    ("numeric state", 999),
)


def run_test():
    start_time = time.perf_counter()
    baseline_response = send_curl_command(PowerManagerApis.get_power_state)
    baseline = parse_power_state(baseline_response)
    if not isinstance(baseline, dict) or not baseline.get("currentState"):
        log_error(
            "TCID42_InvalidPowerStateHandling Failed "
            "(unable to read baseline power state)"
        )
        return False

    baseline_state = baseline["currentState"]
    failures = []

    try:
        for label, state in INVALID_STATES:
            log_info(f"Submitting {label}: powerState={state!r}")
            response = send_curl_command(
                PowerManagerApis.set_power_state(
                    state,
                    standby_reason="TCID42-negative",
                )
            )
            log_warning(f"Response: {response}")
            if not isinstance(parse_error(response), dict):
                failures.append(f"{label} was not rejected")

            current = parse_power_state(
                send_curl_command(PowerManagerApis.get_power_state)
            )
            if (
                not isinstance(current, dict)
                or current.get("currentState") != baseline_state
            ):
                failures.append(
                    f"{label} changed state from {baseline_state} to {current}"
                )

        health = send_curl_command(PowerManagerApis.get_power_state)
        if not is_ok(health):
            failures.append("plugin became unresponsive")
    finally:
        send_curl_command(
            PowerManagerApis.set_power_state(
                baseline_state,
                standby_reason="TCID42-restore",
            )
        )

    if failures:
        log_error(
            "TCID42_InvalidPowerStateHandling Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed_time = time.perf_counter() - start_time
    message = "TCID42_InvalidPowerStateHandling Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
