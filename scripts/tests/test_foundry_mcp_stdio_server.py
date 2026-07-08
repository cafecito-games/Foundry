#!/usr/bin/env python3
"""Unit tests for scripts/foundry_mcp/stdio_server.py."""

from __future__ import annotations

import io
import json
import sys
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

from scripts.foundry_mcp import FoundryMCPStdioServer


class FakeClient:
    def __init__(self) -> None:
        self.endpoint = "http://127.0.0.1:3000/mcp"
        self.token = "fake-token"
        self.initialized = False
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.resources: list[str] = []

    def initialize(self, *, client_name: str = "foundry_mcp_stdio_bridge", client_version: str = "1.0") -> dict[str, Any]:
        self.initialized = True
        return {"protocolVersion": "2025-11-25", "clientInfo": {"name": client_name, "version": client_version}}

    def call_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        args = arguments or {}
        self.calls.append((name, args))
        return {"structuredContent": {"ok": True, "tool": name, "arguments": args}, "isError": False}

    def read_resource(self, uri: str) -> dict[str, Any]:
        self.resources.append(uri)
        return {"contents": [{"uri": uri, "text": "{}"}]}


class FakeSession:
    def __init__(self) -> None:
        self.client = FakeClient()
        self.closed = False

    def close(self) -> None:
        self.closed = True


class FakeSessionFactory:
    def __init__(self) -> None:
        self.connected: list[tuple[str, str]] = []
        self.launched: list[dict[str, Any]] = []
        self.last_session: FakeSession | None = None

    def connect(self, endpoint: str, token: str, *, timeout: float = 60.0) -> FakeSession:
        self.connected.append((endpoint, token))
        self.last_session = FakeSession()
        self.last_session.client.endpoint = endpoint
        self.last_session.client.token = token
        return self.last_session

    def launch(self, **kwargs: Any) -> FakeSession:
        self.launched.append(kwargs)
        self.last_session = FakeSession()
        self.last_session.client.endpoint = "http://127.0.0.1:4000/mcp"
        self.last_session.client.token = "launched-token"
        return self.last_session


