"""
/**
 * @file TCID20_PowerModeNotify.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID20_PowerModeNotify
 * @details Registers for onPowerModeChanged and validates the final getter
 *          state after a power-mode transition.
 */
"""

import os
import time

from utils import send_curl_command, is_ok, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis
from PowerManager_CombinationHelpers import note_partial, parse_power_state


def run_test():
    start_time = time.perf_counter()

    register_resp = send_curl_command(PowerManagerApis.register_event("onPowerModeChanged", "pm_plugin_004_changed"))
    log_warning(f"Register response: {register_resp}")
    if not is_ok(register_resp):
        log_error("TCID20_PowerModeNotify Failed ❌ (event registration failed)")
        return False

    on_resp = send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-004"))
    log_warning(f"ON response: {on_resp}")
    if not is_ok(on_resp):
        log_error("TCID20_PowerModeNotify Failed ❌ (failed to set ON)")
        return False

    light_resp = send_curl_command(PowerManagerApis.set_power_state("LIGHT_SLEEP", standby_reason="PM-PLUGIN-004"))
    log_warning(f"LIGHT_SLEEP response: {light_resp}")
    if not is_ok(light_resp):
        log_error("TCID20_PowerModeNotify Failed ❌ (failed to set LIGHT_SLEEP)")
        return False

    time.sleep(1)
    state_resp = send_curl_command(PowerManagerApis.get_power_state)
    log_warning(f"Read-back state response: {state_resp}")
    state = parse_power_state(state_resp)
    if not isinstance(state, dict) or state.get("currentState") != "LIGHT_SLEEP":
        log_error("TCID20_PowerModeNotify Failed ❌ (expected LIGHT_SLEEP state)")
        send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-004-restore"))
        return False

    note_partial("Async onPowerModeChanged delivery is not asserted by this curl-only testcase.")
    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-004-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID20_PowerModeNotify Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True