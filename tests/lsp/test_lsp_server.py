#!/usr/bin/env python3
"""
tamarin-lsp E2E Integration Test Suite

Launches `tamarin-lsp --stdio` as a subprocess and tests the JSON-RPC protocol:
  - Framing: Content-Length: <len>\r\n\r\n
  - Lifecycle: initialize -> initialized -> shutdown -> exit
  - Error Handling: Invalid method, uninitialized request
  - Capabilities verification
"""

import sys
import os
import subprocess
import json
import shutil
import pathlib
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
IS_WINDOWS = sys.platform == "win32"
EXE_EXT = ".exe" if IS_WINDOWS else ""

USE_COLOR = sys.stdout.isatty() or os.environ.get("CI") is not None
def green(t): return f"\033[92m{t}\033[0m" if USE_COLOR else t
def red(t): return f"\033[91m{t}\033[0m" if USE_COLOR else t
def cyan(t): return f"\033[96m{t}\033[0m" if USE_COLOR else t
def yellow(t): return f"\033[93m{t}\033[0m" if USE_COLOR else t

def find_lsp_binary() -> pathlib.Path | None:
    env_p = os.environ.get("TAMARIN_LSP_EXE") or os.environ.get("TAMARIN_LSP_BIN")
    if env_p and pathlib.Path(env_p).exists():
        return pathlib.Path(env_p)
    candidates = [
        REPO_ROOT / "build" / "Release",
        REPO_ROOT / "build",
        REPO_ROOT / "build" / "Debug",
        REPO_ROOT / "bin"
    ]
    for c in candidates:
        bin_p = c / f"tamarin-lsp{EXE_EXT}"
        if bin_p.exists():
            return bin_p
    which_p = shutil.which(f"tamarin-lsp{EXE_EXT}")
    return pathlib.Path(which_p) if which_p else None

class LspClient:
    def __init__(self, binary_path: pathlib.Path):
        self.proc = subprocess.Popen(
            [str(binary_path), "--stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0
        )

    def send_raw(self, payload: str):
        content = payload.encode("utf-8")
        header = f"Content-Length: {len(content)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header + content)
        self.proc.stdin.flush()

    def send_request(self, method: str, req_id: int, params: dict | None = None):
        msg = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method
        }
        if params is not None:
            msg["params"] = params
        self.send_raw(json.dumps(msg))

    def send_notification(self, method: str, params: dict | None = None):
        msg = {
            "jsonrpc": "2.0",
            "method": method
        }
        if params is not None:
            msg["params"] = params
        self.send_raw(json.dumps(msg))

    def read_message(self, timeout_sec: float = 5.0) -> dict | None:
        start_time = time.time()
        # Read headers
        content_length = None
        header_bytes = bytearray()
        while True:
            if time.time() - start_time > timeout_sec:
                return None
            line = self.proc.stdout.readline()
            if not line:
                return None
            line_str = line.decode("ascii", errors="replace").strip()
            if not line_str:
                break
            if line_str.lower().startswith("content-length:"):
                content_length = int(line_str.split(":", 1)[1].strip())

        if content_length is None:
            return None

        body_bytes = self.proc.stdout.read(content_length)
        body_str = body_bytes.decode("utf-8", errors="replace")
        return json.loads(body_str)

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.stdout.close()
            self.proc.stderr.close()
            self.proc.terminate()
            self.proc.wait(timeout=2.0)
        except Exception:
            pass

def run_tests():
    lsp_bin = find_lsp_binary()
    if not lsp_bin or not lsp_bin.exists():
        print(red("Error: tamarin-lsp executable not found!"))
        sys.exit(1)

    print(cyan("======================================================================"))
    print(cyan(" tamarin-lsp Protocol & Lifecycle E2E Test Suite"))
    print(cyan("======================================================================"))
    print(f"  Binary: {lsp_bin}\n")

    client = LspClient(lsp_bin)
    passed = 0
    failed = 0

    def check(name: str, condition: bool, detail: str = ""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  {green('[PASS]')} {name}")
        else:
            failed += 1
            print(f"  {red('[FAIL]')} {name} {yellow(f'({detail})') if detail else ''}")

    try:
        # Test 1: Initialize negotiation
        client.send_request("initialize", 1, {
            "processId": os.getpid(),
            "rootUri": "file:///workspace",
            "capabilities": {}
        })
        resp1 = client.read_message()
        check("Initialize response received", resp1 is not None)
        if resp1:
            check("Initialize response id == 1", resp1.get("id") == 1)
            res = resp1.get("result", {})
            caps = res.get("capabilities", {})
            check("Capabilities: textDocumentSync == 1", caps.get("textDocumentSync") == 1)
            check("Capabilities: hoverProvider == True", caps.get("hoverProvider") is True)
            check("Capabilities: definitionProvider == True", caps.get("definitionProvider") is True)
            server_info = res.get("serverInfo", {})
            check("ServerInfo: name == 'tamarin-lsp'", server_info.get("name") == "tamarin-lsp")

        # Test 2: Initialized notification
        client.send_notification("initialized", {})
        check("Initialized notification sent without crash", client.proc.poll() is None)

        # Test 3: Unknown method returns -32601 (MethodNotFound)
        client.send_request("nonexistent/method", 2, {})
        resp3 = client.read_message()
        check("Unknown method response received", resp3 is not None)
        if resp3:
            check("Unknown method id == 2", resp3.get("id") == 2)
            err = resp3.get("error", {})
            check("Unknown method error code == -32601", err.get("code") == -32601)

        # Test 4: Shutdown request
        client.send_request("shutdown", 3)
        resp4 = client.read_message()
        check("Shutdown response received", resp4 is not None)
        if resp4:
            check("Shutdown response id == 3", resp4.get("id") == 3)
            check("Shutdown result is null", resp4.get("result") is None)

        # Test 5: Exit notification terminates process with 0
        client.send_notification("exit")
        ret = client.proc.wait(timeout=3.0)
        check("Process exits cleanly with code 0 on exit notification", ret == 0, f"ExitCode={ret}")

    finally:
        client.close()

    print(cyan("\n======================================================================"))
    print(cyan(f" SCORECARD: {passed} Passed, {failed} Failed"))
    print(cyan("======================================================================\n"))

    if failed > 0:
        sys.exit(1)
    else:
        print(green("ALL tamarin-lsp E2E TESTS PASSED!\n"))

if __name__ == "__main__":
    run_tests()
