"""
/**
 * @file TCID39_InvalidTemperatureThresholds.py
 * @brief Rejects invalid thermal threshold relationships atomically.
 *
 * @testcase TCID39_InvalidTemperatureThresholds
 * @details Verifies reversed, equal, and negative thermal thresholds are
 *          rejected without changing the existing platform thresholds.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error
from utils import (
    SKIPPED,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    parse_result,
    send_curl_command,
)


INVALID_THRESHOLDS = (
    ("high exceeds critical", 110.0, 100.0),
    ("high equals critical", 100.0, 100.0),
    ("negative high", -1.0, 110.0),
    ("negative critical", 100.0, -1.0),
)


def _read_thresholds():
    response = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    if not is_ok(response):
        return None
    result = parse_result(response)
    if not isinstance(result, dict):
        return None
    high = result.get("high")
    critical = result.get("critical")
    if (
        isinstance(high, bool)
        or isinstance(critical, bool)
        or not isinstance(high, (int, float))
        or not isinstance(critical, (int, float))
    ):
        return None
    return float(high), float(critical)


def run_test():
    start_time = time.perf_counter()
    original = _read_thresholds()
    if original is None:
        log_warning(
            "TCID39_InvalidTemperatureThresholds Skipped "
            "(thermal threshold API is unavailable)"
        )
        return SKIPPED

    failures = []
    try:
        for label, high, critical in INVALID_THRESHOLDS:
            log_info(
                f"Submitting {label}: high={high}, critical={critical}"
            )
            response = send_curl_command(
                PowerManagerApis.set_temperature_thresholds(high, critical)
            )
            log_warning(f"Response: {response}")
            if not isinstance(parse_error(response), dict):
                failures.append(f"{label} was not rejected")

            current = _read_thresholds()
            if current != original:
                failures.append(
                    f"{label} changed thresholds from {original} to {current}"
                )
    finally:
        send_curl_command(
            PowerManagerApis.set_temperature_thresholds(
                original[0], original[1]
            )
        )

    if failures:
        log_error(
            "TCID39_InvalidTemperatureThresholds Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed_time = time.perf_counter() - start_time
    message = "TCID39_InvalidTemperatureThresholds Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
