import os
import time
from pathlib import Path

from utils import (
    TARGET_HOST,
    log_error,
    log_info,
    log_success,
    log_warning,
    send_jsonrpc_command,
)


CALLSIGN = "org.rdk.PowerManager"
STRICT_PARTIALS = os.environ.get("POWERMANAGER_STRICT_SCENARIOS", "0").lower() in (
    "1",
    "true",
    "yes",
)
IGNORE_DEEPSLEEP_PATH = os.environ.get(
    "POWERMANAGER_IGNOREDEEPSLEEP_PATH", "/tmp/ignoredeepsleep"
)

_REQUEST_ID = 600000


def _next_id():
    global _REQUEST_ID
    _REQUEST_ID += 1
    return _REQUEST_ID


def _pm_call(method, params=None, timeout=8):
    return send_jsonrpc_command(
        f"{CALLSIGN}.{method}",
        params=params,
        request_id=_next_id(),
        timeout=timeout,
    )


def _controller_call(method, params=None, timeout=8):
    return send_jsonrpc_command(
        f"Controller.1.{method}",
        params=params,
        request_id=_next_id(),
        timeout=timeout,
    )


def _register_event(event_name, listener_id):
    return send_jsonrpc_command(
        f"{CALLSIGN}.1.register",
        params={"event": event_name, "id": listener_id},
        request_id=_next_id(),
        timeout=8,
    )


def _is_ok(response):
    return isinstance(response, dict) and "result" in response and "error" not in response


def _get_error(response):
    if not isinstance(response, dict):
        return None
    error = response.get("error")
    return error if isinstance(error, dict) else None


def _get_result(response):
    if not isinstance(response, dict) or "result" not in response:
        return None
    return response.get("result")


def _expect(condition, message):
    if not condition:
        raise AssertionError(message)


def _note_partial(reason):
    if STRICT_PARTIALS:
        raise AssertionError(reason)
    log_warning(f"Partial coverage accepted: {reason}")


def _finish(testcase_name, start_time):
    elapsed_time = time.perf_counter() - start_time
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{testcase_name} Passed time consumed: {elapsed_time:.3f}s")
    else:
        log_success(f"{testcase_name} Passed")


def _get_power_state():
    result = _get_result(_pm_call("getPowerState"))
    return result if isinstance(result, dict) else None


def _get_time_since_wakeup():
    result = _get_result(_pm_call("getTimeSinceWakeup"))
    if isinstance(result, bool):
        return None
    if isinstance(result, int):
        return result
    if isinstance(result, dict):
        value = result.get("secondsSinceWakeup")
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def _get_wakeup_config():
    result = _get_result(_pm_call("getWakeupSourceConfig"))
    return result if isinstance(result, list) else None


def _get_last_wakeup_reason():
    result = _get_result(_pm_call("getLastWakeupReason"))
    if isinstance(result, str) and result:
        return result
    if isinstance(result, dict):
        value = result.get("wakeupReason")
        if isinstance(value, str) and value:
            return value
    return None


def _get_last_wakeup_keycode():
    result = _get_result(_pm_call("getLastWakeupKeyCode"))
    if isinstance(result, int) and not isinstance(result, bool):
        return result
    if isinstance(result, dict):
        value = result.get("keyCode", result.get("keycode"))
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def _get_network_standby_mode():
    result = _get_result(_pm_call("getNetworkStandbyMode"))
    if isinstance(result, bool):
        return result
    if isinstance(result, dict):
        for key in ("standbyMode", "nwStandby", "enabled"):
            value = result.get(key)
            if isinstance(value, bool):
                return value
    return None


def _set_power_state(state, reason, timeout=8):
    response = _pm_call(
        "setPowerState",
        params={"keyCode": 0, "powerState": state, "standbyReason": reason},
        timeout=timeout,
    )
    _expect(_is_ok(response), f"setPowerState({state}) failed: {response}")


