"""
/**
 * @file TCID21_DeepSleepTimerClamp.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID21_DeepSleepTimerClamp
 * @details Validates acceptance of an oversized deep-sleep timer value and logs
 *          the current partial limitation around backend-dependent runtime behavior.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import note_partial


def run_test():
    start_time = time.perf_counter()

    large_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(90000))
    log_warning(f"Oversized timer response: {large_resp}")
    if not is_ok(large_resp):
        log_error("TCID21_DeepSleepTimerClamp Failed ❌ (oversized timer request failed)")
        return False

    followup_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
    log_warning(f"Follow-up timer response: {followup_resp}")
    if not is_ok(followup_resp):
        log_error("TCID21_DeepSleepTimerClamp Failed ❌ (follow-up timer request failed)")
        return False

    if not note_partial("The >86400 clamp exists in code, but runtime behavior after normalization is backend-dependent and is not asserted here."):
        log_error("TCID21_DeepSleepTimerClamp Failed ❌ (strict partial mode enabled)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID21_DeepSleepTimerClamp Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True