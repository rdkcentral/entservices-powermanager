"""
/**
 * @file TCID28_WakeupSourceNetworkStandbySync.py
 * @brief L3 PowerManager functional testcase for the wakeup-source -> network-standby sync.
 *
 * @testcase TCID28_WakeupSourceNetworkStandbySync
 * @details Drives setWakeupSourceConfig with WIFI and LAN set to the SAME state.
 *          This is the only path that triggers the automatic network-standby
 *          reconciliation inside PowerManagerImplementation::setWakeupSourceConfig
 *          (isWakeupSrcEnabled(WIFI/LAN) -> GetNetworkStandbyMode ->
 *          SetNetworkStandbyMode). No other registered test exercises this
 *          direction (the combination scenario runner that did is not in the suite).
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
 *  - Setting WIFI+LAN enabled then disabled is handled and the plugin stays
 *    responsive; the original wakeup configuration is restored best-effort.
 *
 * @pass_criteria
 *  - Both configuration writes return a response and the post-check succeeds.
 *
 * @failure_criteria
 *  - Any write yields no response, or the plugin becomes unresponsive.
 */
"""

import time
import os

from utils import send_curl_command, parse_result, is_ok, log_info, log_success, log_error, log_warning
import PowerManager_Curl as PowerManagerApis


def _responded(resp):
    return bool(resp) and not resp.startswith("< No response")


def _config_list(result):
    if isinstance(result, dict):
        cfg = result.get("wakeupSources")
        if isinstance(cfg, list):
            return cfg
    if isinstance(result, list):
        return result
    return None


def run_test():
    start_time = time.perf_counter()

    # Snapshot current wakeup-source configuration for best-effort restore.
    baseline_resp = send_curl_command(PowerManagerApis.get_wakeup_source_config)
    log_warning(f"Baseline wakeup config: {baseline_resp}")
    baseline = _config_list(parse_result(baseline_resp))

    # 1. WIFI + LAN both ENABLED -> drives the nwStandby reconciliation (both equal).
    log_info("Executing setWakeupSourceConfig (WIFI+LAN enabled)")
    resp_on = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "WIFI", "enabled": True},
            {"wakeupSource": "LAN", "enabled": True},
        ])
    )
    log_warning(f"Response: {resp_on}")
    if not _responded(resp_on):
        log_error("TCID28_WakeupSourceNetworkStandbySync Failed ❌ (no response for WIFI+LAN enable)")
        return False

    # 2. WIFI + LAN both DISABLED -> drives the reconciliation in the other direction.
    log_info("Executing setWakeupSourceConfig (WIFI+LAN disabled)")
    resp_off = send_curl_command(
        PowerManagerApis.set_wakeup_source_config([
            {"wakeupSource": "WIFI", "enabled": False},
            {"wakeupSource": "LAN", "enabled": False},
        ])
    )
    log_warning(f"Response: {resp_off}")
    if not _responded(resp_off):
        log_error("TCID28_WakeupSourceNetworkStandbySync Failed ❌ (no response for WIFI+LAN disable)")
        return False

    # Best-effort restore of the original configuration.
    if isinstance(baseline, list) and baseline:
        entries = [
            {"wakeupSource": e.get("wakeupSource"), "enabled": e.get("enabled")}
            for e in baseline
            if isinstance(e, dict) and e.get("wakeupSource") is not None and isinstance(e.get("enabled"), bool)
        ]
        if entries:
            send_curl_command(PowerManagerApis.set_wakeup_source_config(entries))

    # Health check: the plugin must remain responsive.
    log_info("Verifying plugin is still responsive via getPowerState")
    health = send_curl_command(PowerManagerApis.get_power_state)
    log_warning(f"Response: {health}")
    if not is_ok(health):
        log_error("TCID28_WakeupSourceNetworkStandbySync Failed ❌ (plugin unresponsive after sync)")
        return False
    log_success("✅ Wakeup-source / network-standby sync handled; plugin responsive")

    elapsed_time = time.perf_counter() - start_time
    msg = "TCID28_WakeupSourceNetworkStandbySync Passed ✅"
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
