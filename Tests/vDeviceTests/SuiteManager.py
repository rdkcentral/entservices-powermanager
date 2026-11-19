"""
/**
 * @file SuiteManager.py
 * @brief SuiteManager.py
 *
 * @testcase SuiteManager
 * @details Orchestrates the PowerManager L3 test suite by dynamically loading and executing
 *          test case modules, activating the required RDK plugin via JSON-RPC, and reporting
 *          per-test pass/fail results with summary statistics.
 *
 * @precondition
 *  - WPEFramework is running and reachable at the configured JSON-RPC endpoint.
 *  - The org.rdk.PowerManager plugin is available for activation.
 *  - All test case modules listed in SUITES are present under the Testcases/ directory.
 *
 * @dependencies
 *  - utils.py
 *  - PowerManager_Curl.py
 *  - Testcases/*.py
 *
 * @expected_result
 *  - All registered test cases are executed in order and results are logged.
 *
 * @pass_criteria
 *  - Each test case module's run_test() returns True and is reported as PASSED.
 *
 * @failure_criteria
 *  - Any test case returns False, raises an exception, or the plugin fails to activate.
 */
"""

import importlib
import io
import sys
from pathlib import Path
import os

from utils import (
    PLUGIN_READY_TIMEOUT,
    SKIPPED,
    WPEFRAMEWORK_JSONRPC_URL,
    activate_plugin,
    deactivate_plugin,
    log_error,
    log_info,
    log_success,
    log_warning,
    wait_for_plugin_ready,
)


BASE_DIR = Path(__file__).resolve().parent
SUITES = {
    "powermanager": {
        "banner": "******************** L3 SUITE - RDK - POWER MANAGER ****************************",
        "module_dir": BASE_DIR / "Testcases",
        "prerequisite_callsigns": [],
        "plugin_callsigns": ["org.rdk.PowerManager"],
        "init_module": "Init_PowerManager_Populate",
        "lifecycle": "per_test",
        "tests": [
            "TCID01_DeepSleepNaturalWakeupTime",
            "TCID02_TimerWakeDisable",
            "TCID03_NetworkDeepSleepDisabled",
            "TCID04_AsymmetricDeepSleep",
            "TCID05_NetworkExternalWake",
            "TCID06_CECExternalWake",
            "TCID07_VoiceExternalWake",
            "TCID08_PresenceExternalWake",
            "TCID09_FrontPanelExternalWake",
            "TCID10_RCUBTExternalWake",
            "TCID11_RCUIRExternalWake",
            "TCID12_RCURF4CEExternalWake",
            "TCID13_WLANExternalWake",
            "TCID14_TimerExternalWakePrecedence",
            "TCID15_MixedTriggerPrecedence",
            "TCID16_IgnoreDeepSleep",
            "TCID17_InvalidWakeupUpdate",
            "TCID18_DeepSleepFlow",
            "TCID19_Get_Overtemp_Grace_Interval",
            "TCID20_Get_Thermal_State",
            "TCID21_Get_Power_State_Before_Reboot",
            "TCID22_Set_Overtemp_Grace_Interval",
            "TCID23_Delay_Power_Mode_Change_By",
            "TCID24_PowerModePreChangeClientLifecycle",
            "TCID25_InvalidParameterHandling",
            "TCID26_ActivePreChangeTransaction",
            "TCID27_TemperatureThresholds",
            "TCID28_WakeupSourceNetworkStandbySync",
            "TCID29_InvalidScenarios",
            "TCID30_ExternallyTriggeredReboot",
            "TCID31_Reboot",
        ],
    },
}

def normalize_suite_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def normalize_test_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def resolve_selected_tests(suite_name, requested_tests):
    if not requested_tests:
        return list(SUITES[suite_name]["tests"])

    available_tests = SUITES[suite_name]["tests"]
    resolved = []

    for requested in requested_tests:
        requested_norm = normalize_test_name(requested)
        matches = [name for name in available_tests if normalize_test_name(name) == requested_norm]
        if not matches:
            raise ValueError(f"Unknown test '{requested}'. Available tests: {available_tests}")
        resolved.extend(matches)

    return resolved


