# Typed MCP Contracts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace raw MCP schema/input contract construction with typed C++ contracts and boundary validation for every editor automation MCP tool.

**Architecture:** Add a focused `editor_automation_mcp_contracts` layer that owns typed JSON Schema definitions, tool/resource definitions, input parsers, and stable output helpers. Keep the dispatcher as the JSON-RPC router, but parse each `tools/call` argument dictionary into a typed input before invoking existing automation internals.

**Tech Stack:** Godot/Foundry C++, `Dictionary`/`Array`/`Variant`, `RefCounted`/`Ref<T>`, doctest-based editor tests, SCons editor test build.

---

### Task 1: Contract Tests

**Files:**
- Modify: `tests/editor/test_editor_automation_mcp.h`

- [ ] **Step 1: Write failing tests for typed schemas and parser validation**

Add `#include "editor/automation/editor_automation_mcp_contracts.h"` next to the existing MCP dispatcher include.

Add tests near the existing MCP schema tests:

```cpp
TEST_CASE("[Editor][Automation][MCP] typed contract schemas expose stable fields") {
	const Array tools = EditorAutomationMCPContracts::build_tools_list();
	REQUIRE(tools.size() == 9);

	const Dictionary act = tool_named(tools, "act");
	REQUIRE_FALSE(act.is_empty());
	const Dictionary input = act["inputSchema"];
	const Dictionary props = input["properties"];
	CHECK(props.has("selector"));
	CHECK(props.has("action"));
	CHECK(props.has("args"));
	CHECK(props.has("wait"));
	const Dictionary action_schema = props["action"];
	CHECK(action_schema.has("enum"));
}

TEST_CASE("[Editor][Automation][MCP] typed tool inputs reject wrong argument types") {
	Dictionary bad_find;
	bad_find["selector"] = "button";
	const EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput> find_result =
			EditorAutomationMCPFindElementsInput::parse(bad_find);
	CHECK_FALSE(find_result.ok);
	CHECK(find_result.error.field == "selector");

	Dictionary bad_act;
	bad_act["action"] = 42;
	const EditorAutomationMCPParseResult<EditorAutomationMCPActInput> act_result =
			EditorAutomationMCPActInput::parse(bad_act);
	CHECK_FALSE(act_result.ok);
	CHECK(act_result.error.field == "action");
}

TEST_CASE("[Editor][Automation][MCP] tools/call reports invalid params for wrong argument types") {
	EditorAutomationMCPDispatcher dispatcher;

	Dictionary arguments;
	arguments["selector"] = "button";
	Dictionary params;
	params["name"] = "find_elements";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(50, "tools/call", params));
	CHECK(response.has("error"));
	const Dictionary error = response["error"];
	CHECK((int)error["code"] == EditorAutomationMCPDispatcher::INVALID_PARAMS);
	CHECK(String(error["message"]).contains("selector"));
}
```

- [ ] **Step 2: Run the focused build/test command to verify RED**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
```

Expected: compile fails because `editor_automation_mcp_contracts.h` and typed contract symbols do not exist yet.

### Task 2: Typed Contract Layer

**Files:**
- Create: `editor/automation/editor_automation_mcp_contracts.h`
- Create: `editor/automation/editor_automation_mcp_contracts.cpp`

- [ ] **Step 1: Add typed schema, tool/resource definitions, parse result, nested inputs, and tool input structs**

Create `EditorAutomationMCPJsonSchema`, `EditorAutomationMCPToolDefinition`, `EditorAutomationMCPResourceDefinition`, `EditorAutomationMCPResourceTemplateDefinition`, `EditorAutomationMCPParseResult<T>`, and input structs for every MCP tool.

Key implementation details:

- Use `FOUNDRY_SOFTCLASS(EditorAutomationMCPJsonSchema, RefCounted)` and `Ref<EditorAutomationMCPJsonSchema>` for recursive schema nodes.
- `to_dictionary()` must emit the same JSON Schema keywords currently used by `editor_automation_mcp_schemas.cpp`.
- Parser helpers must reject wrong types and invalid enum values.
- Each input struct must expose `static Ref<EditorAutomationMCPJsonSchema> schema()`, `static EditorAutomationMCPParseResult<T> parse(const Dictionary &)`, and `Dictionary to_dictionary() const`.
- Keep open-ended nested payloads as dictionaries only after validating the field type.

- [ ] **Step 2: Run the focused build to verify GREEN for contract symbols**

Run:

```bash
python3 -m SCons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
```

Expected: build may still fail because schema generation has not been rewired, but the missing-header/symbol failures from Task 1 should be gone.

### Task 3: Typed Schema Generation

**Files:**
- Modify: `editor/automation/editor_automation_mcp_schemas.cpp`
- Modify: `editor/automation/editor_automation_mcp_schemas.h` if needed

- [ ] **Step 1: Replace raw schema dictionary builders with contract delegation**

Update the public schema facade so:

```cpp
Array EditorAutomationMCPSchemas::build_tools_list() {
	return EditorAutomationMCPContracts::build_tools_list();
}
```

Use equivalent typed contract delegation for resources, resource templates, selector schema, action args schema, wait condition schema, log marker schema, and event marker schema.

- [ ] **Step 2: Run schema-focused tests**

Run after the build exists:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*MCP*tool schemas*" --force-colors
```

