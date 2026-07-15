"""
/**
 * @file utils.py
 * @brief utils.py
 *
 * @testcase utils
 * @details Provides shared utility functions and constants used across all PowerManager
 *          test cases, including JSON-RPC command dispatch, vComponent YAML execution
 *          (scenario hooks - TODO), curl-based API invocation, and structured
 *          pass/fail logging helpers.
 *
 * @precondition
 *  - WPEFramework JSON-RPC endpoint is reachable at WPEFRAMEWORK_JSONRPC_URL.
 *
 * @dependencies
 *  - Standard Python libraries: os, json, subprocess, tempfile, re, pathlib
 *
 * @expected_result
 *  - Utility functions execute without error and return structured results to callers.
 *
 * @pass_criteria
 *  - All helper functions return expected data types and callers receive valid responses.
 *
 * @failure_criteria
 *  - Subprocess errors, JSON parse failures, missing YAML files, or unreachable endpoints.
 */
"""

import os
import json
import subprocess
from pathlib import Path


# Base paths for vComponent YAML commands (scenario hooks - TODO).
# Prefer testcase-local YAMLs, fallback to /etc paths, and allow env overrides.
_BASE_DIR = Path(__file__).resolve().parent
_LOCAL_POWERMANAGER_CMD_BASE = _BASE_DIR / "vcomponent_configurations" / "commands"


def _pick_existing_dir(primary, fallback):
    if primary.is_dir():
        return str(primary)
    return fallback


POWERMANAGER_CMD_BASE = os.environ.get("POWERMANAGER_CMD_BASE") or _pick_existing_dir(
    _LOCAL_POWERMANAGER_CMD_BASE,
    "/etc/powermanager/vcomponent_configurations/commands",
)

# Endpoint selection for local/QEMU execution.
# - TARGET_HOST sets both MW and vComponent host in one place.
# - Explicit URL env vars take precedence.
TARGET_HOST = os.environ.get("TARGET_HOST", "127.0.0.1")
JSONRPC_PORT = os.environ.get("JSONRPC_PORT", "9998")
VCOMPONENT_PORT = os.environ.get("VCOMPONENT_PORT", "8080")
WPEFRAMEWORK_JSONRPC_URL = (
    os.environ.get("WPEFRAMEWORK_JSONRPC_URL")
    or os.environ.get("JSONRPC_URL")
    or f"http://{TARGET_HOST}:{JSONRPC_PORT}/jsonrpc"
)
VCOMPONENT_API_URL = (
    os.environ.get("VCOMPONENT_API_URL")
    or f"http://{TARGET_HOST}:{VCOMPONENT_PORT}/api/postKVP"
)


# ---------- ANSI COLOR CONSTANTS ----------
RESET = "\033[0m"
BOLD = "\033[1m"

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
CYAN = "\033[96m"

# ---------- OPTIONAL LOG HELPERS ----------
def log_info(msg):
    print(f"{CYAN}{msg}{RESET}")

def log_success(msg):
    print(f"{GREEN}{BOLD}{msg}{RESET}")

def log_warning(msg):
    print(f"{YELLOW}{msg}{RESET}")

def log_error(msg):
    print(f"{RED}{BOLD}{msg}{RESET}")


def log_with_timing(msg, elapsed_time):
    '''Log a message with timing info, only if POWERMANAGER_TIMING_ENABLED is set.
    Args:
        msg: Base message without timing
        elapsed_time: Elapsed time in seconds (float)
    Returns:
        Message with timing if timing is enabled, base message otherwise
    '''
    if os.environ.get("POWERMANAGER_TIMING_ENABLED"):
        return f"{msg} time consumed: {elapsed_time:.3f}s"
    return msg


def send_jsonrpc_command(method, params=None, request_id=1, timeout=5):
    '''Send a JSON-RPC request to WPEFramework and return parsed response dict.
    Returns None when request fails or response is not JSON.
    '''
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
    }
    if params is not None:
        payload["params"] = params

    cmd = [
        "curl", "-sS", "--max-time", str(timeout),
        "-H", "Content-Type: application/json",
        "-X", "POST",
        "--data", json.dumps(payload),
        WPEFRAMEWORK_JSONRPC_URL,
    ]

    try:
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            return None
        body = (result.stdout or "").strip()
        if not body:
            return None
        return json.loads(body)
    except Exception:
        return None


