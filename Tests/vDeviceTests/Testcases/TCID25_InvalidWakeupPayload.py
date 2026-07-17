"""
/**
 * @file TCID25_InvalidWakeupPayload.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID25_InvalidWakeupPayload
 * @details Validates that an invalid wakeup-source payload is rejected and does
 *          not mutate the existing configuration.
 */
"""

import os
import time

from utils import send_curl_command, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_error, parse_wakeup_config, wakeup_map


def run_test():
    start_time = time.perf_counter()

    before_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Baseline config response: {before_resp}")
    before_config = parse_wakeup_config(before_resp)
    if not isinstance(before_config, list):
        log_error("TCID25_InvalidWakeupPayload Failed ❌ (unable to read baseline config)")
        return False

    invalid_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "UNKNOWN", "enabled": True},
            {"wakeupSource": "IR", "enabled": False},
        ])
    )
    log_warning(f"Invalid set response: {invalid_resp}")
    if not isinstance(parse_error(invalid_resp), dict):
        log_error("TCID25_InvalidWakeupPayload Failed ❌ (invalid payload was not rejected)")
        return False

    after_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Post-invalid config response: {after_resp}")
    after_config = parse_wakeup_config(after_resp)
    if not isinstance(after_config, list) or wakeup_map(before_config) != wakeup_map(after_config):
        log_error("TCID25_InvalidWakeupPayload Failed ❌ (config changed after invalid payload)")
        return False

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID25_InvalidWakeupPayload Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True