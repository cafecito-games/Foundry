/**************************************************************************/
/*  editor_workflow_test_driver.cpp                                       */
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

#include "editor_workflow_test_driver.h"

#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_wait.h"

#include "core/io/json.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_command_palette.h"
#endif

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
	current_step = String();
	last_action = String();
	last_route = String();
	last_selector = Dictionary();
	failure = Failure();
}

void EditorWorkflowTestDriver::set_step(const String &p_step) {
	current_step = p_step;
}

EditorAutomationSnapshot EditorWorkflowTestDriver::_capture_snapshot() const {
	if (options.snapshot_root != nullptr) {
		return EditorAutomationSnapshot::capture_from_node(options.snapshot_root);
	}
	return EditorAutomationSnapshot::capture_from_editor();
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
			p_kind, p_message, failure.selector, p_candidates, p_snapshot, log_marker);
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

Dictionary EditorWorkflowTestDriver::observe_ui(int p_max_depth, bool p_include_hidden) {
	int max_depth = p_max_depth < 0 ? options.max_tree_depth : p_max_depth;
	if (max_depth < 0) {
		max_depth = 0;
	}

	const EditorAutomationSnapshot snapshot = _capture_snapshot();
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
		_record_failure(wait_result.kind, wait_result.message, last_selector, snapshot);
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
	last_route = "command_palette";
	Dictionary selector;
	selector["command"] = p_command;
	last_selector = selector;

	Dictionary result;
	result["command"] = p_command;

	if (p_command.is_empty()) {
		result["ok"] = false;
		result["kind"] = "invalid_parameter";
		result["message"] = "run_command requires a non-empty command.";
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_failure("invalid_parameter", result["message"], selector, snapshot);
		return result;
	}

#ifdef TOOLS_ENABLED
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	if (palette == nullptr) {
		result["ok"] = false;
		result["kind"] = "unavailable";
		result["message"] = "EditorCommandPalette is not available.";
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_failure("unavailable", result["message"], selector, snapshot);
		return result;
	}

	List<String> actions;
	palette->get_actions_list(&actions);
	bool found = false;
	for (const String &action : actions) {
		if (action == p_command) {
			found = true;
			break;
		}
	}
	if (!found) {
		result["ok"] = false;
		result["kind"] = "unknown_command";
		result["message"] = vformat("Unknown command '%s'.", p_command);
		const EditorAutomationSnapshot snapshot = _capture_snapshot();
		_record_failure("unknown_command", result["message"], selector, snapshot);
		return result;
	}

	palette->execute_command(p_command);
	result["ok"] = true;
	result["route"] = "command_palette";
	return result;
#else
	result["ok"] = false;
	result["kind"] = "unavailable";
	result["message"] = "Command palette is only available in editor builds.";
	const EditorAutomationSnapshot snapshot = _capture_snapshot();
	_record_failure("unavailable", result["message"], selector, snapshot);
	return result;
#endif
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
