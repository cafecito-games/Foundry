# Foundry Editor Automation MCP Bridge

`scripts/foundry_mcp_server.py` exposes Foundry editor automation as a local
stdio MCP server. Register it once in an MCP-capable agent, then use normal tool
calls instead of writing one-off Python scripts.

The bridge is stdlib-only. It uses `scripts/foundry_mcp` internally to launch or
connect to the editor's HTTP MCP endpoint, retain the session, and proxy tool
calls.

## Register the Bridge

Run MCP clients from the repository root so the server can import
`scripts.foundry_mcp`.

Claude Code:

```sh
claude mcp add foundry-editor -- python3 scripts/foundry_mcp_server.py
claude mcp list
```

Project-scoped Claude config (`.mcp.json`):

```json
{
  "mcpServers": {
    "foundry-editor": {
      "type": "stdio",
      "command": "python3",
      "args": ["scripts/foundry_mcp_server.py"]
    }
  }
}
```

Codex config (`~/.codex/config.toml` or trusted project `.codex/config.toml`):

```toml
[mcp_servers.foundry-editor]
command = "python3"
args = ["scripts/foundry_mcp_server.py"]
cwd = "/Users/christian/CafecitoGames/Foundry"
startup_timeout_sec = 20
tool_timeout_sec = 120
```

Cursor project config (`.cursor/mcp.json`) or global config (`~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "foundry-editor": {
      "command": "python3",
      "args": ["scripts/foundry_mcp_server.py"]
    }
  }
}
```

Restart or reconnect the MCP client after editing config.

## Bridge Tools

The bridge exposes stable tools:

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

Use `foundry_call_tool` for newly added editor-side MCP tools before the bridge
has a dedicated wrapper.

## Start or Connect

When the agent should own the editor process, call `foundry_launch_editor`:

```json
{
  "binary": "bin/foundry.linuxbsd.editor.dev.x86_64",
  "project": "tests/fixtures/editor_automation_mvp",
  "display": ":1"
}
```

When another terminal already launched the editor with `--automation`, call
`foundry_connect`:

```json
{
  "endpoint": "http://127.0.0.1:3000/mcp",
  "token": "token-from-FOUNDRY_AUTOMATION"
}
```

Both tools initialize the editor MCP client by default. Pass
`"initialize": false` only when debugging the MCP handshake itself.

## Common Automation Loop

1. `foundry_launch_editor` or `foundry_connect`
2. `foundry_observe_ui` with a shallow `max_depth`
3. `foundry_find_elements` with semantic selectors
4. `foundry_act` using the selected role/name/handle
5. `foundry_wait_for` for modal, idle, import, or selector conditions
6. `foundry_poll_events` and `foundry_read_editor_log` for diagnostics
7. `foundry_disconnect`

Example tool arguments:

```json
{
  "selector": {
    "role": "button",
    "name": "Add Child Node"
  },
  "action": "click"
}
```

Bridge tool failures, such as calling `foundry_observe_ui` before connecting,
return MCP tool results with `isError: true` and structured diagnostics. JSON-RPC
or HTTP failures inside the bridge are also returned as structured tool errors
where possible, so the agent can inspect the result and decide what to do next.

## Direct Python Client

Use the direct Python client for checked-in scripts and tests that need custom
control over process lifetime:

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

Agents should prefer the stdio MCP bridge for interactive work because it gives
them real tool calls and avoids generated scratch scripts.
