"""
/**
 * @file TCID44_AtomicInvalidWakeupConfig.py
 * @brief Verifies an invalid wakeup-source request is applied atomically.
 *
 * @testcase TCID44_AtomicInvalidWakeupConfig
 * @details Places UNKNOWN between two valid wakeup-source entries and verifies
 *          the complete request is rejected without partially changing either
 *          valid entry or any other wakeup-source configuration.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import (
    parse_error,
    parse_wakeup_config,
    wakeup_map,
)
from utils import (
    is_ok,
    log_error,
    log_success,
    log_warning,
    send_curl_command,
)


def run_test():
    start_time = time.perf_counter()
    baseline = parse_wakeup_config(
        send_curl_command(PowerManagerApis.get_wakeup_source_config)
    )
    if not isinstance(baseline, list):
        log_error(
            "TCID44_AtomicInvalidWakeupConfig Failed "
            "(unable to read baseline configuration)"
        )
        return False

    baseline_map = wakeup_map(baseline)
    ir_value = baseline_map.get("IR", False)
    cec_value = baseline_map.get("CEC", False)
    failures = []

    try:
        invalid_response = send_curl_command(
            PowerManagerApis.set_wakeup_source_config(
                [
                    {
                        "wakeupSource": "IR",
                        "enabled": not ir_value,
                    },
                    {
                        "wakeupSource": "UNKNOWN",
                        "enabled": True,
                    },
                    {
                        "wakeupSource": "CEC",
                        "enabled": not cec_value,
                    },
                ]
            )
        )
        log_warning(f"Invalid update response: {invalid_response}")
        if not isinstance(parse_error(invalid_response), dict):
            failures.append("mixed invalid request was not rejected")

        after = parse_wakeup_config(
            send_curl_command(
                PowerManagerApis.get_wakeup_source_config
            )
        )
        after_map = wakeup_map(after)
        if not isinstance(after, list):
            failures.append("unable to read post-request configuration")
        elif after_map != baseline_map:
            failures.append(
                f"configuration changed: before={baseline_map}, "
                f"after={after_map}"
            )
    finally:
        current = parse_wakeup_config(
            send_curl_command(
                PowerManagerApis.get_wakeup_source_config
            )
        )
        if wakeup_map(current) != baseline_map:
            restore_response = send_curl_command(
                PowerManagerApis.set_wakeup_source_config(baseline)
            )
            if not is_ok(restore_response):
                failures.append("failed to restore baseline configuration")

    if failures:
        log_error(
            "TCID44_AtomicInvalidWakeupConfig Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed = time.perf_counter() - start_time
    message = "TCID44_AtomicInvalidWakeupConfig Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed:.3f}s")
    else:
        log_success(message)
    return True
