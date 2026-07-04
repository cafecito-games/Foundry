/**************************************************************************/
/*  editor_automation_mcp_schemas.cpp                                     */
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

#include "editor_automation_mcp_schemas.h"

namespace {

Dictionary _string_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = "string";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _integer_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = "integer";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _number_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = "number";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _boolean_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = "boolean";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _object_schema(const String &p_description = String()) {
	Dictionary schema;
	schema["type"] = "object";
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _array_schema(const Dictionary &p_items, const String &p_description = String()) {
	Dictionary schema;
	schema["type"] = "array";
	schema["items"] = p_items;
	if (!p_description.is_empty()) {
		schema["description"] = p_description;
	}
	return schema;
}

Dictionary _enum_string_schema(const Array &p_values, const String &p_description) {
	Dictionary schema = _string_schema(p_description);
	schema["enum"] = p_values;
	return schema;
}

Dictionary _make_tool(const String &p_name, const String &p_description, const Dictionary &p_properties, const Array &p_required, const Dictionary &p_output_schema) {
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
	tool["outputSchema"] = p_output_schema;
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

Dictionary _make_resource_template(const String &p_uri_template, const String &p_name, const String &p_description) {
	Dictionary resource;
	resource["uriTemplate"] = p_uri_template;
	resource["name"] = p_name;
	resource["description"] = p_description;
	resource["mimeType"] = "application/json";
	return resource;
}

Dictionary _element_node_schema() {
	Dictionary schema = _object_schema("Semantic UI element node.");
	Dictionary props;
	props["id"] = _string_schema("Snapshot-scoped element id.");
	props["handle"] = _string_schema("Durable element handle.");
	props["role"] = _string_schema("Semantic role.");
	props["name"] = _string_schema("Accessible/visible name.");
	props["text"] = _string_schema("Visible text/value.");
	props["class"] = _string_schema("Engine class name.");
	props["path"] = _string_schema("Node path.");
	props["visible"] = _boolean_schema("Visibility.");
	props["enabled"] = _boolean_schema("Enabled state.");
	props["focused"] = _boolean_schema("Focus state.");
	props["pressed"] = _boolean_schema("Pressed state.");
	props["selected"] = _boolean_schema("Selected state.");
	props["bounds"] = _array_schema(_integer_schema("Coordinate or size component."), "Bounds as [x, y, width, height].");
	props["actions"] = _array_schema(_string_schema("Semantic action name."), "Supported semantic actions.");
	props["metadata"] = _object_schema("Additional element metadata.");
	props["children"] = _array_schema(_object_schema(), "Child element nodes.");
	props["children_truncated"] = _boolean_schema("True when additional children were omitted.");
	props["child_count"] = _integer_schema("Total child count when truncated.");
	props["children_next_cursor"] = _string_schema("Opaque cursor to fetch the next page of children.");
	schema["properties"] = props;
	return schema;
}

Dictionary _ok_result_schema() {
	Dictionary schema = _object_schema("Structured tool result.");
	Dictionary props;
	props["ok"] = _boolean_schema("Whether the operation succeeded.");
	schema["properties"] = props;
	return schema;
}

Array _wait_condition_types() {
	Array types;
	types.push_back("next_frame");
	types.push_back("editor_idle");
	types.push_back("selector_appears");
	types.push_back("selector_disappears");
	types.push_back("selector_matches");
	types.push_back("focus_matches");
	types.push_back("modal_stack_changed");
	types.push_back("modal_stack_settled");
	types.push_back("filesystem_idle");
	types.push_back("import_reload_idle");
	types.push_back("script_analysis_idle");
	types.push_back("log_contains");
	types.push_back("no_new_errors");
	return types;
}

Array _action_names() {
	Array actions;
	actions.push_back("click");
	actions.push_back("focus");
	actions.push_back("type_text");
	actions.push_back("set_text");
	actions.push_back("submit");
	actions.push_back("press_key");
	actions.push_back("drag");
	actions.push_back("select");
	actions.push_back("activate");
	actions.push_back("expand");
	actions.push_back("collapse");
	actions.push_back("scroll");
	actions.push_back("choose_menu_item");
	actions.push_back("set_value");
	return actions;
}

Array _route_enum_values() {
	Array values;
	values.push_back("auto");
	values.push_back("semantic");
	values.push_back("input");
	return values;
}

Array _severity_enum_values() {
	Array values;
	values.push_back("error");
	values.push_back("warning");
	values.push_back("editor");
	values.push_back("stdout");
	values.push_back("stdout_rich");
	return values;
}

Array _scroll_direction_enum_values() {
	Array values;
	values.push_back("up");
	values.push_back("down");
	values.push_back("left");
	values.push_back("right");
	return values;
}

Array _wait_status_enum_values() {
	Array values;
	values.push_back("pending");
	values.push_back("complete");
	values.push_back("cancelled");
	return values;
}

} // namespace

Dictionary EditorAutomationMCPSchemas::selector_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["description"] = "Semantic selector by role, name, text, class, path, state, and containment.";

	Dictionary props;
	props["id"] = _string_schema("Snapshot-scoped opaque element id from observe_ui/find_elements.");
	props["handle"] = _string_schema("Durable element handle from observe_ui/find_elements.");
	props["role"] = _string_schema("Exact semantic role.");
	props["role_contains"] = _string_schema("Substring match against role.");
	props["name"] = _string_schema("Exact accessible/visible name.");
	props["name_contains"] = _string_schema("Substring match against name.");
	props["text"] = _string_schema("Exact visible text/value.");
	props["text_contains"] = _string_schema("Substring match against text.");
	props["class"] = _string_schema("Exact engine class name.");
	props["class_contains"] = _string_schema("Substring match against class name.");
	props["path"] = _string_schema("Exact node path.");
	props["path_contains"] = _string_schema("Substring match against node path.");
	props["visible"] = _boolean_schema("Match visibility.");
	props["enabled"] = _boolean_schema("Match enabled state.");
	props["focused"] = _boolean_schema("Match focus state.");
	props["visible_only"] = _boolean_schema("Keep only visible elements when true.");
	props["enabled_only"] = _boolean_schema("Keep only enabled elements when true.");
	props["selected"] = _boolean_schema("Match selected state.");
	props["metadata"] = _object_schema("Metadata key/value pairs to match.");
	props["case_sensitive"] = _boolean_schema("Case-sensitive string matching.");
	props["nth"] = _integer_schema("Pick the Nth match after filtering.");
	props["index"] = _integer_schema("Synonym for nth.");
	props["within"] = _object_schema("Nested selector scope. Accepts the same selector fields as the parent selector.");
	schema["properties"] = props;
	return schema;
}

Dictionary EditorAutomationMCPSchemas::action_args_schema() {
	Dictionary schema = _object_schema("Action-specific arguments passed to act.");
	Dictionary props;
	props["text"] = _string_schema("Text for type_text/set_text.");
	props["key"] = _string_schema("Key name for press_key.");
	props["value"] = _string_schema("Value for set_value.");
	props["route"] = _enum_string_schema(_route_enum_values(), "Route preference override.");
	props["button"] = _string_schema("Mouse button for click/drag.");
	props["modifiers"] = _array_schema(_string_schema("Modifier key name."), "Modifier keys.");
	props["position"] = _array_schema(_number_schema("Coordinate component."), "Relative click position [x, y].");
	props["direction"] = _enum_string_schema(_scroll_direction_enum_values(), "Scroll direction.");
	props["amount"] = _integer_schema("Scroll amount.");
	props["page"] = _boolean_schema("Scroll by page when true.");
	props["target"] = selector_schema();
	props["target_point"] = _array_schema(_number_schema("Coordinate component."), "Absolute drag target point.");
	props["waypoints"] = _array_schema(_array_schema(_number_schema("Coordinate component.")), "Drag path.");
	props["path"] = _array_schema(_array_schema(_number_schema("Coordinate component.")), "Alias for waypoints.");
	schema["properties"] = props;
	return schema;
}

Dictionary EditorAutomationMCPSchemas::wait_condition_schema() {
	Dictionary schema = _object_schema("Wait condition object.");
	Dictionary props;
	const Array condition_types = _wait_condition_types();
	props["type"] = _enum_string_schema(condition_types, "Wait condition kind.");
	props["condition"] = _enum_string_schema(condition_types, "Shorthand alias for type.");
	props["selector"] = selector_schema();
	props["text"] = _string_schema("Substring for log_contains.");
	props["severity"] = _enum_string_schema(_severity_enum_values(), "Severity filter.");
	props["severities"] = _array_schema(_string_schema("Log severity."), "Multiple severities.");
	props["marker"] = log_marker_schema();
	props["fields"] = _object_schema("Field equality checks for selector_matches.");
	props["baseline"] = _object_schema("Baseline modal stack for modal_stack_changed.");
	schema["properties"] = props;
	return schema;
}

Dictionary EditorAutomationMCPSchemas::log_marker_schema() {
	Dictionary schema = _object_schema("Log read marker.");
	Dictionary props;
	props["message_index"] = _integer_schema("Exclusive lower bound message index.");
	schema["properties"] = props;
	return schema;
}

Dictionary EditorAutomationMCPSchemas::event_marker_schema() {
	Dictionary schema = _object_schema("Automation event read marker.");
	Dictionary props;
	props["event_index"] = _integer_schema("Exclusive lower bound event index.");
	schema["properties"] = props;
	return schema;
}

static Dictionary _failure_attachment_schema() {
	Dictionary schema = _object_schema("Optional failure screenshot attachment.");
	Dictionary props;
	props["status"] = _enum_string_schema([]() {
		Array values;
		values.push_back("available");
		values.push_back("unavailable");
		values.push_back("truncated");
		return values;
	}(), "Screenshot capture status.");
	props["reason"] = _string_schema("Reason when status is unavailable or truncated.");
	props["format"] = _string_schema("Image format (png).");
	props["encoding"] = _string_schema("Inline encoding (base64).");
	props["data"] = _string_schema("Inline image payload when available.");
	props["capture_mode"] = _enum_string_schema([]() {
		Array values;
		values.push_back("full_window");
		values.push_back("cropped");
		return values;
	}(), "Whether the image is full-window or cropped to the target/focused element.");
	props["viewport"] = _object_schema("Captured viewport/window metadata.");
	props["image"] = _object_schema("Returned image dimensions.");
	props["crop"] = _object_schema("Source crop rectangle in viewport coordinates.");
	props["highlight"] = _object_schema("Target/focused element bounds in viewport coordinates.");
	props["byte_size"] = _integer_schema("Raw encoded image size in bytes.");
	props["max_bytes"] = _integer_schema("Configured max inline payload size.");
	schema["properties"] = props;
	return schema;
}

static void _add_failure_attachment_input_props(Dictionary &r_props) {
	r_props["attach_screenshot_on_failure"] = _boolean_schema("When true, attach an optional viewport screenshot to failure diagnostics.");
	r_props["max_screenshot_bytes"] = _integer_schema("Maximum inline screenshot payload size in bytes (default 524288).");
}

static void _add_failure_details_output_props(Dictionary &r_output_props) {
	r_output_props["details"] = _object_schema("Structured failure diagnostics.");
	Dictionary details_props;
	details_props["screenshot"] = _failure_attachment_schema();
	Dictionary details_schema = _object_schema("Failure details payload.");
	details_schema["properties"] = details_props;
	r_output_props["details"] = details_schema;
}

Array EditorAutomationMCPSchemas::build_tools_list() {
	Array tools;

	{
		Dictionary props;
		props["max_depth"] = _integer_schema("Maximum tree depth to include (default 8).");
		props["include_hidden"] = _boolean_schema("Include hidden elements (default false).");
		props["max_children"] = _integer_schema("Maximum children per node before pagination (default 32).");
		props["subtree_cursor"] = _string_schema("Opaque cursor from children_next_cursor to fetch the next child page.");
		Dictionary output = _object_schema("observe_ui structured result.");
		Dictionary output_props;
		output_props["generation"] = _integer_schema("Snapshot generation.");
		output_props["focused_element_id"] = _string_schema("Focused element id.");
		output_props["tree"] = _array_schema(_element_node_schema(), "Root element trees.");
		output_props["windows"] = _array_schema(_element_node_schema(), "Alias of tree.");
		output_props["modal_stack"] = _array_schema(_object_schema(), "Modal stack entries.");
		output_props["element_count"] = _integer_schema("Total elements in snapshot.");
		output_props["limits"] = _object_schema("Applied limits and truncation flags.");
		output_props["subtree"] = _element_node_schema();
		output["properties"] = output_props;
		tools.push_back(_make_tool("observe_ui",
				"Returns the current windows, focused element, modal stack, and visible semantic tree.",
				props, Array(), output));
	}

	{
		Dictionary props;
		props["selector"] = selector_schema();
		props["max_results"] = _integer_schema("Maximum number of matches to return per page (default 20).");
		props["cursor"] = _string_schema("Opaque pagination cursor from next_cursor.");
		_add_failure_attachment_input_props(props);
		Array required;
		required.push_back("selector");
		Dictionary output = _object_schema("find_elements structured result.");
		Dictionary output_props;
		output_props["ok"] = _boolean_schema("Whether matches were found.");
		output_props["elements"] = _array_schema(_element_node_schema(), "Matched element summaries.");
		output_props["match_count"] = _integer_schema("Total match count.");
		output_props["truncated"] = _boolean_schema("True when more matches remain.");
		output_props["next_cursor"] = _string_schema("Cursor for the next page of matches.");
		_add_failure_details_output_props(output_props);
		output["properties"] = output_props;
		tools.push_back(_make_tool("find_elements",
				"Resolves selectors and returns matches or structured no-match/ambiguous diagnostics.",
				props, required, output));
	}

	{
		Dictionary props;
		props["selector"] = selector_schema();
		props["action"] = _enum_string_schema(_action_names(), "Action to perform.");
		props["route"] = _enum_string_schema(_route_enum_values(), "Route preference.");
		props["args"] = action_args_schema();
		props["wait"] = wait_condition_schema();
		props["wait_timeout_ms"] = _integer_schema("Timeout in milliseconds for the optional wait clause (default 5000).");
		props["wait_id"] = _string_schema("Poll or continue an existing cooperative act+wait by id.");
		_add_failure_attachment_input_props(props);
		Array required;
		required.push_back("action");
		Dictionary act_output = _ok_result_schema();
		Dictionary act_output_props = act_output["properties"];
		_add_failure_details_output_props(act_output_props);
		act_output["properties"] = act_output_props;
		tools.push_back(_make_tool("act",
				"Performs a semantic or input action on a selected element and optionally waits for a UI condition in one call.",
				props, required, act_output));
	}

	{
		Dictionary props;
		const Array condition_types = _wait_condition_types();
		props["condition"] = _enum_string_schema(condition_types, "Wait condition shorthand.");
		props["type"] = _enum_string_schema(condition_types, "Wait condition kind.");
		props["selector"] = selector_schema();
		props["timeout_ms"] = _integer_schema("Timeout in milliseconds (default 5000).");
		props["wait_id"] = _string_schema("Poll or cancel an existing cooperative wait by id.");
		props["cancel"] = _boolean_schema("When true with wait_id, cancel the pending wait.");
		props["cooperative"] = _boolean_schema("When true (default), return immediately while the wait is pending.");
		props["text"] = _string_schema("Substring for log_contains.");
		props["severity"] = _string_schema("Severity filter for log conditions.");
		props["marker"] = log_marker_schema();
		props["fields"] = _object_schema("Field equality checks for selector_matches.");
		_add_failure_attachment_input_props(props);
		Dictionary output = _ok_result_schema();
		Dictionary output_props = output["properties"];
		output_props["status"] = _enum_string_schema(_wait_status_enum_values(), "Cooperative wait status.");
		output_props["wait_id"] = _string_schema("Cooperative wait id.");
		_add_failure_details_output_props(output_props);
		tools.push_back(_make_tool("wait_for",
				"Waits cooperatively for a UI condition and returns success/failure diagnostics without blocking the editor for the full timeout.",
				props, Array(), output));
	}

	tools.push_back(_make_tool("read_editor_state",
			"Returns selected nodes, open scenes, active scene, current script, playing state, and unsaved state.",
			Dictionary(), Array(), _object_schema("Current editor state.")));

	{
		Dictionary props;
		props["severity"] = _enum_string_schema(_severity_enum_values(), "Optional severity filter.");
		props["since"] = log_marker_schema();
		props["limit"] = _integer_schema("Maximum recent entries when no marker is provided (default 64).");
		Dictionary output = _object_schema("Editor log readback.");
		Dictionary output_props;
		output_props["entries"] = _array_schema(_object_schema(), "Log entries.");
		output_props["count"] = _integer_schema("Returned entry count.");
		output_props["marker"] = log_marker_schema();
		output["properties"] = output_props;
		tools.push_back(_make_tool("read_editor_log",
				"Returns editor log entries, optionally filtered by severity and since-marker. For push notifications, use poll_events; this tool remains the polling fallback.",
				props, Array(), output));
	}

	{
		Dictionary props;
		props["command"] = _string_schema("Command palette command key or editor shortcut path.");
		Array required;
		required.push_back("command");
		tools.push_back(_make_tool("run_command",
				"Executes a command palette command or editor shortcut action by key via the existing editor registries.",
				props, required, _ok_result_schema()));
	}

	{
		Dictionary props;
		props["query"] = _string_schema("Optional substring filter against command keys and labels.");
		props["category"] = _string_schema("Optional category prefix filter.");
		props["runnable_only"] = _boolean_schema("When true, include only commands runnable by run_command.");
		props["limit"] = _integer_schema("Optional maximum number of commands to return.");
		Dictionary output = _object_schema("Command discovery result.");
		Dictionary output_props;
		output_props["ok"] = _boolean_schema("Whether listing succeeded.");
		output_props["commands"] = _array_schema(_object_schema(), "Command entries.");
		output["properties"] = output_props;
		tools.push_back(_make_tool("list_commands",
				"Lists command palette commands and editor shortcut actions with runnable metadata.",
				props, Array(), output));
	}

	{
		Dictionary props;
		props["since"] = event_marker_schema();
		Array kinds;
		kinds.push_back("editor_log_error");
		kinds.push_back("editor_log_warning");
		kinds.push_back("automation");
		props["kinds"] = _array_schema(_enum_string_schema(kinds, "Automation event kind."), "Optional event kind filter.");
		props["limit"] = _integer_schema("Maximum events to return (default 64).");
		Dictionary output = _object_schema("Automation event poll result.");
		Dictionary output_props;
		output_props["events"] = _array_schema(_object_schema(), "Automation events since the marker.");
		output_props["count"] = _integer_schema("Returned event count.");
		output_props["marker"] = event_marker_schema();
		output_props["has_more"] = _boolean_schema("True when additional events remain after this page.");
		output_props["transport"] = _object_schema("Transport capabilities for notifications.");
		output["properties"] = output_props;
		tools.push_back(_make_tool("poll_events",
				"Returns queued editor log/automation events. The POST-only MCP transport cannot push notifications; poll this tool (or read_editor_log) instead.",
				props, Array(), output));
	}

	return tools;
}

Array EditorAutomationMCPSchemas::build_resource_templates_list() {
	Array templates;
	templates.push_back(_make_resource_template("foundry://element/{id}", "Element", "Single semantic UI element summary."));
	templates.push_back(_make_resource_template("foundry://ui/subtree/{id}", "UI Subtree", "Semantic UI subtree rooted at an element id."));
	templates.push_back(_make_resource_template("foundry://ui/subtree/{id}/depth/{depth}", "UI Subtree (depth)", "Semantic UI subtree with an explicit max depth."));
	templates.push_back(_make_resource_template("foundry://scene/tree", "Scene Tree", "Edited scene node hierarchy."));
	return templates;
}

Array EditorAutomationMCPSchemas::build_resources_list() {
	Array resources;
	resources.push_back(_make_resource("foundry://ui/tree", "UI Tree", "Current visible semantic UI tree."));
	resources.push_back(_make_resource("foundry://editor/state", "Editor State", "Current editor state readback."));
	resources.push_back(_make_resource("foundry://editor/log", "Editor Log", "Recent editor log entries."));
	resources.push_back(_make_resource("foundry://scene/active", "Active Scene", "Active scene and edited root information."));
	resources.push_back(_make_resource("foundry://scene/tree", "Scene Tree", "Edited scene node hierarchy."));
	resources.push_back(_make_resource("foundry://commands", "Editor Commands", "Command palette commands and editor shortcut actions with runnable metadata."));
	return resources;
}
