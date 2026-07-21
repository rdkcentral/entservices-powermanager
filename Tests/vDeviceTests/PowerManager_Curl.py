"""
/**
 * @file PowerManager_Curl.py
 * @brief PowerManager JSON-RPC curl command library.
 *
 * @testcase PowerManager_Curl
 * @details Central library of curl command strings / builders for every
 *          org.rdk.PowerManager JSON-RPC method exercised by the test suite.
 *          Static getters are exposed as ready-to-send string constants; setters
 *          and parameterized calls are exposed as builder functions.
 *
 * @note Parameter names below follow the IPowerManager interface signatures.
 *       If a target build exposes different JSON-RPC parameter spelling, adjust
 *       the builders here (single source of truth) rather than the test cases.
 */
"""

import json

from utils import WPEFRAMEWORK_JSONRPC_URL

CALLSIGN = "org.rdk.PowerManager"

# ---------------------------------------------------------------------------
# PowerState enum values (WPEFramework::Exchange::IPowerManager::PowerState)
# ---------------------------------------------------------------------------
POWER_STATE_UNKNOWN = 0
POWER_STATE_OFF = 1
POWER_STATE_STANDBY = 2
POWER_STATE_ON = 3
POWER_STATE_STANDBY_LIGHT_SLEEP = 4
POWER_STATE_STANDBY_DEEP_SLEEP = 5


def _curl(method, params=None, timeout=5, request_id=42):
    """Build a curl command string for a PowerManager JSON-RPC method."""
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": f"{CALLSIGN}.{method}",
    }
    if params is not None:
        payload["params"] = params
    # Use json.dumps to guarantee valid JSON; single-quote wrap for shell -d.
    data = json.dumps(payload)
    return (
        f"curl --max-time {timeout} "
        f'--header "Content-Type: application/json" '
        f"--request POST "
        f"-d '{data}' "
        f"{WPEFRAMEWORK_JSONRPC_URL}"
    )


def _curl_raw(method, params=None, timeout=5, request_id=42):
    """Build a curl command string for an arbitrary JSON-RPC method."""
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
    }
    if params is not None:
        payload["params"] = params
    data = json.dumps(payload)
    return (
        f"curl --max-time {timeout} "
        f'--header "Content-Type: application/json" '
        f"--request POST "
        f"-d '{data}' "
        f"{WPEFRAMEWORK_JSONRPC_URL}"
    )


# ---------------------------------------------------------------------------
# Getters (safe, no side effects)
# ---------------------------------------------------------------------------
get_power_state = _curl("getPowerState")
get_temperature_thresholds = _curl("getTemperatureThresholds")
get_overtemp_grace_interval = _curl("getOvertempGraceInterval")
get_thermal_state = _curl("getThermalState")
get_last_wakeup_reason = _curl("getLastWakeupReason")
get_last_wakeup_keycode = _curl("getLastWakeupKeyCode")
get_time_since_wakeup = _curl("getTimeSinceWakeup")
get_network_standby_mode = _curl("getNetworkStandbyMode")
get_wakeup_source_config = _curl("getWakeupSourceConfig")
get_power_state_before_reboot = _curl("getPowerStateBeforeReboot")


# ---------------------------------------------------------------------------
# Setters / actions (builders)
# ---------------------------------------------------------------------------
def set_power_state(power_state, key_code=0, standby_reason="l2-vdevice-test", timeout=8):
    return _curl(
        "setPowerState",
        params={
            "keyCode": key_code,
            "powerState": power_state,
            "standbyReason": standby_reason,
        },
        timeout=timeout,
    )


def set_temperature_thresholds(high, critical):
    return _curl(
        "setTemperatureThresholds",
        params={"high": high, "critical": critical},
    )


def set_overtemp_grace_interval(grace_interval):
    return _curl(
        "setOvertempGraceInterval",
        params={"graceInterval": grace_interval},
    )


def set_deep_sleep_timer(time_out):
    return _curl("setDeepSleepTimer", params={"timeOut": time_out})


def set_network_standby_mode(standby_mode):
    return _curl(
        "setNetworkStandbyMode",
        params={"standbyMode": bool(standby_mode)},
    )


def set_network_standby_mode_nw(standby_mode):
    return _curl(
        "setNetworkStandbyMode",
        params={"nwStandby": bool(standby_mode)},
    )


def add_power_mode_prechange_client(client_name):
    return _curl(
        "addPowerModePreChangeClient",
        params={"clientName": client_name},
    )


def delay_power_mode_change_by(client_id, transaction_id, delay_period):
    return _curl(
        "delayPowerModeChangeBy",
        params={
            "clientId": client_id,
            "transactionId": transaction_id,
            "delayPeriod": delay_period,
        },
    )


def remove_power_mode_prechange_client(client_id):
    return _curl(
        "removePowerModePreChangeClient",
        params={"clientId": client_id},
    )


def set_wakeup_source_config(wakeup_sources):
    """wakeup_sources: list of {"wakeupSource": <int>, "enabled": <bool>} entries."""
    return _curl(
        "setWakeupSourceConfig",
        params={"wakeupSources": wakeup_sources},
    )


def reboot(reboot_requestor="vdevice-test",
           reboot_reason_custom="functional-test",
           reboot_reason_other="functional-test"):
    return _curl(
        "reboot",
        params={
            "rebootRequestor": reboot_requestor,
            "rebootReasonCustom": reboot_reason_custom,
            "rebootReasonOther": reboot_reason_other,
        },
        timeout=8,
    )


def register_event(event_name, listener_id, timeout=8):
    return _curl_raw(
        f"{CALLSIGN}.1.register",
        params={"event": event_name, "id": listener_id},
        timeout=timeout,
    )


def controller_activate(callsign=CALLSIGN, timeout=8):
    return _curl_raw(
        "Controller.1.activate",
        params={"callsign": callsign},
        timeout=timeout,
    )


def controller_deactivate(callsign=CALLSIGN, timeout=8):
    return _curl_raw(
        "Controller.1.deactivate",
        params={"callsign": callsign},
        timeout=timeout,
    )


# ---------------------------------------------------------------------------
# Negative / malformed variants (wrong param keys) for robustness tests
# ---------------------------------------------------------------------------
set_temperature_thresholds_invalid = _curl(
    "setTemperatureThresholds",
    params={"hiigh": 100.0, "critiical": 110.0},
)

set_overtemp_grace_interval_invalid = _curl(
    "setOvertempGraceInterval",
    params={"grraceInterval": 120},
)

set_network_standby_mode_invalid = _curl(
    "setNetworkStandbyMode",
    params={"standbyMoode": True},
)
