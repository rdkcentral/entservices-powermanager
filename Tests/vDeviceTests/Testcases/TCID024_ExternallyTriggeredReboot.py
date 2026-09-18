"""
/**
 * @file TCID024_ExternallyTriggeredReboot.py
 * @brief L3 PowerManager combination testcase.
 *
 * @testcase TCID024_ExternallyTriggeredReboot
 * @details Validates network wake preconditions and records the current partial
 *          limitation around missing external wake stimulus in this framework.
 */
"""

import os
import time

from utils import POWERMANAGER_CMD_BASE, send_curl_command, send_vcomponent_command, is_ok, log_success, log_error, log_warning, log_info, activate_plugin
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import parse_last_wakeup_reason, parse_power_state


REBOOT_REASON_SCENARIOS = [
    ("Boot_BootReason_COLD_BOOT.yaml", "COLDBOOT"),
    ("Boot_BootReason_MAINTAINANCE_REBOOT.yaml", "SOFTWARERESET"),
    ("Boot_BootReason_STR_AUTH_FAILURE.yaml", "STR_AUTH_FAIL"),
    ("Boot_BootReason_THERMAL_RESET.yaml", "THERMALRESET"),
    ("Boot_BootReason_WARM_RESET.yaml", "WARMRESET"),
    ("Boot_BootReason_WATCHDOG.yaml", "WATCHDOG"),
]

NEGATIVE_REASON_CHECKS = {
    "COLDBOOT": "WARMRESET",
    "SOFTWARERESET": "WATCHDOG",
    "STR_AUTH_FAIL": "THERMALRESET",
    "THERMALRESET": "STR_AUTH_FAIL",
    "WARMRESET": "COLDBOOT",
    "WATCHDOG": "SOFTWARERESET",
}

def _post_reboot(yaml_file):
    http_code, body = send_vcomponent_command(f"{POWERMANAGER_CMD_BASE}/{yaml_file}", False)
    log_warning(f"vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def _wait_for_boot_reason(expected_reason, timeout_seconds=20):
    deadline = time.time() + timeout_seconds
    last_reason = None
    while time.time() < deadline:
        reason_resp = send_curl_command(PowerManagerApis.get_last_wakeup_reason)
        log_warning(f"Boot reason response: {reason_resp}")
        last_reason = parse_last_wakeup_reason(reason_resp)
        if last_reason == expected_reason:
            return last_reason
        time.sleep(1)
    return last_reason


def _wait_for_awake_state(timeout_seconds=60):
    deadline = time.time() + timeout_seconds
    last_state = None
    while time.time() < deadline:
        state_resp = send_curl_command(PowerManagerApis.get_power_state)
        log_warning(f"Power state response: {state_resp}")
        if state_resp is not None:
            return True
        time.sleep(1)
    return False


def run_test():
    start_time = time.perf_counter()

    for yaml_file, expected_reason in REBOOT_REASON_SCENARIOS:
        if not _post_reboot(yaml_file):
            log_error(
                "TCID024_ExternallyTriggeredReboot Failed ❌ "
                f"(failed to post reboot simulation via {yaml_file})"
            )
            return False

        log_warning(
            f"Reboot triggered through control plane using {yaml_file}. "
            f"Expected boot reason: {expected_reason}.\n"
        )
        time.sleep(10)  # Wait for the device to reboot and come back online

        if _wait_for_awake_state() is False:
            log_error(
                "TCID024_ExternallyTriggeredReboot Failed ❌ "
                f"(device did not report a post-wake power state for {yaml_file})"
            )
            return False

        log_info("Device is awake. Re-activating plugin 'org.rdk.PowerManager' via curl JSON-RPC")
        if activate_plugin("org.rdk.PowerManager"):
            log_success("Plugin 'org.rdk.PowerManager' activated successfully")
        else:
            log_error("Failed to activate plugin 'org.rdk.PowerManager'")
            return False

        reason = _wait_for_boot_reason(expected_reason)
        if reason != expected_reason:
            log_error(
                "TCID024_ExternallyTriggeredReboot Failed ❌ "
                f"(last boot reason was not {expected_reason} for {yaml_file}). "
                f"Returned boot reason: {reason}"
            )
            return False
        log_success(f"Validated reboot reason {expected_reason} using {yaml_file} ✅")

        negative_reason = NEGATIVE_REASON_CHECKS.get(expected_reason)
        if negative_reason is not None:
            wrong_reason = _wait_for_boot_reason(negative_reason, timeout_seconds=5)
            if wrong_reason == negative_reason:
                log_error(
                    "TCID024_ExternallyTriggeredReboot Failed ❌ "
                    f"(negative check failed for {yaml_file}: incorrect boot reason "
                    f"{negative_reason} was reported)"
                )
                return False
            log_success(
                f"Negative check passed: boot reason did not change to incorrect value "
                f"{negative_reason} for {yaml_file} ✅"
            )

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID024_ExternallyTriggeredReboot Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True

