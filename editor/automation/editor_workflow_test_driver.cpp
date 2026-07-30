/**************************************************************************/
/*  editor_workflow_test_driver.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "editor_workflow_test_driver.h"

#include "editor/automation/editor_automation_commands.h"
#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_wait.h"

#include "core/io/json.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

namespace {

Dictionary _element_tree(const EditorAutomationSnapshotData &p_data, int p_index, int p_depth, int p_max_depth, bool p_include_hidden, bool &r_truncated) {
	const EditorAutomationElement &element = p_data.elements[p_index];
	Dictionary dict;
	dict["id"] = element.id;
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
	if (element.internal) {
		dict["internal"] = true;
	}

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

} // namespace

void EditorWorkflowTestDriver::configure(const Options &p_options) {
	options = p_options;
}

void EditorWorkflowTestDriver::begin_workflow() {
	log_marker = EditorAutomationLog::create_marker();
	step_log_marker = log_marker;
	current_step = String();
	last_action = String();
	last_route = String();
	last_selector = Dictionary();
	failure = Failure();
}

void EditorWorkflowTestDriver::set_step(const String &p_step) {
	current_step = p_step;
	step_log_marker = EditorAutomationLog::create_marker();
}

void EditorWorkflowTestDriver::end_step() {
	assert_no_new_errors_since_step();
}

EditorAutomationSnapshot EditorWorkflowTestDriver::_capture_snapshot(bool p_include_internal) const {
	EditorAutomationSnapshotOptions snapshot_options;
	snapshot_options.include_internal = p_include_internal;
	if (options.snapshot_root != nullptr) {
		return EditorAutomationSnapshot::capture_from_node(options.snapshot_root, snapshot_options);
	}
	return EditorAutomationSnapshot::capture_from_editor(snapshot_options);
}

EditorAutomationFailureAttachmentOptions EditorWorkflowTestDriver::_failure_attachment_options() const {
	EditorAutomationFailureAttachmentOptions attachment_options;
	attachment_options.attach_screenshot = options.attach_screenshot_on_failure;
	attachment_options.snapshot_root = options.snapshot_root;
	attachment_options.max_screenshot_bytes = options.max_screenshot_bytes;
	return attachment_options;
}

void EditorWorkflowTestDriver::_record_failure(const String &p_kind, const String &p_message, const Dictionary &p_selector, const EditorAutomationSnapshot &p_snapshot, const Array &p_candidates) {
	if (failure.failed) {
		return;
	}
	failure.failed = true;
	failure.step = current_step;
	failure.message = p_message;
	failure.action = last_action;
	failure.route = last_route;
	failure.selector = p_selector.is_empty() ? last_selector : p_selector;

	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_action_failure(
			p_kind, p_message, failure.selector, p_candidates, p_snapshot, log_marker, 16, _failure_attachment_options());
	failure.diagnostics = diagnostics.to_dictionary();
}

void EditorWorkflowTestDriver::_record_assertion_failure(const String &p_message) {
	if (failure.failed) {
		return;
	}
	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	_record_failure("workflow_assertion_failed", p_message, Dictionary(), snapshot);
}

void EditorWorkflowTestDriver::_record_wait_failure(
		const String &p_kind,
		const String &p_message,
		const Dictionary &p_condition,
		const Dictionary &p_action,
		const Dictionary &p_selector,
		const EditorAutomationSnapshot &p_snapshot) {
	if (failure.failed) {
		return;
	}
	failure.failed = true;
	failure.step = current_step;
	failure.message = p_message;
	failure.action = last_action;
	failure.route = last_route;
	failure.selector = p_selector.is_empty() ? last_selector : p_selector;

	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
			p_kind, p_message, p_condition, p_action, failure.selector, p_snapshot, log_marker, 16, _failure_attachment_options());
	failure.diagnostics = diagnostics.to_dictionary();
}

bool EditorWorkflowTestDriver::_check_result(const Dictionary &p_result, const String &p_context, const Dictionary &p_selector, const EditorAutomationSnapshot &p_snapshot) {
	if (failure.failed) {
		return false;
	}
	const bool ok = (bool)p_result.get("ok", false);
	if (!ok) {
		const String kind = p_result.get("kind", p_context);
		const String message = p_result.get("message", vformat("%s failed.", p_context));
		const Array candidates = p_result.get("candidates", Array());
		_record_failure(kind, message, p_selector, p_snapshot, candidates);
		return false;
	}
	return true;
}

Dictionary EditorWorkflowTestDriver::observe_ui(int p_max_depth, bool p_include_hidden, bool p_include_internal) {
	int max_depth = p_max_depth < 0 ? options.max_tree_depth : p_max_depth;
	if (max_depth < 0) {
		max_depth = 0;
	}

	const EditorAutomationSnapshot snapshot = _capture_snapshot(p_include_internal);
	const EditorAutomationSnapshotData &data = snapshot.get_data();

	bool truncated = false;
	Array roots;
	for (int root_index : data.root_indices) {
		const EditorAutomationElement &root = data.elements[root_index];
		if (!p_include_hidden && !root.visible) {
			continue;
		}
		roots.push_back(_element_tree(data, root_index, 0, max_depth, p_include_hidden, truncated));
	}

	Dictionary result;
	result["ok"] = true;
	result["generation"] = snapshot.get_generation();
	result["focused_element_id"] = snapshot.get_focused_element_id();
	result["tree"] = roots;
	result["windows"] = roots;
	result["modal_stack"] = EditorAutomationState::capture_modal_stack(options.snapshot_root);
	result["element_count"] = data.elements.size();

	Dictionary limits;
	limits["max_depth"] = max_depth;
	limits["include_hidden"] = p_include_hidden;
	limits["include_internal"] = p_include_internal;
	limits["truncated"] = truncated;
	result["limits"] = limits;
	return result;
}

Dictionary EditorWorkflowTestDriver::find(const Dictionary &p_selector, int p_max_results) {
	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(snapshot, p_selector);
	Dictionary result = selector_result.to_dictionary();

	const bool has_matches = (selector_result.status == EditorAutomationSelectorStatus::OK ||
									 selector_result.status == EditorAutomationSelectorStatus::AMBIGUOUS) &&
			!selector_result.match_indices.is_empty();

	if (has_matches) {
		result["ok"] = true;
		result["ambiguous"] = selector_result.status == EditorAutomationSelectorStatus::AMBIGUOUS;
		Array elements;
		const int total = selector_result.match_indices.size();
		bool truncated = false;
		for (int i = 0; i < total; i++) {
			if (elements.size() >= p_max_results) {
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
		result["ok"] = false;
	}
	return result;
}

Dictionary EditorWorkflowTestDriver::act(const Dictionary &p_selector, const String &p_action, const Dictionary &p_args, const String &p_route) {
	last_selector = p_selector;
	last_action = p_action;
	last_route = p_route;

	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	Dictionary options_dict = p_args;
	if (!p_route.is_empty()) {
		options_dict["route"] = p_route;
	}

	const EditorAutomationActionResult action_result = EditorAutomationDriver::perform(snapshot, p_action, p_selector, options_dict);
	Dictionary result = action_result.to_dictionary();
	if (!action_result.ok) {
		_record_failure(action_result.kind, action_result.message, p_selector, snapshot, action_result.candidates);
	}
	return result;
}

Dictionary EditorWorkflowTestDriver::wait_for(const Dictionary &p_condition, int p_timeout_ms) {
	if (p_timeout_ms < 0) {
		p_timeout_ms = options.default_wait_timeout_ms;
	}
	if (p_timeout_ms < 0) {
		p_timeout_ms = 0;
	}

	EditorAutomationWaitContext context;
	context.snapshot_root = options.snapshot_root;

	const EditorAutomationWaitResult wait_result = EditorAutomationWait::wait_for(p_condition, p_timeout_ms / 1000.0, context);
	Dictionary result = wait_result.to_dictionary();
	if (!wait_result.ok) {
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_wait_failure(wait_result.kind, wait_result.message, p_condition, Dictionary(), last_selector, snapshot);
	}
	return result;
}

Dictionary EditorWorkflowTestDriver::read_editor_state() {
	Dictionary result = EditorAutomationState::read_editor_state();
	result["ok"] = true;
	return result;
}

Dictionary EditorWorkflowTestDriver::read_editor_log(const EditorAutomationLogMarker *p_since, const String &p_severity, int p_limit) {
	PackedStringArray severities;
	if (!p_severity.is_empty()) {
		severities.push_back(p_severity);
	}

	Array entries;
	if (p_since != nullptr) {
		entries = EditorAutomationLog::read_since(*p_since, severities);
	} else {
		if (p_limit <= 0) {
			p_limit = 64;
		}
		entries = EditorAutomationLog::read_recent(p_limit, severities);
	}

	Dictionary result;
	result["ok"] = true;
	result["entries"] = entries;
	result["count"] = entries.size();
	Dictionary marker;
	marker["message_index"] = EditorAutomationLog::get_message_count();
	result["marker"] = marker;
	return result;
}

Dictionary EditorWorkflowTestDriver::run_command(const String &p_command) {
	last_action = "run_command";
	last_route = String();
	Dictionary selector;
	selector["command"] = p_command;
	last_selector = selector;

	const Dictionary result = EditorAutomationCommands::execute(p_command);
	if ((bool)result.get("ok", false)) {
		last_route = result.get("route", String());
		return result;
	}

	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	const String kind = result.get("kind", String());
	const String message = result.get("message", vformat("run_command failed for '%s'.", p_command));
	const Array candidates = result.get("candidates", Array());
	_record_failure(kind, message, selector, snapshot, candidates);
	return result;
}

bool EditorWorkflowTestDriver::assert_no_new_errors(const PackedStringArray &p_severities) {
	PackedStringArray severities = p_severities;
	if (severities.is_empty()) {
		severities.push_back("error");
	}

	if (EditorAutomationLog::has_new_messages_since(log_marker, severities)) {
		const Array entries = EditorAutomationLog::read_since(log_marker, severities);
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_failure("editor_error_after_action", "New editor errors were logged during the workflow.", Dictionary(), snapshot);
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::require_ok(const Dictionary &p_result, const String &p_context) {
	if (failure.failed) {
		return false;
	}
	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	return _check_result(p_result, p_context, last_selector, snapshot);
}

void EditorWorkflowTestDriver::flush_frames(int p_count) {
	for (int i = 0; i < p_count; i++) {
		if (SceneTree::get_singleton() != nullptr) {
			SceneTree::get_singleton()->process(1.0 / 60.0);
		}
		if (MessageQueue::get_singleton() != nullptr) {
			MessageQueue::get_singleton()->flush();
		}
		OS::get_singleton()->delay_usec(1000);
	}
}

bool EditorWorkflowTestDriver::wait_editor_idle(int p_timeout_ms) {
	Dictionary condition;
	condition["type"] = "editor_idle";
	return require_ok(wait_for(condition, p_timeout_ms), "wait_editor_idle");
}

bool EditorWorkflowTestDriver::wait_import_idle(int p_timeout_ms) {
	Dictionary condition;
	condition["type"] = "import_reload_idle";
	return require_ok(wait_for(condition, p_timeout_ms), "wait_import_idle");
}

bool EditorWorkflowTestDriver::wait_workspace_settled(int p_timeout_ms) {
	Dictionary condition;
	condition["type"] = "workspace_settled";
	return require_ok(wait_for(condition, p_timeout_ms), "wait_workspace_settled");
}

bool EditorWorkflowTestDriver::wait_filesystem_idle(int p_timeout_ms) {
	Dictionary condition;
	condition["type"] = "filesystem_idle";
	return require_ok(wait_for(condition, p_timeout_ms), "wait_filesystem_idle");
}

bool EditorWorkflowTestDriver::wait_script_analysis_idle(int p_timeout_ms) {
	Dictionary condition;
	condition["type"] = "script_analysis_idle";
	return require_ok(wait_for(condition, p_timeout_ms), "wait_script_analysis_idle");
}

Dictionary EditorWorkflowTestDriver::capture_workspace_context() const {
	const Dictionary state = const_cast<EditorWorkflowTestDriver *>(this)->read_editor_state();
	Dictionary context;
	context["editor_state"] = state;
	if (state.has("focused_tile_id")) {
		context["focused_tile_id"] = state["focused_tile_id"];
	}
	if (state.has("workspace")) {
		context["workspace"] = state["workspace"];
	}
	if (state.has("active_scene_path")) {
		context["active_scene_path"] = state["active_scene_path"];
	}
	if (state.has("main_screen")) {
		context["main_screen"] = state["main_screen"];
	}
	if (state.has("inspector")) {
		context["inspector"] = state["inspector"];
	}
	if (state.has("view_2d")) {
		context["view_2d"] = state["view_2d"];
	}
	if (state.has("view_3d")) {
		context["view_3d"] = state["view_3d"];
	}
	if (state.has("undo_redo")) {
		context["undo_redo"] = state["undo_redo"];
	}
	return context;
}

Dictionary EditorWorkflowTestDriver::make_failure_details(const String &p_step) const {
	Dictionary details;
	details["step"] = p_step.is_empty() ? current_step : p_step;
	details["workspace_context"] = capture_workspace_context();
	if (failure.failed && !failure.diagnostics.is_empty()) {
		details["diagnostics"] = failure.diagnostics;
	}
	if (failure.failed) {
		details["failure_report"] = format_failure_report();
	}
	return details;
}

bool EditorWorkflowTestDriver::assert_no_new_errors_since_step(const PackedStringArray &p_severities) {
	if (failure.failed) {
		return false;
	}
	PackedStringArray severities = p_severities;
	if (severities.is_empty()) {
		severities.push_back("error");
	}
	if (EditorAutomationLog::has_new_messages_since(step_log_marker, severities)) {
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_failure("editor_error_after_step", "New editor errors were logged during the current workflow step.", Dictionary(), snapshot);
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_focused_tile_id(int p_tile_id) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const int focused = (int)state.get("focused_tile_id", -1);
	if (focused != p_tile_id) {
		_record_assertion_failure(vformat("Expected focused tile id %d but found %d.", p_tile_id, focused));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_active_scene_path(const String &p_path) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const String active_path = state.get("active_scene_path", String());
	if (active_path != p_path) {
		_record_assertion_failure(vformat("Expected active scene path '%s' but found '%s'.", p_path, active_path));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_selected_node_class(const String &p_class, const String &p_exclude_name, bool p_require_match) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Array selected = state.get("selected_nodes", Array());
	bool found = false;
	for (int i = 0; i < selected.size(); i++) {
		const Dictionary entry = selected[i];
		const String node_class = entry.get("class", String());
		const String node_name = entry.get("name", String());
		if (node_class == p_class && (p_exclude_name.is_empty() || node_name != p_exclude_name)) {
			found = true;
			break;
		}
	}
	if (found != p_require_match) {
		_record_assertion_failure(vformat("Selected node class assertion failed for '%s'.", p_class));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_selected_paths(const PackedStringArray &p_paths) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Array selected = state.get("selected_paths", Array());
	PackedStringArray actual;
	for (int i = 0; i < selected.size(); i++) {
		actual.push_back(selected[i]);
	}
	actual.sort();
	PackedStringArray expected = p_paths;
	expected.sort();
	if (actual != expected) {
		_record_assertion_failure(vformat("Expected selected paths %s but found %s.", String(", ").join(expected), String(", ").join(actual)));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_inspector_target_class(const String &p_class) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Dictionary inspector = state.get("inspector", Dictionary());
	if (!(bool)inspector.get("supported", false)) {
		_record_assertion_failure("Inspector state is unavailable.");
		return false;
	}
	const String target_class = inspector.get("target_class", String());
	if (target_class != p_class) {
		_record_assertion_failure(vformat("Expected inspector target class '%s' but found '%s'.", p_class, target_class));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_undo_history_id(int p_history_id) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Dictionary undo_redo = state.get("undo_redo", Dictionary());
	if (!(bool)undo_redo.get("supported", false)) {
		_record_assertion_failure("Undo/redo state is unavailable.");
		return false;
	}
	const int history_id = (int)undo_redo.get("current_history_id", -1);
	if (history_id != p_history_id) {
		_record_assertion_failure(vformat("Expected undo history id %d but found %d.", p_history_id, history_id));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_scene_unsaved(bool p_expected) {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Dictionary unsaved = state.get("unsaved", Dictionary());
	const bool current = (bool)unsaved.get("current_scene", false);
	if (current != p_expected) {
		_record_assertion_failure(vformat("Expected scene unsaved=%s but found %s.", p_expected ? "true" : "false", current ? "true" : "false"));
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_playing(bool p_expected, int p_timeout_ms) {
	if (failure.failed) {
		return false;
	}
	if (p_timeout_ms < 0) {
		p_timeout_ms = options.default_wait_timeout_ms;
	}
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + (uint64_t)p_timeout_ms;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		const Dictionary state = read_editor_state();
		const Dictionary playing = state.get("playing", Dictionary());
		const bool is_playing = (bool)playing.get("is_playing", false);
		if (is_playing == p_expected) {
			return true;
		}
		flush_frames(5);
	}
	_record_assertion_failure(vformat("Expected playing=%s but timed out.", p_expected ? "true" : "false"));
	return false;
}

bool EditorWorkflowTestDriver::assert_view_2d_has_zoom() {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Dictionary view_2d = state.get("view_2d", Dictionary());
	if (!(bool)view_2d.get("supported", false) || !view_2d.has("zoom")) {
		_record_assertion_failure("2D view state is unavailable or missing zoom.");
		return false;
	}
	return true;
}

bool EditorWorkflowTestDriver::assert_view_3d_has_camera() {
	if (failure.failed) {
		return false;
	}
	const Dictionary state = read_editor_state();
	const Dictionary view_3d = state.get("view_3d", Dictionary());
	if (!(bool)view_3d.get("supported", false) || !view_3d.has("camera_position")) {
		_record_assertion_failure("3D view state is unavailable or missing camera.");
		return false;
	}
	return true;
}

String EditorWorkflowTestDriver::format_failure_report() const {
	if (!failure.failed) {
		return String();
	}

	String report;
	report += vformat("Editor automation workflow failed at step '%s'.\n", failure.step);
	if (!failure.message.is_empty()) {
		report += vformat("Message: %s\n", failure.message);
	}
	if (!failure.action.is_empty()) {
		report += vformat("Action: %s\n", failure.action);
	}
	if (!failure.route.is_empty()) {
		report += vformat("Route: %s\n", failure.route);
	}
	if (!failure.selector.is_empty()) {
		report += vformat("Selector: %s\n", JSON::stringify(failure.selector, "", false));
	}
	if (!failure.diagnostics.is_empty()) {
		report += vformat("Diagnostics: %s\n", JSON::stringify(failure.diagnostics, "  ", true));
	}
	return report;
}
