#!/usr/bin/env python3
"""Stdio MCP bridge for Foundry editor automation."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, TextIO

from scripts.foundry_mcp.client import PROTOCOL_VERSION, FoundryMCPError
from scripts.foundry_mcp.session import FoundryAutomationStartupError, FoundryEditorAutomationSession

JSONRPC_VERSION = "2.0"
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603


def _object_schema(description: str, properties: dict[str, Any] | None = None, required: list[str] | None = None) -> dict[str, Any]:
    schema: dict[str, Any] = {"type": "object", "description": description, "properties": properties or {}}
    if required:
        schema["required"] = required
    return schema


def _string_schema(description: str) -> dict[str, str]:
    return {"type": "string", "description": description}


def _tool(name: str, description: str, input_schema: dict[str, Any]) -> dict[str, Any]:
    return {"name": name, "description": description, "inputSchema": input_schema}


class FoundryMCPStdioServer:
    def __init__(self, *, session_factory: Any = FoundryEditorAutomationSession) -> None:
        self.session_factory = session_factory
        self.session: Any | None = None

    def serve(
        self,
        *,
        input_stream: TextIO | None = None,
        output_stream: TextIO | None = None,
        error_stream: TextIO | None = None,
    ) -> None:
        if input_stream is None:
            input_stream = sys.stdin
        if output_stream is None:
            output_stream = sys.stdout
        if error_stream is None:
            error_stream = sys.stderr

        for line in input_stream:
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
                response = self.handle_message(message)
            except Exception as exc:  # noqa: BLE001
                response = self._error(None, INTERNAL_ERROR, f"Unhandled bridge error: {exc}")
            if response is None:
                continue
            output_stream.write(json.dumps(response, separators=(",", ":")) + "\n")
            output_stream.flush()

    def handle_message(self, message: dict[str, Any]) -> dict[str, Any] | None:
        message_id = message.get("id")
        is_notification = "id" not in message
        method = message.get("method")
        if not isinstance(method, str):
            if is_notification:
                return None
            return self._error(message_id, INVALID_REQUEST, "Request is missing a string 'method'.")

        if is_notification:
            return None

        params = message.get("params", {})
        if params is None:
            params = {}
        if not isinstance(params, dict):
            return self._error(message_id, INVALID_PARAMS, "Request 'params' must be an object.")

        if method == "initialize":
            return self._result(message_id, self._initialize(params))
        if method == "tools/list":
            return self._result(message_id, {"tools": self.build_tools_list()})
        if method == "tools/call":
            return self._result(message_id, self._handle_tools_call(params))
        if method == "ping":
            return self._result(message_id, {})

        return self._error(message_id, METHOD_NOT_FOUND, f"Unknown method '{method}'.")

    def _initialize(self, _params: dict[str, Any]) -> dict[str, Any]:
        return {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": "foundry-editor-automation-bridge", "version": "1.0.0"},
            "instructions": (
                "Use foundry_launch_editor when the bridge should own the editor process, "
                "or foundry_connect when an automation endpoint/token already exists. "
                "Then use observe/find/act/wait tools for editor UI workflows."
            ),
        }

    @staticmethod
    def build_tools_list() -> list[dict[str, Any]]:
        selector = _object_schema("Semantic editor automation selector.")
        any_args = _object_schema("Arguments forwarded to the Foundry editor MCP tool.")
        return [
            _tool(
                "foundry_launch_editor",
                "Launch the Foundry editor with automation enabled and retain the session.",
                _object_schema(
                    "Launch arguments.",
                    {
                        "binary": _string_schema("Path to the foundry editor binary."),
                        "project": _string_schema("Path to the Foundry project directory."),
                        "display": _string_schema("DISPLAY value for GUI-capable runs."),
                        "port": {"type": "integer", "description": "Automation port, or 0 for any free port."},
                        "token": _string_schema("Optional automation token."),
                        "startup_timeout_s": {"type": "number", "description": "Seconds to wait for FOUNDRY_AUTOMATION."},
                        "initialize": {"type": "boolean", "description": "Initialize the editor MCP client after launch."},
                        "extra_args": {"type": "array", "items": {"type": "string"}, "description": "Extra CLI args."},
                    },
                    ["binary", "project"],
                ),
            ),
            _tool(
                "foundry_connect",
                "Connect to an already-running Foundry automation endpoint.",
                _object_schema(
                    "Connection arguments.",
                    {
                        "endpoint": _string_schema("Foundry automation MCP endpoint URL."),
                        "token": _string_schema("Bearer token printed in FOUNDRY_AUTOMATION."),
                        "initialize": {"type": "boolean", "description": "Initialize the editor MCP client after connect."},
                    },
                    ["endpoint", "token"],
                ),
            ),
            _tool("foundry_disconnect", "Close the retained Foundry editor automation session.", _object_schema("No arguments.")),
            _tool("foundry_status", "Return bridge connection status.", _object_schema("No arguments.")),
            _tool(
                "foundry_call_tool",
                "Call any tool exposed by the Foundry editor MCP server.",
                _object_schema(
                    "Generic editor tool call.",
                    {"name": _string_schema("Editor MCP tool name."), "arguments": any_args},
                    ["name"],
                ),
            ),
            _tool("foundry_observe_ui", "Call editor observe_ui.", any_args),
            _tool(
                "foundry_find_elements",
                "Call editor find_elements.",
                _object_schema("find_elements arguments.", {"selector": selector}, ["selector"]),
            ),
            _tool(
                "foundry_act",
                "Call editor act.",
                _object_schema(
                    "act arguments.",
                    {"selector": selector, "action": _string_schema("Action name."), "args": any_args},
                    ["selector", "action"],
                ),
            ),
            _tool("foundry_wait_for", "Call editor wait_for.", any_args),
            _tool("foundry_read_editor_state", "Call editor read_editor_state.", any_args),
            _tool("foundry_read_editor_log", "Call editor read_editor_log.", any_args),
            _tool(
                "foundry_run_command",
                "Call editor run_command.",
                _object_schema("run_command arguments.", {"command": _string_schema("Command key or name.")}, ["command"]),
            ),
            _tool("foundry_list_commands", "Call editor list_commands.", any_args),
            _tool("foundry_poll_events", "Call editor poll_events.", any_args),
            _tool("foundry_capture_screenshot", "Call editor capture_screenshot.", any_args),
            _tool(
                "foundry_read_resource",
                "Read a Foundry editor MCP resource.",
                _object_schema("Resource read arguments.", {"uri": _string_schema("Resource URI.")}, ["uri"]),
            ),
        ]

    def _handle_tools_call(self, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name")
        if not isinstance(name, str) or not name:
            return self._tool_result({"ok": False, "kind": "invalid_params", "message": "tools/call requires a name."}, True)
        arguments = params.get("arguments", {})
        if arguments is None:
            arguments = {}
        if not isinstance(arguments, dict):
            return self._tool_result({"ok": False, "kind": "invalid_params", "message": "Tool arguments must be an object."}, True)

        try:
            if name == "foundry_launch_editor":
                return self._tool_result(self._tool_launch_editor(arguments), False)
            if name == "foundry_connect":
                return self._tool_result(self._tool_connect(arguments), False)
            if name == "foundry_disconnect":
                return self._tool_result(self._tool_disconnect(), False)
            if name == "foundry_status":
                return self._tool_result(self._status(), False)
            if name == "foundry_read_resource":
                return self._tool_result(self._read_resource(arguments), False)
            editor_tool = self._editor_tool_name(name)
            if editor_tool is not None:
                return self._proxy_tool(editor_tool, arguments)
        except FoundryMCPBridgeError as exc:
            return self._tool_result({"ok": False, "kind": exc.kind, "message": str(exc)}, True)
        except (FoundryAutomationStartupError, FoundryMCPError, OSError, ValueError, TypeError) as exc:
            return self._tool_result({"ok": False, "kind": "bridge_error", "message": str(exc)}, True)

        return self._tool_result({"ok": False, "kind": "unknown_tool", "message": f"Unknown bridge tool '{name}'."}, True)

    def _tool_launch_editor(self, arguments: dict[str, Any]) -> dict[str, Any]:
        binary = self._required_string(arguments, "binary")
        project = self._required_string(arguments, "project")
        self._close_existing_session()
        session = self.session_factory.launch(
            binary=Path(binary),
            project=Path(project),
            display=arguments.get("display", ":1"),
            port=arguments.get("port", 0),
            token=arguments.get("token"),
            extra_args=arguments.get("extra_args"),
            startup_timeout_s=float(arguments.get("startup_timeout_s", 180.0)),
        )
        self.session = session
        if arguments.get("initialize", True):
            session.client.initialize(client_name="foundry_mcp_stdio_bridge", client_version="1.0")
        return self._status()

    def _tool_connect(self, arguments: dict[str, Any]) -> dict[str, Any]:
        endpoint = self._required_string(arguments, "endpoint")
        token = self._required_string(arguments, "token")
        self._close_existing_session()
        session = self.session_factory.connect(endpoint, token)
        self.session = session
        if arguments.get("initialize", True):
            session.client.initialize(client_name="foundry_mcp_stdio_bridge", client_version="1.0")
        return self._status()

    def _tool_disconnect(self) -> dict[str, Any]:
        had_session = self.session is not None
        self._close_existing_session()
        return {"ok": True, "connected": False, "had_session": had_session}

    def _read_resource(self, arguments: dict[str, Any]) -> dict[str, Any]:
        session = self._require_session()
        uri = self._required_string(arguments, "uri")
        return session.client.read_resource(uri)

    def _proxy_tool(self, editor_tool: str, arguments: dict[str, Any]) -> dict[str, Any]:
        session = self._require_session()
        if editor_tool == "__generic__":
            name = self._required_string(arguments, "name")
            editor_arguments = arguments.get("arguments", {})
            if editor_arguments is None:
                editor_arguments = {}
            if not isinstance(editor_arguments, dict):
                raise ValueError("foundry_call_tool arguments.arguments must be an object.")
            return session.client.call_tool(name, editor_arguments)
        return session.client.call_tool(editor_tool, arguments)

    def _editor_tool_name(self, bridge_tool: str) -> str | None:
        mapping = {
            "foundry_call_tool": "__generic__",
            "foundry_observe_ui": "observe_ui",
            "foundry_find_elements": "find_elements",
            "foundry_act": "act",
            "foundry_wait_for": "wait_for",
            "foundry_read_editor_state": "read_editor_state",
            "foundry_read_editor_log": "read_editor_log",
            "foundry_run_command": "run_command",
            "foundry_list_commands": "list_commands",
            "foundry_poll_events": "poll_events",
            "foundry_capture_screenshot": "capture_screenshot",
        }
        return mapping.get(bridge_tool)

    def _require_session(self) -> Any:
        if self.session is None:
            raise FoundryMCPBridgeError("not_connected", "No Foundry editor automation session is connected.")
        return self.session

    def _close_existing_session(self) -> None:
        if self.session is not None:
            self.session.close()
            self.session = None

    def _status(self) -> dict[str, Any]:
        if self.session is None:
            return {"ok": True, "connected": False}
        client = self.session.client
        return {
            "ok": True,
            "connected": True,
            "endpoint": getattr(client, "endpoint", None),
            "initialized": bool(getattr(client, "initialized", False)),
        }

    @staticmethod
    def _required_string(arguments: dict[str, Any], field: str) -> str:
        value = arguments.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"Missing string argument '{field}'.")
        return value

    @staticmethod
    def _tool_result(structured: dict[str, Any], is_error: bool) -> dict[str, Any]:
        return {
            "content": [{"type": "text", "text": json.dumps(structured, separators=(",", ":"))}],
            "structuredContent": structured,
            "isError": is_error,
        }

    @staticmethod
    def _result(message_id: Any, result: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": JSONRPC_VERSION, "id": message_id, "result": result}

    @staticmethod
    def _error(message_id: Any, code: int, message: str, data: Any = None) -> dict[str, Any]:
        error: dict[str, Any] = {"code": code, "message": message}
        if data is not None:
            error["data"] = data
        return {"jsonrpc": JSONRPC_VERSION, "id": message_id, "error": error}


class FoundryMCPBridgeError(RuntimeError):
    def __init__(self, kind: str, message: str) -> None:
        super().__init__(message)
        self.kind = kind
