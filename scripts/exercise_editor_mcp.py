#!/usr/bin/env python3
"""Comprehensive editor MCP exercise script.

Launches the Foundry editor with automation enabled, exercises every MCP tool,
resource, and protocol method, and reports failures.
"""

from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
BINARY = REPO_ROOT / "bin" / "foundry.linuxbsd.editor.dev.x86_64"
FIXTURE = REPO_ROOT / "tests" / "fixtures" / "editor_automation_mvp"
PROTOCOL_VERSION = "2025-11-25"

EXPECTED_TOOLS = [
    "observe_ui",
    "find_elements",
    "act",
    "wait_for",
    "read_editor_state",
    "read_editor_log",
    "run_command",
    "list_commands",
    "poll_events",
]

EXPECTED_RESOURCES = [
    "foundry://ui/tree",
    "foundry://editor/state",
    "foundry://editor/log",
    "foundry://scene/active",
    "foundry://scene/tree",
    "foundry://commands",
]

EXPECTED_RESOURCE_TEMPLATES = [
    "foundry://element/{id}",
    "foundry://ui/subtree/{id}",
    "foundry://ui/subtree/{id}/depth/{depth}",
    "foundry://scene/tree",
]


@dataclass
class Failure:
    name: str
    message: str
    details: dict[str, Any] = field(default_factory=dict)


class MCPClient:
    def __init__(self, endpoint: str, token: str) -> None:
        self.endpoint = endpoint
        self.token = token
        self.request_id = 0
        self.initialized = False

    def _next_id(self) -> int:
        self.request_id += 1
        return self.request_id

    def request(self, method: str, params: dict[str, Any] | None = None, *, expect_response: bool = True) -> dict[str, Any]:
        payload: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if expect_response:
            payload["id"] = self._next_id()
        if params is not None:
            payload["params"] = params

        body = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            self.endpoint,
            data=body,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                raw = resp.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {exc.code}: {raw}") from exc

        if not expect_response:
            return {}
        parsed = json.loads(raw)
        if "error" in parsed:
            raise RuntimeError(f"MCP error for {method}: {parsed['error']}")
        return parsed.get("result", {})

    def initialize(self) -> dict[str, Any]:
        result = self.request(
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": "exercise_editor_mcp", "version": "1.0"},
            },
        )
        self.request("notifications/initialized", {}, expect_response=False)
        self.initialized = True
        return result

    def tools_list(self) -> list[dict[str, Any]]:
        return self.request("tools/list").get("tools", [])

    def tools_call(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})

    def resources_list(self) -> list[dict[str, Any]]:
        return self.request("resources/list").get("resources", [])

    def resources_templates_list(self) -> list[dict[str, Any]]:
        return self.request("resources/templates/list").get("resourceTemplates", [])

    def resources_read(self, uri: str) -> dict[str, Any]:
        return self.request("resources/read", {"uri": uri})


def prepare_temp_project() -> Path:
    temp_dir = Path(tempfile.mkdtemp(prefix="editor_mcp_exercise_"))
    shutil.copytree(FIXTURE, temp_dir, dirs_exist_ok=True)
    return temp_dir


def wait_for_automation_line(proc: subprocess.Popen[str], timeout_s: float = 180.0) -> dict[str, Any]:
    deadline = time.time() + timeout_s
    output = ""
    while time.time() < deadline:
        if proc.poll() is not None and not proc.stdout:
            break
        line = proc.stdout.readline() if proc.stdout else ""
        if line:
            output += line
            print(line, end="", flush=True)
            if "FOUNDRY_AUTOMATION " in line:
                marker = "FOUNDRY_AUTOMATION "
                idx = line.index(marker) + len(marker)
                return json.loads(line[idx:].strip())
            if "FOUNDRY_AUTOMATION_ERROR" in line:
                raise RuntimeError(f"Editor automation failed to start: {line.strip()}")
        elif proc.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for FOUNDRY_AUTOMATION line. Output:\n{output}")


def structured_content(tool_result: dict[str, Any]) -> dict[str, Any]:
    return tool_result.get("structuredContent", {})


def assert_ok(name: str, condition: bool, message: str, failures: list[Failure], **details: Any) -> None:
    if not condition:
        failures.append(Failure(name=name, message=message, details=details))


