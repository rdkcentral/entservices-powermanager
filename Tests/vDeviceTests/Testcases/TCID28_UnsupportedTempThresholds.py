"""
/**
 * @file TCID28_UnsupportedTempThresholds.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID28_UnsupportedTempThresholds
 * @details Validates stable ERROR_GENERAL behavior across unsupported
 *          temperature-threshold getter and setter calls.
 */
"""

import os
import time

from utils import send_curl_command, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error


def run_test():
    start_time = time.perf_counter()

    first_resp = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    log_warning(f"Initial get response: {first_resp}")
    first_error = parse_error(first_resp)
    if not isinstance(first_error, dict) or first_error.get("message") != "ERROR_GENERAL":
        log_error("TCID28_UnsupportedTempThresholds Failed ❌ (unexpected initial get response)")
        return False

    set_resp = send_curl_command(PowerManagerApis.set_temperature_thresholds(100.0, 110.0))
    log_warning(f"Set response: {set_resp}")
    set_error = parse_error(set_resp)
    if not isinstance(set_error, dict) or set_error.get("message") != "ERROR_GENERAL":
        log_error("TCID28_UnsupportedTempThresholds Failed ❌ (unexpected set response)")
        return False

    second_resp = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    log_warning(f"Second get response: {second_resp}")
    second_error = parse_error(second_resp)
    if not isinstance(second_error, dict) or second_error.get("message") != "ERROR_GENERAL":
        log_error("TCID28_UnsupportedTempThresholds Failed ❌ (unexpected second get response)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID28_UnsupportedTempThresholds Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True