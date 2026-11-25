"""
/**
 * @file TCID38_NegativeOvertempGraceInterval.py
 * @brief Rejects invalid over-temperature grace intervals atomically.
 *
 * @testcase TCID38_NegativeOvertempGraceInterval
 * @details Verifies invalid intervals return JSON-RPC errors and preserve the
 *          previously configured over-temperature grace interval.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error
from utils import (
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    parse_result,
    send_curl_command,
)


INVALID_INTERVALS = (
    ("negative interval", -1),
    ("minimum signed integer", -2147483648),
    ("null interval", None),
    ("string interval", "120"),
    ("fractional interval", 1.5),
)


def _read_interval():
    value = parse_result(
        send_curl_command(PowerManagerApis.get_overtemp_grace_interval)
    )
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def run_test():
    start_time = time.perf_counter()
    original = _read_interval()
    if original is None:
        log_error(
            "TCID38_NegativeOvertempGraceInterval Failed "
            "(unable to read baseline interval)"
        )
        return False

    failures = []
    try:
        for label, interval in INVALID_INTERVALS:
            log_info(f"Submitting {label}: graceInterval={interval!r}")
            response = send_curl_command(
                PowerManagerApis.set_overtemp_grace_interval(interval)
            )
            log_warning(f"Response: {response}")
            if not isinstance(parse_error(response), dict):
                failures.append(f"{label} was not rejected")

            current = _read_interval()
            if current != original:
                failures.append(
                    f"{label} changed interval from {original} to {current}"
                )

        if not is_ok(send_curl_command(PowerManagerApis.get_power_state)):
            failures.append("plugin became unresponsive")
    finally:
        send_curl_command(
            PowerManagerApis.set_overtemp_grace_interval(original)
        )

    if failures:
        log_error(
            "TCID38_NegativeOvertempGraceInterval Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed_time = time.perf_counter() - start_time
    message = "TCID38_NegativeOvertempGraceInterval Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
