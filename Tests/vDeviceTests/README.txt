PowerManager L2 vDevice Test Suite
==================================

JSON-RPC functional test suite for the org.rdk.PowerManager plugin, modelled on
the HdmiCecSource vDeviceTests suite.

EXECUTION
---------
cd Tests/vDeviceTests

with timing   : python3 SuiteManager.py -t powermanager
without timing: python3 SuiteManager.py powermanager

Default Actions:
Plugin activation is done by default before suite execution:
- powermanager -> Controller.1.activate(callsign=org.rdk.PowerManager)
- Init_PowerManager_Populate is run to establish a known baseline.

Disable default activation only if needed:
- export AUTO_ACTIVATE_PLUGINS=0

ENDPOINTS / DEFAULTS
--------------------
- MW JSON-RPC: http://127.0.0.1:9998/jsonrpc
- vComponent API (scenario hooks - TODO): http://127.0.0.1:8080/api/postKVP

Useful overrides:
- TARGET_HOST (applies to both endpoints)
- JSONRPC_PORT
- VCOMPONENT_PORT
- WPEFRAMEWORK_JSONRPC_URL (full URL, highest priority)
- VCOMPONENT_API_URL (full URL, highest priority)

Examples:

# inside QEMU guest (services on localhost)
python3 SuiteManager.py powermanager

# from host against QEMU target IP
export TARGET_HOST=192.168.1.50
export JSONRPC_PORT=9998
python3 SuiteManager.py powermanager

SCENARIO HOOKS (TODO)
---------------------
This first version is JSON-RPC driven only. The vComponent (deepsleep) runtime
event-injection mechanism is not yet wired up. utils.send_vcomponent_command()
mirrors the HdmiCec postKVP contract so multi-step scenario tests (e.g. deepsleep
timeout wakeup, thermal-mode transitions) can be added later without changing the
test harness. Verify the endpoint/contract before relying on it.

NOTES
-----
- JSON-RPC parameter names in PowerManager_Curl.py follow the IPowerManager
  interface signatures. If a target build exposes different parameter spelling,
  adjust the builders in PowerManager_Curl.py (single source of truth).

Troubleshooting:
- On "connection refused", verify WPEFramework JSON-RPC is reachable using the
  endpoint overrides above.
