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
import time
import json
import base64
import hashlib
import socket
import ssl
import struct
import subprocess
from pathlib import Path
from urllib.parse import urlsplit


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
# DeepSleep vcomponent control plane defaults to 8081. Keep override support via
# DEEPSLEEP_VCOMPONENT_PORT / VCOMPONENT_API_URL for target-specific deployments.
DEEPSLEEP_VCOMPONENT_PORT = os.environ.get("DEEPSLEEP_VCOMPONENT_PORT", "8081")
BOOT_VCOMPONENT_PORT = os.environ.get("BOOT_VCOMPONENT_PORT", "8081")
WPEFRAMEWORK_JSONRPC_URL = (
    os.environ.get("WPEFRAMEWORK_JSONRPC_URL")
    or os.environ.get("JSONRPC_URL")
    or f"http://{TARGET_HOST}:{JSONRPC_PORT}/jsonrpc"
)
PLUGIN_READY_TIMEOUT = float(
    os.environ.get(
        "POWERMANAGER_PLUGIN_READY_TIMEOUT",
        os.environ.get(
            "POWERMANAGER_PLUGIN_INIT_WAIT",
            os.environ.get("PLUGIN_INIT_WAIT", "6"),
        ),
    )
)
if PLUGIN_READY_TIMEOUT < 0:
    raise ValueError("Plugin readiness timeout must be non-negative")
