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
