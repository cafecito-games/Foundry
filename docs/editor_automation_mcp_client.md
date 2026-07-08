# Foundry Editor Automation MCP Python Client

`scripts/foundry_mcp` is the repository-local Python client for Foundry's editor
automation MCP server. Use it from ad hoc agent scripts, smoke tests, and local
tool wrappers instead of rewriting JSON-RPC/HTTP boilerplate.

The package is intentionally stdlib-only. If a future wrapper needs third-party
dependencies, manage them with `uv`; do not vendor dependency setup into agent
prompts.

## Launch and Own the Editor

Use this path when the agent should start and stop the editor process.

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

`launch()` uses the supported command-first CLI, waits for the
`FOUNDRY_AUTOMATION` line, creates a client from the endpoint/token, and
terminates the editor process when the context manager exits.

## Connect to an Existing Editor

Use this path when another terminal, IDE task, or agent already launched the
editor with `--automation`.

```python
from scripts.foundry_mcp import FoundryEditorAutomationSession

session = FoundryEditorAutomationSession.connect(
    "http://127.0.0.1:3000/mcp",
    "token-from-FOUNDRY_AUTOMATION",
)
session.client.initialize()
state = session.client.structured_tool("read_editor_state")
```

Prefer passing endpoint and token through environment variables when wrapping
this for another agent:

```sh
export FOUNDRY_MCP_ENDPOINT="http://127.0.0.1:3000/mcp"
export FOUNDRY_MCP_TOKEN="token-from-FOUNDRY_AUTOMATION"
```

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

## Thin Tool Wrapper Pattern

Claude, Codex, and Cursor integrations should expose a small command wrapper
that imports `scripts.foundry_mcp`, reads input from CLI flags or JSON stdin,
and prints JSON output. Keep the wrapper as the tool boundary; keep MCP
transport, auth headers, request IDs, startup parsing, and process cleanup in
`scripts/foundry_mcp`.

A wrapper should follow this contract:

- Run from the repository root so `scripts.foundry_mcp` imports directly.
- Use `FOUNDRY_MCP_ENDPOINT` and `FOUNDRY_MCP_TOKEN` for connect-only mode.
- Accept `--binary` and `--project` only when the wrapper should launch the editor.
- Print only machine-readable JSON on stdout.
- Send logs and human status text to stderr.
- Return a non-zero exit code for JSON-RPC/HTTP/startup failures.
- Preserve tool-level failures as JSON results with `isError: true`.

## Claude

For Claude Code, the simplest reliable integration is a repository-local Python
wrapper invoked through the shell/tool command Claude already has. The wrapper
should import `scripts.foundry_mcp` and expose task-specific commands such as
`observe-ui`, `act`, or `read-state`.

For Claude environments that can register custom command tools, register the
wrapper command rather than pasting MCP code into prompts. Pass endpoint/token as
environment variables for an existing editor:

```sh
FOUNDRY_MCP_ENDPOINT=http://127.0.0.1:3000/mcp \
FOUNDRY_MCP_TOKEN=... \
python3 scripts/my_foundry_mcp_tool.py read-state
```

If the Claude environment supports connecting directly to authenticated HTTP MCP
servers, connecting to Foundry's printed endpoint is also reasonable. The Python
client remains useful for repeatable scripted workflows and smoke tests.

## Codex

Codex agents working in this repository can import the package directly from a
Python one-liner or from a checked-in helper script:

```sh
python3 - <<'PY'
import json
import os

from scripts.foundry_mcp import FoundryEditorAutomationSession

session = FoundryEditorAutomationSession.connect(
    os.environ["FOUNDRY_MCP_ENDPOINT"],
    os.environ["FOUNDRY_MCP_TOKEN"],
)
session.client.initialize()
print(json.dumps(session.client.structured_tool("read_editor_state")))
PY
```

For repeated use, add a small script under `scripts/` that wraps one workflow
and returns JSON. Prefer that script over embedding transport code into Codex
instructions or generated scratch files.

## Cursor

Use a Cursor project task, command, or tool configuration that runs a Python
wrapper from the repository root. For an already-running editor, pass
`FOUNDRY_MCP_ENDPOINT` and `FOUNDRY_MCP_TOKEN` through the task environment. For
workflows where Cursor should start the editor, let the wrapper call
`FoundryEditorAutomationSession.launch()`.

Keep Cursor-side configuration thin: it should name the wrapper command, pass
arguments/environment, and consume JSON. Editor-specific MCP details should stay
inside `scripts/foundry_mcp`.