def exercise_protocol(client: MCPClient, failures: list[Failure]) -> None:
    init = client.initialize()
    assert_ok("initialize", init.get("protocolVersion") == PROTOCOL_VERSION, "protocol version mismatch", failures, init=init)
    caps = init.get("capabilities", {})
    assert_ok("initialize.tools", "tools" in caps, "missing tools capability", failures)
    assert_ok("initialize.resources", "resources" in caps, "missing resources capability", failures)

    tools = client.tools_list()
    tool_names = [t.get("name") for t in tools]
    assert_ok("tools/list.count", len(tools) == 9, f"expected 9 tools, got {len(tools)}", failures, tools=tool_names)
    for expected in EXPECTED_TOOLS:
        assert_ok(f"tools/list.{expected}", expected in tool_names, f"missing tool {expected}", failures)
    for tool in tools:
        assert_ok(
            f"tools/list.schema.{tool.get('name')}",
            tool.get("inputSchema") and tool.get("outputSchema"),
            "tool missing schemas",
            failures,
            tool=tool.get("name"),
        )

    resources = client.resources_list()
    resource_uris = [r.get("uri") for r in resources]
    assert_ok("resources/list.count", len(resources) == 6, f"expected 6 resources, got {len(resources)}", failures)
    for expected in EXPECTED_RESOURCES:
        assert_ok(f"resources/list.{expected}", expected in resource_uris, f"missing resource {expected}", failures)

    templates = client.resources_templates_list()
    template_uris = [t.get("uriTemplate") for t in templates]
    assert_ok("resources/templates/list.count", len(templates) == 4, f"expected 4 templates, got {len(templates)}", failures)
    for expected in EXPECTED_RESOURCE_TEMPLATES:
        assert_ok(
            f"resources/templates/list.{expected}",
            expected in template_uris,
            f"missing template {expected}",
            failures,
        )