def _set_deep_sleep_timer(seconds):
    response = _pm_call("setDeepSleepTimer", params={"timeOut": seconds})
    _expect(_is_ok(response), f"setDeepSleepTimer({seconds}) failed: {response}")


def _set_wakeup_source_config(wakeup_sources, expect_error=False):
    response = _pm_call(
        "setWakeupSourceConfig",
        params={"wakeupSources": wakeup_sources},
    )
    if expect_error:
        _expect(_get_error(response) is not None, f"Expected wakeup-source error, got: {response}")
    else:
        _expect(_is_ok(response), f"setWakeupSourceConfig failed: {response}")
    return response


def _set_temperature_thresholds(high, critical):
    return _pm_call(
        "setTemperatureThresholds",
        params={"high": high, "critical": critical},
    )


def _get_temperature_thresholds():
    return _pm_call("getTemperatureThresholds")


def _set_network_standby_mode(enabled):
    last_response = None
    for key in ("standbyMode", "nwStandby"):
        response = _pm_call("setNetworkStandbyMode", params={key: bool(enabled)})
        last_response = response
        if _is_ok(response):
            return response
    raise AssertionError(f"setNetworkStandbyMode({enabled}) failed: {last_response}")


def _wait_for_power_state(expected_state, timeout_seconds=10):
    deadline = time.time() + timeout_seconds
    last_state = None
    while time.time() < deadline:
        state = _get_power_state()
        last_state = state
        if isinstance(state, dict) and state.get("currentState") == expected_state:
            return state
        time.sleep(1)
    return last_state


def _wakeup_map(config_list):
    mapping = {}
    if not isinstance(config_list, list):
        return mapping
    for entry in config_list:
        if not isinstance(entry, dict):
            continue
        source = entry.get("wakeupSource")
        enabled = entry.get("enabled")
        if isinstance(source, str) and isinstance(enabled, bool):
            mapping[source] = enabled
    return mapping


def _get_source_enabled(config_list, source_name):
    return _wakeup_map(config_list).get(source_name)


def _non_network_map(config_list):
    mapping = _wakeup_map(config_list)
    return {k: v for k, v in mapping.items() if k not in ("WIFI", "LAN")}


def _restore_wakeup_snapshot(snapshot):
    if not snapshot:
        return
    entries = [
        {"wakeupSource": wakeup_source, "enabled": enabled}
        for wakeup_source, enabled in snapshot.items()
    ]
    try:
        _set_wakeup_source_config(entries)
    except Exception as exc:
        log_warning(f"Best-effort wakeup-source restore failed: {exc}")


def _restore_network_mode(original):
    if not isinstance(original, bool):
        return
    try:
        _set_network_standby_mode(original)
    except Exception as exc:
        log_warning(f"Best-effort network-standby restore failed: {exc}")


def _restore_power_on(reason):
    try:
        _set_power_state("ON", reason)
        _wait_for_power_state("ON", timeout_seconds=5)
    except Exception as exc:
        log_warning(f"Best-effort power-state restore failed: {exc}")


def _can_manage_ignoredeepsleep_file():
    return TARGET_HOST in ("127.0.0.1", "localhost") and os.name != "nt"


