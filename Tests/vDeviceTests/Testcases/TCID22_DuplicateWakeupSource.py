"""
/**
 * @file TCID22_DuplicateWakeupSource.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID22_DuplicateWakeupSource
 * @details Validates deterministic handling of duplicate wakeup-source entries
 *          within a single setWakeupSourceConfig payload.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, parse_wakeup_config, wakeup_map


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID22_DuplicateWakeupSource Failed ❌ (unable to read baseline config)")
        return False

    set_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "IR", "enabled": True},
            {"wakeupSource": "IR", "enabled": False},
        ])
    )
    log_warning(f"Set response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID22_DuplicateWakeupSource Failed ❌ (duplicate payload request failed)")
        return False

    read_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Read-back config response: {read_resp}")
    read_config = parse_wakeup_config(read_resp)
    if not isinstance(read_config, list) or get_source_enabled(read_config, "IR") is not False:
        log_error("TCID22_DuplicateWakeupSource Failed ❌ (IR did not resolve to the last payload value)")
        return False

    restore_entries = [
        {"wakeupSource": source, "enabled": enabled}
        for source, enabled in wakeup_map(original_config).items()
    ]
    send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID22_DuplicateWakeupSource Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True