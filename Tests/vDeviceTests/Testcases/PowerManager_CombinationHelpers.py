import json
import os

from utils import TARGET_HOST, log_warning, parse_result


STRICT_PARTIALS = os.environ.get("POWERMANAGER_STRICT_SCENARIOS", "0").lower() in (
    "1",
    "true",
    "yes",
)


def parse_json_response(curl_response):
    if not curl_response or curl_response.startswith("< No response"):
        return None
    try:
        return json.loads(curl_response)
    except json.JSONDecodeError:
        return None


def parse_error(curl_response):
    body = parse_json_response(curl_response)
    if not isinstance(body, dict):
        return None
    error = body.get("error")
    return error if isinstance(error, dict) else None


def parse_power_state(curl_response):
    result = parse_result(curl_response)
    return result if isinstance(result, dict) else None


def parse_time_since_wakeup(curl_response):
    result = parse_result(curl_response)
    if isinstance(result, bool):
        return None
    if isinstance(result, int):
        return result
    if isinstance(result, dict):
        value = result.get("secondsSinceWakeup")
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def parse_wakeup_config(curl_response):
    result = parse_result(curl_response)
    return result if isinstance(result, list) else None


def parse_last_wakeup_reason(curl_response):
    result = parse_result(curl_response)
    if isinstance(result, str) and result:
        return result
    if isinstance(result, dict):
        value = result.get("wakeupReason")
        if isinstance(value, str) and value:
            return value
    return None


def parse_last_wakeup_keycode(curl_response):
    result = parse_result(curl_response)
    if isinstance(result, int) and not isinstance(result, bool):
        return result
    if isinstance(result, dict):
        value = result.get("keyCode", result.get("keycode"))
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return None


def parse_network_standby(curl_response):
    result = parse_result(curl_response)
    if isinstance(result, bool):
        return result
    if isinstance(result, dict):
        for key in ("standbyMode", "nwStandby", "enabled"):
            value = result.get(key)
            if isinstance(value, bool):
                return value
    return None


def wakeup_map(config_list):
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


def get_source_enabled(config_list, source_name):
    return wakeup_map(config_list).get(source_name)


def non_network_map(config_list):
    mapping = wakeup_map(config_list)
    return {key: value for key, value in mapping.items() if key not in ("WIFI", "LAN")}


def note_partial(reason):
    if STRICT_PARTIALS:
        return False
    log_warning(f"Partial coverage accepted: {reason}")
    return True


def can_manage_ignoredeepsleep_file():
    return TARGET_HOST in ("127.0.0.1", "localhost") and os.name != "nt"