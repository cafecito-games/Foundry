# Foundry MCP Python Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reusable stdlib-only Python client and session helper for Foundry's editor automation MCP server.

**Architecture:** Add a small `scripts/foundry_mcp` package split into protocol (`client.py`) and process lifecycle (`session.py`). Refactor `scripts/exercise_editor_mcp.py` to import the package and keep only smoke-test fixture logic.

**Tech Stack:** Python 3.8-compatible stdlib, `unittest`, `urllib.request`, `subprocess`, Foundry's command-first editor CLI.

---

## File Structure

- Create `scripts/foundry_mcp/__init__.py`: package exports for client, session, and errors.
- Create `scripts/foundry_mcp/client.py`: JSON-RPC/MCP HTTP client, errors, tool/resource helpers, thin Foundry tool wrappers.
- Create `scripts/foundry_mcp/session.py`: automation-line parsing, launch/connect session helper, context-manager cleanup.
- Create `scripts/tests/test_foundry_mcp_client.py`: fake HTTP server tests for protocol behavior.
- Create `scripts/tests/test_foundry_mcp_session.py`: parser and fake-process lifecycle tests.
- Modify `scripts/exercise_editor_mcp.py`: remove embedded `MCPClient`, import `FoundryMCPClient`, and include `capture_screenshot` in expected tools.
- Modify `AGENTS.md`: tell agents to prefer `scripts.foundry_mcp` for editor automation MCP instead of rewriting HTTP scripts.
- Create `docs/editor_automation_mcp_client.md`: usage and integration guide, including Claude, Codex, and Cursor setup patterns.

## Task 1: Client Protocol Tests

**Files:**
- Create: `scripts/tests/test_foundry_mcp_client.py`
- Create later: `scripts/foundry_mcp/client.py`

- [ ] **Step 1: Write failing tests for JSON-RPC, auth, structured tools, and resources**

Add `scripts/tests/test_foundry_mcp_client.py` with a local fake HTTP server:

```python
#!/usr/bin/env python3
"""Unit tests for scripts/foundry_mcp/client.py."""

from __future__ import annotations

import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from scripts.foundry_mcp import FoundryMCPClient, FoundryMCPError


class _FakeMCPHandler(BaseHTTPRequestHandler):
    requests: list[dict[str, Any]] = []
    responses: list[tuple[int, dict[str, Any] | str]] = []

    def do_POST(self) -> None:
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode("utf-8")
        payload = json.loads(body)
        self.__class__.requests.append(
            {
                "authorization": self.headers.get("Authorization"),
                "content_type": self.headers.get("Content-Type"),
                "payload": payload,
            }
        )
        status, response = self.__class__.responses.pop(0)
        raw = response if isinstance(response, str) else json.dumps(response)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw.encode("utf-8"))))
        self.end_headers()
        self.wfile.write(raw.encode("utf-8"))

    def log_message(self, _format: str, *_args: Any) -> None:
        return


class FakeMCPServer:
    def __enter__(self) -> FakeMCPServer:
        _FakeMCPHandler.requests = []
        _FakeMCPHandler.responses = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _FakeMCPHandler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()
        host, port = self.server.server_address
        self.endpoint = f"http://{host}:{port}/mcp"
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    @property
    def requests(self) -> list[dict[str, Any]]:
        return _FakeMCPHandler.requests

    def queue(self, status: int, response: dict[str, Any] | str) -> None:
        _FakeMCPHandler.responses.append((status, response))


class FoundryMCPClientTestCase(unittest.TestCase):
    def test_initialize_sends_protocol_version_and_initialized_notification(self) -> None:
        with FakeMCPServer() as server:
            server.queue(
                200,
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {"protocolVersion": "2025-11-25", "capabilities": {}},
                },
            )
            server.queue(200, "")
            client = FoundryMCPClient(server.endpoint, "secret")

            result = client.initialize()

            self.assertEqual(result["protocolVersion"], "2025-11-25")
            self.assertTrue(client.initialized)
            self.assertEqual(server.requests[0]["authorization"], "Bearer secret")
            self.assertEqual(server.requests[0]["content_type"], "application/json")
            self.assertEqual(server.requests[0]["payload"]["method"], "initialize")
            self.assertEqual(server.requests[0]["payload"]["params"]["protocolVersion"], "2025-11-25")
            self.assertEqual(server.requests[1]["payload"]["method"], "notifications/initialized")
            self.assertNotIn("id", server.requests[1]["payload"])

    def test_json_rpc_error_raises_foundry_mcp_error(self) -> None:
        with FakeMCPServer() as server:
            server.queue(200, {"jsonrpc": "2.0", "id": 1, "error": {"code": -32602, "message": "bad params"}})
            client = FoundryMCPClient(server.endpoint, "secret")

            with self.assertRaises(FoundryMCPError) as raised:
                client.request("tools/list")

            self.assertEqual(raised.exception.code, -32602)
            self.assertEqual(raised.exception.message, "bad params")
            self.assertEqual(raised.exception.method, "tools/list")

    def test_http_error_raises_foundry_mcp_error_with_raw_body(self) -> None:
        with FakeMCPServer() as server:
            server.queue(401, '{"error":"Unauthorized"}')
            client = FoundryMCPClient(server.endpoint, "wrong")

            with self.assertRaises(FoundryMCPError) as raised:
                client.request("tools/list")

            self.assertEqual(raised.exception.http_status, 401)
            self.assertIn("Unauthorized", raised.exception.raw_body or "")

    def test_call_tool_returns_full_result_even_when_tool_reports_error(self) -> None:
        with FakeMCPServer() as server:
            tool_result = {"structuredContent": {"ok": False, "kind": "no_match"}, "isError": True}
            server.queue(200, {"jsonrpc": "2.0", "id": 1, "result": tool_result})
            client = FoundryMCPClient(server.endpoint, "secret")

            result = client.call_tool("find_elements", {"selector": {"role": "button"}})

            self.assertEqual(result, tool_result)
            self.assertEqual(server.requests[0]["payload"]["method"], "tools/call")
            self.assertEqual(server.requests[0]["payload"]["params"]["name"], "find_elements")

    def test_structured_tool_extracts_structured_content(self) -> None:
        with FakeMCPServer() as server:
            server.queue(
                200,
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {"structuredContent": {"ok": True, "value": 7}, "isError": False},
                },
            )
            client = FoundryMCPClient(server.endpoint, "secret")

            self.assertEqual(client.structured_tool("read_editor_state"), {"ok": True, "value": 7})

    def test_resource_helpers_use_mcp_methods(self) -> None:
        with FakeMCPServer() as server:
            server.queue(200, {"jsonrpc": "2.0", "id": 1, "result": {"resources": [{"uri": "foundry://ui/tree"}]}})
            server.queue(
                200,
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "result": {"resourceTemplates": [{"uriTemplate": "foundry://ui/subtree/{id}"}]},
                },
            )
            server.queue(200, {"jsonrpc": "2.0", "id": 3, "result": {"contents": [{"text": "{}"}]}})
            client = FoundryMCPClient(server.endpoint, "secret")

            self.assertEqual(client.list_resources(), [{"uri": "foundry://ui/tree"}])
            self.assertEqual(client.list_resource_templates(), [{"uriTemplate": "foundry://ui/subtree/{id}"}])
            self.assertEqual(client.read_resource("foundry://ui/tree"), {"contents": [{"text": "{}"}]})
            self.assertEqual([r["payload"]["method"] for r in server.requests], [
                "resources/list",
                "resources/templates/list",
                "resources/read",
            ])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail because the package does not exist**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
```

Expected: `ModuleNotFoundError: No module named 'scripts.foundry_mcp'`.

## Task 2: Minimal Client Implementation

**Files:**
- Create: `scripts/foundry_mcp/__init__.py`
- Create: `scripts/foundry_mcp/client.py`
- Test: `scripts/tests/test_foundry_mcp_client.py`

- [ ] **Step 1: Implement `FoundryMCPClient` and `FoundryMCPError`**

Create `scripts/foundry_mcp/__init__.py`:

```python
"""Reusable Python helpers for Foundry editor automation MCP."""

from __future__ import annotations

from scripts.foundry_mcp.client import FoundryMCPClient, FoundryMCPError

__all__ = ["FoundryMCPClient", "FoundryMCPError"]
```

Create `scripts/foundry_mcp/client.py` with:

```python
#!/usr/bin/env python3
"""Small stdlib MCP client for Foundry editor automation."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any

PROTOCOL_VERSION = "2025-11-25"


class FoundryMCPError(RuntimeError):
    def __init__(
        self,
        message: str,
        *,
        method: str | None = None,
        code: int | None = None,
        data: Any = None,
        http_status: int | None = None,
        raw_body: str | None = None,
    ) -> None:
        super().__init__(message)
        self.method = method
        self.code = code
        self.message = message
        self.data = data
        self.http_status = http_status
        self.raw_body = raw_body


class FoundryMCPClient:
    def __init__(self, endpoint: str, token: str, *, timeout: float = 60.0) -> None:
        self.endpoint = endpoint
        self.token = token
        self.timeout = timeout
        self.request_id = 0
        self.initialized = False

    def _next_id(self) -> int:
        self.request_id += 1
        return self.request_id

    def request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        payload: dict[str, Any] = {"jsonrpc": "2.0", "id": self._next_id(), "method": method}
        if params is not None:
            payload["params"] = params
        response = self._post(payload, method=method, expect_response=True)
        if "error" in response:
            error = response["error"]
            raise FoundryMCPError(
                str(error.get("message", "MCP request failed.")),
                method=method,
                code=error.get("code"),
                data=error.get("data"),
            )
        return response.get("result", {})

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        payload: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        self._post(payload, method=method, expect_response=False)

    def _post(self, payload: dict[str, Any], *, method: str, expect_response: bool) -> dict[str, Any]:
        body = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            self.endpoint,
            data=body,
            headers={"Authorization": f"Bearer {self.token}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            raise FoundryMCPError(
                f"HTTP {exc.code} for {method}: {raw}",
                method=method,
                http_status=exc.code,
                raw_body=raw,
            ) from exc
        except urllib.error.URLError as exc:
            raise FoundryMCPError(f"Connection failed for {method}: {exc}", method=method) from exc

        if not expect_response:
            return {}
        if not raw:
            return {}
        return json.loads(raw)

    def initialize(self, *, client_name: str = "foundry_mcp", client_version: str = "1.0") -> dict[str, Any]:
        result = self.request(
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": client_name, "version": client_version},
            },
        )
        self.notify("notifications/initialized", {})
        self.initialized = True
        return result

    def list_tools(self) -> list[dict[str, Any]]:
        return self.request("tools/list").get("tools", [])

    def call_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})

    def structured_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        return self.call_tool(name, arguments).get("structuredContent", {})

    def list_resources(self) -> list[dict[str, Any]]:
        return self.request("resources/list").get("resources", [])

    def list_resource_templates(self) -> list[dict[str, Any]]:
        return self.request("resources/templates/list").get("resourceTemplates", [])

    def read_resource(self, uri: str) -> dict[str, Any]:
        return self.request("resources/read", {"uri": uri})
```