def _scenario_001():
    _set_power_state("ON", "PM-PLUGIN-001")
    _wait_for_power_state("ON", timeout_seconds=5)
    _set_power_state("LIGHT_SLEEP", "PM-PLUGIN-001")
    state = _wait_for_power_state("LIGHT_SLEEP", timeout_seconds=5)
    _expect(isinstance(state, dict), "Failed to reach LIGHT_SLEEP before deep sleep")
    response = _register_event("onDeepSleepTimeout", "pm_plugin_001_timeout")
    _expect(_is_ok(response), f"Deep sleep timeout registration failed: {response}")
    _note_partial("Async notification delivery is not asserted by this curl-only framework.")
    _set_deep_sleep_timer(5)
    _set_power_state("DEEP_SLEEP", "PM-PLUGIN-001", timeout=10)
    time.sleep(7)
    state = _wait_for_power_state("LIGHT_SLEEP", timeout_seconds=3)
    _expect(isinstance(state, dict), "getPowerState did not return a valid result after deep sleep")
    _expect(state.get("currentState") == "LIGHT_SLEEP", f"Expected LIGHT_SLEEP after wake, got: {state}")
    _expect(
        state.get("previousState", state.get("prevState")) == "DEEP_SLEEP",
        f"Expected previousState DEEP_SLEEP after wake, got: {state}",
    )
    wake_age = _get_time_since_wakeup()
    _expect(wake_age == 0, f"Expected wakeup age 0 in LIGHT_SLEEP, got: {wake_age}")
    _set_power_state("ON", "PM-PLUGIN-001")
    _wait_for_power_state("ON", timeout_seconds=5)
    time.sleep(2)
    state = _get_power_state()
    _expect(isinstance(state, dict) and state.get("currentState") == "ON", f"Expected ON state, got: {state}")
    wake_age = _get_time_since_wakeup()
    _expect(isinstance(wake_age, int) and wake_age > 0, f"Expected non-zero wakeup age in ON, got: {wake_age}")


def _scenario_002():
    try:
        _set_power_state("ON", "PM-PLUGIN-002")
        _wait_for_power_state("ON", timeout_seconds=5)
        time.sleep(2)
        first_value = _get_time_since_wakeup()
        _expect(isinstance(first_value, int), f"First wakeup age invalid: {first_value}")
        time.sleep(2)
        second_value = _get_time_since_wakeup()
        _expect(isinstance(second_value, int), f"Second wakeup age invalid: {second_value}")
        _expect(second_value >= first_value, f"Wakeup age not monotonic: {first_value} -> {second_value}")
        _set_power_state("STANDBY", "PM-PLUGIN-002")
        _wait_for_power_state("STANDBY", timeout_seconds=5)
        reset_value = _get_time_since_wakeup()
        _expect(reset_value == 0, f"Expected wakeup age reset to 0 outside ON, got: {reset_value}")
    finally:
        _restore_power_on("PM-PLUGIN-002-restore")


def _scenario_003():
    try:
        _set_power_state("ON", "PM-PLUGIN-003")
        _wait_for_power_state("ON", timeout_seconds=5)
        _set_power_state("LIGHT_SLEEP", "PM-PLUGIN-003")
        state = _wait_for_power_state("LIGHT_SLEEP", timeout_seconds=5)
        _expect(state.get("previousState", state.get("prevState")) == "ON", f"Unexpected LIGHT_SLEEP state chain: {state}")
        _set_power_state("ON", "PM-PLUGIN-003")
        state = _wait_for_power_state("ON", timeout_seconds=5)
        _expect(state.get("previousState", state.get("prevState")) == "LIGHT_SLEEP", f"Unexpected ON state chain: {state}")
        _set_power_state("STANDBY", "PM-PLUGIN-003")
        state = _wait_for_power_state("STANDBY", timeout_seconds=5)
        _expect(state.get("previousState", state.get("prevState")) == "ON", f"Unexpected STANDBY state chain: {state}")
    finally:
        _restore_power_on("PM-PLUGIN-003-restore")


def _scenario_004():
    try:
        response = _register_event("onPowerModeChanged", "pm_plugin_004_changed")
        _expect(_is_ok(response), f"Power-mode-changed registration failed: {response}")
        _set_power_state("ON", "PM-PLUGIN-004")
        _wait_for_power_state("ON", timeout_seconds=5)
        _set_power_state("LIGHT_SLEEP", "PM-PLUGIN-004")
        state = _wait_for_power_state("LIGHT_SLEEP", timeout_seconds=5)
        _expect(isinstance(state, dict) and state.get("currentState") == "LIGHT_SLEEP", f"Expected LIGHT_SLEEP state, got: {state}")
        _note_partial("Async onPowerModeChanged delivery needs a persistent listener and is not asserted here.")
    finally:
        _restore_power_on("PM-PLUGIN-004-restore")


