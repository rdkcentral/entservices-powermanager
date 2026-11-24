"""
/**
 * @file TCID35_RebootBeginEvent.py
 * @brief Validates onRebootBegin delivery over a persistent WebSocket.
 *
 * @testcase TCID35_RebootBeginEvent
 * @details Registers for onRebootBegin, invokes the reboot API, and validates
 *          the rebootRequestor, rebootReasonCustom, and rebootReasonOther
 *          fields in the asynchronous event payload.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from utils import (
    JsonRpcEventListener,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
)


EVENT_TIMEOUT = float(os.environ.get("POWERMANAGER_EVENT_TIMEOUT", "8"))
REBOOT_REQUESTOR = "APIUnitTest"
REBOOT_REASON_CUSTOM = "FIRMWARE_FAILURE"
REBOOT_REASON_OTHER = "PowerManager event handling test"


def run_test():
    start_time = time.perf_counter()
    listener = JsonRpcEventListener(
        PowerManagerApis.CALLSIGN,
        "onRebootBegin",
        "TCID35_onRebootBegin",
        timeout=EVENT_TIMEOUT,
    )

    log_info("Registering onRebootBegin over WebSocket")
    if not listener.connect():
        log_error("TCID35_RebootBeginEvent Failed (event registration rejected)")
        return False
    log_success("Registered onRebootBegin event listener")

    try:
        log_info(
            "Executing reboot with "
            f"rebootRequestor={REBOOT_REQUESTOR}, "
            f"rebootReasonCustom={REBOOT_REASON_CUSTOM}, "
            f"rebootReasonOther={REBOOT_REASON_OTHER}"
        )
        reboot_resp = send_curl_command(
            PowerManagerApis.reboot(
                reboot_requestor=REBOOT_REQUESTOR,
                reboot_reason_custom=REBOOT_REASON_CUSTOM,
                reboot_reason_other=REBOOT_REASON_OTHER,
            )
        )
        log_warning(f"Reboot response: {reboot_resp}")
        if not is_ok(reboot_resp):
            log_error("TCID35_RebootBeginEvent Failed (reboot call did not succeed)")
            return False
        log_success("Reboot request accepted")

        log_info(f"Waiting up to {EVENT_TIMEOUT:.0f}s for onRebootBegin")
        notification = listener.wait_for_event(
            lambda params: (
                params.get("rebootRequestor") == REBOOT_REQUESTOR
                and params.get("rebootReasonCustom") == REBOOT_REASON_CUSTOM
                and params.get("rebootReasonOther") == REBOOT_REASON_OTHER
            ),
            timeout=EVENT_TIMEOUT,
        )
        if notification is None:
            log_error(
                "TCID35_RebootBeginEvent Failed "
                "(onRebootBegin with the expected reboot details was not received)"
            )
            return False
        log_success(f"Captured onRebootBegin event: {notification}")
        log_success(
            "Validated rebootRequestor, rebootReasonCustom, and rebootReasonOther"
        )
    finally:
        listener.close()

    elapsed_time = time.perf_counter() - start_time
    message = "TCID35_RebootBeginEvent Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
