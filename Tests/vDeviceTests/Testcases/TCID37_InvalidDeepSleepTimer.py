"""
/**
 * @file TCID37_InvalidDeepSleepTimer.py
 * @brief Validates the effective behavior of invalid deep-sleep timer values.
 *
 * @testcase TCID37_InvalidDeepSleepTimer
 * @details An accepted JSON-RPC setter response does not prove that a timer was
 *          applied. Each invalid value is therefore followed by a bounded
 *          deep-sleep attempt. The test records whether DEEP_SLEEP was entered,
 *          captures the effective wakeupTimeout from onDeepSleepTimeout when
 *          natural wake occurs, and fails safely if the device remains asleep.
 *
 * @pass_criteria
 *  - The invalid value is rejected, or
 *  - DEEP_SLEEP is not entered, or
 *  - The device enters DEEP_SLEEP and naturally wakes within the safety window
 *    with an onDeepSleepTimeout event reporting the effective timer value.
 *
 * @failure_criteria
 *  - The device remains in DEEP_SLEEP beyond the safety window,
 *  - Natural wake occurs without a timeout event/effective timer value, or
 *  - Recovery to an awake state fails.
 */
"""

import os
import time

import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import (
    build_wakeup_override_entries,
    get_source_enabled,
    parse_error,
    parse_power_state,
    parse_wakeup_config,
    wakeup_map,
)
from utils import (
    JsonRpcEventListener,
    POWERMANAGER_CMD_BASE,
    is_ok,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_curl_command,
    send_vcomponent_command,
)


INVALID_TIMEOUTS = (
    ("negative timeout", -1),
    ("negative timeout below boundary", -100),
    ("timeout above 24-hour limit", 86401),
    ("maximum signed integer", 2147483647),
    ("null timeout", None),
    ("string timeout", "5"),
)

ENTRY_TIMEOUT = float(
    os.environ.get("POWERMANAGER_DEEP_SLEEP_ENTRY_TIMEOUT", "5")
)
NATURAL_WAKE_TIMEOUT = float(
    os.environ.get("POWERMANAGER_INVALID_TIMER_WAKE_TIMEOUT", "12")
)
RECOVERY_TIMEOUT = float(
    os.environ.get("POWERMANAGER_DEEP_SLEEP_RECOVERY_TIMEOUT", "20")
)


def _get_power_state():
    return parse_power_state(
        send_curl_command(PowerManagerApis.get_power_state)
    )


def _wait_for_state(predicate, timeout):
    deadline = time.monotonic() + timeout
    last_state = None
    while time.monotonic() < deadline:
        last_state = _get_power_state()
        if isinstance(last_state, dict) and predicate(
            last_state.get("currentState")
        ):
            return last_state
        time.sleep(0.25)
    return last_state


def _post_cec_wake():
    path = f"{POWERMANAGER_CMD_BASE}/DeepSleep_Wakeup_CEC.yaml"
    http_code, body = send_vcomponent_command(path)
    log_warning(f"CEC recovery response: HTTP {http_code} {body}")
    return http_code == 200


def _recover_to_on():
    state = _get_power_state()
    if isinstance(state, dict) and state.get("currentState") == "DEEP_SLEEP":
        _post_cec_wake()
        _wait_for_state(
            lambda current: current != "DEEP_SLEEP",
            RECOVERY_TIMEOUT,
        )

    response = send_curl_command(
        PowerManagerApis.set_power_state(
            "ON",
            standby_reason="TCID37-recovery",
        )
    )
    if not is_ok(response):
        return False
    state = _wait_for_state(
        lambda current: current == "ON",
        RECOVERY_TIMEOUT,
    )
    return (
        isinstance(state, dict)
        and state.get("currentState") == "ON"
    )