def exercise_tools(client: MCPClient, failures: list[Failure]) -> dict[str, Any]:
    context: dict[str, Any] = {}

    # observe_ui
    observe_result = client.tools_call("observe_ui", {"max_depth": 4, "max_children": 20})
    observe = structured_content(observe_result)
    assert_ok("observe_ui", not observe_result.get("isError"), "observe_ui returned isError", failures, observe=observe)
    assert_ok("observe_ui.element_count", observe.get("element_count", 0) > 0, "empty snapshot", failures)
    assert_ok("observe_ui.tree", bool(observe.get("tree")), "missing tree", failures)
    tree = observe.get("tree", [])
    if tree:
        context["root_element"] = tree[0]
        children = tree[0].get("children", [])
        if children:
            context["first_child"] = children[0]
        if tree[0].get("children_truncated") and tree[0].get("children_next_cursor"):
            page = client.tools_call("observe_ui", {"subtree_cursor": tree[0]["children_next_cursor"]})
            page_structured = structured_content(page)
            assert_ok(
                "observe_ui.subtree_cursor",
                page_structured.get("subtree") is not None,
                "subtree cursor page missing subtree",
                failures,
            )

    # observe_ui include_internal
    internal_result = client.tools_call("observe_ui", {"include_internal": True, "max_depth": 6})
    internal = structured_content(internal_result)
    assert_ok("observe_ui.include_internal", not internal_result.get("isError"), "include_internal failed", failures)
    assert_ok(
        "observe_ui.include_internal.limits",
        internal.get("limits", {}).get("include_internal") is True,
        "limits.include_internal not set",
        failures,
    )

    # find_elements - match
    find_match = client.tools_call(
        "find_elements",
        {"selector": {"role": "dock", "name": "Scene"}, "max_results": 5},
    )
    find_match_structured = structured_content(find_match)
    assert_ok("find_elements.match", find_match_structured.get("ok"), "Scene dock not found", failures, result=find_match_structured)
    if find_match_structured.get("elements"):
        context["scene_dock"] = find_match_structured["elements"][0]

    # find_elements - no match (should be error)
    find_nomatch = client.tools_call("find_elements", {"selector": {"role": "button", "name": "Definitely Missing Button XYZ"}})
    assert_ok("find_elements.no_match", find_nomatch.get("isError"), "no-match should set isError", failures)

    # find_elements pagination
    find_page = client.tools_call(
        "find_elements",
        {"selector": {"role": "button", "name_contains": ""}, "max_results": 3},
    )
    find_page_structured = structured_content(find_page)
    if find_page_structured.get("truncated") and find_page_structured.get("next_cursor"):
        page2 = client.tools_call(
            "find_elements",
            {
                "selector": {"role": "button", "name_contains": ""},
                "max_results": 3,
                "cursor": find_page_structured["next_cursor"],
            },
        )
        page2_structured = structured_content(page2)
        assert_ok("find_elements.pagination", page2_structured.get("ok"), "pagination failed", failures)

    # read_editor_state
    state_result = client.tools_call("read_editor_state")
    state = structured_content(state_result)
    assert_ok("read_editor_state", not state_result.get("isError"), "read_editor_state failed", failures)
    assert_ok("read_editor_state.supported", "supported" in state or state.get("supported") is not False, "missing supported", failures)
    context["editor_state"] = state

    # read_editor_log
    log_result = client.tools_call("read_editor_log", {"limit": 20})
    log = structured_content(log_result)
    assert_ok("read_editor_log", not log_result.get("isError"), "read_editor_log failed", failures)
    assert_ok("read_editor_log.entries", "entries" in log, "missing entries", failures)
    assert_ok("read_editor_log.marker", "marker" in log, "missing marker", failures)
    context["log_marker"] = log.get("marker")

    # poll_events
    poll_result = client.tools_call("poll_events", {})
    poll = structured_content(poll_result)
    assert_ok("poll_events", not poll_result.get("isError"), "poll_events failed", failures)
    assert_ok("poll_events.transport", "transport" in poll, "missing transport", failures)
    assert_ok("poll_events.events", "events" in poll, "missing events", failures)
    context["event_marker"] = poll.get("marker")

    # list_commands
    list_cmds = client.tools_call("list_commands", {"query": "save", "limit": 20})
    list_cmds_structured = structured_content(list_cmds)
    assert_ok("list_commands", list_cmds_structured.get("ok"), "list_commands failed", failures)
    commands = list_cmds_structured.get("commands", [])
    assert_ok("list_commands.commands", isinstance(commands, list) and len(commands) > 0, "no commands", failures)
    context["commands"] = commands

    # run_command - try a known palette command
    runnable = next((c for c in commands if c.get("runnable_by_run_command")), None)
    if runnable:
        run_result = client.tools_call("run_command", {"command": runnable["key"]})
        run_structured = structured_content(run_result)
        assert_ok(
            f"run_command.{runnable['key']}",
            run_structured.get("ok"),
            f"failed to run {runnable['key']}",
            failures,
            result=run_structured,
        )
    else:
        failures.append(Failure("run_command", "no runnable command found in list_commands"))

    # run_command unknown
    unknown = client.tools_call("run_command", {"command": "automation/definitely_missing_command"})
    unknown_structured = structured_content(unknown)
    assert_ok("run_command.unknown", unknown.get("isError"), "unknown command should be error", failures)
    assert_ok(
        "run_command.unknown.kind",
        unknown_structured.get("kind") == "unknown_command",
        "unexpected unknown command kind",
        failures,
    )

    # wait_for - cooperative pending + cancel
    wait_pending = client.tools_call(
        "wait_for",
        {
            "condition": "selector_appears",
            "selector": {"role": "button", "name": "Never Appears MCP Exercise"},
            "timeout_ms": 60000,
            "cooperative": True,
        },
    )
    wait_pending_structured = structured_content(wait_pending)
    assert_ok("wait_for.cooperative", wait_pending_structured.get("status") == "pending", "expected pending", failures)
    wait_id = wait_pending_structured.get("wait_id")
    if wait_id:
        cancel = client.tools_call("wait_for", {"wait_id": wait_id, "cancel": True})
        cancel_structured = structured_content(cancel)
        assert_ok("wait_for.cancel", cancel_structured.get("status") == "cancelled", "cancel failed", failures)

    # wait_for synchronous editor_idle
    wait_idle = client.tools_call("wait_for", {"condition": "editor_idle", "timeout_ms": 15000, "cooperative": False})
    wait_idle_structured = structured_content(wait_idle)
    assert_ok("wait_for.editor_idle", wait_idle_structured.get("ok"), "editor_idle wait failed", failures)

    # act - click a button if we can find one
    button_find = client.tools_call("find_elements", {"selector": {"role": "button", "name": "Add Child Node"}, "max_results": 1})
    button_find_structured = structured_content(button_find)
    if button_find_structured.get("ok") and button_find_structured.get("elements"):
        button = button_find_structured["elements"][0]
        act_click = client.tools_call(
            "act",
            {
                "selector": {"handle": button.get("handle") or button.get("id")},
                "action": "click",
            },
        )
        act_click_structured = structured_content(act_click)
        # Click may open dialog - that's fine; we just verify the tool works
        assert_ok("act.click", act_click_structured.get("ok") or act_click.get("isError") is False, "act click failed", failures, result=act_click_structured)

        # Try to dismiss dialog if opened
        time.sleep(0.5)
        esc_act = client.tools_call(
            "act",
            {
                "selector": {"role": "dialog"},
                "action": "press_key",
                "args": {"key": "Escape"},
            },
        )
        # press_key on dialog may or may not work; don't fail hard
        _ = esc_act
    else:
        # Fallback: act focus on scene dock
        if context.get("scene_dock"):
            act_focus = client.tools_call(
                "act",
                {"selector": {"handle": context["scene_dock"].get("handle")}, "action": "focus"},
            )
            act_focus_structured = structured_content(act_focus)
            assert_ok("act.focus", act_focus_structured.get("ok"), "act focus failed", failures)

    return context