def request(message_id: int, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": "2.0", "id": message_id, "method": method}
    if params is not None:
        message["params"] = params
    return message


def tool_args(response: dict[str, Any]) -> dict[str, Any]:
    return response["result"]["structuredContent"]


class FoundryMCPStdioServerTestCase(unittest.TestCase):
    def test_initialize_and_tools_list_expose_bridge_tools(self) -> None:
        server = FoundryMCPStdioServer(session_factory=FakeSessionFactory())

        init = server.handle_message(request(1, "initialize", {"protocolVersion": "2025-11-25"}))
        tools = server.handle_message(request(2, "tools/list"))

        self.assertEqual(init["result"]["serverInfo"]["name"], "foundry-editor-automation-bridge")
        tool_names = [tool["name"] for tool in tools["result"]["tools"]]
        self.assertIn("foundry_connect", tool_names)
        self.assertIn("foundry_launch_editor", tool_names)
        self.assertIn("foundry_observe_ui", tool_names)
        self.assertIn("foundry_call_tool", tool_names)

    def test_connect_initializes_and_retains_session(self) -> None:
        factory = FakeSessionFactory()
        server = FoundryMCPStdioServer(session_factory=factory)

        response = server.handle_message(
            request(
                1,
                "tools/call",
                {
                    "name": "foundry_connect",
                    "arguments": {"endpoint": "http://127.0.0.1:1/mcp", "token": "abc"},
                },
            )
        )

        structured = tool_args(response)
        self.assertFalse(response["result"]["isError"])
        self.assertEqual(structured["endpoint"], "http://127.0.0.1:1/mcp")
        self.assertTrue(structured["initialized"])
        self.assertEqual(factory.connected, [("http://127.0.0.1:1/mcp", "abc")])
        self.assertIs(server.session, factory.last_session)

    def test_launch_expands_paths_and_allows_default_binary(self) -> None:
        factory = FakeSessionFactory()
        server = FoundryMCPStdioServer(session_factory=factory, default_binary="~/bin/foundry")

        response = server.handle_message(
            request(
                1,
                "tools/call",
                {"name": "foundry_launch_editor", "arguments": {"project": "~/test-foundry-project-2"}},
            )
        )

        self.assertFalse(response["result"]["isError"])
        self.assertEqual(factory.launched[0]["binary"], Path.home() / "bin/foundry")
        self.assertEqual(factory.launched[0]["project"], Path.home() / "test-foundry-project-2")

    def test_proxy_tool_calls_use_existing_session_client(self) -> None:
        factory = FakeSessionFactory()
        server = FoundryMCPStdioServer(session_factory=factory)
        server.handle_message(
            request(
                1,
                "tools/call",
                {"name": "foundry_connect", "arguments": {"endpoint": "http://127.0.0.1:1/mcp", "token": "abc"}},
            )
        )

        response = server.handle_message(
            request(
                2,
                "tools/call",
                {"name": "foundry_observe_ui", "arguments": {"max_depth": 2}},
            )
        )

        self.assertFalse(response["result"]["isError"])
        self.assertEqual(tool_args(response), {"ok": True, "tool": "observe_ui", "arguments": {"max_depth": 2}})
        self.assertEqual(factory.last_session.client.calls[-1], ("observe_ui", {"max_depth": 2}))

    def test_generic_call_tool_and_read_resource_proxy_to_editor(self) -> None:
        factory = FakeSessionFactory()
        server = FoundryMCPStdioServer(session_factory=factory)
        server.handle_message(
            request(
                1,
                "tools/call",
                {"name": "foundry_connect", "arguments": {"endpoint": "http://127.0.0.1:1/mcp", "token": "abc"}},
            )
        )

        call = server.handle_message(
            request(
                2,
                "tools/call",
                {
                    "name": "foundry_call_tool",
                    "arguments": {"name": "custom_editor_tool", "arguments": {"value": 5}},
                },
            )
        )
        resource = server.handle_message(
            request(
                3,
                "tools/call",
                {"name": "foundry_read_resource", "arguments": {"uri": "foundry://editor/state"}},
            )
        )

        self.assertEqual(tool_args(call), {"ok": True, "tool": "custom_editor_tool", "arguments": {"value": 5}})
        self.assertEqual(tool_args(resource), {"contents": [{"uri": "foundry://editor/state", "text": "{}"}]})

    def test_tool_call_without_session_returns_tool_error(self) -> None:
        server = FoundryMCPStdioServer(session_factory=FakeSessionFactory())

        response = server.handle_message(
            request(1, "tools/call", {"name": "foundry_observe_ui", "arguments": {"max_depth": 2}})
        )

        self.assertTrue(response["result"]["isError"])
        self.assertEqual(tool_args(response)["kind"], "not_connected")

    def test_disconnect_closes_session(self) -> None:
        factory = FakeSessionFactory()
        server = FoundryMCPStdioServer(session_factory=factory)
        server.handle_message(
            request(
                1,
                "tools/call",
                {"name": "foundry_connect", "arguments": {"endpoint": "http://127.0.0.1:1/mcp", "token": "abc"}},
            )
        )

        response = server.handle_message(request(2, "tools/call", {"name": "foundry_disconnect", "arguments": {}}))

        self.assertFalse(response["result"]["isError"])
        self.assertTrue(factory.last_session.closed)
        self.assertIsNone(server.session)

    def test_stdio_loop_writes_one_response_per_request_and_skips_notifications(self) -> None:
        server = FoundryMCPStdioServer(session_factory=FakeSessionFactory())
        input_stream = io.StringIO(
            "\n".join(
                [
                    json.dumps(request(1, "initialize", {"protocolVersion": "2025-11-25"})),
                    json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}),
                    json.dumps(request(2, "tools/list")),
                    "",
                ]
            )
        )
        output_stream = io.StringIO()

        server.serve(input_stream=input_stream, output_stream=output_stream)

        lines = [json.loads(line) for line in output_stream.getvalue().splitlines()]
        self.assertEqual([line["id"] for line in lines], [1, 2])
        self.assertIn("tools", lines[1]["result"])

    def test_stdio_loop_reports_parse_error_with_jsonl_hint(self) -> None:
        server = FoundryMCPStdioServer(session_factory=FakeSessionFactory())
        input_stream = io.StringIO(
            '{"jsonrpc":"2.0","id":1,"method":"initialize","params":\n'
            '{"protocolVersion":"2025-11-25"}}\n'
        )
        output_stream = io.StringIO()

        server.serve(input_stream=input_stream, output_stream=output_stream)

        lines = [json.loads(line) for line in output_stream.getvalue().splitlines()]
        self.assertEqual(lines[0]["error"]["code"], -32700)
        self.assertIn("one complete JSON-RPC object per line", lines[0]["error"]["message"])


if __name__ == "__main__":
    unittest.main()
