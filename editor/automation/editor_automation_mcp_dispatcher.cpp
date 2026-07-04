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
#include "editor/automation/editor_automation_events.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_mcp_contracts.h"
#include "editor/automation/editor_automation_mcp_schemas.h"
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
const int DEFAULT_MAX_CHILDREN_PER_NODE = 32;
const String SUBTREE_CURSOR_PREFIX = "subtree:v1:";

String _encode_subtree_cursor(
		const String &p_parent_handle,
		int p_offset,
		int p_max_children,
		int p_max_depth,
		bool p_include_hidden,
		uint64_t p_generation) {
	return vformat("%s%s|%d|%d|%d|%d|%d",
			SUBTREE_CURSOR_PREFIX,
			p_parent_handle,
			p_offset,
			p_max_children,
			p_max_depth,
			p_include_hidden ? 1 : 0,
			(int64_t)p_generation);
}

bool _decode_subtree_cursor(
		const String &p_cursor,
		String &r_parent_handle,
		int &r_offset,
		int &r_max_children,
		int &r_max_depth,
		bool &r_include_hidden,
		uint64_t &r_generation) {
	if (!p_cursor.begins_with(SUBTREE_CURSOR_PREFIX)) {
		return false;
	}
	const PackedStringArray parts = p_cursor.substr(SUBTREE_CURSOR_PREFIX.length()).split("|", false);
	if (parts.size() != 6) {
		return false;
	}
	r_parent_handle = parts[0];
	r_offset = parts[1].to_int();
	r_max_children = parts[2].to_int();
	r_max_depth = parts[3].to_int();
	r_include_hidden = parts[4].to_int() != 0;
	r_generation = (uint64_t)parts[5].to_int();
	return !r_parent_handle.is_empty();
}

int _resolve_element_index(const EditorAutomationSnapshot &p_snapshot, const String &p_id_or_handle) {
	const EditorAutomationSnapshotData &data = p_snapshot.get_data();
	const int *by_id = data.id_to_index.getptr(p_id_or_handle);
	if (by_id != nullptr) {
		return *by_id;
	}
	const EditorAutomationElement *by_handle = p_snapshot.find_by_handle(p_id_or_handle);
	if (by_handle == nullptr) {
		return -1;
	}
	const int *by_handle_index = data.handle_to_index.getptr(by_handle->handle);
	return by_handle_index != nullptr ? *by_handle_index : -1;
}

String _encode_find_cursor(int p_offset) {
	return vformat("find:v1:%d", p_offset);
}

bool _decode_find_cursor(const String &p_cursor, int &r_offset) {
	if (!p_cursor.begins_with("find:v1:")) {
		return false;
	}
	r_offset = p_cursor.substr(8).to_int();
	return r_offset >= 0;
}

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

template <typename TInput>
bool _parse_tool_input(const String &p_tool_name, const Dictionary &p_arguments, TInput &r_input, Dictionary &r_error) {
	const EditorAutomationMCPParseResult<TInput> parsed = TInput::parse(p_arguments);
	if (parsed.ok) {
		r_input = parsed.value;
		return true;
	}

	r_error["code"] = EditorAutomationMCPDispatcher::INVALID_PARAMS;
	r_error["message"] = vformat("Invalid arguments for '%s': %s", p_tool_name, parsed.error.message);
	Dictionary data;
	data["field"] = parsed.error.field;
	r_error["data"] = data;
	return false;
}

