# Foundry MCP Python Client Design

**Date:** 2026-07-08
**Status:** Approved

## Problem

Agents currently have to rewrite small Python scripts whenever they need to talk
to the Foundry editor automation MCP server. The repository already has a useful
one-off client embedded in `scripts/exercise_editor_mcp.py`, but that client is
mixed with smoke-test fixture setup and protocol assertions. It is not a stable
importable surface for agents or future scripts.

The new client should let agents either connect to an existing automation
endpoint or launch an editor process themselves, parse the
`FOUNDRY_AUTOMATION` startup line, connect, and clean up the process reliably.

## Goals

- Provide a small reusable Python client for Foundry's POST-only HTTP MCP
  transport.
- Provide a stdio MCP bridge that agents can register as a normal tool server,
  so they do not need to write Python scripts for each interaction.
- Support both workflows:
  - connect to an already-running editor automation endpoint;
  - launch `foundry editor open --automation`, discover the endpoint/token, and
    own process shutdown.
- Keep the first implementation stdlib-only so it works in the engine checkout
  without dependency setup.
- Reuse the client from `scripts/exercise_editor_mcp.py` so the smoke exercise no
  longer carries copy-pasted protocol code.
- Document the recommended agent usage pattern in `AGENTS.md` so future agents
  use the reusable client instead of rewriting MCP boilerplate.
- Document how to expose this client as an external tool/workflow for Claude,
  Codex, and Cursor.
- Cover JSON-RPC, tool/resource helpers, startup-line parsing, auth headers,
  structured MCP errors, and process lifecycle with Python unit tests.

## Non-Goals

- Do not implement a general MCP SDK.
- Do not add a dependency unless the stdlib approach becomes inadequate. If a
  future dependency is needed, install and manage it with `uv`.
- Do not replace the editor automation C++ tests.
- Do not hide server-side tool failures. Tool results with `isError: true` must
  remain inspectable by callers.
- Do not require a built editor binary for the Python unit tests.

## Architecture

The client has two layers.

`FoundryMCPClient` is the protocol layer. It owns endpoint, bearer token, request
IDs, JSON-RPC request/notification formatting, HTTP POST handling, MCP
initialization, and small convenience helpers for tools and resources. It does
not know how to launch the editor.

`FoundryEditorAutomationSession` is the lifecycle layer. It launches the editor
when needed, reads stdout until the `FOUNDRY_AUTOMATION` line appears, creates a
`FoundryMCPClient`, and terminates the process on context-manager exit. It also
has a connect-only constructor for existing endpoints.

`FoundryMCPStdioServer` is the agent-facing bridge. It is a small stdlib MCP
server that runs over stdin/stdout, owns zero or one `FoundryEditorAutomationSession`,
and exposes stable tools such as `foundry_launch_editor`,
`foundry_connect`, `foundry_observe_ui`, `foundry_act`, and
`foundry_call_tool`. The bridge delegates editor work to the HTTP client and
session helper. It does not reimplement editor automation behavior.

The expected file layout is:

```text
scripts/foundry_mcp/
  __init__.py
  client.py
  session.py
  stdio_server.py
scripts/foundry_mcp_server.py
scripts/tests/
  test_foundry_mcp_client.py
  test_foundry_mcp_session.py
  test_foundry_mcp_stdio_server.py
AGENTS.md
docs/editor_automation_mcp_client.md
```

## Client API

The protocol layer should expose:

```python
client = FoundryMCPClient(endpoint, token)
client.initialize()
client.request("tools/list")
client.notify("notifications/initialized")
client.list_tools()
client.call_tool("observe_ui", {"max_depth": 4})
client.structured_tool("observe_ui", {"max_depth": 4})
client.list_resources()
client.list_resource_templates()
client.read_resource("foundry://editor/state")
```

Editor-specific tool wrappers are intentionally thin:

```python
client.observe_ui(max_depth=4, max_children=20)
client.find_elements({"role": "button", "name": "Add Child Node"})
client.act({"role": "button", "name": "Add Child Node"}, "click")
client.wait_for("editor_idle", timeout_ms=15000)
client.read_editor_state()
client.read_editor_log(limit=20)
client.run_command("editor/save_scene")
client.list_commands(query="save", limit=20)
client.poll_events()
client.capture_screenshot()
```

Wrappers should return the complete MCP tool result, not only the structured
payload. `structured_tool()` is available when a caller explicitly wants
`structuredContent`.

## Session API

The lifecycle layer should support launching:

```python
with FoundryEditorAutomationSession.launch(
    binary=Path("bin/foundry.linuxbsd.editor.dev.x86_64"),
    project=Path("tests/fixtures/editor_automation_mvp"),
    display=":1",
) as session:
    session.client.initialize()
    state = session.client.read_editor_state()
```

It should also support connect-only use:

```python
session = FoundryEditorAutomationSession.connect(endpoint, token)
session.client.initialize()
```

Launch should accept `port=0`, optional `token`, optional `extra_args`, optional
environment overrides, and a startup timeout. It should use the supported
command-first CLI:

```text
foundry editor open --project <project> --automation --automation-transport=mcp
```

When a port or token is provided, it should append `--automation-port <port>` and
`--automation-token <token>`.

## Stdio MCP Bridge API

The bridge should be launched by MCP clients as a local stdio server:

```sh
python3 scripts/foundry_mcp_server.py
```

It should expose these tools:

- `foundry_launch_editor`
- `foundry_connect`
- `foundry_disconnect`
- `foundry_status`
- `foundry_call_tool`
- `foundry_observe_ui`
- `foundry_find_elements`
- `foundry_act`
- `foundry_wait_for`
- `foundry_read_editor_state`
- `foundry_read_editor_log`
- `foundry_run_command`
- `foundry_list_commands`
- `foundry_poll_events`
- `foundry_capture_screenshot`
- `foundry_read_resource`

`foundry_launch_editor` and `foundry_connect` should initialize the editor MCP
client by default, unless `initialize: false` is passed. The bridge should retain
the session for later tool calls until `foundry_disconnect` or process exit.

Bridge tool failures, such as calling `foundry_observe_ui` before connecting,
should return MCP tool results with `isError: true` and structured diagnostics.
Protocol errors, such as malformed JSON-RPC requests or unknown bridge methods,
should return JSON-RPC errors.

The bridge should support a common fallback path with `foundry_call_tool`, so
new editor-side MCP tools can be used before the bridge grows a dedicated
wrapper.

## Error Handling

The client should raise a dedicated `FoundryMCPError` for JSON-RPC error
responses and HTTP errors. The exception should preserve the method, error code,
message, response data, and raw HTTP body when available.

Tool-level failures are different from JSON-RPC failures. A successful
`tools/call` response can contain `isError: true`; `call_tool()` should return
that result without raising so callers can inspect server diagnostics. A helper
can be added later for callers that want exceptions on tool-level errors.

Startup failures should raise `FoundryAutomationStartupError` with the captured
editor output. If the editor prints `FOUNDRY_AUTOMATION_ERROR`, the exception
message should include that line.

## Testing

Python unit tests should use stdlib `unittest` and avoid launching the real
editor.

Client tests should run against a tiny fake HTTP server and verify:

- `initialize()` sends the protocol version and then
  `notifications/initialized`;
- requests include `Authorization: Bearer <token>` and `Content-Type:
  application/json`;
- JSON-RPC errors raise `FoundryMCPError`;
- HTTP errors raise `FoundryMCPError` with the raw body;
- tool calls return full MCP tool results, including `isError: true`;
- `structured_tool()` extracts `structuredContent`;
- resources and resource templates are listed/read through the expected MCP
  methods.

Session tests should avoid a real editor process and verify:

- `parse_automation_line()` extracts endpoint and token from a
  `FOUNDRY_AUTOMATION {...}` line;
- startup parsing rejects malformed lines and `FOUNDRY_AUTOMATION_ERROR`;
- launch builds the command-first editor invocation;
- context-manager exit terminates the child process and kills it if graceful
  termination times out.

`scripts/exercise_editor_mcp.py` remains the integration exercise against a real
editor binary, but it should import `FoundryMCPClient` and update its expected
tool list to include `capture_screenshot`.

## Documentation

`AGENTS.md` should tell agents to import `scripts.foundry_mcp` before writing
custom MCP HTTP code. It should show the two common paths: `FoundryEditorAutomationSession.launch()`
when the agent owns the editor process and `FoundryEditorAutomationSession.connect()`
when the editor is already running.

`docs/editor_automation_mcp_client.md` should be the longer user-facing guide.
It should include:

- stdio bridge registration examples for Claude Code, Codex, and Cursor;
- launch/connect examples for direct Python use;
- observe/select/act/wait examples;
- guidance that `call_tool()` returns full MCP tool results while
  `structured_tool()` extracts `structuredContent`;
- notes about `isError: true` tool results versus JSON-RPC exceptions;
- how to add this as an external helper/tooling layer in Claude, Codex, and
  Cursor, using repository-local Python commands and environment variables for
  endpoint/token handoff.
