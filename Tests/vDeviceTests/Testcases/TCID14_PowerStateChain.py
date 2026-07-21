"""
/**
 * @file TCID14_PowerStateChain.py
 * @brief L2 PowerManager combination testcase.
 *
 * @testcase TCID14_PowerStateChain
 * @details Validates previousState/currentState transitions across a chained
 *          setPowerState sequence, while keeping curl-only coverage of
 *          onPowerModeChanged event registration.
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
        log_error("TCID14_PowerStateChain Failed ❌ (event registration failed)")
        return False
    note_partial("Async onPowerModeChanged delivery is not asserted by this curl-only testcase.")

    for state_name in ("ON", "LIGHT_SLEEP", "ON", "STANDBY"):
        response = send_curl_command(PowerManagerApis.set_power_state(state_name, standby_reason="PM-PLUGIN-003"))
        log_warning(f"{state_name} response: {response}")
        if not is_ok(response):
            log_error(f"TCID14_PowerStateChain Failed ❌ (failed to set {state_name})")
            send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-003-restore"))
            return False
        time.sleep(1)

        if state_name == "LIGHT_SLEEP":
            state_resp = send_curl_command(PowerManagerApis.get_power_state)
            log_warning(f"LIGHT_SLEEP state response: {state_resp}")
            state = parse_power_state(state_resp)
            if not isinstance(state, dict) or state.get("currentState") != "LIGHT_SLEEP" or state.get("previousState", state.get("prevState")) != "ON":
                log_error("TCID14_PowerStateChain Failed ❌ (ON -> LIGHT_SLEEP chain mismatch)")
                send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-003-restore"))
                return False
        elif state_name == "ON":
            state_resp = send_curl_command(PowerManagerApis.get_power_state)
            log_warning(f"ON state response: {state_resp}")
            state = parse_power_state(state_resp)
            if not isinstance(state, dict) or state.get("currentState") != "ON":
                log_error("TCID14_PowerStateChain Failed ❌ (currentState mismatch for ON)")
                return False
        elif state_name == "STANDBY":
            state_resp = send_curl_command(PowerManagerApis.get_power_state)
            log_warning(f"STANDBY state response: {state_resp}")
            state = parse_power_state(state_resp)
            if not isinstance(state, dict) or state.get("currentState") != "STANDBY" or state.get("previousState", state.get("prevState")) != "ON":
                log_error("TCID14_PowerStateChain Failed ❌ (ON -> STANDBY chain mismatch)")
                send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-003-restore"))
                return False

    send_curl_command(PowerManagerApis.set_power_state("ON", standby_reason="PM-PLUGIN-003-restore"))

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID14_PowerStateChain Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True