Expected: schema tests pass and `tools/list` still has the same public tool count and core field names.

### Task 4: Dispatcher Boundary Parsing

**Files:**
- Modify: `editor/automation/editor_automation_mcp_dispatcher.h`
- Modify: `editor/automation/editor_automation_mcp_dispatcher.cpp`

- [ ] **Step 1: Parse every `tools/call` argument dictionary into a typed input**

In `_handle_tools_call()`, for each known tool:

- call the matching `Input::parse(arguments)`
- on failure, set `r_ok = false`, `r_error["code"] = INVALID_PARAMS`, and include the contract error message
- pass `parsed.value.to_dictionary()` into the existing tool implementation for the first migration slice

This gives every tool typed boundary validation without rewriting the automation internals in the same change.

- [ ] **Step 2: Add typed stable output helpers where low-risk**

Use contract helpers for stable wrappers that do not require changing automation internals:

- log marker dictionaries
- event marker dictionaries
- common tool failure dictionaries
- element summary/tree dictionaries where the source data is already `EditorAutomationElement`

- [ ] **Step 3: Run MCP invalid-param tests**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*MCP*invalid params*" --force-colors
```

Expected: dispatcher returns JSON-RPC `INVALID_PARAMS` for wrong typed tool arguments.

### Task 5: Contract Coverage Tests

**Files:**
- Modify: `tests/editor/test_editor_automation_mcp.h`

- [ ] **Step 1: Add parser tests for every tool input**

Add test coverage that exercises at least one wrong-type field for each tool input:

- `MCPObserveUIInput`: `max_depth` wrong type
- `MCPFindElementsInput`: `selector` wrong type
- `MCPActInput`: `action` wrong type
- `MCPWaitForInput`: missing condition and missing wait id
- `MCPReadEditorLogInput`: `since` wrong type
- `MCPRunCommandInput`: missing `command`
- `MCPListCommandsInput`: `limit` wrong type
- `MCPPollEventsInput`: `kinds` wrong type

- [ ] **Step 2: Run all editor automation MCP tests**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*MCP*" --force-colors
```

Expected: doctest status is success. Cleanup leak noise may occur only if the final doctest summary is successful.

### Task 6: Final Build and Cleanup

**Files:**
- Review: all files touched in this plan

- [ ] **Step 1: Run formatting/sanity checks**

Run:

```bash
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 2: Run focused test suite**

Run:

```bash
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*MCP*" --force-colors
```

Expected: doctest status is success.

- [ ] **Step 3: Commit implementation**

Run:

```bash
git add editor/automation/editor_automation_mcp_contracts.h editor/automation/editor_automation_mcp_contracts.cpp editor/automation/editor_automation_mcp_schemas.h editor/automation/editor_automation_mcp_schemas.cpp editor/automation/editor_automation_mcp_dispatcher.h editor/automation/editor_automation_mcp_dispatcher.cpp tests/editor/test_editor_automation_mcp.h docs/superpowers/plans/2026-07-04-typed-mcp-contracts.md
git commit -m "Add typed MCP automation contracts"
```