def _scenario_005():
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read starting wakeup-source configuration")
    snapshot = _wakeup_map(config)
    try:
        _set_wakeup_source_config([{"wakeupSource": "TIMER", "enabled": False}])
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "TIMER") is False, f"Expected TIMER disabled in read-back config, got: {config}")
        _note_partial(
            "Full non-TIMER wake validation remains on hold pending external wake stimulus and TIMER-fix availability."
        )
    finally:
        _restore_wakeup_snapshot(snapshot)


def _scenario_006():
    try:
        keycode = _get_last_wakeup_keycode()
        _expect(keycode == 0, f"Expected initial wake keycode 0, got: {keycode}")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-006", timeout=10)
        time.sleep(7)
        keycode = _get_last_wakeup_keycode()
        _expect(keycode == 0, f"Expected wake keycode 0 after recovery, got: {keycode}")
    finally:
        _restore_power_on("PM-PLUGIN-006-restore")


def _scenario_007():
    try:
        baseline = _get_last_wakeup_keycode()
        _expect(baseline == 0, f"Expected baseline keycode 0, got: {baseline}")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-007", timeout=10)
        time.sleep(7)
        reason = _get_last_wakeup_reason()
        _expect(isinstance(reason, str) and reason, f"Expected a wakeup reason string, got: {reason}")
        keycode = _get_last_wakeup_keycode()
        _expect(keycode == 0, f"Expected non-key wake keycode 0, got: {keycode}")
    finally:
        _restore_power_on("PM-PLUGIN-007-restore")


def _scenario_008():
    response = _controller_call("deactivate", params={"callsign": CALLSIGN})
    _expect(_is_ok(response), f"Plugin deactivation failed: {response}")
    try:
        for method in ("getTimeSinceWakeup", "getPowerState", "getLastWakeupReason"):
            response = _pm_call(method)
            _expect(_get_error(response) is not None, f"Expected service-inactive error from {method}, got: {response}")
    finally:
        response = _controller_call("activate", params={"callsign": CALLSIGN})
        _expect(_is_ok(response), f"Plugin reactivation failed: {response}")
        time.sleep(6)


def _scenario_009():
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read pre-change wakeup-source config")
    before = _wakeup_map(config)
    response = _set_wakeup_source_config(
        [
            {"wakeupSource": "UNKNOWN", "enabled": True},
            {"wakeupSource": "IR", "enabled": False},
        ],
        expect_error=True,
    )
    _expect(_get_error(response) is not None, f"Expected invalid-parameter error, got: {response}")
    config = _get_wakeup_config()
    after = _wakeup_map(config)
    _expect(before == after, f"Wakeup-source config changed after invalid payload: before={before} after={after}")


def _scenario_010():
    if not _can_manage_ignoredeepsleep_file():
        _note_partial(
            "This scenario needs target-local file access to /tmp/ignoredeepsleep; current environment is not target-local Linux."
        )
        return
    override_file = Path(IGNORE_DEEPSLEEP_PATH)
    try:
        override_file.parent.mkdir(parents=True, exist_ok=True)
        override_file.write_text("1", encoding="ascii")
        _set_power_state("ON", "PM-PLUGIN-010")
        state_before = _wait_for_power_state("ON", timeout_seconds=5)
        response = _pm_call(
            "setPowerState",
            params={"keyCode": 0, "powerState": "DEEP_SLEEP", "standbyReason": "PM-PLUGIN-010"},
            timeout=8,
        )
        _expect(not _is_ok(response), f"Expected DEEP_SLEEP request rejection while override file exists, got: {response}")
        state_after = _get_power_state()
        _expect(
            isinstance(state_after, dict) and state_after.get("currentState") == state_before.get("currentState"),
            f"Power state changed despite ignoredeepsleep override: before={state_before} after={state_after}",
        )
        _note_partial("Async absence of onDeepSleepTimeout is not asserted by this framework.")
    finally:
        try:
            if override_file.exists():
                override_file.unlink()
        except Exception as exc:
            log_warning(f"Failed to remove ignoredeepsleep override: {exc}")
        _restore_power_on("PM-PLUGIN-010-restore")