def load_test_cases(suite_name, selected_tests=None):
    suite_config = SUITES[suite_name]
    module_dir = str(suite_config["module_dir"])

    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)

    test_cases = []
    for module_name in resolve_selected_tests(suite_name, selected_tests):
        module = importlib.import_module(module_name)
        run_fn = getattr(module, "run_test", None)
        if not callable(run_fn):
            module_file = getattr(module, "__file__", "<unknown>")
            exported = [name for name in dir(module) if not name.startswith("__")]
            raise AttributeError(
                f"Module '{module_name}' from '{module_file}' does not expose callable run_test(). "
                f"Exported names: {exported}"
            )
        test_cases.append((module_name, run_fn))

    return suite_config["banner"], test_cases


def run_suite_init(suite_name):
    module_name = SUITES[suite_name].get("init_module")
    if not module_name:
        return True

    base_dir = str(BASE_DIR)
    if base_dir not in sys.path:
        sys.path.insert(0, base_dir)

    try:
        module = importlib.import_module(module_name)
    except Exception as exc:
        log_error(f"Init module import failed: {module_name} ({exc})")
        return False

    run_fn = getattr(module, "run_test", None)
    if not callable(run_fn):
        log_error(f"Init module missing run_test(): {module_name}")
        return False

    log_info(f"Running test initialization: {module_name}.run_test()")
    try:
        ok = run_fn() is True
    except Exception as exc:
        log_error(f"Suite initialization threw exception: {exc}")
        return False

    if ok:
        log_success("Test initialization completed successfully")
    else:
        log_error("Test initialization failed")
    return ok


def activate_test_plugins(suite_name):
    suite_config = SUITES[suite_name]
    callsigns = list(suite_config["prerequisite_callsigns"])
    callsigns.extend(suite_config["plugin_callsigns"])

    activated = []
    for callsign in callsigns:
        log_info(
            f"Auto-activating plugin '{callsign}' "
            f"via curl JSON-RPC at {WPEFRAMEWORK_JSONRPC_URL}"
        )
        try:
            activation_ok = activate_plugin(callsign)
        except Exception as exc:
            log_error(f"Plugin activation raised an exception: {callsign} ({exc})")
            return False, activated
        if not activation_ok:
            log_error(f"Plugin activation failed: {callsign}")
            return False, activated

        activated.append(callsign)
        log_success(f"Plugin activated: {callsign}")
        log_info(
            f"Waiting up to {PLUGIN_READY_TIMEOUT}s for plugin readiness..."
        )
        try:
            ready = wait_for_plugin_ready(callsign)
        except Exception as exc:
            log_error(f"Plugin readiness check raised an exception: {callsign} ({exc})")
            return False, activated
        if not ready:
            log_error(f"Plugin did not become ready: {callsign}")
            return False, activated
        log_success(f"Plugin ready: {callsign}")

    return True, activated


def deactivate_test_plugins(callsigns):
    success = True
    for callsign in reversed(callsigns):
        log_info(f"Auto-deactivating plugin '{callsign}'")
        try:
            deactivated = deactivate_plugin(callsign)
        except Exception as exc:
            log_error(f"Plugin deactivation raised an exception: {callsign} ({exc})")
            success = False
            continue
        if deactivated:
            log_success(f"Plugin deactivated: {callsign}")
        else:
            log_error(f"Plugin deactivation failed: {callsign}")
            success = False
    return success


