# Foundry MCP Stdio Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose Foundry editor automation as a normal stdio MCP server that Claude, Codex, Cursor, and other agents can register once and call as tools.

**Architecture:** Add `scripts/foundry_mcp/stdio_server.py` for JSON-RPC/MCP stdio handling and `scripts/foundry_mcp_server.py` as the executable entrypoint. The bridge owns one optional `FoundryEditorAutomationSession` and delegates all editor work to the existing session/client layer.

**Tech Stack:** Python 3.8-compatible stdlib, MCP JSON-RPC over newline-delimited stdio, existing `scripts.foundry_mcp` client/session helpers, `unittest`.

---

## File Structure

- Create `scripts/foundry_mcp/stdio_server.py`: stdio MCP server, bridge tool schemas, request routing, session ownership.
- Create `scripts/foundry_mcp_server.py`: executable repo-root entrypoint for MCP clients.
- Create `scripts/tests/test_foundry_mcp_stdio_server.py`: direct JSON-RPC handler and fake session/client tests.
- Modify `scripts/foundry_mcp/__init__.py`: export `FoundryMCPStdioServer`.
- Modify `AGENTS.md`: make stdio bridge the primary recommendation.
- Modify `docs/editor_automation_mcp_client.md`: add concrete Claude Code, Codex, and Cursor registration snippets.

## Tasks

- [ ] Add failing tests for initialize/tools/list, connect, proxy calls, no-session errors, disconnect, and line-based stdio serving.
- [ ] Implement `FoundryMCPStdioServer` minimally to pass the tests.
- [ ] Add executable `scripts/foundry_mcp_server.py`.
- [ ] Update docs and AGENTS.md to lead with stdio bridge registration.
- [ ] Run focused Python tests and `py_compile`.