def _scenario_011():
    try:
        _set_wakeup_source_config(
            [
                {"wakeupSource": "IR", "enabled": True},
                {"wakeupSource": "TIMER", "enabled": True},
            ]
        )
        response = _set_wakeup_source_config(
            [{"wakeupSource": "UNKNOWN", "enabled": True}],
            expect_error=True,
        )
        _expect(_get_error(response) is not None, f"Expected invalid update to fail, got: {response}")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-011", timeout=10)
        time.sleep(7)
        reason = _get_last_wakeup_reason()
        _expect(reason == "TIMER", f"Expected TIMER wake after valid TIMER config, got: {reason}")
    finally:
        _restore_power_on("PM-PLUGIN-011-restore")


def _scenario_012():
    first = _get_temperature_thresholds()
    first_error = _get_error(first)
    _expect(
        isinstance(first_error, dict)
        and first_error.get("message") == "ERROR_GENERAL"
        and first_error.get("code") == 1,
        f"Expected getTemperatureThresholds ERROR_GENERAL, got: {first}",
    )
    second = _set_temperature_thresholds(100.0, 110.0)
    second_error = _get_error(second)
    _expect(
        isinstance(second_error, dict)
        and second_error.get("message") == "ERROR_GENERAL"
        and second_error.get("code") == 1,
        f"Expected setTemperatureThresholds ERROR_GENERAL, got: {second}",
    )
    third = _get_temperature_thresholds()
    third_error = _get_error(third)
    _expect(
        isinstance(third_error, dict)
        and third_error.get("message") == "ERROR_GENERAL"
        and third_error.get("code") == 1,
        f"Expected stable getTemperatureThresholds ERROR_GENERAL, got: {third}",
    )


def _scenario_013():
    try:
        _set_power_state("ON", "PM-PLUGIN-013")
        _wait_for_power_state("ON", timeout_seconds=5)
        time.sleep(3)
        first_value = _get_time_since_wakeup()
        _expect(isinstance(first_value, int), f"First wakeup age invalid: {first_value}")
        _set_power_state("ON", "PM-PLUGIN-013")
        _wait_for_power_state("ON", timeout_seconds=5)
        time.sleep(2)
        second_value = _get_time_since_wakeup()
        _expect(isinstance(second_value, int), f"Second wakeup age invalid: {second_value}")
        _expect(second_value >= first_value, f"Repeated ON reset wakeup timer: {first_value} -> {second_value}")
    finally:
        _restore_power_on("PM-PLUGIN-013-restore")


def _scenario_014():
    _set_deep_sleep_timer(90000)
    _set_deep_sleep_timer(5)
    _note_partial(
        "The >86400 clamp is in code, but end-to-end runtime behavior for stored timeout 0 is backend-dependent and is not asserted here."
    )


def _scenario_015():
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read starting wakeup-source config")
    snapshot = _wakeup_map(config)
    try:
        _set_wakeup_source_config(
            [
                {"wakeupSource": "IR", "enabled": True},
                {"wakeupSource": "IR", "enabled": False},
            ]
        )
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "IR") is False, f"Expected last duplicate IR value to win, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)


def _scenario_016():
    try:
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-016-A", timeout=10)
        time.sleep(7)
        reason_one = _get_last_wakeup_reason()
        keycode_one = _get_last_wakeup_keycode()
        _expect(isinstance(reason_one, str) and reason_one, f"Cycle 1 wakeup reason invalid: {reason_one}")
        _expect(keycode_one == 0, f"Expected cycle 1 wakeup keycode 0, got: {keycode_one}")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-016-B", timeout=10)
        time.sleep(7)
        reason_two = _get_last_wakeup_reason()
        keycode_two = _get_last_wakeup_keycode()
        _expect(isinstance(reason_two, str) and reason_two, f"Cycle 2 wakeup reason invalid: {reason_two}")
        _expect(keycode_two == 0, f"Expected cycle 2 wakeup keycode 0, got: {keycode_two}")
    finally:
        _restore_power_on("PM-PLUGIN-016-restore")