- [ ] **Step 2: Run client tests to verify they pass**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
```

Expected: `OK`.

- [ ] **Step 3: Commit client protocol slice**

Run:

```bash
git add scripts/foundry_mcp/__init__.py scripts/foundry_mcp/client.py scripts/tests/test_foundry_mcp_client.py
git commit -m "Add Foundry MCP protocol client"
```

## Task 3: Foundry Tool Wrapper Tests and Implementation

**Files:**
- Modify: `scripts/tests/test_foundry_mcp_client.py`
- Modify: `scripts/foundry_mcp/client.py`

- [ ] **Step 1: Add failing tests for thin Foundry tool wrappers**

Append a test that queues ten tool responses and asserts wrapper arguments:

```python
    def test_foundry_tool_wrappers_forward_expected_arguments(self) -> None:
        with FakeMCPServer() as server:
            for index in range(10):
                server.queue(
                    200,
                    {
                        "jsonrpc": "2.0",
                        "id": index + 1,
                        "result": {"structuredContent": {"ok": True, "index": index}, "isError": False},
                    },
                )
            client = FoundryMCPClient(server.endpoint, "secret")

            client.observe_ui(max_depth=2, max_children=3)
            client.find_elements({"role": "button"}, max_results=5)
            client.act({"role": "button", "name": "Run"}, "click", args={"button": "left"})
            client.wait_for("editor_idle", timeout_ms=10, cooperative=False)
            client.read_editor_state()
            client.read_editor_log(limit=4)
            client.run_command("editor/save_scene")
            client.list_commands(query="save", limit=2)
            client.poll_events()
            client.capture_screenshot(padding=8)

            params = [request["payload"]["params"] for request in server.requests]
            self.assertEqual(params[0]["name"], "observe_ui")
            self.assertEqual(params[0]["arguments"], {"max_depth": 2, "max_children": 3})
            self.assertEqual(params[1]["name"], "find_elements")
            self.assertEqual(params[1]["arguments"]["selector"], {"role": "button"})
            self.assertEqual(params[2]["arguments"]["action"], "click")
            self.assertEqual(params[3]["arguments"]["condition"], "editor_idle")
            self.assertEqual(params[4]["name"], "read_editor_state")
            self.assertEqual(params[5]["arguments"]["limit"], 4)
            self.assertEqual(params[6]["arguments"]["command"], "editor/save_scene")
            self.assertEqual(params[7]["arguments"], {"query": "save", "limit": 2})
            self.assertEqual(params[8]["name"], "poll_events")
            self.assertEqual(params[9]["arguments"], {"padding": 8})
```

- [ ] **Step 2: Run test to verify it fails because wrappers are missing**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
```

Expected: `AttributeError: 'FoundryMCPClient' object has no attribute 'observe_ui'`.

- [ ] **Step 3: Implement wrapper methods**

Add these methods to `FoundryMCPClient`:

```python
    def observe_ui(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("observe_ui", arguments)

    def find_elements(self, selector: dict[str, Any], **arguments: Any) -> dict[str, Any]:
        arguments["selector"] = selector
        return self.call_tool("find_elements", arguments)

    def act(
        self,
        selector: dict[str, Any],
        action: str,
        *,
        args: dict[str, Any] | None = None,
        **arguments: Any,
    ) -> dict[str, Any]:
        arguments["selector"] = selector
        arguments["action"] = action
        if args is not None:
            arguments["args"] = args
        return self.call_tool("act", arguments)

    def wait_for(self, condition: str, **arguments: Any) -> dict[str, Any]:
        arguments["condition"] = condition
        return self.call_tool("wait_for", arguments)

    def read_editor_state(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("read_editor_state", arguments)

    def read_editor_log(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("read_editor_log", arguments)

    def run_command(self, command: str, **arguments: Any) -> dict[str, Any]:
        arguments["command"] = command
        return self.call_tool("run_command", arguments)

    def list_commands(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("list_commands", arguments)

    def poll_events(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("poll_events", arguments)

    def capture_screenshot(self, **arguments: Any) -> dict[str, Any]:
        return self.call_tool("capture_screenshot", arguments)
```