// Depth-limited element tree with truncation metadata and child pagination.
Dictionary _element_tree(
		const EditorAutomationSnapshotData &p_data,
		int p_index,
		int p_depth,
		int p_max_depth,
		bool p_include_hidden,
		int p_max_children,
		int p_child_offset,
		bool &r_truncated,
		bool p_emit_child_cursors) {
	const EditorAutomationElement &element = p_data.elements[p_index];
	EditorAutomationMCPElementNode node = EditorAutomationMCPElementNode::from_element(element);

	LocalVector<int> visible_children;
	for (int child_index : element.children) {
		const EditorAutomationElement &child = p_data.elements[child_index];
		if (!p_include_hidden && !child.visible) {
			continue;
		}
		visible_children.push_back(child_index);
	}

	const int total_children = visible_children.size();
	int emitted_children = 0;
	Array children;
	const int effective_max_children = p_max_children > 0 ? p_max_children : total_children;
	for (int i = p_child_offset; i < total_children; i++) {
		if (emitted_children >= effective_max_children) {
			r_truncated = true;
			break;
		}
		const int child_index = visible_children[i];
		if (p_depth + 1 > p_max_depth) {
			r_truncated = true;
			continue;
		}
		children.push_back(_element_tree(p_data, child_index, p_depth + 1, p_max_depth, p_include_hidden, p_max_children, 0, r_truncated, p_emit_child_cursors));
		emitted_children++;
	}
	node.has_children = true;
	node.children = children;
	if (children.size() < total_children || p_child_offset > 0 || (p_child_offset + emitted_children) < total_children) {
		node.children_truncated = true;
		node.child_count = total_children;
		if (p_emit_child_cursors && (p_child_offset + emitted_children) < total_children) {
			node.children_next_cursor = _encode_subtree_cursor(
					element.handle,
					p_child_offset + emitted_children,
					effective_max_children,
					p_max_depth,
					p_include_hidden,
					p_data.generation);
		}
	} else if (p_depth + 1 > p_max_depth && total_children > 0) {
		node.children_truncated = true;
		node.child_count = total_children;
		if (p_emit_child_cursors) {
			node.children_next_cursor = _encode_subtree_cursor(
					element.handle,
					0,
					effective_max_children,
					p_max_depth + 4,
					p_include_hidden,
					p_data.generation);
		}
	}
	return node.to_dictionary();
}

Dictionary _element_summary(const EditorAutomationElement &p_element) {
	return EditorAutomationMCPElementNode::from_element(p_element).to_dictionary();
}

} // namespace

Array EditorAutomationMCPDispatcher::build_tools_list() {
	return EditorAutomationMCPSchemas::build_tools_list();
}

Array EditorAutomationMCPDispatcher::build_resources_list() {
	return EditorAutomationMCPSchemas::build_resources_list();
}

Array EditorAutomationMCPDispatcher::build_resource_templates_list() {
	return EditorAutomationMCPSchemas::build_resource_templates_list();
}

