"""
/**
 * @file TCID25_InvalidParameterHandling.py
 * @brief L3 PowerManager robustness testcase for malformed setter parameters.
 *
 * @testcase TCID25_InvalidParameterHandling
 * @details Sends setter requests with misspelled JSON parameter keys that no
 *          existing test case exercises:
 *            - setTemperatureThresholds  (wrong keys)
 *            - setOvertempGraceInterval  (wrong key)
 *            - setNetworkStandbyMode     (wrong key)
 *          Verifies the plugin handles each request gracefully (returns a valid
 *          JSON-RPC response and does not drop the connection) and remains
 *          healthy afterwards by reading getPowerState.
 *
 * @precondition
 *  - org.rdk.PowerManager plugin is active and reachable via JSON-RPC endpoint.
 *
 * @dependencies
 *  - utils.py
 *  - PowerManager_Curl.py
 *  - SuiteManager.py
 *
 * @expected_result
 *  - Each malformed request yields a parseable JSON-RPC response and the plugin
 *    stays responsive (getPowerState succeeds afterwards).
 *
 * @pass_criteria
 *  - All malformed requests return a valid response and the post-check succeeds.
 *
 * @failure_criteria
 *  - Any malformed request yields no response, or the plugin becomes unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def _responded(resp):
    """True when the plugin returned a JSON body (not the no-response sentinel)."""
    return bool(resp) and not resp.startswith("< No response")


def run_test():
    start_time = time.perf_counter()

    malformed_calls = [
        ("setTemperatureThresholds (malformed keys)", PowerManagerApis.set_temperature_thresholds_invalid),
        ("setOvertempGraceInterval (malformed key)", PowerManagerApis.set_overtemp_grace_interval_invalid),
        ("setNetworkStandbyMode (malformed key)", PowerManagerApis.set_network_standby_mode_invalid),
    ]

    for label, cmd in malformed_calls:
        log_info(f"Executing {label}")
        resp = send_curl_command(cmd)
        log_warning(f"Response: {resp}")
        if not _responded(resp):
            log_error(f"TCID25_InvalidParameterHandling Failed ❌ (no response for {label})")
            return False
        log_success(f"✅ Handled gracefully: {label}")

    # Health check: the plugin must remain responsive after malformed input.
    log_info("Verifying plugin is still responsive via getPowerState")
    health = send_curl_command(PowerManagerApis.get_power_state)
    log_warning(f"Response: {health}")
    if not is_ok(health):
        log_error("TCID25_InvalidParameterHandling Failed ❌ (plugin unresponsive after malformed input)")
        return False
    log_success("✅ Plugin responsive after malformed input")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID25_InvalidParameterHandling Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