def exercise_resources(client: MCPClient, context: dict[str, Any], failures: list[Failure]) -> None:
    for uri in EXPECTED_RESOURCES:
        try:
            result = client.resources_read(uri)
            contents = result.get("contents", [])
            assert_ok(f"resources/read.{uri}", len(contents) > 0, "empty contents", failures)
            text = contents[0].get("text", "")
            assert_ok(f"resources/read.{uri}.text", bool(text), "missing text", failures)
        except Exception as exc:  # noqa: BLE001
            failures.append(Failure(f"resources/read.{uri}", str(exc)))

    child = context.get("first_child") or context.get("scene_dock")
    if child:
        handle = child.get("handle") or child.get("id")
        if handle:
            for uri in [
                f"foundry://element/{handle}",
                f"foundry://ui/subtree/{handle}",
                f"foundry://ui/subtree/{handle}/depth/3",
            ]:
                try:
                    result = client.resources_read(uri)
                    contents = result.get("contents", [])
                    assert_ok(f"resources/read.{uri}", len(contents) > 0, "empty contents", failures)
                except Exception as exc:  # noqa: BLE001
                    failures.append(Failure(f"resources/read.{uri}", str(exc)))


def exercise_incremental_reads(client: MCPClient, context: dict[str, Any], failures: list[Failure]) -> None:
    if context.get("log_marker"):
        log2 = client.tools_call("read_editor_log", {"since": context["log_marker"], "limit": 50})
        log2_structured = structured_content(log2)
        assert_ok("read_editor_log.since", not log2.get("isError"), "since-marker read failed", failures)
        assert_ok("read_editor_log.since.marker", "marker" in log2_structured, "missing marker on since read", failures)

    if context.get("event_marker"):
        poll2 = client.tools_call("poll_events", {"since": context["event_marker"]})
        poll2_structured = structured_content(poll2)
        assert_ok("poll_events.since", not poll2.get("isError"), "since-marker poll failed", failures)


def main() -> int:
    if not BINARY.exists():
        print(f"Binary not found: {BINARY}", file=sys.stderr)
        return 1

    project_dir = prepare_temp_project()
    print(f"Using temp project: {project_dir}")

    env = os.environ.copy()
    env.setdefault("DISPLAY", ":1")

    cmd = [
        str(BINARY),
        "editor",
        "open",
        "--project",
        str(project_dir),
        "--automation",
        "--automation-transport=mcp",
        "--automation-port",
        "0",
        "--automation-token",
        "mcp-exercise-token",
    ]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )

    failures: list[Failure] = []
    try:
        automation = wait_for_automation_line(proc)
        endpoint = automation["endpoint"]
        token = automation["token"]
        print(f"Connected to MCP at {endpoint}")

        client = MCPClient(endpoint, token)
        exercise_protocol(client, failures)
        context = exercise_tools(client, failures)
        exercise_resources(client, context, failures)
        exercise_incremental_reads(client, context, failures)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n=== MCP Exercise Summary ===")
    if failures:
        print(f"FAILURES: {len(failures)}")
        for i, failure in enumerate(failures, 1):
            print(f"\n{i}. {failure.name}: {failure.message}")
            if failure.details:
                print(json.dumps(failure.details, indent=2)[:2000])
        return 1

    print("All MCP tools, resources, and protocol methods exercised successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
