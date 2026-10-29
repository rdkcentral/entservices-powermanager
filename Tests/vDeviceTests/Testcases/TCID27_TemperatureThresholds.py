"""
/**
 * @file TCID27_TemperatureThresholds.py
 * @brief L3 PowerManager functional testcase for temperature threshold get/set.
 *
 * @testcase TCID27_TemperatureThresholds
 * @details Exercises getTemperatureThresholds and setTemperatureThresholds,
 *          which no other active-plugin test covers (the combination scenario
 *          runner that used them is not registered in the suite). Targets
 *          PowerManagerImplementation::GetTemperatureThresholds and
 *          SetTemperatureThresholds.
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
 *  - Each call returns a JSON-RPC response. When the platform supports thermal
 *    protection, the written thresholds read back unchanged.
 *
 * @pass_criteria
 *  - All calls return a parseable response; when the setter succeeds the
 *    read-back matches the written values.
 *
 * @failure_criteria
 *  - Any call yields no response, or a successful setter fails to persist.
 *
 * @note On platforms without thermal protection the thermal controller returns
 *       an error; the impl methods still execute, so the test only requires a
 *       response (it does not force success).
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


TEST_HIGH = 100.0
TEST_CRITICAL = 110.0


def _responded(resp):
    return bool(resp) and not resp.startswith("< No response")


def _thresholds(result):
    """Return (high, critical) from a getTemperatureThresholds result, or (None, None)."""
    if isinstance(result, dict):
        return result.get("high"), result.get("critical")
    return None, None


def run_test():
    start_time = time.perf_counter()

    # Read current thresholds (exercises GetTemperatureThresholds).
    log_info("Executing getTemperatureThresholds (baseline)")
    baseline_resp = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    log_warning(f"Response: {baseline_resp}")
    if not _responded(baseline_resp):
        log_error("TCID27_TemperatureThresholds Failed ❌ (no response to getTemperatureThresholds)")
        return False
    orig_high, orig_critical = _thresholds(parse_result(baseline_resp))

    # Write new thresholds (exercises SetTemperatureThresholds).
    log_info("Executing setTemperatureThresholds")
    set_resp = send_curl_command(PowerManagerApis.set_temperature_thresholds(TEST_HIGH, TEST_CRITICAL))
    log_warning(f"Response: {set_resp}")
    if not _responded(set_resp):
        log_error("TCID27_TemperatureThresholds Failed ❌ (no response to setTemperatureThresholds)")
        return False

    # Read back (exercises GetTemperatureThresholds again).
    log_info("Executing getTemperatureThresholds (read-back)")
    readback_resp = send_curl_command(PowerManagerApis.get_temperature_thresholds)
    log_warning(f"Response: {readback_resp}")
    if not _responded(readback_resp):
        log_error("TCID27_TemperatureThresholds Failed ❌ (no response to read-back)")
        return False

    # When the setter succeeded on a thermal-capable platform, verify persistence.
    if is_ok(set_resp):
        high, critical = _thresholds(parse_result(readback_resp))
        if high is not None and critical is not None:
            if high != TEST_HIGH or critical != TEST_CRITICAL:
                log_error(f"TCID27_TemperatureThresholds Failed ❌ (read-back mismatch: {high}/{critical})")
                return False
            log_success("✅ Thresholds persisted and read back correctly")
        # Restore the original thresholds when they were readable numbers.
        if isinstance(orig_high, (int, float)) and isinstance(orig_critical, (int, float)):
            send_curl_command(PowerManagerApis.set_temperature_thresholds(orig_high, orig_critical))
    else:
        log_success("✅ Thermal thresholds handled gracefully (platform reported not supported)")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID27_TemperatureThresholds Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
