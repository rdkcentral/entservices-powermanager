"""
/**
 * @file TCID21_TimerWakeDisable.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID21_TimerWakeDisable
 * @details Disables TIMER in wakeup-source configuration and validates the
 *          read-back configuration.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import get_source_enabled, note_partial, parse_wakeup_config, wakeup_map


def run_test():
    start_time = time.perf_counter()

    original_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Original config response: {original_resp}")
    original_config = parse_wakeup_config(original_resp)
    if not isinstance(original_config, list):
        log_error("TCID21_TimerWakeDisable Failed ❌ (unable to read baseline config)")
        return False

    set_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "TIMER", "enabled": False}
        ])
    )
    log_warning(f"Set response: {set_resp}")
    if not is_ok(set_resp):
        log_error("TCID21_TimerWakeDisable Failed ❌ (failed to disable TIMER)")
        return False

    read_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Read-back config response: {read_resp}")
    read_config = parse_wakeup_config(read_resp)
    if not isinstance(read_config, list) or get_source_enabled(read_config, "TIMER") is not False:
        log_error("TCID21_TimerWakeDisable Failed ❌ (TIMER not disabled in read-back)")
        return False

    note_partial("Non-TIMER wake validation remains on hold pending external wake stimulus and TIMER-fix availability.")

    restore_entries = [
        {"wakeupSource": source, "enabled": enabled}
        for source, enabled in wakeup_map(original_config).items()
    ]
    send_curl_command(PowerManagerApis.set_wakeup_source_config(restore_entries))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID21_TimerWakeDisable Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True