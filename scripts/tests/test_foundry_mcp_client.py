#!/usr/bin/env python3
"""Unit tests for scripts/foundry_mcp/client.py."""

from __future__ import annotations

import json
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, cast

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from scripts.foundry_mcp import FoundryMCPClient, FoundryMCPError  # noqa: E402


class _FakeMCPHandler(BaseHTTPRequestHandler):
    requests: list[dict[str, Any]] = []
    queued_responses: list[tuple[int, dict[str, Any] | str]] = []

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
        status, response = self.__class__.queued_responses.pop(0)
        raw = response if isinstance(response, str) else json.dumps(response)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw.encode("utf-8"))))
        self.end_headers()
        self.wfile.write(raw.encode("utf-8"))

    def log_message(self, _format: str, *_args: Any) -> None:
        return


class FakeMCPServer:
    endpoint: str

    def __enter__(self) -> FakeMCPServer:
        _FakeMCPHandler.requests = []
        _FakeMCPHandler.queued_responses = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _FakeMCPHandler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()
        host, port = cast("tuple[str, int]", self.server.server_address)
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
        _FakeMCPHandler.queued_responses.append((status, response))


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
            self.assertEqual(
                [request["payload"]["method"] for request in server.requests],
                [
                    "resources/list",
                    "resources/templates/list",
                    "resources/read",
                ],
            )

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
            self.assertEqual(params[1]["arguments"]["max_results"], 5)
            self.assertEqual(params[2]["arguments"]["action"], "click")
            self.assertEqual(params[2]["arguments"]["args"], {"button": "left"})
            self.assertEqual(params[3]["arguments"]["condition"], "editor_idle")
            self.assertEqual(params[3]["arguments"]["cooperative"], False)
            self.assertEqual(params[4]["name"], "read_editor_state")
            self.assertEqual(params[5]["arguments"]["limit"], 4)
            self.assertEqual(params[6]["arguments"]["command"], "editor/save_scene")
            self.assertEqual(params[7]["arguments"], {"query": "save", "limit": 2})
            self.assertEqual(params[8]["name"], "poll_events")
            self.assertEqual(params[9]["arguments"], {"padding": 8})


if __name__ == "__main__":
    unittest.main()