- [ ] **Step 4: Run client tests to verify wrappers pass**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
```

Expected: `OK`.

- [ ] **Step 5: Commit wrapper slice**

Run:

```bash
git add scripts/foundry_mcp/client.py scripts/tests/test_foundry_mcp_client.py
git commit -m "Add Foundry MCP tool wrappers"
```

## Task 4: Session Tests and Implementation

**Files:**
- Create: `scripts/tests/test_foundry_mcp_session.py`
- Create: `scripts/foundry_mcp/session.py`
- Modify: `scripts/foundry_mcp/__init__.py`

- [ ] **Step 1: Write failing tests for startup parsing and lifecycle**

Create `scripts/tests/test_foundry_mcp_session.py`:

```python
#!/usr/bin/env python3
"""Unit tests for scripts/foundry_mcp/session.py."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from typing import Any

from scripts.foundry_mcp import (
    FoundryAutomationStartupError,
    FoundryEditorAutomationSession,
    parse_automation_line,
)


class _FakeStdout:
    def __init__(self, lines: list[str]) -> None:
        self.lines = lines

    def readline(self) -> str:
        if self.lines:
            return self.lines.pop(0)
        return ""


class _FakeProcess:
    def __init__(self, lines: list[str], *, wait_timeout: bool = False) -> None:
        self.stdout = _FakeStdout(lines)
        self.wait_timeout = wait_timeout
        self.terminated = False
        self.killed = False
        self.returncode: int | None = None

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.terminated = True

    def kill(self) -> None:
        self.killed = True

    def wait(self, timeout: float | None = None) -> int:
        if self.wait_timeout and not self.killed:
            import subprocess

            raise subprocess.TimeoutExpired("foundry", timeout)
        self.returncode = 0
        return 0


class FoundryMCPSessionTestCase(unittest.TestCase):
    def test_parse_automation_line_extracts_endpoint_and_token(self) -> None:
        line = 'noise FOUNDRY_AUTOMATION {"endpoint":"http://127.0.0.1:1/mcp","token":"abc"}'

        info = parse_automation_line(line)

        self.assertEqual(info["endpoint"], "http://127.0.0.1:1/mcp")
        self.assertEqual(info["token"], "abc")

    def test_parse_automation_line_rejects_error_marker(self) -> None:
        with self.assertRaises(FoundryAutomationStartupError):
            parse_automation_line("FOUNDRY_AUTOMATION_ERROR failed")

    def test_wait_for_automation_line_reports_captured_output_on_timeout(self) -> None:
        process = _FakeProcess(["first line\n"])

        with self.assertRaises(FoundryAutomationStartupError) as raised:
            FoundryEditorAutomationSession.wait_for_automation(process, timeout_s=0.01)

        self.assertIn("first line", str(raised.exception))

    def test_connect_creates_client_without_process(self) -> None:
        session = FoundryEditorAutomationSession.connect("http://127.0.0.1:9/mcp", "secret")

        self.assertEqual(session.client.endpoint, "http://127.0.0.1:9/mcp")
        self.assertIsNone(session.process)

    def test_launch_builds_command_first_invocation(self) -> None:
        calls: list[dict[str, Any]] = []
        line = 'FOUNDRY_AUTOMATION {"endpoint":"http://127.0.0.1:2/mcp","token":"tok"}\n'

        def fake_popen(command: list[str], **kwargs: Any) -> _FakeProcess:
            calls.append({"command": command, "kwargs": kwargs})
            return _FakeProcess([line])

        with tempfile.TemporaryDirectory() as tmp:
            session = FoundryEditorAutomationSession.launch(
                binary=Path("/tmp/foundry"),
                project=Path(tmp),
                display=":9",
                token="tok",
                port=0,
                popen=fake_popen,
            )

        self.assertEqual(calls[0]["command"][:5], ["/tmp/foundry", "editor", "open", "--project", tmp])
        self.assertIn("--automation", calls[0]["command"])
        self.assertIn("--automation-transport=mcp", calls[0]["command"])
        self.assertEqual(os.environ.get("DISPLAY"), os.environ.get("DISPLAY"))
        self.assertEqual(session.client.endpoint, "http://127.0.0.1:2/mcp")

    def test_context_manager_kills_process_when_terminate_times_out(self) -> None:
        process = _FakeProcess([], wait_timeout=True)
        session = FoundryEditorAutomationSession.connect("http://127.0.0.1:9/mcp", "secret")
        session.process = process

        with session:
            pass

        self.assertTrue(process.terminated)
        self.assertTrue(process.killed)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run session tests to verify they fail because session exports are missing**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_session.py
```

Expected: `ImportError` for `FoundryEditorAutomationSession`.

- [ ] **Step 3: Implement session module**

Create `scripts/foundry_mcp/session.py` with:

```python
#!/usr/bin/env python3
"""Process/session helpers for Foundry editor automation MCP."""

from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any, Callable

from scripts.foundry_mcp.client import FoundryMCPClient

AUTOMATION_MARKER = "FOUNDRY_AUTOMATION "
AUTOMATION_ERROR_MARKER = "FOUNDRY_AUTOMATION_ERROR"


class FoundryAutomationStartupError(RuntimeError):
    pass


def parse_automation_line(line: str) -> dict[str, Any]:
    if AUTOMATION_ERROR_MARKER in line:
        raise FoundryAutomationStartupError(line.strip())
    if AUTOMATION_MARKER not in line:
        raise FoundryAutomationStartupError("Line does not contain FOUNDRY_AUTOMATION marker.")
    raw = line[line.index(AUTOMATION_MARKER) + len(AUTOMATION_MARKER) :].strip()
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise FoundryAutomationStartupError(f"Malformed FOUNDRY_AUTOMATION payload: {raw}") from exc
    if not data.get("endpoint") or not data.get("token"):
        raise FoundryAutomationStartupError(f"FOUNDRY_AUTOMATION payload missing endpoint/token: {raw}")
    return data


class FoundryEditorAutomationSession:
    def __init__(self, client: FoundryMCPClient, process: subprocess.Popen[str] | None = None) -> None:
        self.client = client
        self.process = process

    @classmethod
    def connect(cls, endpoint: str, token: str, *, timeout: float = 60.0) -> FoundryEditorAutomationSession:
        return cls(FoundryMCPClient(endpoint, token, timeout=timeout))

    @classmethod
    def launch(
        cls,
        *,
        binary: Path,
        project: Path,
        display: str | None = ":1",
        port: int | None = 0,
        token: str | None = None,
        extra_args: list[str] | None = None,
        env: dict[str, str] | None = None,
        startup_timeout_s: float = 180.0,
        popen: Callable[..., subprocess.Popen[str]] = subprocess.Popen,
    ) -> FoundryEditorAutomationSession:
        command = [
            str(binary),
            "editor",
            "open",
            "--project",
            str(project),
            "--automation",
            "--automation-transport=mcp",
        ]
        if port is not None:
            command.extend(["--automation-port", str(port)])
        if token is not None:
            command.extend(["--automation-token", token])
        if extra_args:
            command.extend(extra_args)

        child_env = os.environ.copy()
        if env:
            child_env.update(env)
        if display is not None:
            child_env.setdefault("DISPLAY", display)

        process = popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=child_env)
        automation = cls.wait_for_automation(process, timeout_s=startup_timeout_s)
        return cls(FoundryMCPClient(automation["endpoint"], automation["token"]), process=process)

    @staticmethod
    def wait_for_automation(process: Any, *, timeout_s: float = 180.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        output = ""
        while time.monotonic() < deadline:
            line = process.stdout.readline() if process.stdout else ""
            if line:
                output += line
                if AUTOMATION_MARKER in line or AUTOMATION_ERROR_MARKER in line:
                    try:
                        return parse_automation_line(line)
                    except FoundryAutomationStartupError as exc:
                        raise FoundryAutomationStartupError(f"{exc}\nCaptured output:\n{output}") from exc
            elif process.poll() is not None:
                break
            else:
                time.sleep(0.05)
        raise FoundryAutomationStartupError(f"Timed out waiting for FOUNDRY_AUTOMATION line.\nCaptured output:\n{output}")

    def close(self, *, terminate_timeout_s: float = 10.0) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=terminate_timeout_s)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=terminate_timeout_s)

    def __enter__(self) -> FoundryEditorAutomationSession:
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback) -> None:
        self.close()
```

Modify `scripts/foundry_mcp/__init__.py`:

```python
"""Reusable Python helpers for Foundry editor automation MCP."""

from __future__ import annotations

from scripts.foundry_mcp.client import FoundryMCPClient, FoundryMCPError
from scripts.foundry_mcp.session import (
    FoundryAutomationStartupError,
    FoundryEditorAutomationSession,
    parse_automation_line,
)

__all__ = [
    "FoundryAutomationStartupError",
    "FoundryEditorAutomationSession",
    "FoundryMCPClient",
    "FoundryMCPError",
    "parse_automation_line",
]
```

- [ ] **Step 4: Run client and session tests to verify they pass**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
python3 scripts/tests/test_foundry_mcp_session.py
```

Expected: both report `OK`.

- [ ] **Step 5: Commit session slice**

Run:

```bash
git add scripts/foundry_mcp/__init__.py scripts/foundry_mcp/session.py scripts/tests/test_foundry_mcp_session.py
git commit -m "Add Foundry editor automation session helper"
```

## Task 5: Refactor MCP Exercise Script

**Files:**
- Modify: `scripts/exercise_editor_mcp.py`
- Test: `scripts/tests/test_foundry_mcp_client.py`
- Test: `scripts/tests/test_foundry_mcp_session.py`

- [ ] **Step 1: Update the exercise script to use the reusable client**

Change `scripts/exercise_editor_mcp.py`:

```python
from scripts.foundry_mcp import FoundryMCPClient
```

Delete the embedded `MCPClient` class. Replace client construction:

```python
client = FoundryMCPClient(endpoint, token)
```

Replace method calls:

```python
client.tools_call(...) -> client.call_tool(...)
client.tools_list() -> client.list_tools()
client.resources_list() -> client.list_resources()
client.resources_templates_list() -> client.list_resource_templates()
client.resources_read(uri) -> client.read_resource(uri)
```

Add `"capture_screenshot"` to `EXPECTED_TOOLS`.

- [ ] **Step 2: Run tests after refactor**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
python3 scripts/tests/test_foundry_mcp_session.py
python3 -m py_compile scripts/exercise_editor_mcp.py
```

Expected: tests pass and `py_compile` exits 0.

- [ ] **Step 3: Commit exercise refactor**

Run:

```bash
git add scripts/exercise_editor_mcp.py scripts/foundry_mcp scripts/tests/test_foundry_mcp_client.py scripts/tests/test_foundry_mcp_session.py
git commit -m "Reuse Foundry MCP client in exercise script"
```

## Task 6: Agent Documentation

**Files:**
- Modify: `AGENTS.md`
- Create: `docs/editor_automation_mcp_client.md`

- [ ] **Step 1: Add an AGENTS.md note under the editor MCP section**

Add this text after the existing "Starting and connecting" bullets:

````markdown
### Python MCP client for agents

Prefer the reusable Python client in `scripts/foundry_mcp/` instead of writing
ad hoc `urllib` MCP scripts. Use `FoundryEditorAutomationSession.launch()` when
the agent should start and own the editor process, and
`FoundryEditorAutomationSession.connect()` when the editor is already running and
the endpoint/token are known.

```python
from pathlib import Path

from scripts.foundry_mcp import FoundryEditorAutomationSession

with FoundryEditorAutomationSession.launch(
    binary=Path("bin/foundry.linuxbsd.editor.dev.x86_64"),
    project=Path("tests/fixtures/editor_automation_mvp"),
) as session:
    session.client.initialize()
    ui = session.client.structured_tool("observe_ui", {"max_depth": 3})
```

For existing editor processes:

```python
from scripts.foundry_mcp import FoundryEditorAutomationSession

session = FoundryEditorAutomationSession.connect(endpoint, token)
session.client.initialize()
state = session.client.structured_tool("read_editor_state")
```

`call_tool()` returns the complete MCP tool result, including `isError`; use
`structured_tool()` only when the script explicitly wants `structuredContent`.
````

- [ ] **Step 2: Create the integration guide**

Create `docs/editor_automation_mcp_client.md` with:

````markdown
# Foundry Editor Automation MCP Python Client

`scripts/foundry_mcp` is the repository-local Python client for Foundry's editor
automation MCP server. Use it from ad hoc agent scripts, smoke tests, and local
tool wrappers instead of rewriting JSON-RPC/HTTP boilerplate.

## Launch and Own the Editor

```python
from pathlib import Path

from scripts.foundry_mcp import FoundryEditorAutomationSession

with FoundryEditorAutomationSession.launch(
    binary=Path("bin/foundry.linuxbsd.editor.dev.x86_64"),
    project=Path("tests/fixtures/editor_automation_mvp"),
    display=":1",
) as session:
    session.client.initialize()
    ui = session.client.structured_tool("observe_ui", {"max_depth": 4})
```

Use this path when the agent is responsible for starting and stopping the
editor. The session parses the `FOUNDRY_AUTOMATION` startup line and terminates
the editor process on exit.

## Connect to an Existing Editor

```python
from scripts.foundry_mcp import FoundryEditorAutomationSession

session = FoundryEditorAutomationSession.connect(
    "http://127.0.0.1:3000/mcp",
    "token-from-FOUNDRY_AUTOMATION",
)
session.client.initialize()
state = session.client.structured_tool("read_editor_state")
```

Use this path when another terminal, IDE task, or agent already launched the
editor with `--automation`.

## Common Automation Loop

```python
client = session.client
client.initialize()
ui = client.structured_tool("observe_ui", {"max_depth": 3})
button = client.structured_tool(
    "find_elements",
    {"selector": {"role": "button", "name": "Add Child Node"}, "max_results": 1},
)
client.call_tool("act", {"selector": {"role": "button", "name": "Add Child Node"}, "action": "click"})
client.call_tool("wait_for", {"condition": "modal_stack_settled", "timeout_ms": 5000})
events = client.structured_tool("poll_events")
```

`call_tool()` returns the full MCP tool result. A server-side tool failure is a
successful JSON-RPC response with `isError: true`; inspect the returned
`structuredContent` for diagnostics. JSON-RPC and HTTP failures raise
`FoundryMCPError`.

## Claude

Use a repository-local Python wrapper script as the Claude tool command. The
wrapper should import `scripts.foundry_mcp`, read `FOUNDRY_MCP_ENDPOINT` and
`FOUNDRY_MCP_TOKEN` for connect-only mode, or accept `--binary` and `--project`
flags for launch mode. Keep the wrapper thin: parse CLI arguments, call the
client/session helper, print JSON.

## Codex

Codex agents working in this repository can import the package directly from the
repo root:

```sh
python3 - <<'PY'
from scripts.foundry_mcp import FoundryEditorAutomationSession
session = FoundryEditorAutomationSession.connect("$FOUNDRY_MCP_ENDPOINT", "$FOUNDRY_MCP_TOKEN")
session.client.initialize()
print(session.client.structured_tool("read_editor_state"))
PY
```

For repeated use, add a small script under `scripts/` that wraps one workflow
and returns JSON. Prefer this over embedding MCP transport code into prompts.

## Cursor

Use a project task or command that runs a Python wrapper from the repository
root. Pass endpoint/token through environment variables for an already-running
editor, or let the wrapper launch the editor with
`FoundryEditorAutomationSession.launch()`. Cursor-side commands should treat the
Python wrapper as the stable tool boundary and keep editor-specific MCP details
inside `scripts/foundry_mcp`.
````

- [ ] **Step 3: Verify Markdown references compile as plain text**

Run:

```bash
python3 -m py_compile scripts/foundry_mcp/__init__.py scripts/foundry_mcp/client.py scripts/foundry_mcp/session.py
```

Expected: exits 0. Markdown is not compiled, but this confirms the documented imports still resolve syntactically.

- [ ] **Step 4: Commit documentation slice**

Run:

```bash
git add AGENTS.md docs/editor_automation_mcp_client.md
git commit -m "Document Foundry MCP Python client usage"
```

## Task 7: Final Verification

**Files:**
- All changed Python files

- [ ] **Step 1: Run focused Python tests**

Run:

```bash
python3 scripts/tests/test_foundry_mcp_client.py
python3 scripts/tests/test_foundry_mcp_session.py
python3 scripts/tests/test_review_gallery.py
```

Expected: all report `OK`.

- [ ] **Step 2: Run syntax checks for changed scripts**

Run:

```bash
python3 -m py_compile scripts/foundry_mcp/__init__.py scripts/foundry_mcp/client.py scripts/foundry_mcp/session.py scripts/exercise_editor_mcp.py scripts/tests/test_foundry_mcp_client.py scripts/tests/test_foundry_mcp_session.py
```

Expected: exits 0.

- [ ] **Step 3: Inspect git status**

Run:

```bash
git status --short
```

Expected: clean after commits, or only intentionally uncommitted final edits.