def _verify_accepted_timer(label, timeout):
    listener = JsonRpcEventListener(
        PowerManagerApis.CALLSIGN,
        "onDeepSleepTimeout",
        f"TCID37_{label.replace(' ', '_')}",
        timeout=NATURAL_WAKE_TIMEOUT,
    )
    if not listener.connect():
        return False, "unable to register onDeepSleepTimeout"

    try:
        baseline = send_curl_command(
            PowerManagerApis.set_power_state(
                "ON",
                standby_reason="TCID37-baseline",
            )
        )
        if not is_ok(baseline):
            return False, "unable to request ON baseline"
        on_state = _wait_for_state(
            lambda current: current == "ON",
            RECOVERY_TIMEOUT,
        )
        if (
            not isinstance(on_state, dict)
            or on_state.get("currentState") != "ON"
        ):
            return False, "device did not reach ON baseline"

        deep_response = send_curl_command(
            PowerManagerApis.set_power_state(
                "DEEP_SLEEP",
                standby_reason=f"TCID37-{label}",
                timeout=10,
            )
        )
        log_warning(f"DEEP_SLEEP response: {deep_response}")
        if not is_ok(deep_response):
            return True, "DEEP_SLEEP request was rejected"

        entered = _wait_for_state(
            lambda current: current == "DEEP_SLEEP",
            ENTRY_TIMEOUT,
        )
        if (
            not isinstance(entered, dict)
            or entered.get("currentState") != "DEEP_SLEEP"
        ):
            notification = listener.wait_for_event(
                lambda params: isinstance(
                    params.get("wakeupTimeout"), int
                ),
                timeout=1,
            )
            if notification is not None:
                effective_timeout = notification["wakeupTimeout"]
                return (
                    True,
                    "DEEP_SLEEP completed before it could be observed; "
                    f"effective timer was {effective_timeout}s",
                )
            return True, "DEEP_SLEEP was not entered"

        log_info(
            f"DEEP_SLEEP entered; waiting up to "
            f"{NATURAL_WAKE_TIMEOUT:.0f}s for natural wake"
        )
        notification = listener.wait_for_event(
            lambda params: isinstance(
                params.get("wakeupTimeout"), int
            ),
            timeout=NATURAL_WAKE_TIMEOUT,
        )
        state = _get_power_state()
        current_state = (
            state.get("currentState")
            if isinstance(state, dict)
            else None
        )

        if notification is None:
            if current_state == "DEEP_SLEEP":
                return (
                    False,
                    "device remained in DEEP_SLEEP beyond the safety window",
                )
            return (
                False,
                "device woke without onDeepSleepTimeout/effective timer value",
            )

        effective_timeout = notification["wakeupTimeout"]
        log_info(
            f"Requested timeOut={timeout!r}; effective "
            f"wakeupTimeout={effective_timeout}"
        )
        awake = _wait_for_state(
            lambda current: current != "DEEP_SLEEP",
            RECOVERY_TIMEOUT,
        )
        if (
            not isinstance(awake, dict)
            or awake.get("currentState") == "DEEP_SLEEP"
        ):
            return (
                False,
                f"timeout event reported {effective_timeout}, "
                "but device did not leave DEEP_SLEEP",
            )

        return (
            True,
            f"naturally woke with effective timer {effective_timeout}s",
        )
    finally:
        listener.close()


def run_test():
    start_time = time.perf_counter()
    failures = []

    original_config = parse_wakeup_config(
        send_curl_command(PowerManagerApis.get_wakeup_source_config)
    )
    if not isinstance(original_config, list):
        log_error(
            "TCID37_InvalidDeepSleepTimer Failed "
            "(unable to read wakeup-source configuration)"
        )
        return False

    try:
        required = build_wakeup_override_entries(
            original_config,
            {"TIMER": True, "CEC": True},
        )
        if required:
            config_response = send_curl_command(
                PowerManagerApis.set_wakeup_source_config(required)
            )
            if not is_ok(config_response):
                log_error(
                    "TCID37_InvalidDeepSleepTimer Failed "
                    "(unable to enable TIMER and CEC wake sources)"
                )
                return False

        configured = parse_wakeup_config(
            send_curl_command(
                PowerManagerApis.get_wakeup_source_config
            )
        )
        if (
            not isinstance(configured, list)
            or get_source_enabled(configured, "TIMER") is not True
            or get_source_enabled(configured, "CEC") is not True
        ):
            log_error(
                "TCID37_InvalidDeepSleepTimer Failed "
                "(TIMER/CEC wake sources are not enabled)"
            )
            return False

        for label, timeout in INVALID_TIMEOUTS:
            log_info(f"Submitting {label}: timeOut={timeout!r}")
            response = send_curl_command(
                PowerManagerApis.set_deep_sleep_timer(timeout)
            )
            log_warning(f"Setter response: {response}")

            error = parse_error(response)
            if isinstance(error, dict):
                log_success(f"{label} was rejected: {error}")
                continue
            if not is_ok(response):
                failures.append(
                    f"{label}: setter returned an invalid response"
                )
                continue

            ok, outcome = _verify_accepted_timer(label, timeout)
            log_warning(f"{label} outcome: {outcome}")
            if not ok:
                failures.append(f"{label}: {outcome}")

            if not _recover_to_on():
                failures.append(
                    f"{label}: recovery to ON failed"
                )
                break
    finally:
        _recover_to_on()
        restore_entries = [
            {"wakeupSource": source, "enabled": enabled}
            for source, enabled in wakeup_map(original_config).items()
        ]
        send_curl_command(
            PowerManagerApis.set_wakeup_source_config(restore_entries)
        )

    if failures:
        log_error(
            "TCID37_InvalidDeepSleepTimer Failed: "
            + "; ".join(failures)
        )
        return False

    elapsed_time = time.perf_counter() - start_time
    message = "TCID37_InvalidDeepSleepTimer Passed"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{message} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(message)
    return True
