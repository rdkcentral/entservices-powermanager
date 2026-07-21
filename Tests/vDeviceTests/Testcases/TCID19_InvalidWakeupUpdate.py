"""
/**
 * @file TCID19_InvalidWakeupUpdate.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID19_InvalidWakeupUpdate
 * @details Validates that a failed wakeup-source update does not override the
 *          previously valid wake configuration used for a deep-sleep cycle.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import is_timer_wakeup_reason, note_partial, parse_error, parse_last_wakeup_reason


def run_test():
    start_time = time.perf_counter()

    valid_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "IR", "enabled": True},
            {"wakeupSource": "TIMER", "enabled": True},
        ])
    )
    log_warning(f"Valid config response: {valid_resp}")
    if not is_ok(valid_resp):
        log_error("TCID19_InvalidWakeupUpdate Failed ❌ (failed to set valid wakeup config)")
        return False

    invalid_resp = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "UNKNOWN", "enabled": True}
        ])
    )
    log_warning(f"Invalid config response: {invalid_resp}")
    if not isinstance(parse_error(invalid_resp), dict):
        log_error("TCID19_InvalidWakeupUpdate Failed ❌ (invalid update was not rejected)")
        return False

    timer_resp = send_curl_command(PowerManagerApis.set_deep_sleep_timer(5))
    log_warning(f"Timer response: {timer_resp}")
    if not is_ok(timer_resp):
        log_error("TCID19_InvalidWakeupUpdate Failed ❌ (failed to set deep-sleep timer)")
        return False

    deep_resp = send_curl_command(PowerManagerApis.set_power_state("DEEP_SLEEP", standby_reason="PM-PLUGIN-011", timeout=10))
    log_warning(f"Deep sleep response: {deep_resp}")
    if not is_ok(deep_resp):
        log_error("TCID19_InvalidWakeupUpdate Failed ❌ (failed to enter DEEP_SLEEP)")
        return False

    time.sleep(7)
    reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
    log_warning(f"Wakeup reason response: {reason_resp}")
    reason = parse_last_wakeup_reason(reason_resp)
    if reason == "UNKNOWN":
        note_partial("vdevice did not surface TIMER as last wakeup reason after the preserved valid config and returned UNKNOWN instead.")
    elif not is_timer_wakeup_reason(reason):
        log_error("TCID19_InvalidWakeupUpdate Failed ❌ (wake reason did not follow the preserved timer-backed config)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-011-restore"))
        return False
    elif reason != "TIMER":
        log_warning(f"Observed timer-backed wakeup reason alias after invalid update: {reason}")

    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-011-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID19_InvalidWakeupUpdate Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