def activate_plugin(callsign):
    '''Activate an RDK plugin via Controller.1.activate.
    Returns True on success, False otherwise.
    '''
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


def send_curl_command(curl_command):
    '''This function is used to send the curl commands to get the output response using os module'''
    output_response = ""
    try:
        # Respect endpoint overrides even when curl strings hardcode localhost.
        if WPEFRAMEWORK_JSONRPC_URL:
            curl_command = curl_command.replace(
                "http://127.0.0.1:9998/jsonrpc", WPEFRAMEWORK_JSONRPC_URL
            )

        # Send the curl command using os.popen
        response = os.popen(curl_command)

        # Find the line that is a valid JSON for extracting only the json response
        for line in response.readlines():
            try:
                json.loads(line)
                output_response = line
                break
            except json.JSONDecodeError:
                pass

        # Add a message when the obtained output response is empty
        if len(output_response) < 5:
            output_response = "< No response from WPEFramework >"
    except Exception as exc:
        print(f"Inside Utils.py : Exception in send_curl_command function: {exc}")
    finally:
        return output_response


def send_vcomponent_command(yaml_file_path):
    '''Post a YAML command file to the vComponent HTTP API.

    NOTE (scenario hooks - TODO): The PowerManager/deepsleep vComponent runtime
    command mechanism is not yet wired up. This helper mirrors the HdmiCec
    postKVP contract so scenario (Group B) tests can be enabled later without
    changing the test case code. Verify the endpoint/contract before relying on it.

    Uses: curl -sS -X POST -H "Content-Type: application/x-yaml"
               --data-binary @<yaml_file> http://127.0.0.1:8080/api/postKVP
    Returns (http_code: int, body: str) tuple. http_code 200 indicates success.
    '''
    try:
        if not Path(yaml_file_path).is_file():
            return 0, f"YAML file not found: {yaml_file_path}"

        cmd = [
            "curl", "-sS", "-w", "\n%{http_code}",
            "-X", "POST",
            "-H", "Content-Type: application/x-yaml",
            "--data-binary", f"@{yaml_file_path}",
            VCOMPONENT_API_URL,
        ]
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout = result.stdout or ""
        parts = stdout.rsplit("\n", 1)
        if len(parts) == 2:
            body = parts[0]
            http_code_str = parts[1].strip()
        else:
            body = stdout.strip()
            http_code_str = "0"
        try:
            http_code = int(http_code_str)
        except ValueError:
            http_code = 0
        # Some vComponent builds close the connection without an HTTP response
        # after applying YAML (CURLE_GOT_NOTHING 52). Treat as accepted.
        if (
            http_code == 0
            and result.returncode == 52
            and "Empty reply from server" in (result.stderr or "")
        ):
            return 200, "Empty reply from server (accepted)"
        if http_code == 0 and result.stderr.strip():
            body = result.stderr.strip()
        return http_code, body
    except Exception as exc:
        return 0, f"Exception in send_vcomponent_command: {exc}"


def parse_result(curl_response):
    '''Parse a JSON-RPC curl response string and return the 'result' dict.
    Returns None on transport/parse error or when no result field is present.
    '''
    if not curl_response or curl_response.startswith("< No response"):
        return None
    try:
        body = json.loads(curl_response)
    except json.JSONDecodeError:
        return None
    if not isinstance(body, dict) or "result" not in body:
        return None
    return body.get("result")


def is_ok(curl_response):
    '''Return True if a JSON-RPC response indicates success.

    Setters/actions in the PowerManager interface return {"result": null} on
    success, which parse_result() cannot distinguish from a missing result. Use
    this helper to validate that a call succeeded: the response must be valid
    JSON containing a 'result' key (even when null) and no 'error' field.
    '''
    if not curl_response or curl_response.startswith("< No response"):
        return False
    try:
        body = json.loads(curl_response)
    except json.JSONDecodeError:
        return False
    if not isinstance(body, dict):
        return False
    return "result" in body and "error" not in body