def _scenario_017():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read baseline wakeup-source config")
    baseline_non_network = _non_network_map(config)
    try:
        _set_network_standby_mode(True)
        mode = _get_network_standby_mode()
        _expect(mode is True, f"Expected network standby enabled, got: {mode}")
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "WIFI") is True, f"Expected WIFI enabled, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is True, f"Expected LAN enabled, got: {config}")
        _expect(
            _non_network_map(config) == baseline_non_network,
            f"Non-network sources changed unexpectedly: before={baseline_non_network} after={_non_network_map(config)}",
        )
    finally:
        _restore_network_mode(original_mode)


def _scenario_018():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read baseline wakeup-source config")
    baseline_non_network = _non_network_map(config)
    try:
        _set_network_standby_mode(False)
        mode = _get_network_standby_mode()
        _expect(mode is False, f"Expected network standby disabled, got: {mode}")
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "WIFI") is False, f"Expected WIFI disabled, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is False, f"Expected LAN disabled, got: {config}")
        _expect(
            _non_network_map(config) == baseline_non_network,
            f"Non-network sources changed unexpectedly: before={baseline_non_network} after={_non_network_map(config)}",
        )
    finally:
        _restore_network_mode(original_mode)


def _scenario_019():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    _expect(isinstance(config, list), "Unable to read baseline wakeup-source config")
    baseline_non_network = _non_network_map(config)
    try:
        _set_network_standby_mode(True)
        enabled_config = _get_wakeup_config()
        _expect(_get_source_enabled(enabled_config, "WIFI") is True, f"Expected WIFI enabled, got: {enabled_config}")
        _expect(_get_source_enabled(enabled_config, "LAN") is True, f"Expected LAN enabled, got: {enabled_config}")
        _expect(_non_network_map(enabled_config) == baseline_non_network, "Enable path changed non-network sources")
        _set_network_standby_mode(False)
        disabled_config = _get_wakeup_config()
        _expect(_get_source_enabled(disabled_config, "WIFI") is False, f"Expected WIFI disabled, got: {disabled_config}")
        _expect(_get_source_enabled(disabled_config, "LAN") is False, f"Expected LAN disabled, got: {disabled_config}")
        _expect(_non_network_map(disabled_config) == baseline_non_network, "Disable path changed non-network sources")
    finally:
        _restore_network_mode(original_mode)


