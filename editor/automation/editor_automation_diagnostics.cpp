/**************************************************************************/
/*  editor_automation_diagnostics.cpp                                     */
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

#include "editor_automation_diagnostics.h"

#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"

namespace {

Dictionary _element_to_summary(const EditorAutomationElement &p_element) {
	Dictionary summary;
	summary["id"] = p_element.id;
	summary["role"] = p_element.role;
	summary["name"] = p_element.name;
	summary["text"] = p_element.text;
	summary["class"] = p_element.class_name;
	summary["path"] = p_element.path;
	summary["focused"] = p_element.focused;
	if (!p_element.metadata.is_empty()) {
		if (p_element.metadata.has("window_object_id")) {
			summary["window_object_id"] = p_element.metadata["window_object_id"];
		}
		if (p_element.metadata.has("window_title")) {
			summary["window_title"] = p_element.metadata["window_title"];
		}
		if (p_element.metadata.has("window_focused")) {
			summary["window_focused"] = p_element.metadata["window_focused"];
		}
	}
	return summary;
}

Dictionary _element_subtree_at_index(const EditorAutomationSnapshot &p_snapshot, int p_index, int p_depth) {
	const EditorAutomationElement &element = p_snapshot.get_element(p_index);
	Dictionary dict = _element_to_summary(element);
	if (p_depth <= 0) {
		return dict;
	}
	Array children;
	for (int child_index : element.children) {
		children.push_back(_element_subtree_at_index(p_snapshot, child_index, p_depth - 1));
	}
	dict["children"] = children;
	return dict;
}

} // namespace

Dictionary EditorAutomationDiagnostics::to_dictionary() const {
	Dictionary dict;
	dict["ok"] = false;
	dict["kind"] = kind;
	dict["message"] = message;
	if (!details.is_empty()) {
		dict["details"] = details;
	}
	return dict;
}

Dictionary EditorAutomationDiagnosticsBuilder::element_summary(const EditorAutomationElement &p_element) {
	return _element_to_summary(p_element);
}

Dictionary EditorAutomationDiagnosticsBuilder::element_subtree(const EditorAutomationSnapshot &p_snapshot, const String &p_element_id, int p_depth) {
	const EditorAutomationElement *element = p_snapshot.find_by_id(p_element_id);
	if (element == nullptr) {
		return Dictionary();
	}
	const int *index = p_snapshot.get_data().id_to_index.getptr(p_element_id);
	ERR_FAIL_NULL_V(index, Dictionary());
	return _element_subtree_at_index(p_snapshot, *index, p_depth);
}

Dictionary EditorAutomationDiagnosticsBuilder::element_subtree_for_selector(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector, int p_depth) {
	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(p_snapshot, p_selector);
	if (selector_result.status == EditorAutomationSelectorStatus::OK && selector_result.match_indices.size() == 1) {
		const EditorAutomationElement &element = p_snapshot.get_element(selector_result.match_indices[0]);
		return element_subtree(p_snapshot, element.id, p_depth);
	}
	if (!p_snapshot.get_focused_element_id().is_empty()) {
		return element_subtree(p_snapshot, p_snapshot.get_focused_element_id(), p_depth);
	}
	return Dictionary();
}

EditorAutomationDiagnostics EditorAutomationDiagnosticsBuilder::build_for_action_failure(
		const String &p_kind,
		const String &p_message,
		const Dictionary &p_selector,
		const Array &p_candidates,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationLogMarker &p_log_marker,
		int p_trace_count) {
	EditorAutomationDiagnostics diagnostics;
	diagnostics.kind = p_kind;
	diagnostics.message = p_message;

	Dictionary details;
	if (!p_selector.is_empty()) {
		details["selector"] = p_selector;
	}
	if (!p_candidates.is_empty()) {
		details["candidates"] = p_candidates;
	}

	const String &focused_id = p_snapshot.get_focused_element_id();
	if (!focused_id.is_empty()) {
		const EditorAutomationElement *focused = p_snapshot.find_by_id(focused_id);
		if (focused != nullptr) {
			details["focused_element"] = _element_to_summary(*focused);
		} else {
			details["focused_element_id"] = focused_id;
		}
	}

	details["modal_stack"] = EditorAutomationState::capture_modal_stack();

	const Dictionary editor_state = EditorAutomationState::read_editor_state();
	details["editor_state"] = editor_state;
	if (editor_state.get("selected_nodes", Array()).get_type() == Variant::ARRAY) {
		details["selected_nodes"] = editor_state["selected_nodes"];
	}
	if (editor_state.has("open_scenes")) {
		details["open_scenes"] = editor_state["open_scenes"];
	}
	if (editor_state.has("active_scene_path")) {
		details["active_scene_path"] = editor_state["active_scene_path"];
	}
	if (editor_state.has("script")) {
		details["script"] = editor_state["script"];
	}

	details["log_entries"] = EditorAutomationLog::read_since(p_log_marker);
	details["action_trace"] = EditorAutomationTrace::get_singleton().get_recent(p_trace_count);
	details["ui_subtree"] = element_subtree_for_selector(p_snapshot, p_selector);

	diagnostics.details = details;
	return diagnostics;
}

EditorAutomationDiagnostics EditorAutomationDiagnosticsBuilder::build_for_wait_failure(
		const String &p_kind,
		const String &p_message,
		const Dictionary &p_condition,
		const Dictionary &p_action,
		const Dictionary &p_selector,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationLogMarker &p_log_marker,
		int p_trace_count) {
	EditorAutomationDiagnostics diagnostics;
	diagnostics.kind = p_kind;
	diagnostics.message = p_message;

	Dictionary details;
	if (!p_condition.is_empty()) {
		details["condition"] = p_condition;
	}
	if (!p_action.is_empty()) {
		details["action"] = p_action;
	}
	if (!p_selector.is_empty()) {
		details["selector"] = p_selector;
	}

	const String &focused_id = p_snapshot.get_focused_element_id();
	if (!focused_id.is_empty()) {
		const EditorAutomationElement *focused = p_snapshot.find_by_id(focused_id);
		if (focused != nullptr) {
			details["focused_element"] = _element_to_summary(*focused);
		} else {
			details["focused_element_id"] = focused_id;
		}
	}

	details["modal_stack"] = EditorAutomationState::capture_modal_stack();
	Dictionary marker_dict;
	marker_dict["message_index"] = p_log_marker.message_index;
	details["log_marker"] = marker_dict;
	details["log_entries"] = EditorAutomationLog::read_since(p_log_marker);
	details["action_trace"] = EditorAutomationTrace::get_singleton().get_recent(p_trace_count);
	details["ui_subtree"] = element_subtree_for_selector(p_snapshot, p_selector);

	diagnostics.details = details;
	return diagnostics;
}
