/**************************************************************************/
/*  editor_automation_mcp_dispatcher.cpp                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "editor_automation_mcp_dispatcher.h"

#include "editor/automation/editor_automation_commands.h"
#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/automation/editor_automation_wait.h"

#include "core/io/json.h"
#include "core/math/math_funcs.h"

const char *EditorAutomationMCPDispatcher::PROTOCOL_VERSION = "2025-11-25";

namespace {

const int MAX_TREE_RESULT_LIMIT = 20;

String _read_string(const Dictionary &p_dict, const char *p_key, const String &p_default = String()) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return p_default;
	}
	return value;
}

bool _read_bool(const Dictionary &p_dict, const char *p_key, bool p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::BOOL) {
		return p_default;
	}
	return value;
}

int _read_int(const Dictionary &p_dict, const char *p_key, int p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (!value.is_num()) {
		return p_default;
	}
	return (int)value;
}

Dictionary _read_dict(const Dictionary &p_dict, const char *p_key) {
	if (!p_dict.has(p_key)) {
		return Dictionary();
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return value;
}

// Depth-limited element tree with truncation metadata, walking the snapshot data
// directly so both max_depth and include_hidden can be enforced.
Dictionary _element_tree(const EditorAutomationSnapshotData &p_data, int p_index, int p_depth, int p_max_depth, bool p_include_hidden, bool &r_truncated) {
	const EditorAutomationElement &element = p_data.elements[p_index];
	Dictionary dict;
	dict["id"] = element.id;
	dict["handle"] = element.handle;
	dict["role"] = element.role;
	dict["name"] = element.name;
	dict["text"] = element.text;
	dict["class"] = element.class_name;
	dict["path"] = element.path;
	dict["visible"] = element.visible;
	dict["enabled"] = element.enabled;
	dict["focused"] = element.focused;
	dict["pressed"] = element.pressed;
	dict["selected"] = element.selected;

	Array bounds;
	bounds.push_back(element.bounds.position.x);
	bounds.push_back(element.bounds.position.y);
	bounds.push_back(element.bounds.size.x);
	bounds.push_back(element.bounds.size.y);
	dict["bounds"] = bounds;
	dict["actions"] = element.actions;
	if (!element.metadata.is_empty()) {
		dict["metadata"] = element.metadata;
	}

	int total_children = 0;
	Array children;
	for (int child_index : element.children) {
		const EditorAutomationElement &child = p_data.elements[child_index];
		if (!p_include_hidden && !child.visible) {
			continue;
		}
		total_children++;
		if (p_depth + 1 > p_max_depth) {
			r_truncated = true;
			continue;
		}
		children.push_back(_element_tree(p_data, child_index, p_depth + 1, p_max_depth, p_include_hidden, r_truncated));
	}
	dict["children"] = children;
	if (children.size() < total_children) {
		dict["children_truncated"] = true;
		dict["child_count"] = total_children;
	}
	return dict;
}

Dictionary _string_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = "string";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _object_schema() {
	Dictionary schema;
	schema["type"] = "object";
	return schema;
}

Dictionary _selector_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["description"] = "Semantic selector by role, name, text, class, path, state, and containment. Snapshot-scoped `id` values and durable `handle` values from observe_ui reconcile across later snapshots when the underlying object or virtual key is still valid.";

	Dictionary props;
	props["id"] = _string_schema("Snapshot-scoped opaque element id from observe_ui/find_elements.");
	props["handle"] = _string_schema("Durable element handle from observe_ui/find_elements.");
	props["role"] = _string_schema("Exact semantic role, e.g. button, text_field, checkbox, dialog, tab.");
	props["role_contains"] = _string_schema("Substring match against role.");
	props["name"] = _string_schema("Exact accessible/visible name.");
	props["name_contains"] = _string_schema("Substring match against the accessible/visible name.");
	props["text"] = _string_schema("Exact visible text/value.");
	props["text_contains"] = _string_schema("Substring match against the visible text/value.");
	props["class"] = _string_schema("Exact engine class name, e.g. Button.");
	props["class_contains"] = _string_schema("Substring match against the class name.");
	props["path"] = _string_schema("Exact node path.");
	props["path_contains"] = _string_schema("Substring match against the node path.");

	Dictionary visible = _string_schema("Match elements whose visibility equals this exact value.");
	visible["type"] = "boolean";
	props["visible"] = visible;
	Dictionary enabled = _string_schema("Match elements whose enabled state equals this exact value.");
	enabled["type"] = "boolean";
	props["enabled"] = enabled;
	Dictionary focused = _string_schema("Match elements whose focus state equals this exact value.");
	focused["type"] = "boolean";
	props["focused"] = focused;
	Dictionary visible_only = _string_schema("When true, keep only visible elements. Ignored when false.");
	visible_only["type"] = "boolean";
	props["visible_only"] = visible_only;
	Dictionary enabled_only = _string_schema("When true, keep only enabled elements. Ignored when false.");
	enabled_only["type"] = "boolean";
	props["enabled_only"] = enabled_only;

	Dictionary selected = _string_schema("Match elements whose selected state equals this exact value.");
	selected["type"] = "boolean";
	props["selected"] = selected;

	Dictionary metadata_schema = _object_schema();
	metadata_schema["description"] = "Match elements whose metadata dictionary contains these exact key/value pairs (e.g. node_name, node_path, label).";
	props["metadata"] = metadata_schema;

	Dictionary case_sensitive = _string_schema("Whether string field matching (exact and *_contains) is case-sensitive. Default true.");
	case_sensitive["type"] = "boolean";
	props["case_sensitive"] = case_sensitive;

	Dictionary nth = _string_schema("Deterministic disambiguation: pick the Nth match (0-based, negative counts from the end) after all filters are applied, in stable snapshot order.");
	nth["type"] = "integer";
	props["nth"] = nth;
	Dictionary index = _string_schema("Synonym for `nth`.");
	index["type"] = "integer";
	props["index"] = index;

	props["within"] = _object_schema();
	schema["properties"] = props;
	return schema;
}

Dictionary _make_tool(const String &p_name, const String &p_description, const Dictionary &p_properties, const Array &p_required) {
	Dictionary tool;
	tool["name"] = p_name;
	tool["description"] = p_description;

	Dictionary input_schema;
	input_schema["type"] = "object";
	input_schema["properties"] = p_properties;
	if (!p_required.is_empty()) {
		input_schema["required"] = p_required;
	}
	tool["inputSchema"] = input_schema;
	return tool;
}

Dictionary _make_resource(const String &p_uri, const String &p_name, const String &p_description) {
	Dictionary resource;
	resource["uri"] = p_uri;
	resource["name"] = p_name;
	resource["description"] = p_description;
	resource["mimeType"] = "application/json";
	return resource;
}

} // namespace

Array EditorAutomationMCPDispatcher::build_tools_list() {
	Array tools;

	{
		Dictionary props;
		Dictionary max_depth = _string_schema("Maximum tree depth to include (default 8).");
		max_depth["type"] = "integer";
		props["max_depth"] = max_depth;
		Dictionary include_hidden = _string_schema("Include hidden elements (default false).");
		include_hidden["type"] = "boolean";
		props["include_hidden"] = include_hidden;
		tools.push_back(_make_tool("observe_ui",
				"Returns the current windows, focused element, modal stack, and visible semantic tree.",
				props, Array()));
	}

	{
		Dictionary props;
		props["selector"] = _selector_schema();
		Dictionary max_results = _string_schema("Maximum number of matches to return (default 20).");
		max_results["type"] = "integer";
		props["max_results"] = max_results;
		Array required;
		required.push_back("selector");
		tools.push_back(_make_tool("find_elements",
				"Resolves selectors and returns matches or structured no-match/ambiguous diagnostics.",
				props, required));
	}

	{
		Dictionary props;
		props["selector"] = _selector_schema();
		props["action"] = _string_schema("Action to perform, e.g. click, focus, type_text, set_text, submit, press_key, drag, select, activate, expand, collapse, scroll, choose_menu_item, set_value.");
		props["route"] = _string_schema("Route preference: auto, semantic, or input.");
		Dictionary args_schema = _object_schema();
		args_schema["description"] = "Action arguments such as text, key, or value.";
		props["args"] = args_schema;
		Dictionary wait_schema = _object_schema();
		wait_schema["description"] = "Optional wait/settle clause after the action. Accepts a full condition object (type, selector, marker, ...) or shorthand { \"condition\": \"editor_idle\" }.";
		props["wait"] = wait_schema;
		Dictionary wait_timeout = _string_schema("Timeout in milliseconds for the optional wait clause (default 5000).");
		wait_timeout["type"] = "integer";
		props["wait_timeout_ms"] = wait_timeout;
		Array required;
		required.push_back("action");
		tools.push_back(_make_tool("act",
				"Performs a semantic or input action on a selected element and optionally waits for a UI condition in one call.",
				props, required));
	}

	{
		Dictionary props;
		props["condition"] = _string_schema("Wait condition, e.g. selector_appears, selector_disappears, focus_matches, modal_stack_changed, filesystem_idle, log_contains, no_new_errors.");
		props["selector"] = _selector_schema();
		Dictionary timeout = _string_schema("Timeout in milliseconds (default 5000).");
		timeout["type"] = "integer";
		props["timeout_ms"] = timeout;
		Dictionary wait_id = _string_schema("Poll or cancel an existing cooperative wait by id.");
		props["wait_id"] = wait_id;
		Dictionary cancel = _string_schema("When true with wait_id, cancel the pending wait.");
		cancel["type"] = "boolean";
		props["cancel"] = cancel;
		Dictionary cooperative = _string_schema("When true (default), return immediately while the wait is pending instead of blocking the server.");
		cooperative["type"] = "boolean";
		props["cooperative"] = cooperative;
		tools.push_back(_make_tool("wait_for",
				"Waits cooperatively for a UI condition and returns success/failure diagnostics without blocking the editor for the full timeout.",
				props, Array()));
	}

	{
		Dictionary props;
		tools.push_back(_make_tool("read_editor_state",
				"Returns selected nodes, open scenes, active scene, current script, playing state, and unsaved state.",
				props, Array()));
	}

	{
		Dictionary props;
		props["severity"] = _string_schema("Optional severity filter: error, warning, editor, stdout, stdout_rich.");
		Dictionary since = _object_schema();
		since["description"] = "Optional log marker {\"message_index\": N} to read entries since.";
		props["since"] = since;
		Dictionary limit = _string_schema("Maximum recent entries when no marker is provided (default 64).");
		limit["type"] = "integer";
		props["limit"] = limit;
		tools.push_back(_make_tool("read_editor_log",
				"Returns editor log entries, optionally filtered by severity and since-marker.",
				props, Array()));
	}

	{
		Dictionary props;
		props["command"] = _string_schema("Command palette command key or editor shortcut path, e.g. editor/save_scene or scene_tree/add_child_node.");
		Array required;
		required.push_back("command");
		tools.push_back(_make_tool("run_command",
				"Executes a command palette command or editor shortcut action by key via the existing editor registries.",
				props, required));
	}

	{
		Dictionary props;
		props["query"] = _string_schema("Optional substring or subsequence filter against command keys and labels.");
		props["category"] = _string_schema("Optional category prefix filter, e.g. scene_tree or editor.");
		Dictionary runnable_only = _string_schema("When true, include only commands runnable by run_command.");
		runnable_only["type"] = "boolean";
		props["runnable_only"] = runnable_only;
		Dictionary limit = _string_schema("Optional maximum number of commands to return.");
		limit["type"] = "integer";
		props["limit"] = limit;
		tools.push_back(_make_tool("list_commands",
				"Lists command palette commands and editor shortcut actions with runnable metadata.",
				props, Array()));
	}

	return tools;
}

Array EditorAutomationMCPDispatcher::build_resources_list() {
	Array resources;
	resources.push_back(_make_resource("foundry://ui/tree", "UI Tree", "Current visible semantic UI tree."));
	resources.push_back(_make_resource("foundry://editor/state", "Editor State", "Current editor state readback."));
	resources.push_back(_make_resource("foundry://editor/log", "Editor Log", "Recent editor log entries."));
	resources.push_back(_make_resource("foundry://scene/active", "Active Scene", "Active scene and edited root information."));
	resources.push_back(_make_resource("foundry://commands", "Editor Commands", "Command palette commands and editor shortcut actions with runnable metadata."));
	return resources;
}

void EditorAutomationMCPDispatcher::reset() {
	initialized = false;
	negotiated_protocol_version = String();
}

Dictionary EditorAutomationMCPDispatcher::_make_result(const Variant &p_id, const Variant &p_result) {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;
	response["result"] = p_result;
	return response;
}

Dictionary EditorAutomationMCPDispatcher::_make_error(const Variant &p_id, int p_code, const String &p_message, const Variant &p_data) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	if (p_data.get_type() != Variant::NIL) {
		error["data"] = p_data;
	}
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;
	response["error"] = error;
	return response;
}

Dictionary EditorAutomationMCPDispatcher::_tool_result_from_structured(const Dictionary &p_structured, bool p_is_error) {
	Dictionary result;

	// Compact JSON text content for clients that only render text.
	Array content;
	Dictionary text_content;
	text_content["type"] = "text";
	text_content["text"] = JSON::stringify(p_structured, "", false);
	content.push_back(text_content);
	result["content"] = content;

	// Structured content for clients that support it.
	result["structuredContent"] = p_structured;
	result["isError"] = p_is_error;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::handle_message(const Dictionary &p_message) {
	bool has_response = false;
	return handle_message(p_message, has_response);
}

Dictionary EditorAutomationMCPDispatcher::handle_message(const Dictionary &p_message, bool &r_has_response) {
	r_has_response = true;

	Variant id = p_message.has("id") ? p_message.get("id", Variant()) : Variant();
	// Godot's JSON parser represents all numbers as doubles. Echo whole-number
	// ids back as integers so strict JSON-RPC/MCP clients can match them.
	if (id.get_type() == Variant::FLOAT) {
		const double value = id;
		if (value == Math::floor(value) && Math::is_finite(value)) {
			id = (int64_t)value;
		}
	}
	const bool is_notification = !p_message.has("id");

	if (!p_message.has("method") || p_message.get("method", Variant()).get_type() != Variant::STRING) {
		if (is_notification) {
			r_has_response = false;
			return Dictionary();
		}
		return _make_error(id, INVALID_REQUEST, "Request is missing a string 'method'.");
	}

	const String method = p_message.get("method", Variant());
	const Dictionary params = _read_dict(p_message, "params");

	// Notifications never produce a response.
	if (is_notification) {
		r_has_response = false;
		if (method == "notifications/initialized") {
			initialized = true;
		}
		return Dictionary();
	}

	if (method == "initialize") {
		bool ok = false;
		Dictionary error;
		const Dictionary result = _handle_initialize(params, ok, error);
		if (!ok) {
			return _make_error(id, (int)error.get("code", INVALID_PARAMS), error.get("message", "Initialization failed."), error.get("data", Variant()));
		}
		return _make_result(id, result);
	}

	if (method == "tools/list") {
		Dictionary d;
		d["tools"] = build_tools_list();
		return _make_result(id, d);
	}

	if (method == "tools/call") {
		bool ok = false;
		Dictionary error;
		const Dictionary result = _handle_tools_call(params, ok, error);
		if (!ok) {
			return _make_error(id, (int)error.get("code", INVALID_PARAMS), error.get("message", "Tool call failed."), error.get("data", Variant()));
		}
		return _make_result(id, result);
	}

	if (method == "resources/list") {
		Dictionary d;
		d["resources"] = build_resources_list();
		return _make_result(id, d);
	}

	if (method == "resources/read") {
		bool ok = false;
		Dictionary error;
		const Dictionary result = _handle_resources_read(params, ok, error);
		if (!ok) {
			return _make_error(id, (int)error.get("code", INVALID_PARAMS), error.get("message", "Resource read failed."), error.get("data", Variant()));
		}
		return _make_result(id, result);
	}

	return _make_error(id, METHOD_NOT_FOUND, vformat("Unknown method '%s'.", method));
}

Dictionary EditorAutomationMCPDispatcher::_handle_initialize(const Dictionary &p_params, bool &r_ok, Dictionary &r_error) {
	const String requested_version = _read_string(p_params, "protocolVersion", PROTOCOL_VERSION);
	// Accept the client's requested version, but always report our supported
	// version so clients can detect mismatches. We only hard-fail on empty.
	if (requested_version.is_empty()) {
		r_ok = false;
		r_error["code"] = INVALID_PARAMS;
		r_error["message"] = "Missing protocolVersion.";
		return Dictionary();
	}
	negotiated_protocol_version = PROTOCOL_VERSION;

	Dictionary capabilities;
	Dictionary tools_cap;
	tools_cap["listChanged"] = false;
	capabilities["tools"] = tools_cap;
	Dictionary resources_cap;
	resources_cap["listChanged"] = false;
	resources_cap["subscribe"] = false;
	capabilities["resources"] = resources_cap;

	Dictionary server_info;
	server_info["name"] = "foundry-editor-automation";
	server_info["version"] = "1.0.0";

	Dictionary result;
	result["protocolVersion"] = PROTOCOL_VERSION;
	result["capabilities"] = capabilities;
	result["serverInfo"] = server_info;
	result["instructions"] = "Local editor automation. Use tools/list to discover generic observe/act tools backed by the shared automation core.";

	r_ok = true;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_handle_tools_call(const Dictionary &p_params, bool &r_ok, Dictionary &r_error) {
	const String name = _read_string(p_params, "name");
	if (name.is_empty()) {
		r_ok = false;
		r_error["code"] = INVALID_PARAMS;
		r_error["message"] = "tools/call requires a 'name'.";
		return Dictionary();
	}
	const Dictionary arguments = _read_dict(p_params, "arguments");

	bool is_error = false;
	Dictionary structured;
	if (name == "observe_ui") {
		structured = _tool_observe_ui(arguments, is_error);
	} else if (name == "find_elements") {
		structured = _tool_find_elements(arguments, is_error);
	} else if (name == "act") {
		structured = _tool_act(arguments, is_error);
	} else if (name == "wait_for") {
		structured = _tool_wait_for(arguments, is_error);
	} else if (name == "read_editor_state") {
		structured = _tool_read_editor_state(arguments, is_error);
	} else if (name == "read_editor_log") {
		structured = _tool_read_editor_log(arguments, is_error);
	} else if (name == "run_command") {
		structured = _tool_run_command(arguments, is_error);
	} else if (name == "list_commands") {
		structured = _tool_list_commands(arguments, is_error);
	} else {
		r_ok = false;
		r_error["code"] = METHOD_NOT_FOUND;
		r_error["message"] = vformat("Unknown tool '%s'.", name);
		return Dictionary();
	}

	r_ok = true;
	return _tool_result_from_structured(structured, is_error);
}

Dictionary EditorAutomationMCPDispatcher::_tool_observe_ui(const Dictionary &p_args, bool &r_is_error) {
	r_is_error = false;
	int max_depth = _read_int(p_args, "max_depth", options.max_tree_depth);
	if (max_depth < 0) {
		max_depth = 0;
	}
	const bool include_hidden = _read_bool(p_args, "include_hidden", false);

	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();
	const EditorAutomationSnapshotData &data = snapshot.get_data();

	bool truncated = false;
	Array roots;
	for (int root_index : data.root_indices) {
		const EditorAutomationElement &root = data.elements[root_index];
		if (!include_hidden && !root.visible) {
			continue;
		}
		roots.push_back(_element_tree(data, root_index, 0, max_depth, include_hidden, truncated));
	}

	Dictionary result;
	result["generation"] = snapshot.get_generation();
	result["focused_element_id"] = snapshot.get_focused_element_id();
	result["tree"] = roots;
	result["windows"] = roots;
	result["modal_stack"] = EditorAutomationState::capture_modal_stack(_snapshot_root());
	result["element_count"] = data.elements.size();

	Dictionary limits;
	limits["max_depth"] = max_depth;
	limits["include_hidden"] = include_hidden;
	limits["truncated"] = truncated;
	result["limits"] = limits;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_tool_find_elements(const Dictionary &p_args, bool &r_is_error) {
	const Dictionary selector = _read_dict(p_args, "selector");
	int max_results = _read_int(p_args, "max_results", MAX_TREE_RESULT_LIMIT);
	if (max_results <= 0) {
		max_results = MAX_TREE_RESULT_LIMIT;
	}

	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();

	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(snapshot, selector);
	Dictionary result = selector_result.to_dictionary();

	// find_elements is a multi-match tool: a selector that resolves to several
	// elements (AMBIGUOUS for single-target callers) is a valid result here, not
	// an error. Only genuine no-match / stale / invalid selectors are failures.
	const bool has_matches = (selector_result.status == EditorAutomationSelectorStatus::OK ||
									 selector_result.status == EditorAutomationSelectorStatus::AMBIGUOUS) &&
			!selector_result.match_indices.is_empty();

	if (has_matches) {
		r_is_error = false;
		result["ok"] = true;
		result["ambiguous"] = selector_result.status == EditorAutomationSelectorStatus::AMBIGUOUS;
		Array elements;
		const int total = selector_result.match_indices.size();
		bool truncated = false;
		for (int i = 0; i < total; i++) {
			if (elements.size() >= max_results) {
				truncated = true;
				break;
			}
			bool ignored = false;
			elements.push_back(_element_tree(snapshot.get_data(), selector_result.match_indices[i], 0, 0, true, ignored));
		}
		result["elements"] = elements;
		result["match_count"] = total;
		result["truncated"] = truncated;
	} else {
		// No-match / stale / invalid are structured automation failures.
		r_is_error = true;
	}
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_build_condition_from_args(const Dictionary &p_args) {
	Dictionary condition;
	if (p_args.has("type")) {
		condition["type"] = _read_string(p_args, "type");
	} else {
		condition["type"] = _read_string(p_args, "condition");
	}
	if (p_args.has("selector")) {
		condition["selector"] = _read_dict(p_args, "selector");
	}
	static const char *passthrough_keys[] = { "text", "severity", "severities", "marker", "fields", "baseline" };
	for (const char *key : passthrough_keys) {
		if (p_args.has(key)) {
			condition[key] = p_args.get(key, Variant());
		}
	}
	return condition;
}

Dictionary EditorAutomationMCPDispatcher::_wait_context_from_handle(const EditorAutomationCooperativeWaitHandle &p_handle) {
	Dictionary result;
	result["wait_id"] = p_handle.wait_id;
	result["condition"] = p_handle.condition;
	result["elapsed_sec"] = p_handle.elapsed_sec;

	switch (p_handle.status) {
		case EditorAutomationCooperativeWaitStatus::PENDING:
			result["status"] = "pending";
			result["ok"] = false;
			break;
		case EditorAutomationCooperativeWaitStatus::CANCELLED:
			result["status"] = "cancelled";
			result["ok"] = false;
			result["kind"] = p_handle.result.kind;
			result["message"] = p_handle.result.message;
			if (!p_handle.result.details.is_empty()) {
				result["details"] = p_handle.result.details;
			}
			break;
		case EditorAutomationCooperativeWaitStatus::COMPLETE:
			result["status"] = "complete";
			result["ok"] = p_handle.result.ok;
			if (!p_handle.result.kind.is_empty()) {
				result["kind"] = p_handle.result.kind;
			}
			if (!p_handle.result.message.is_empty()) {
				result["message"] = p_handle.result.message;
			}
			if (!p_handle.result.details.is_empty()) {
				result["details"] = p_handle.result.details;
			}
			break;
	}
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_cooperative_wait_response(const EditorAutomationCooperativeWaitHandle &p_handle, bool &r_is_error) {
	Dictionary result = _wait_context_from_handle(p_handle);
	if (p_handle.act_context.active) {
		result["action"] = p_handle.act_context.action_result;
		if (!p_handle.act_context.action.is_empty()) {
			result["action_name"] = p_handle.act_context.action;
		}
		if (!p_handle.act_context.selector.is_empty()) {
			result["selector"] = p_handle.act_context.selector;
		}
		Dictionary marker_dict;
		marker_dict["message_index"] = p_handle.act_context.log_marker.message_index;
		result["log_marker"] = marker_dict;
		result["action_trace"] = EditorAutomationTrace::get_singleton().get_recent(16);
	}

	if (p_handle.status == EditorAutomationCooperativeWaitStatus::PENDING) {
		r_is_error = false;
		return result;
	}

	if (p_handle.act_context.active && p_handle.act_context.action_result.get("ok", false) && !p_handle.result.ok) {
		result["ok"] = false;
		result["kind"] = p_handle.result.kind == "timeout" ? "wait_timeout" : "wait_failed";
		result["message"] = p_handle.result.message;

		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
				String(result["kind"]),
				p_handle.result.message,
				p_handle.condition,
				p_handle.act_context.action_result,
				p_handle.act_context.selector,
				snapshot,
				p_handle.act_context.log_marker);
		result["details"] = diagnostics.details;
		r_is_error = true;
		return result;
	}

	if (!p_handle.result.ok && p_handle.status == EditorAutomationCooperativeWaitStatus::COMPLETE &&
			p_handle.result.kind == "timeout" && !p_handle.act_context.active) {
		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
				"timeout",
				p_handle.result.message,
				p_handle.condition,
				Dictionary(),
				Dictionary(),
				snapshot,
				EditorAutomationLog::create_marker());
		result["details"] = diagnostics.details;
	}

	r_is_error = !p_handle.result.ok && p_handle.status != EditorAutomationCooperativeWaitStatus::CANCELLED;
	if (p_handle.status == EditorAutomationCooperativeWaitStatus::CANCELLED) {
		r_is_error = true;
	}
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_compose_act_wait_result(
		const Dictionary &p_action_result,
		const EditorAutomationCooperativeWaitHandle &p_handle,
		const Dictionary &p_condition,
		const Dictionary &p_selector,
		const String &p_action,
		const EditorAutomationLogMarker &p_log_marker,
		bool &r_is_error) {
	if (p_handle.status == EditorAutomationCooperativeWaitStatus::PENDING) {
		Dictionary result;
		result["ok"] = false;
		result["status"] = "pending";
		result["wait_id"] = p_handle.wait_id;
		result["action"] = p_action_result;
		result["condition"] = p_condition;
		result["elapsed_sec"] = p_handle.elapsed_sec;
		Dictionary marker_dict;
		marker_dict["message_index"] = p_log_marker.message_index;
		result["log_marker"] = marker_dict;
		r_is_error = false;
		return result;
	}

	Dictionary result;
	result["action"] = p_action_result;
	result["wait"] = _wait_context_from_handle(p_handle);
	result["condition"] = p_condition;
	Dictionary marker_dict;
	marker_dict["message_index"] = p_log_marker.message_index;
	result["log_marker"] = marker_dict;
	result["action_trace"] = EditorAutomationTrace::get_singleton().get_recent(16);

	const bool action_ok = (bool)p_action_result.get("ok", false);
	const bool wait_ok = p_handle.result.ok;
	result["ok"] = action_ok && wait_ok;

	if (action_ok && wait_ok) {
		r_is_error = false;
		return result;
	}

	if (!action_ok) {
		result["ok"] = false;
		result["kind"] = "action_failed";
		result["message"] = p_action_result.get("message", "Action failed before wait.");
		r_is_error = true;
		return result;
	}

	result["ok"] = false;
	result["kind"] = p_handle.result.kind == "timeout" ? "wait_timeout" : "wait_failed";
	result["message"] = p_handle.result.message;

	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();
	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
			String(result["kind"]),
			p_handle.result.message,
			p_condition,
			p_action_result,
			p_selector,
			snapshot,
			p_log_marker);
	result["details"] = diagnostics.details;
	r_is_error = true;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_tool_act(const Dictionary &p_args, bool &r_is_error) {
	const String wait_id = _read_string(p_args, "wait_id");
	if (!wait_id.is_empty()) {
		EditorAutomationCooperativeWaitHandle handle;
		if (!EditorAutomationWait::poll_cooperative(wait_id, handle)) {
			r_is_error = true;
			Dictionary result;
			result["ok"] = false;
			result["kind"] = "unknown_wait";
			result["message"] = vformat("Unknown wait id '%s'.", wait_id);
			return result;
		}
		if (handle.act_context.active) {
			return _compose_act_wait_result(
					handle.act_context.action_result,
					handle,
					handle.condition,
					handle.act_context.selector,
					handle.act_context.action,
					handle.act_context.log_marker,
					r_is_error);
		}
		return _cooperative_wait_response(handle, r_is_error);
	}

	const String action = _read_string(p_args, "action");
	const Dictionary selector = _read_dict(p_args, "selector");

	Dictionary options_dict = _read_dict(p_args, "args");
	const String route = _read_string(p_args, "route");
	if (!route.is_empty()) {
		options_dict["route"] = route;
	}

	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();

	const EditorAutomationLogMarker log_marker = EditorAutomationLog::create_marker();
	const EditorAutomationActionResult action_result = EditorAutomationDriver::perform(snapshot, action, selector, options_dict);
	const Dictionary action_dict = action_result.to_dictionary();

	const Dictionary wait_clause = _read_dict(p_args, "wait");
	if (wait_clause.is_empty()) {
		r_is_error = !action_result.ok;
		return action_dict;
	}

	if (!action_result.ok) {
		Dictionary result;
		result["ok"] = false;
		result["kind"] = "action_failed";
		result["message"] = action_result.message;
		result["action"] = action_dict;
		r_is_error = true;
		return result;
	}

	Dictionary condition = _build_condition_from_args(wait_clause);
	int wait_timeout_ms = _read_int(p_args, "wait_timeout_ms", (int)(options.default_wait_timeout_sec * 1000.0));
	if (wait_timeout_ms < 0) {
		wait_timeout_ms = 0;
	}

	EditorAutomationWaitContext context;
	context.snapshot_root = _snapshot_root();

	EditorAutomationActWaitContext act_context;
	act_context.active = true;
	act_context.action = action;
	act_context.selector = selector;
	act_context.action_result = action_dict;
	act_context.log_marker = log_marker;

	const String new_wait_id = EditorAutomationWait::begin_cooperative(condition, wait_timeout_ms / 1000.0, context, act_context);
	EditorAutomationCooperativeWaitHandle handle;
	EditorAutomationWait::poll_cooperative(new_wait_id, handle);
	return _compose_act_wait_result(action_dict, handle, condition, selector, action, log_marker, r_is_error);
}

Dictionary EditorAutomationMCPDispatcher::_tool_wait_for(const Dictionary &p_args, bool &r_is_error) {
	const String wait_id = _read_string(p_args, "wait_id");
	if (!wait_id.is_empty()) {
		if (_read_bool(p_args, "cancel", false)) {
			EditorAutomationCooperativeWaitHandle handle;
			if (!EditorAutomationWait::cancel_cooperative(wait_id, handle)) {
				r_is_error = true;
				Dictionary result;
				result["ok"] = false;
				result["kind"] = "unknown_wait";
				result["message"] = vformat("Unknown wait id '%s'.", wait_id);
				return result;
			}
			return _cooperative_wait_response(handle, r_is_error);
		}

		EditorAutomationCooperativeWaitHandle handle;
		if (!EditorAutomationWait::poll_cooperative(wait_id, handle)) {
			r_is_error = true;
			Dictionary result;
			result["ok"] = false;
			result["kind"] = "unknown_wait";
			result["message"] = vformat("Unknown wait id '%s'.", wait_id);
			return result;
		}
		return _cooperative_wait_response(handle, r_is_error);
	}

	const Dictionary condition = _build_condition_from_args(p_args);
	if (_read_string(condition, "type").is_empty()) {
		r_is_error = true;
		Dictionary result;
		result["ok"] = false;
		result["kind"] = "invalid_params";
		result["message"] = "wait_for requires 'condition' or 'wait_id'.";
		return result;
	}
	int timeout_ms = _read_int(p_args, "timeout_ms", (int)(options.default_wait_timeout_sec * 1000.0));
	if (timeout_ms < 0) {
		timeout_ms = 0;
	}

	EditorAutomationWaitContext context;
	context.snapshot_root = _snapshot_root();

	const bool cooperative = _read_bool(p_args, "cooperative", true);
	if (!cooperative) {
		const EditorAutomationWaitResult wait_result = EditorAutomationWait::wait_for(condition, timeout_ms / 1000.0, context);
		r_is_error = !wait_result.ok;
		return wait_result.to_dictionary();
	}

	const String new_wait_id = EditorAutomationWait::begin_cooperative(condition, timeout_ms / 1000.0, context);
	EditorAutomationCooperativeWaitHandle handle;
	EditorAutomationWait::poll_cooperative(new_wait_id, handle);
	return _cooperative_wait_response(handle, r_is_error);
}

Dictionary EditorAutomationMCPDispatcher::_tool_read_editor_state(const Dictionary &p_args, bool &r_is_error) {
	r_is_error = false;
	return EditorAutomationState::read_editor_state();
}

Dictionary EditorAutomationMCPDispatcher::_tool_read_editor_log(const Dictionary &p_args, bool &r_is_error) {
	r_is_error = false;
	PackedStringArray severities;
	const String severity = _read_string(p_args, "severity");
	if (!severity.is_empty()) {
		severities.push_back(severity);
	}

	Array entries;
	if (p_args.has("since")) {
		const Dictionary since = _read_dict(p_args, "since");
		EditorAutomationLogMarker marker;
		marker.message_index = _read_int(since, "message_index", 0);
		entries = EditorAutomationLog::read_since(marker, severities);
	} else {
		int limit = _read_int(p_args, "limit", 64);
		if (limit <= 0) {
			limit = 64;
		}
		entries = EditorAutomationLog::read_recent(limit, severities);
	}

	Dictionary result;
	result["entries"] = entries;
	result["count"] = entries.size();
	Dictionary marker;
	marker["message_index"] = EditorAutomationLog::get_message_count();
	result["marker"] = marker;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_tool_run_command(const Dictionary &p_args, bool &r_is_error) {
	const String command = _read_string(p_args, "command");
	const Dictionary result = EditorAutomationCommands::execute(command);
	r_is_error = !(bool)result.get("ok", false);
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_tool_list_commands(const Dictionary &p_args, bool &r_is_error) {
	r_is_error = false;
	return EditorAutomationCommands::list_commands(p_args);
}

Dictionary EditorAutomationMCPDispatcher::_resource_payload(const String &p_uri, bool &r_ok) {
	r_ok = true;
	if (p_uri == "foundry://ui/tree") {
		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		return snapshot.to_dictionary();
	}
	if (p_uri == "foundry://editor/state") {
		return EditorAutomationState::read_editor_state();
	}
	if (p_uri == "foundry://editor/log") {
		Dictionary payload;
		payload["entries"] = EditorAutomationLog::read_recent(64);
		return payload;
	}
	if (p_uri == "foundry://scene/active") {
		const Dictionary state = EditorAutomationState::read_editor_state();
		Dictionary payload;
		payload["active_scene_path"] = state.get("active_scene_path", String());
		payload["active_scene_index"] = state.get("active_scene_index", -1);
		payload["edited_scene_root"] = state.get("edited_scene_root", Dictionary());
		payload["selected_nodes"] = state.get("selected_nodes", Array());
		payload["unsaved"] = state.get("unsaved", Dictionary());
		return payload;
	}
	if (p_uri == "foundry://commands") {
		return EditorAutomationCommands::list_commands();
	}
	r_ok = false;
	return Dictionary();
}

Dictionary EditorAutomationMCPDispatcher::_handle_resources_read(const Dictionary &p_params, bool &r_ok, Dictionary &r_error) {
	const String uri = _read_string(p_params, "uri");
	if (uri.is_empty()) {
		r_ok = false;
		r_error["code"] = INVALID_PARAMS;
		r_error["message"] = "resources/read requires a 'uri'.";
		return Dictionary();
	}

	bool found = false;
	const Dictionary payload = _resource_payload(uri, found);
	if (!found) {
		r_ok = false;
		r_error["code"] = INVALID_PARAMS;
		r_error["message"] = vformat("Unknown resource '%s'.", uri);
		return Dictionary();
	}

	Dictionary content;
	content["uri"] = uri;
	content["mimeType"] = "application/json";
	content["text"] = JSON::stringify(payload, "", false);

	Array contents;
	contents.push_back(content);

	Dictionary result;
	result["contents"] = contents;
	r_ok = true;
	return result;
}