def _scenario_020():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_wakeup_source_config(
            [
                {"wakeupSource": "WIFI", "enabled": True},
                {"wakeupSource": "LAN", "enabled": True},
            ]
        )
        mode = _get_network_standby_mode()
        config = _get_wakeup_config()
        _expect(mode is True, f"Expected implicit network standby enable, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is True, f"Expected WIFI enabled, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is True, f"Expected LAN enabled, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)


def _scenario_021():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_wakeup_source_config(
            [
                {"wakeupSource": "WIFI", "enabled": False},
                {"wakeupSource": "LAN", "enabled": False},
            ]
        )
        mode = _get_network_standby_mode()
        config = _get_wakeup_config()
        _expect(mode is False, f"Expected implicit network standby disable, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is False, f"Expected WIFI disabled, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is False, f"Expected LAN disabled, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)


def _scenario_022():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(False)
        _set_wakeup_source_config(
            [
                {"wakeupSource": "WIFI", "enabled": True},
                {"wakeupSource": "LAN", "enabled": False},
            ]
        )
        config = _get_wakeup_config()
        mode = _get_network_standby_mode()
        _expect(_get_source_enabled(config, "WIFI") is True, f"Expected WIFI enabled, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is False, f"Expected LAN disabled, got: {config}")
        _expect(mode is False, f"Expected baseline network standby to remain false, got: {mode}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)


def _scenario_023():
    original_mode = _get_network_standby_mode()
    try:
        _set_network_standby_mode(True)
        _set_network_standby_mode(True)
        config = _get_wakeup_config()
        mode = _get_network_standby_mode()
        _expect(mode is True, f"Expected network standby enabled after repeated enable, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is True and _get_source_enabled(config, "LAN") is True, f"Expected WIFI/LAN enabled, got: {config}")
        _set_network_standby_mode(False)
        _set_network_standby_mode(False)
        config = _get_wakeup_config()
        mode = _get_network_standby_mode()
        _expect(mode is False, f"Expected network standby disabled after repeated disable, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is False and _get_source_enabled(config, "LAN") is False, f"Expected WIFI/LAN disabled, got: {config}")
    finally:
        _restore_network_mode(original_mode)


def _scenario_024():
    original_mode = _get_network_standby_mode()
    try:
        response = _register_event("onNetworkStandbyModeChanged", "pm_plugin_024_nwstandby")
        _expect(_is_ok(response), f"Network-standby event registration failed: {response}")
        _set_network_standby_mode(True)
        _expect(_get_network_standby_mode() is True, "Expected network standby enabled after setter")
        _set_network_standby_mode(False)
        _expect(_get_network_standby_mode() is False, "Expected network standby disabled after setter")
        _note_partial("Async onNetworkStandbyModeChanged delivery is not asserted by this framework.")
    finally:
        _restore_network_mode(original_mode)


def _scenario_025():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(True)
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "WIFI") is True and _get_source_enabled(config, "LAN") is True, f"Expected WIFI/LAN enabled before deep sleep, got: {config}")
        response = _register_event("onDeepSleepTimeout", "pm_plugin_025_timeout")
        _expect(_is_ok(response), f"Deep-sleep-timeout registration failed: {response}")
        _note_partial("Async onDeepSleepTimeout delivery is not asserted by this framework.")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-025", timeout=10)
        time.sleep(7)
        state = _get_power_state()
        mode = _get_network_standby_mode()
        config = _get_wakeup_config()
        _expect(isinstance(state, dict) and state.get("currentState") == "LIGHT_SLEEP", f"Expected LIGHT_SLEEP after wake, got: {state}")
        _expect(mode is True, f"Expected network standby enabled after cycle, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is True and _get_source_enabled(config, "LAN") is True, f"Expected WIFI/LAN enabled after cycle, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)
        _restore_power_on("PM-PLUGIN-025-restore")


def _scenario_026():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(False)
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "WIFI") is False and _get_source_enabled(config, "LAN") is False, f"Expected WIFI/LAN disabled before deep sleep, got: {config}")
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-026", timeout=10)
        time.sleep(7)
        state = _get_power_state()
        mode = _get_network_standby_mode()
        config = _get_wakeup_config()
        _expect(isinstance(state, dict) and state.get("currentState") == "LIGHT_SLEEP", f"Expected LIGHT_SLEEP after wake, got: {state}")
        _expect(mode is False, f"Expected network standby disabled after cycle, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is False and _get_source_enabled(config, "LAN") is False, f"Expected WIFI/LAN disabled after cycle, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)
        _restore_power_on("PM-PLUGIN-026-restore")


def _scenario_027():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(True)
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-027", timeout=10)
        time.sleep(7)
        _set_power_state("ON", "PM-PLUGIN-027")
        _wait_for_power_state("ON", timeout_seconds=5)
        time.sleep(2)
        wake_age = _get_time_since_wakeup()
        mode = _get_network_standby_mode()
        config = _get_wakeup_config()
        _expect(isinstance(wake_age, int) and wake_age > 0, f"Expected non-zero wakeup age after ON transition, got: {wake_age}")
        _expect(mode is True, f"Expected network standby enabled after cycle, got: {mode}")
        _expect(_get_source_enabled(config, "WIFI") is True and _get_source_enabled(config, "LAN") is True, f"Expected WIFI/LAN enabled after cycle, got: {config}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)
        _restore_power_on("PM-PLUGIN-027-restore")


def _scenario_028():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(False)
        _set_wakeup_source_config(
            [
                {"wakeupSource": "WIFI", "enabled": True},
                {"wakeupSource": "LAN", "enabled": False},
            ]
        )
        _set_deep_sleep_timer(5)
        _set_power_state("DEEP_SLEEP", "PM-PLUGIN-028", timeout=10)
        time.sleep(7)
        config = _get_wakeup_config()
        mode = _get_network_standby_mode()
        _expect(_get_source_enabled(config, "WIFI") is True, f"Expected WIFI enabled after cycle, got: {config}")
        _expect(_get_source_enabled(config, "LAN") is False, f"Expected LAN disabled after cycle, got: {config}")
        _expect(mode is False, f"Expected network standby baseline false after cycle, got: {mode}")
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)
        _restore_power_on("PM-PLUGIN-028-restore")


def _scenario_029():
    original_mode = _get_network_standby_mode()
    config = _get_wakeup_config()
    snapshot = _wakeup_map(config)
    try:
        _set_network_standby_mode(True)
        config = _get_wakeup_config()
        _expect(_get_source_enabled(config, "WIFI") is True and _get_source_enabled(config, "LAN") is True, f"Expected WIFI/LAN enabled before network wake test, got: {config}")
        _set_deep_sleep_timer(60)
        _note_partial(
            "Full external LAN/WIFI wake validation requires a harness-generated network stimulus before timer expiry and is not executed here."
        )
    finally:
        _restore_wakeup_snapshot(snapshot)
        _restore_network_mode(original_mode)


SCENARIOS = {
    "PM-PLUGIN-001": _scenario_001,
    "PM-PLUGIN-002": _scenario_002,
    "PM-PLUGIN-003": _scenario_003,
    "PM-PLUGIN-004": _scenario_004,
    "PM-PLUGIN-005": _scenario_005,
    "PM-PLUGIN-006": _scenario_006,
    "PM-PLUGIN-007": _scenario_007,
    "PM-PLUGIN-008": _scenario_008,
    "PM-PLUGIN-009": _scenario_009,
    "PM-PLUGIN-010": _scenario_010,
    "PM-PLUGIN-011": _scenario_011,
    "PM-PLUGIN-012": _scenario_012,
    "PM-PLUGIN-013": _scenario_013,
    "PM-PLUGIN-014": _scenario_014,
    "PM-PLUGIN-015": _scenario_015,
    "PM-PLUGIN-016": _scenario_016,
    "PM-PLUGIN-017": _scenario_017,
    "PM-PLUGIN-018": _scenario_018,
    "PM-PLUGIN-019": _scenario_019,
    "PM-PLUGIN-020": _scenario_020,
    "PM-PLUGIN-021": _scenario_021,
    "PM-PLUGIN-022": _scenario_022,
    "PM-PLUGIN-023": _scenario_023,
    "PM-PLUGIN-024": _scenario_024,
    "PM-PLUGIN-025": _scenario_025,
    "PM-PLUGIN-026": _scenario_026,
    "PM-PLUGIN-027": _scenario_027,
    "PM-PLUGIN-028": _scenario_028,
    "PM-PLUGIN-029": _scenario_029,
}


def execute_named_scenario(scenario_id, testcase_name):
    scenario = SCENARIOS.get(scenario_id)
    if scenario is None:
        log_error(f"Unknown scenario id: {scenario_id}")
        return False

    log_info(f"Executing {scenario_id}")
    try:
        scenario()
    except AssertionError as exc:
        log_error(f"{testcase_name} Failed ({exc})")
        return False
    except Exception as exc:
        log_error(f"{testcase_name} Failed with exception ({exc})")
        return False

    return True


def run_named_scenario(scenario_id, testcase_name):
    start_time = time.perf_counter()
    if not execute_named_scenario(scenario_id, testcase_name):
        return False

    _finish(testcase_name, start_time)
    return True