SKIPPED = "SKIPPED"
DEEPSLEEP_VCOMPONENT_API_URL = (
    os.environ.get("DEEPSLEEP_VCOMPONENT_API_URL")
    or f"http://{TARGET_HOST}:{DEEPSLEEP_VCOMPONENT_PORT}/api/postKVP"
)
BOOT_VCOMPONENT_API_URL = (
    os.environ.get("BOOT_VCOMPONENT_API_URL")
    or f"http://{TARGET_HOST}:{BOOT_VCOMPONENT_PORT}/api/postKVP"
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
def _emit_log(message):
    print(message, flush=True)


def log_info(msg):
    _emit_log(f"{CYAN}{msg}{RESET}")

def log_success(msg):
    _emit_log(f"{GREEN}{BOLD}{msg}{RESET}")

def log_warning(msg):
    _emit_log(f"{YELLOW}{msg}{RESET}")

def log_error(msg):
    _emit_log(f"{RED}{BOLD}{msg}{RESET}")


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


def activate_plugin(callsign, timeout_seconds=40):
    '''Activate an RDK plugin via Controller.1.activate.
    Returns True on success, False otherwise.
    '''
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        response = send_jsonrpc_command(
            "Controller.1.activate",
            params={"callsign": callsign},
            request_id=1234567890,
        )
        # _emit_log(f"activate_plugin response: {response}")
        if not response or "error" in response:
            time.sleep(1)
            continue
        return "result" in response
    return False


def wait_for_plugin_ready(callsign, timeout_seconds=PLUGIN_READY_TIMEOUT):
    '''Wait until the activated plugin responds to a safe JSON-RPC getter.'''
    deadline = time.monotonic() + timeout_seconds
    while True:
        response = send_jsonrpc_command(
            f"{callsign}.getPowerState",
            request_id=1234567890,
        )
        if (
            isinstance(response, dict)
            and "error" not in response
            and "result" in response
        ):
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(min(0.5, max(0, deadline - time.monotonic())))


def deactivate_plugin(callsign, timeout_seconds=40):
    '''Deactivate an RDK plugin via Controller.1.deactivate.
    Returns True on success, False otherwise.
    '''
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        response = send_jsonrpc_command(
            "Controller.1.deactivate",
            params={"callsign": callsign},
            request_id=1234567890,
        )
        if not response or "error" in response:
            time.sleep(1)
            continue
        return "result" in response
    return False


class _WebSocketConnection:
    """Minimal RFC 6455 text-frame client implemented with the standard library."""

    GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

    def __init__(self, connection):
        self.connection = connection
        self.buffer = bytearray()

    @classmethod
    def connect(cls, url, timeout):
        endpoint = urlsplit(url)
        secure = endpoint.scheme in ("https", "wss")
        host = endpoint.hostname
        if not host:
            raise ValueError(f"Invalid WebSocket endpoint: {url}")
        port = endpoint.port or (443 if secure else 80)
        path = endpoint.path or "/"
        if endpoint.query:
            path = f"{path}?{endpoint.query}"

        connection = socket.create_connection((host, port), timeout=timeout)
        if secure:
            connection = ssl.create_default_context().wrap_socket(
                connection, server_hostname=host
            )
        connection.settimeout(timeout)

        websocket = cls(connection)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {endpoint.netloc}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: jsonrpc\r\n\r\n"
        )
        connection.sendall(request.encode("ascii"))
        response = websocket._receive_headers()
        lines = response.decode("iso-8859-1").split("\r\n")
        if not lines or " 101 " not in lines[0]:
            status = lines[0] if lines else repr(response)
            raise ConnectionError(f"WebSocket upgrade rejected: {status}")

        headers = {}
        for line in lines[1:]:
            name, separator, value = line.partition(":")
            if separator:
                headers[name.strip().lower()] = value.strip()
        expected_accept = base64.b64encode(
            hashlib.sha1(f"{key}{cls.GUID}".encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected_accept:
            raise ConnectionError("WebSocket upgrade returned an invalid accept key")
        return websocket

    def _receive_headers(self):
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.connection.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed during WebSocket upgrade")
            self.buffer.extend(chunk)
            if len(self.buffer) > 65536:
                raise ConnectionError("WebSocket upgrade headers are too large")
        headers, remaining = bytes(self.buffer).split(b"\r\n\r\n", 1)
        self.buffer = bytearray(remaining)
        return headers

    def send_text(self, text):
        self._send_frame(0x1, text.encode("utf-8"))

    def _send_frame(self, opcode, payload=b""):
        mask = os.urandom(4)
        length = len(payload)
        header = bytearray([0x80 | opcode])
        if length < 126:
            header.append(0x80 | length)
        elif length <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", length))
        masked_payload = bytes(
            value ^ mask[index % 4] for index, value in enumerate(payload)
        )
        self.connection.sendall(bytes(header) + mask + masked_payload)

    def receive_text(self):
        fragments = bytearray()
        while True:
            first, second = self._receive_exact(2)
            final = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._receive_exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._receive_exact(8))[0]
            mask = self._receive_exact(4) if masked else None
            payload = self._receive_exact(length)
            if mask:
                payload = bytes(
                    value ^ mask[index % 4] for index, value in enumerate(payload)
                )

            if opcode == 0x8:
                raise ConnectionError("WebSocket closed by server")
            if opcode == 0x9:
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1:
                fragments = bytearray(payload)
            elif opcode == 0x0 and fragments:
                fragments.extend(payload)
            else:
                continue
            if final:
                return fragments.decode("utf-8")

    def _receive_exact(self, length):
        while len(self.buffer) < length:
            chunk = self.connection.recv(max(4096, length - len(self.buffer)))
            if not chunk:
                raise ConnectionError("WebSocket connection closed")
            self.buffer.extend(chunk)
        data = bytes(self.buffer[:length])
        del self.buffer[:length]
        return data

    def settimeout(self, timeout):
        self.connection.settimeout(timeout)

    def close(self):
        try:
            self._send_frame(0x8)
        except (OSError, ConnectionError):
            pass
        finally:
            self.connection.close()


class JsonRpcEventListener:
    """Receive Thunder JSON-RPC events over a persistent WebSocket."""

    def __init__(self, callsign, event_name, listener_id, timeout=8):
        self.callsign = callsign
        self.event_name = event_name
        self.listener_id = listener_id
        self.timeout = timeout
        self._socket = None

    def connect(self):
        try:
            self._socket = _WebSocketConnection.connect(
                WPEFRAMEWORK_JSONRPC_URL, self.timeout
            )
            request_id = 1
            self._socket.send_text(json.dumps({
                "jsonrpc": "2.0",
                "id": request_id,
                "method": f"{self.callsign}.1.register",
                "params": {"event": self.event_name, "id": self.listener_id},
            }))
            response = self._receive_until(
                lambda message: message.get("id") == request_id,
                self.timeout,
            )
            if not response or "error" in response or "result" not in response:
                log_error(f"Event registration rejected: {response}")
                self.close()
                return False
            return True
        except Exception as exc:
            log_error(f"WebSocket event registration failed: {exc}")
            self.close()
            return False

    def wait_for_event(self, predicate=None, timeout=None):
        timeout = self.timeout if timeout is None else timeout

        def matches(message):
            method = message.get("method", "")
            params = message.get("params")
            if not (
                isinstance(method, str)
                and (
                    method == self.event_name
                    or method.endswith(f".{self.event_name}")
                )
                and isinstance(params, dict)
            ):
                return False
            return predicate is None or predicate(params)

        message = self._receive_until(matches, timeout)
        return message.get("params") if message else None

    def _receive_until(self, predicate, timeout):
        if self._socket is None:
            return None

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._socket.settimeout(max(0.1, deadline - time.monotonic()))
            try:
                message = json.loads(self._socket.receive_text())
            except socket.timeout:
                return None
            except (json.JSONDecodeError, TypeError):
                continue
            if isinstance(message, dict) and predicate(message):
                return message
        return None

    def close(self):
        if self._socket is not None:
            try:
                self._socket.close()
            finally:
                self._socket = None


def send_curl_command(curl_command):
    '''This function is used to send the curl commands to get the output response using os module'''
    output_response = ""
    try:
        # Respect endpoint overrides even when curl strings hardcode localhost.
        # if WPEFRAMEWORK_JSONRPC_URL:
        #     curl_command = curl_command.replace(
        #         "http://127.0.0.1:9998/jsonrpc", WPEFRAMEWORK_JSONRPC_URL
        #     )

        # print(f"Utils.py curl_command : {curl_command}")

        # Send the curl command using os.popen
        result = subprocess.run(
            curl_command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        response = result.stdout or ""
        # Find the line that is a valid JSON for extracting only the json response
        for line in response.splitlines():
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
        _emit_log(f"Inside Utils.py : Exception in send_curl_command function: {exc}")
    finally:
        return output_response


def send_vcomponent_command(yaml_file_path, deepsleep=True):
    '''Post a YAML command file to the vComponent HTTP API.

    NOTE (scenario hooks - TODO): The PowerManager/deepsleep vComponent runtime
    command mechanism is not yet wired up. This helper mirrors the HdmiCec
    postKVP contract so scenario (Group B) tests can be enabled later without
    changing the test case code. Verify the endpoint/contract before relying on it.

    Uses: curl -sS -X POST -H "Content-Type: application/x-yaml"
               --data-binary @<yaml_file> http://127.0.0.1:8081/api/postKVP
    Returns (http_code: int, body: str) tuple. http_code 200 indicates success.
    '''
    try:
        if not Path(yaml_file_path).is_file():
            return 0, f"YAML file not found: {yaml_file_path}"

        deepsleep_cmd = [
            "curl", "-sS", "-w", "\n%{http_code}",
            "-X", "POST",
            "-H", "Content-Type: application/x-yaml",
            "--data-binary", f"@{yaml_file_path}",
            DEEPSLEEP_VCOMPONENT_API_URL,
        ]
        boot_cmd = [
            "curl", "-sS", "-w", "\n%{http_code}",
            "-X", "POST",
            "-H", "Content-Type: application/x-yaml",
            "--data-binary", f"@{yaml_file_path}",
            BOOT_VCOMPONENT_API_URL,
        ]
        cmd = deepsleep_cmd
        if deepsleep == False:
            cmd = boot_cmd
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
