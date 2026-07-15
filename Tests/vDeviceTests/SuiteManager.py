"""
/**
 * @file SuiteManager.py
 * @brief SuiteManager.py
 *
 * @testcase SuiteManager
 * @details Orchestrates the PowerManager L2 test suite by dynamically loading and executing
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
import time
from pathlib import Path
import os

from utils import log_error, log_info, log_success, send_jsonrpc_command, WPEFRAMEWORK_JSONRPC_URL


BASE_DIR = Path(__file__).resolve().parent
SUITES = {
    "powermanager": {
        "banner": "******************** L2 SUITE - RDK - POWER MANAGER ****************************",
        "module_dir": BASE_DIR / "Testcases",
        "tests": [
            "TCID01_Get_Power_State",
            "TCID03_Get_Overtemp_Grace_Interval",
            "TCID04_Get_Thermal_State",
            "TCID05_Get_Last_Wakeup_Reason",
            "TCID06_Get_Last_Wakeup_Keycode",
            "TCID07_Get_Network_Standby_Mode",
            "TCID08_Get_Wakeup_Source_Config",
            "TCID09_Get_Power_State_Before_Reboot",
            "TCID10_Get_Time_Since_Wakeup",
            "TCID11_Set_Overtemp_Grace_Interval",
            "TCID12_Set_Network_Standby_Mode",
            "TCID13_Set_Power_State",
            "TCID14_Set_Wakeup_Source_Config",
            "TCID15_Delay_Power_Mode_Change_By",
            "TCID16_Reboot",
        ],
    },
}

# Maps test suite names to their corresponding RDK plugin callsigns for activation
SUITE_PLUGIN_CALLSIGNS = {
    "powermanager": "org.rdk.PowerManager",
}

SUITE_INIT_MODULES = {
    "powermanager": "Init_PowerManager_Populate",
}


def normalize_suite_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def load_test_cases(suite_name):
    suite_config = SUITES[suite_name]
    module_dir = str(suite_config["module_dir"])

    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)

    test_cases = []
    for module_name in suite_config["tests"]:
        module = importlib.import_module(module_name)
        test_cases.append((module_name, module.run_test))

    return suite_config["banner"], test_cases


def activate_plugin_via_curl(callsign):
    response = send_jsonrpc_command(
        "Controller.1.activate",
        params={"callsign": callsign},
        request_id=1234567890,
    )
    if not response:
        return False
    if "error" in response:
        return False
    return "result" in response


def run_suite_init(suite_name):
    module_name = SUITE_INIT_MODULES.get(suite_name)
    if not module_name:
        return True

    if BASE_DIR.as_posix() not in sys.path:
        sys.path.insert(0, str(BASE_DIR))

    try:
        module = importlib.import_module(module_name)
    except Exception as exc:
        log_error(f"Init module import failed: {module_name} ({exc})")
        return False

    run_fn = getattr(module, "run_test", None)
    if not callable(run_fn):
        log_error(f"Init module missing run_test(): {module_name}")
        return False

    log_info(f"Running suite initialization: {module_name}.run_test()")
    try:
        ok = bool(run_fn())
    except Exception as exc:
        log_error(f"Suite initialization threw exception: {exc}")
        return False

    if ok:
        log_success("Suite initialization completed successfully")
    else:
        log_error("Suite initialization failed")
    return ok


def run_suite(suite_name):
    banner, test_cases = load_test_cases(suite_name)
    print(banner)

    auto_activate = os.environ.get("AUTO_ACTIVATE_PLUGINS", "1").lower() not in ("0", "false", "no")
    callsign = SUITE_PLUGIN_CALLSIGNS.get(suite_name)
    if auto_activate and callsign:
        log_info(f"Auto-activating plugin '{callsign}' via curl JSON-RPC at {WPEFRAMEWORK_JSONRPC_URL}")
        if activate_plugin_via_curl(callsign):
            log_success(f"Plugin activated: {callsign}")
            log_info("Waiting 6s for plugin to fully initialise...")
            time.sleep(6)
        else:
            log_error(f"Plugin activation failed: {callsign}")
            log_error("Check JSON-RPC endpoint reachability and plugin availability before running tests.")
            return False

    if not run_suite_init(suite_name):
        log_error("Aborting suite because initialization did not complete successfully.")
        return False

    passed = 0
    failed = 0
    failed_cases = []
    original_stdout = sys.stdout

    for tc_name, tc_fn in test_cases:
        log_info(f"\n{'='*60}")
        log_info(f"Running: {tc_name}")
        log_info(f"{'='*60}")
        captured = io.StringIO()
        sys.stdout = captured
        try:
            result = tc_fn()
        except Exception as exc:
            result = False
            print(f"EXCEPTION in {tc_name}: {exc}")
        finally:
            sys.stdout = original_stdout

        output = captured.getvalue()
        print(output, end="")

        if result:
            passed += 1
            log_success(f"[PASS] {tc_name}")
        else:
            failed += 1
            failed_cases.append(tc_name)
            log_error(f"[FAIL] {tc_name}")

        time.sleep(1)

    log_info(f"\n{'='*60}")
    log_info(f"Suite Summary: {passed} passed, {failed} failed")
    if failed_cases:
        log_error(f"Failed cases: {failed_cases}")
    log_info(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Run PowerManager test suites")
    parser.add_argument("suite", help=f"Test suite name. Available: {list(SUITES.keys())}")
    parser.add_argument("-t", "--timing", action="store_true", help="Enable timing output for passed test cases")

    args = parser.parse_args()

    if args.timing:
        os.environ["POWERMANAGER_TIMING_ENABLED"] = "1"

    suite_arg = normalize_suite_name(args.suite)
    matching = [k for k in SUITES if normalize_suite_name(k) == suite_arg]
    if not matching:
        log_error(f"Unknown suite '{args.suite}'. Available: {list(SUITES.keys())}")
        sys.exit(1)

    ok = run_suite(matching[0])
    sys.exit(0 if ok else 1)
