# Typed MCP Contracts Design

**Date:** 2026-07-04
**Status:** Draft for review

## Problem

The editor automation MCP surface currently defines tool schemas and protocol payloads with raw
`Dictionary` construction. Helper functions reduce some repetition, but schema keywords, field names,
input parsing, and output payloads still depend on string literals such as `"inputSchema"`,
`"properties"`, `"selector"`, `"ok"`, `"kind"`, and `"message"`.

This creates three problems:

- Schema shape mistakes are caught only by tests or clients.
- Runtime tool parsing can drift away from the schemas advertised by `tools/list`.
- Wrong argument types often fall back to defaults instead of producing clear MCP `invalid_params`
  errors.

The goal is to move MCP schemas, tool inputs, and stable tool outputs behind typed C++ contracts so
field names and field types are centrally defined and mechanically checked as much as C++ allows.

## Goals

- Define MCP schema, input, and stable output contracts as typed C++ structs.
- Keep `Dictionary` only at the JSON-RPC boundary and for intentionally open-ended payload fields.
- Make required fields and wrong-type arguments fail through structured `invalid_params` errors.
- Share enum definitions between schema generation and runtime parsing.
- Keep the existing public MCP protocol shape compatible during the first migration.
- Preserve the existing dispatcher and automation internals where they are not directly part of the
  protocol contract.

## Non-goals

- No broad macro or reflection system in the first pass.
- No full rewrite of editor automation internals.
- No immediate removal of flexible diagnostic dictionaries such as `metadata`, `details`, command
  entries, traces, and editor state snapshots.
- No immediate strict rejection of all unknown input fields. The contract layer should support this,
  but compatibility can be tightened per tool later.

## Design

### 1. Contract Layer

Add a dedicated typed MCP contract layer under `editor/automation/`:

- `editor_automation_mcp_contracts.h`
- `editor_automation_mcp_contracts.cpp`

This layer owns MCP-facing schema construction, input parsing, and stable output serialization.
`EditorAutomationMCPDispatcher` remains responsible for JSON-RPC routing and calling the existing
automation systems, but it should stop reading tool argument dictionaries directly once a tool has a
typed input contract.

The existing `EditorAutomationMCPSchemas` facade can stay in place initially. Its implementation can
delegate to typed `MCPToolDefinition` and resource definitions so existing callers continue to use
`EditorAutomationMCPDispatcher::build_tools_list()`.

### 2. Shared Typed Schema Model

Introduce small explicit schema and definition structs:

```cpp
class MCPJsonSchema : public RefCounted {
public:
	enum class Type {
		OBJECT,
		ARRAY,
		STRING,
		INTEGER,
		NUMBER,
		BOOLEAN,
	};

	struct Property {
		StringName name;
		Ref<MCPJsonSchema> schema;
	};

	Type type = Type::OBJECT;
	String description;
	LocalVector<Property> properties;
	Vector<StringName> required;
	Vector<String> enum_values;
	Ref<MCPJsonSchema> items;
	bool additional_properties = true;

	Dictionary to_dictionary() const;
};

struct MCPToolDefinition {
	String name;
	String description;
	Ref<MCPJsonSchema> input_schema;
	Ref<MCPJsonSchema> output_schema;

	Dictionary to_dictionary() const;
};
```

The exact container choices can follow nearby engine conventions. The important point is that JSON
Schema keywords are emitted by typed code instead of open-coded dictionary assignments.

Equivalent typed structs should cover MCP resources and resource templates:

- `MCPResourceDefinition`
- `MCPResourceTemplateDefinition`

### 3. Parse Result and Field Readers

Add a generic parse result used by every input contract:

```cpp
template <typename T>
struct MCPParseResult {
	bool ok = false;
	T value;
	String field;
	String message;

	static MCPParseResult<T> success(const T &p_value);
	static MCPParseResult<T> invalid(const String &p_field, const String &p_message);
};
```

Field readers should be explicit and small:

- `read_optional_string()`
- `read_required_string()`
- `read_optional_bool()`
- `read_optional_int()`
- `read_optional_dictionary()`
- `read_optional_string_array()`
- enum readers such as `read_action_kind()` and `read_route_preference()`

These readers should reject wrong types instead of silently returning defaults. Missing optional
fields still receive defaults.

### 4. Shared Nested Contracts

Define reusable nested MCP contracts before tool-specific inputs:

- `MCPSelector`
- `MCPActionArgs`
- `MCPWaitCondition`
- `MCPLogMarker`
- `MCPEventMarker`
- `MCPFailureAttachmentOptions`
- `MCPElementNode`
- `MCPToolFailure`

Each stable contract exposes the relevant subset of:

```cpp
static MCPJsonSchema schema();
static MCPParseResult<T> parse(const Dictionary &p_dict);
Dictionary to_dictionary() const;
```

`MCPSelector` should be typed for known fields such as `id`, `handle`, `role`, `name`, `text`,
`class`, `path`, visibility flags, `nth`, and `within`. It can keep `Dictionary metadata` for
open-ended metadata matching.

`MCPWaitCondition` should share condition names with `EditorAutomationWait`, and its schema enum
should be built from the same source used by parsing.

`MCPFailureAttachmentOptions` should centralize `attach_screenshot_on_failure` and
`max_screenshot_bytes`, including option defaults supplied by dispatcher options.

### 5. Tool Input Contracts

Every MCP tool with arguments should get a typed input struct:

- `MCPObserveUIInput`
- `MCPFindElementsInput`
- `MCPActInput`
- `MCPWaitForInput`
- `MCPReadEditorLogInput`
- `MCPRunCommandInput`
- `MCPListCommandsInput`
- `MCPPollEventsInput`

`read_editor_state` can have an empty typed input so the tool list remains uniform.

Example shape:

```cpp
struct MCPActInput {
	MCPSelector selector;
	EditorAutomationActionKind action = EditorAutomationActionKind::UNKNOWN;
	EditorAutomationRoutePreference route = EditorAutomationRoutePreference::AUTO;
	MCPActionArgs args;
	MCPWaitCondition wait;
	int wait_timeout_ms = 5000;
	String wait_id;
	MCPFailureAttachmentOptions failure_attachments;

	static MCPJsonSchema schema();
	static MCPParseResult<MCPActInput> parse(const Dictionary &p_dict);
};
```

The dispatcher flow for converted tools becomes:

```text
Dictionary arguments
-> MCPActInput::parse(arguments)
-> existing automation internals
-> typed or semi-typed result object
-> Dictionary response
```

### 6. Stable Output Contracts

Stable protocol payloads should move to typed serializers:

- element summaries and tree nodes
- log and event markers
- pagination cursor fields
- common `ok`, `kind`, `message`, and `details` failure payloads
- wait status payloads
- failure screenshot attachments
- command list wrapper payloads
- event poll wrapper payloads

Some output fields should remain flexible dictionaries because their shapes are intentionally broad or
owned by other editor systems:

- `metadata`
- `details`
- command entries
- action traces
- editor state snapshots
- modal stack entries
- raw log/event entry dictionaries

The contract layer should still provide typed wrapper structs around those fields so top-level output
keys are statically named.

### 7. Validation Behavior

Converted parsers should enforce:

- required fields are present
- required fields have the expected type
- optional fields with wrong types fail instead of silently defaulting
- enum fields use known values
- arrays contain the expected element type when the schema claims a typed array

Unknown fields should initially be allowed to preserve client compatibility. `MCPJsonSchema` should
support `additionalProperties: false` so individual inputs can become stricter later once clients are
ready.

### 8. Migration Plan

Migrate in small slices:

1. Add shared schema, tool/resource definition, parse-result, and field-reader machinery.
2. Convert `EditorAutomationMCPSchemas::build_tools_list()` to typed `MCPToolDefinition` generation.
3. Convert resource and resource-template generation.
4. Convert `find_elements` input parsing first because it is useful and relatively contained.
5. Convert `act` and `wait_for`, including shared selector, action args, wait condition, and failure
   attachment options.
6. Convert simpler tools: `observe_ui`, `read_editor_log`, `run_command`, `list_commands`, and
   `poll_events`.
7. Add typed serializers for common outputs and replace repeated dictionary construction where the
   output shape is stable.
8. Remove obsolete dispatcher-local `_read_*` helpers once all MCP tool paths have typed parsing.

The migration should not change the public `tools/list` shape except where tests explicitly approve
more precise schema validation.

## Testing

Add focused C++ tests for the contract layer:

- every tool schema still includes the expected public fields
- required schema fields match parser-required fields
- parser rejects missing required fields
- parser rejects wrong field types
- parser rejects invalid enum values
- enum schema values match enum parser values
- `tools/list`, `resources/list`, and `resources/templates/list` remain compatible

Add dispatcher-level tests for behavior that changes at the MCP boundary:

- wrong argument types return JSON-RPC `invalid_params`
- missing required arguments return JSON-RPC `invalid_params`
- valid existing calls still reach the automation backend

For outputs, add tests around stable wrapper payloads:

- element node serialization
- marker serialization
- wait status serialization
- failure screenshot attachment serialization
- pagination cursor fields

## Rollout

This should land as an internal refactor with compatibility-preserving behavior first. Once typed
parsing is in place and clients have had time to adapt, follow-up changes can enable stricter schemas
or unknown-field rejection per tool.
