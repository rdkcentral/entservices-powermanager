PowerManager L3 vDevice Test Suite
==================================

JSON-RPC functional test suite for the org.rdk.PowerManager plugin.

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
- vComponent API (DeepSleep simulation default): http://127.0.0.1:8081/api/postKVP

Useful overrides:
- TARGET_HOST (applies to both endpoints)
- JSONRPC_PORT
- DEEPSLEEP_VCOMPONENT_PORT
- BOOT_VCOMPONENT_PORT
- WPEFRAMEWORK_JSONRPC_URL (full URL, highest priority)
- VCOMPONENT_API_URL (full URL, highest priority)

Examples:

# inside QEMU guest (services on localhost)
python3 SuiteManager.py powermanager

# from host against QEMU target IP
1. Export enviroment variables from host machine:
    export TARGET_HOST=127.0.0.1
    export JSONRPC_PORT=9998
    export DEEPSLEEP_VCOMPONENT_PORT=8081
    export BOOT_VCOMPONENT_PORT=9081
    python3 SuiteManager.py powermanager
2. Enable port forwarding to JSORPC port, DeepSleep & Boot vComponent ports via QEMU command:
   hostfwd=tcp:127.0.0.1:9998-:9998,hostfwd=tcp:127.0.0.1:8081-:8081,hostfwd=tcp:127.0.0.1:9081-:9081
3. From vDevice command shell, change WPEFramework's binding IP address in the config /etc/WPEFramework/config.json to:
    "binding":"0.0.0.0",
4. Restart vDevice instance

SCENARIO HOOKS (TODO)
---------------------
DeepSleep simulation uses the vcomponent control plane via
utils.send_vcomponent_command().
The default control-plane port is 8081 for the DeepSleep service; override with DEEPSLEEP_VCOMPONENT_PORT if your
target launches the service on a different port.
The default control-plane port is 9081 for the Boot service; override with BOOT_VCOMPONENT_PORT if your
target launches the service on a different port.

NOTES
-----
- JSON-RPC parameter names in PowerManager_Curl.py follow the IPowerManager
  interface signatures. If a target build exposes different parameter spelling,
  adjust the builders in PowerManager_Curl.py (single source of truth).

Troubleshooting:
- On "connection refused", verify WPEFramework JSON-RPC is reachable using the
  endpoint overrides above.