void EditorAutomationMCPDispatcher::reset() {
	initialized = false;
	negotiated_protocol_version = String();
	EditorAutomationEvents::reset();
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

	if (method == "resources/templates/list") {
		Dictionary d;
		d["resourceTemplates"] = build_resource_templates_list();
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
	Dictionary logging_cap;
	logging_cap["poll_events"] = true;
	logging_cap["server_push"] = false;
	capabilities["logging"] = logging_cap;

	Dictionary server_info;
	server_info["name"] = "foundry-editor-automation";
	server_info["version"] = "1.1.0";

	Dictionary result;
	result["protocolVersion"] = PROTOCOL_VERSION;
	result["capabilities"] = capabilities;
	result["serverInfo"] = server_info;
	result["instructions"] = "Local editor automation backed by the shared automation core. Use tools/list for typed input/output schemas. Large UI trees paginate via children_next_cursor, find_elements next_cursor, and foundry://ui/subtree/{id} resources. The POST-only transport cannot push notifications; poll poll_events (preferred) or read_editor_log as a fallback.";

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
	Dictionary arguments;
	if (p_params.has("arguments")) {
		const Variant raw_arguments = p_params.get("arguments", Variant());
		if (raw_arguments.get_type() != Variant::DICTIONARY) {
			r_ok = false;
			r_error["code"] = INVALID_PARAMS;
			r_error["message"] = "tools/call 'arguments' must be an object.";
			return Dictionary();
		}
		arguments = raw_arguments;
	}

	bool is_error = false;
	Dictionary structured;
	if (name == "observe_ui") {
		EditorAutomationMCPObserveUIInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_observe_ui(input.to_dictionary(), is_error);
	} else if (name == "find_elements") {
		EditorAutomationMCPFindElementsInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_find_elements(input.to_dictionary(), is_error);
	} else if (name == "act") {
		EditorAutomationMCPActInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_act(input.to_dictionary(), is_error);
	} else if (name == "wait_for") {
		EditorAutomationMCPWaitForInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_wait_for(input.to_dictionary(), is_error);
	} else if (name == "read_editor_state") {
		EditorAutomationMCPReadEditorStateInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_read_editor_state(input.to_dictionary(), is_error);
	} else if (name == "read_editor_log") {
		EditorAutomationMCPReadEditorLogInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_read_editor_log(input.to_dictionary(), is_error);
	} else if (name == "run_command") {
		EditorAutomationMCPRunCommandInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_run_command(input.to_dictionary(), is_error);
	} else if (name == "list_commands") {
		EditorAutomationMCPListCommandsInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_list_commands(input.to_dictionary(), is_error);
	} else if (name == "poll_events") {
		EditorAutomationMCPPollEventsInput input;
		if (!_parse_tool_input(name, arguments, input, r_error)) {
			r_ok = false;
			return Dictionary();
		}
		structured = _tool_poll_events(input.to_dictionary(), is_error);
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
	const String subtree_cursor = _read_string(p_args, "subtree_cursor");
	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();
	const EditorAutomationSnapshotData &data = snapshot.get_data();

	if (!subtree_cursor.is_empty()) {
		String parent_handle;
		int offset = 0;
		int max_children = DEFAULT_MAX_CHILDREN_PER_NODE;
		int max_depth = options.max_tree_depth;
		bool include_hidden = false;
		uint64_t generation = 0;
		if (!_decode_subtree_cursor(subtree_cursor, parent_handle, offset, max_children, max_depth, include_hidden, generation)) {
			r_is_error = true;
			Dictionary result;
			result["ok"] = false;
			result["kind"] = "invalid_params";
			result["message"] = "Invalid subtree_cursor.";
			return result;
		}

		const int parent_index = _resolve_element_index(snapshot, parent_handle);
		if (parent_index < 0) {
			r_is_error = true;
			Dictionary result;
			result["ok"] = false;
			result["kind"] = "stale_element";
			result["message"] = vformat("Unknown element handle '%s'.", parent_handle);
			return result;
		}

		bool truncated = false;
		Dictionary subtree = _element_tree(data, parent_index, 0, max_depth, include_hidden, max_children, offset, truncated, true);
		Dictionary result;
		result["generation"] = snapshot.get_generation();
		result["subtree"] = subtree;
		result["subtree_cursor"] = subtree_cursor;
		Dictionary limits;
		limits["max_depth"] = max_depth;
		limits["max_children"] = max_children;
		limits["include_hidden"] = include_hidden;
		limits["offset"] = offset;
		limits["truncated"] = truncated;
		result["limits"] = limits;
		return result;
	}

	int max_depth = _read_int(p_args, "max_depth", options.max_tree_depth);
	if (max_depth < 0) {
		max_depth = 0;
	}
	const bool include_hidden = _read_bool(p_args, "include_hidden", false);
	int max_children = _read_int(p_args, "max_children", DEFAULT_MAX_CHILDREN_PER_NODE);
	if (max_children <= 0) {
		max_children = DEFAULT_MAX_CHILDREN_PER_NODE;
	}

	bool truncated = false;
	Array roots;
	for (int root_index : data.root_indices) {
		const EditorAutomationElement &root = data.elements[root_index];
		if (!include_hidden && !root.visible) {
			continue;
		}
		roots.push_back(_element_tree(data, root_index, 0, max_depth, include_hidden, max_children, 0, truncated, true));
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
	limits["max_children"] = max_children;
	limits["include_hidden"] = include_hidden;
	limits["truncated"] = truncated;
	result["limits"] = limits;
	return result;
}

EditorAutomationFailureAttachmentOptions EditorAutomationMCPDispatcher::_failure_attachment_options(const Dictionary &p_args) const {
	EditorAutomationFailureAttachmentOptions attachment_options;
	if (p_args.has("attach_screenshot_on_failure")) {
		attachment_options.attach_screenshot = _read_bool(p_args, "attach_screenshot_on_failure", false);
	} else {
		attachment_options.attach_screenshot = options.attach_screenshot_on_failure;
	}
	attachment_options.snapshot_root = _snapshot_root();
	attachment_options.max_screenshot_bytes = options.max_screenshot_bytes;
	if (p_args.has("max_screenshot_bytes")) {
		attachment_options.max_screenshot_bytes = _read_int(p_args, "max_screenshot_bytes", options.max_screenshot_bytes);
	}
	return attachment_options;
}

EditorAutomationFailureAttachmentOptions EditorAutomationMCPDispatcher::_failure_attachment_options_from_act_context(const EditorAutomationActWaitContext &p_act_context) const {
	EditorAutomationFailureAttachmentOptions attachment_options;
	attachment_options.attach_screenshot = p_act_context.attach_screenshot_on_failure;
	attachment_options.snapshot_root = _snapshot_root();
	attachment_options.max_screenshot_bytes = p_act_context.max_screenshot_bytes > 0
			? p_act_context.max_screenshot_bytes
			: options.max_screenshot_bytes;
	return attachment_options;
}

Dictionary EditorAutomationMCPDispatcher::_enrich_action_failure(
		const Dictionary &p_action_dict,
		const EditorAutomationActionResult &p_action_result,
		const Dictionary &p_selector,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationLogMarker &p_log_marker,
		const Dictionary &p_args) const {
	const EditorAutomationFailureAttachmentOptions attachment_options = _failure_attachment_options(p_args);
	if (!attachment_options.attach_screenshot) {
		return p_action_dict;
	}

	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_action_failure(
			p_action_result.kind,
			p_action_result.message,
			p_selector,
			p_action_result.candidates,
			p_snapshot,
			p_log_marker,
			16,
			attachment_options);

	Dictionary result = p_action_dict;
	result["details"] = diagnostics.details;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_enrich_selector_failure(
		const Dictionary &p_result,
		const EditorAutomationSelectorResult &p_selector_result,
		const Dictionary &p_selector,
		const EditorAutomationSnapshot &p_snapshot,
		const Dictionary &p_args) const {
	const EditorAutomationFailureAttachmentOptions attachment_options = _failure_attachment_options(p_args);
	if (!attachment_options.attach_screenshot) {
		return p_result;
	}

	const String kind = p_selector_result.error_kind.is_empty() ? "selector_failed" : p_selector_result.error_kind;
	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_action_failure(
			kind,
			p_selector_result.message,
			p_selector,
			p_selector_result.candidates,
			p_snapshot,
			EditorAutomationLog::create_marker(),
			16,
			attachment_options);

	Dictionary result = p_result;
	result["details"] = diagnostics.details;
	return result;
}

Dictionary EditorAutomationMCPDispatcher::_tool_find_elements(const Dictionary &p_args, bool &r_is_error) {
	const Dictionary selector = _read_dict(p_args, "selector");
	int max_results = _read_int(p_args, "max_results", MAX_TREE_RESULT_LIMIT);
	if (max_results <= 0) {
		max_results = MAX_TREE_RESULT_LIMIT;
	}
	int offset = 0;
	const String cursor = _read_string(p_args, "cursor");
	if (!cursor.is_empty() && !_decode_find_cursor(cursor, offset)) {
		r_is_error = true;
		Dictionary result;
		result["ok"] = false;
		result["kind"] = "invalid_params";
		result["message"] = "Invalid cursor.";
		return result;
	}

	const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
			? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
			: EditorAutomationSnapshot::capture_from_editor();

	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(snapshot, selector);
	Dictionary result = selector_result.to_dictionary();

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
		for (int i = offset; i < total; i++) {
			if (elements.size() >= max_results) {
				truncated = true;
				break;
			}
			const EditorAutomationElement &element = snapshot.get_element(selector_result.match_indices[i]);
			elements.push_back(_element_summary(element));
		}
		result["elements"] = elements;
		result["match_count"] = total;
		result["truncated"] = truncated;
		result["cursor"] = cursor;
		if (truncated) {
			result["next_cursor"] = _encode_find_cursor(offset + elements.size());
		}
	} else {
		r_is_error = true;
		result = _enrich_selector_failure(result, selector_result, selector, snapshot, p_args);
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

Dictionary EditorAutomationMCPDispatcher::_cooperative_wait_response(const EditorAutomationCooperativeWaitHandle &p_handle, bool &r_is_error, const Dictionary &p_args) {
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
		const EditorAutomationFailureAttachmentOptions attachment_options = p_handle.act_context.active
				? _failure_attachment_options_from_act_context(p_handle.act_context)
				: _failure_attachment_options(p_args);
		const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
				String(result["kind"]),
				p_handle.result.message,
				p_handle.condition,
				p_handle.act_context.action_result,
				p_handle.act_context.selector,
				snapshot,
				p_handle.act_context.log_marker,
				16,
				attachment_options);
		result["details"] = diagnostics.details;
		r_is_error = true;
		return result;
	}

	if (!p_handle.result.ok && p_handle.status == EditorAutomationCooperativeWaitStatus::COMPLETE &&
			p_handle.result.kind == "timeout" && !p_handle.act_context.active) {
		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		const EditorAutomationFailureAttachmentOptions attachment_options = _failure_attachment_options(p_args);
		const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
				"timeout",
				p_handle.result.message,
				p_handle.condition,
				Dictionary(),
				Dictionary(),
				snapshot,
				EditorAutomationLog::create_marker(),
				16,
				attachment_options);
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
	const EditorAutomationFailureAttachmentOptions attachment_options = p_handle.act_context.active
			? _failure_attachment_options_from_act_context(p_handle.act_context)
			: _failure_attachment_options(Dictionary());
	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
			String(result["kind"]),
			p_handle.result.message,
			p_condition,
			p_action_result,
			p_selector,
			snapshot,
			p_log_marker,
			16,
			attachment_options);
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
		return _cooperative_wait_response(handle, r_is_error, p_args);
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
		return _enrich_action_failure(action_dict, action_result, selector, snapshot, log_marker, p_args);
	}

	if (!action_result.ok) {
		Dictionary result;
		result["ok"] = false;
		result["kind"] = "action_failed";
		result["message"] = action_result.message;
		result["action"] = _enrich_action_failure(action_dict, action_result, selector, snapshot, log_marker, p_args);
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
	const EditorAutomationFailureAttachmentOptions attachment_options = _failure_attachment_options(p_args);
	act_context.attach_screenshot_on_failure = attachment_options.attach_screenshot;
	act_context.max_screenshot_bytes = attachment_options.max_screenshot_bytes;

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
			return _cooperative_wait_response(handle, r_is_error, p_args);
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
		return _cooperative_wait_response(handle, r_is_error, p_args);
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
		Dictionary result = wait_result.to_dictionary();
		if (!wait_result.ok) {
			const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
					? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
					: EditorAutomationSnapshot::capture_from_editor();
			const Dictionary selector = _read_dict(condition, "selector");
			const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
					wait_result.kind,
					wait_result.message,
					condition,
					Dictionary(),
					selector,
					snapshot,
					EditorAutomationLog::create_marker(),
					16,
					_failure_attachment_options(p_args));
			result["details"] = diagnostics.details;
		}
		r_is_error = !wait_result.ok;
		return result;
	}

	const String new_wait_id = EditorAutomationWait::begin_cooperative(condition, timeout_ms / 1000.0, context);
	EditorAutomationCooperativeWaitHandle handle;
	EditorAutomationWait::poll_cooperative(new_wait_id, handle);
	return _cooperative_wait_response(handle, r_is_error, p_args);
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

Dictionary EditorAutomationMCPDispatcher::_tool_poll_events(const Dictionary &p_args, bool &r_is_error) {
	r_is_error = false;
	EditorAutomationEvents::poll_sources();

	EditorAutomationEventMarker marker;
	if (p_args.has("since")) {
		const Dictionary since = _read_dict(p_args, "since");
		marker.event_index = _read_int(since, "event_index", 0);
	}

	PackedStringArray kinds;
	if (p_args.has("kinds")) {
		const Variant raw = p_args.get("kinds", Variant());
		if (raw.get_type() == Variant::ARRAY) {
			const Array array = raw;
			for (int i = 0; i < array.size(); i++) {
				kinds.push_back(array[i]);
			}
		} else if (raw.get_type() == Variant::STRING || raw.get_type() == Variant::STRING_NAME) {
			kinds.push_back(raw);
		}
	}

	int limit = _read_int(p_args, "limit", 64);
	if (limit <= 0) {
		limit = 64;
	}

	const int requested_limit = limit;
	Array events = EditorAutomationEvents::read_since(marker, kinds, requested_limit + 1);
	bool has_more = false;
	if (events.size() > requested_limit) {
		has_more = true;
		events.resize(requested_limit);
	}

	Dictionary result;
	result["events"] = events;
	result["count"] = events.size();
	Dictionary marker_dict;
	marker_dict["event_index"] = EditorAutomationEvents::get_event_count();
	result["marker"] = marker_dict;
	result["has_more"] = has_more;
	Dictionary transport;
	transport["server_push"] = false;
	transport["poll_tool"] = "poll_events";
	transport["fallback_tool"] = "read_editor_log";
	result["transport"] = transport;
	return result;
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
	if (p_uri == "foundry://scene/tree") {
		return EditorAutomationState::read_scene_tree(_snapshot_root());
	}
	if (p_uri.begins_with("foundry://element/")) {
		const String element_ref = p_uri.substr(String("foundry://element/").length());
		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		const int element_index = _resolve_element_index(snapshot, element_ref);
		if (element_index < 0) {
			r_ok = false;
			return Dictionary();
		}
		const EditorAutomationElement &element = snapshot.get_element(element_index);
		Dictionary payload;
		payload["element"] = _element_summary(element);
		payload["generation"] = snapshot.get_generation();
		return payload;
	}
	if (p_uri.begins_with("foundry://ui/subtree/")) {
		String remainder = p_uri.substr(String("foundry://ui/subtree/").length());
		int max_depth = options.max_tree_depth;
		const String depth_marker = "/depth/";
		const int depth_pos = remainder.rfind(depth_marker);
		if (depth_pos >= 0) {
			max_depth = remainder.substr(depth_pos + depth_marker.length()).to_int();
			remainder = remainder.substr(0, depth_pos);
		}
		const String element_ref = remainder;
		const EditorAutomationSnapshot snapshot = _snapshot_root() != nullptr
				? EditorAutomationSnapshot::capture_from_node(_snapshot_root())
				: EditorAutomationSnapshot::capture_from_editor();
		const int element_index = _resolve_element_index(snapshot, element_ref);
		if (element_index < 0) {
			r_ok = false;
			return Dictionary();
		}
		bool truncated = false;
		Dictionary payload;
		payload["generation"] = snapshot.get_generation();
		payload["subtree"] = _element_tree(snapshot.get_data(), element_index, 0, max_depth, false, DEFAULT_MAX_CHILDREN_PER_NODE, 0, truncated, true);
		Dictionary limits;
		limits["max_depth"] = max_depth;
		limits["truncated"] = truncated;
		payload["limits"] = limits;
		return payload;
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