def run_suite(suite_name, selected_tests=None):
    suite_config = SUITES[suite_name]
    lifecycle = suite_config.get("lifecycle")
    if lifecycle != "per_test":
        raise ValueError(
            f"Unsupported lifecycle '{lifecycle}' for suite '{suite_name}'"
        )

    banner, test_cases = load_test_cases(suite_name, selected_tests)
    print(banner)

    auto_activate = (
        os.environ.get("AUTO_ACTIVATE_PLUGINS", "1").lower()
        not in ("0", "false", "no")
    )
    log_info(f"Plugin lifecycle: {lifecycle}")
    log_info(
        f"Auto plugin activation: {'enabled' if auto_activate else 'disabled'}"
    )
    log_info(f"Plugin readiness timeout: {PLUGIN_READY_TIMEOUT}s")

    passed = 0
    failed = 0
    skipped = 0
    skipped_cases = []
    failed_cases = []
    original_stdout = sys.stdout
    for tc_name, tc_fn in test_cases:
        log_info(f"\n{'='*60}")
        log_info(f"Running: {tc_name}")
        log_info(f"{'='*60}")
        activated_callsigns = []
        setup_ok = True
        cleanup_ok = True
        result = False

        try:
            if auto_activate:
                setup_ok, activated_callsigns = activate_test_plugins(suite_name)
                if not setup_ok:
                    log_error(
                        f"{tc_name} setup failed because plugin activation or "
                        "readiness did not complete."
                    )

            if setup_ok and not run_suite_init(suite_name):
                setup_ok = False
                log_error(
                    f"{tc_name} setup failed because initialization did not "
                    "complete."
                )

            if setup_ok:
                captured = io.StringIO()
                sys.stdout = captured
                try:
                    result = tc_fn()
                except Exception as exc:
                    print(f"EXCEPTION in {tc_name}: {exc}")
                finally:
                    sys.stdout = original_stdout
                print(captured.getvalue(), end="")
        except Exception as exc:
            setup_ok = False
            log_error(f"{tc_name} setup or execution raised an exception: {exc}")
        finally:
            try:
                if auto_activate:
                    cleanup_ok = deactivate_test_plugins(activated_callsigns)
                    if not cleanup_ok:
                        log_error(f"{tc_name} cleanup failed")
            except Exception as exc:
                cleanup_ok = False
                log_error(f"{tc_name} cleanup raised an exception: {exc}")
            finally:
                sys.stdout = original_stdout

        if setup_ok and cleanup_ok and result == SKIPPED:
            skipped += 1
            skipped_cases.append(tc_name)
            log_warning(f"[SKIP] {tc_name}")
        elif setup_ok and cleanup_ok and result is True:
            passed += 1
            log_success(f"[PASS] {tc_name}")
        else:
            failed += 1
            failed_cases.append(tc_name)
            log_error(f"[FAIL] {tc_name}")

    log_info(f"\n{'='*60}")
    log_info(
        f"Suite Summary: {passed} passed, {failed} failed, {skipped} skipped"
    )
    if skipped_cases:
        log_warning(f"Skipped cases: {skipped_cases}")
    if failed_cases:
        log_error(f"Failed cases: {failed_cases}")
    log_info(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Run PowerManager test suites")
    parser.add_argument("suite", help=f"Test suite name. Available: {list(SUITES.keys())}")
    parser.add_argument("-t", "--timing", action="store_true", help="Enable timing output for passed test cases")
    parser.add_argument(
        "--test",
        dest="tests",
        action="append",
        help="Run only the named testcase module. Repeat to run multiple specific tests.",
    )

    args = parser.parse_args()

    if args.timing:
        os.environ["POWERMANAGER_TIMING_ENABLED"] = "1"

    suite_arg = normalize_suite_name(args.suite)
    matching = [k for k in SUITES if normalize_suite_name(k) == suite_arg]
    if not matching:
        log_error(f"Unknown suite '{args.suite}'. Available: {list(SUITES.keys())}")
        sys.exit(1)

    try:
        ok = run_suite(matching[0], args.tests)
    except (ValueError, AttributeError) as exc:
        log_error(str(exc))
        sys.exit(1)
    sys.exit(0 if ok else 1)

