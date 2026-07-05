/**************************************************************************/
/*  test_editor_automation_wait.h                                         */
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

#pragma once

#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/automation/editor_automation_wait.h"

#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationWait {

// Mirrors EditorAutomationServer: polls cooperative waits from the scene
// process notification. Cooperative wait polling pumps frames through
// SceneTree::process(), so this re-enters the wait machinery exactly like the
// live editor does.
class WaitPollReentrancyProbe : public Node {
	FOUNDRY_CLASS(WaitPollReentrancyProbe, Node);

protected:
	void _notification(int p_what) {
		if (p_what != NOTIFICATION_PROCESS) {
			return;
		}
		nested_notifications++;
		const int polled = EditorAutomationWait::poll_all_cooperative();
		if (polled > max_nested_polled) {
			max_nested_polled = polled;
		}
		if (!wait_id.is_empty()) {
			EditorAutomationCooperativeWaitHandle nested_handle;
			if (EditorAutomationWait::poll_cooperative(wait_id, nested_handle)) {
				nested_poll_cooperative_succeeded = true;
				if (nested_handle.status != EditorAutomationCooperativeWaitStatus::PENDING) {
					nested_saw_non_pending = true;
				}
			}
		}
	}

public:
	String wait_id;
	int nested_notifications = 0;
	int max_nested_polled = 0;
	bool nested_poll_cooperative_succeeded = false;
	bool nested_saw_non_pending = false;
};

static void setup_visible_control(Control *p_control, const Size2 &p_size = Size2(120, 32)) {
	p_control->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	p_control->set_size(p_size);
	p_control->set_visible(true);
}

static void flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

TEST_CASE("[Editor][Automation] wait_for selector_appears succeeds when control becomes visible") {
	EditorAutomationTrace::get_singleton().clear();
	EditorAutomationLog::clear_test_messages();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Appear Later");
	button->set_visible(false);
	setup_visible_control(button);
	root->add_child(button);
	flush_frames();

	EditorAutomationWaitContext context;
	context.snapshot_root = root;

	Dictionary condition;
	condition["type"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Appear Later";
	selector["visible"] = true;
	condition["selector"] = selector;

	button->set_visible(true);
	flush_frames();

	const EditorAutomationWaitResult result = EditorAutomationWait::wait_for(condition, 1.0, context);
	CHECK(result.ok);
	CHECK(result.kind.is_empty());

	memdelete(root);
}

TEST_CASE("[Editor][Automation] wait_for selector_disappears succeeds when control is hidden") {
	EditorAutomationTrace::get_singleton().clear();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Hide Me");
	setup_visible_control(button);
	root->add_child(button);
	flush_frames();

	EditorAutomationWaitContext context;
	context.snapshot_root = root;

	Dictionary condition;
	condition["type"] = "selector_disappears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Hide Me";
	condition["selector"] = selector;

	button->set_visible(false);
	flush_frames();

	const EditorAutomationWaitResult result = EditorAutomationWait::wait_for(condition, 1.0, context);
	CHECK(result.ok);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] wait_for focus_matches succeeds after focus action") {
	EditorAutomationTrace::get_singleton().clear();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *first = memnew(LineEdit);
	first->set_name("FirstField");
	setup_visible_control(first);
	root->add_child(first);

	LineEdit *second = memnew(LineEdit);
	second->set_name("SecondField");
	setup_visible_control(second);
	root->add_child(second);
	first->grab_focus();
	flush_frames();

	EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "text_field";
	target["name"] = "SecondField";

	const EditorAutomationActionResult focus_result = EditorAutomationDriver::perform(snapshot, "focus", target);
	CHECK(focus_result.ok);
	flush_frames();

	EditorAutomationWaitContext context;
	context.snapshot_root = root;

	Dictionary condition;
	condition["type"] = "focus_matches";
	condition["selector"] = target;

	const EditorAutomationWaitResult result = EditorAutomationWait::wait_for(condition, 1.0, context);
	CHECK(result.ok);
	CHECK(second->has_focus());

	memdelete(root);
}

TEST_CASE("[Editor][Automation] wait_for timeout result shape") {
	EditorAutomationWaitContext context;

	Dictionary condition;
	condition["type"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Missing";
	condition["selector"] = selector;

	const EditorAutomationWaitResult result = EditorAutomationWait::wait_for(condition, 0.05, context);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "timeout");
	CHECK_FALSE(result.message.is_empty());
	CHECK(result.details.has("timeout_sec"));

	const Dictionary serialized = result.to_dictionary();
	CHECK_FALSE((bool)serialized["ok"]);
	CHECK(serialized["kind"] == "timeout");
}

TEST_CASE("[Editor][Automation] unsupported wait condition result shape") {
	Dictionary condition;
	condition["type"] = "script_analysis_idle";

	const EditorAutomationWaitResult result = EditorAutomationWait::wait_for(condition, 0.1);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "unsupported_condition");
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][Automation] action trace records success and failure") {
	EditorAutomationTrace::get_singleton().clear();
	EditorAutomationLog::clear_test_messages();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Trace");
	setup_visible_control(button);
	root->add_child(button);
	flush_frames();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Trace";

	const EditorAutomationActionResult success = EditorAutomationDriver::perform(snapshot, "click", target);
	CHECK(success.ok);

	const EditorAutomationActionResult failure = EditorAutomationDriver::perform(snapshot, "drag", target);
	CHECK_FALSE(failure.ok);

	const Array trace = EditorAutomationTrace::get_singleton().get_recent(4);
	CHECK(trace.size() >= 2);
	CHECK((bool)((Dictionary)trace[0])["ok"] == false);
	CHECK((bool)((Dictionary)trace[1])["ok"] == true);
	CHECK(((Dictionary)trace[1])["route"] == EditorAutomationActionRouteNames::SEMANTIC_CLICK);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] diagnostics include ambiguous selector candidates") {
	EditorAutomationTrace::get_singleton().clear();
	EditorAutomationLog::clear_test_messages();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *first = memnew(Button);
	first->set_text("Duplicate");
	setup_visible_control(first);
	root->add_child(first);

	Button *second = memnew(Button);
	second->set_text("Duplicate");
	setup_visible_control(second);
	root->add_child(second);
	flush_frames();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Duplicate";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "ambiguous_selector");
	CHECK(result.candidates.size() == 2);

	const EditorAutomationLogMarker marker = EditorAutomationLog::create_marker();
	const EditorAutomationDiagnostics diagnostics = EditorAutomationDiagnosticsBuilder::build_for_action_failure(
			result.kind,
			result.message,
			target,
			result.candidates,
			snapshot,
			marker);
	CHECK(diagnostics.kind == "ambiguous_selector");
	CHECK(diagnostics.details.has("candidates"));
	CHECK(((Array)diagnostics.details["candidates"]).size() == 2);
	CHECK(diagnostics.details.has("action_trace"));
	CHECK(diagnostics.details.has("modal_stack"));
	CHECK(diagnostics.details.has("selector"));
	CHECK(((Array)diagnostics.details["candidates"]).size() == 2);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] editor state readback degrades without full editor") {
	const Dictionary state = EditorAutomationState::read_editor_state();
	CHECK_FALSE((bool)state["supported"]);
	CHECK(((Array)state["open_scenes"]).is_empty());
	CHECK(((Array)state["selected_nodes"]).is_empty());
	CHECK(state.has("script"));
	CHECK(state.has("playing"));
	CHECK(state.has("unsaved"));
	CHECK(state.has("main_screen"));
	CHECK(((Array)state["modal_stack"]).is_empty());
}

TEST_CASE("[Editor][Automation] wait_for no_new_errors distinguishes new log entries") {
	EditorAutomationLog::clear_test_messages();

	EditorAutomationLogMarker marker = EditorAutomationLog::create_marker();

	Dictionary clean_condition;
	clean_condition["type"] = "no_new_errors";
	Dictionary marker_dict;
	marker_dict["message_index"] = marker.message_index;
	clean_condition["marker"] = marker_dict;

	const EditorAutomationWaitResult clean_result = EditorAutomationWait::wait_for(clean_condition, 0.1);
	CHECK(clean_result.ok);

	EditorAutomationLog::push_test_message("Something failed", "error");

	Dictionary dirty_condition;
	dirty_condition["type"] = "no_new_errors";
	dirty_condition["marker"] = marker_dict;

	const EditorAutomationWaitResult dirty_result = EditorAutomationWait::wait_for(dirty_condition, 0.05);
	CHECK_FALSE(dirty_result.ok);
	CHECK(dirty_result.kind == "timeout");

	EditorAutomationLog::clear_test_messages();
}

TEST_CASE("[Editor][Automation] cooperative wait polling survives process-notification re-entry") {
	// Regression test: pumping frames from a cooperative wait poll delivers
	// NOTIFICATION_PROCESS to EditorAutomationServer, which polls cooperative
	// waits again. This used to recurse into the same pending wait until the
	// editor crashed with a stack overflow (e.g. any `act` with a `wait`
	// clause over MCP).
	EditorAutomationWait::clear_all_cooperative();
	EditorAutomationTrace::get_singleton().clear();

	WaitPollReentrancyProbe *probe = memnew(WaitPollReentrancyProbe);
	SceneTree::get_singleton()->get_root()->add_child(probe);
	probe->set_process(true);

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	flush_frames();

	EditorAutomationWaitContext context;
	context.snapshot_root = root;

	Dictionary condition;
	condition["type"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Never Appears";
	condition["selector"] = selector;

	const String wait_id = EditorAutomationWait::begin_cooperative(condition, 0.05, context);
	probe->wait_id = wait_id;

	EditorAutomationCooperativeWaitHandle handle;
	int outer_polls = 0;
	do {
		REQUIRE(EditorAutomationWait::poll_cooperative(wait_id, handle));
		outer_polls++;
	} while (handle.status == EditorAutomationCooperativeWaitStatus::PENDING && outer_polls < 100);

	// The wait completes normally (timeout) instead of crashing.
	CHECK(handle.status == EditorAutomationCooperativeWaitStatus::COMPLETE);
	CHECK(handle.result.kind == "timeout");

	// Frame pumping really re-entered the wait machinery...
	CHECK(probe->nested_notifications > 0);
	CHECK(probe->nested_poll_cooperative_succeeded);
	// ...but nested polls never advanced or finalized the pending wait.
	CHECK(probe->max_nested_polled == 0);
	CHECK_FALSE(probe->nested_saw_non_pending);

	EditorAutomationWait::clear_all_cooperative();
	memdelete(root);
	memdelete(probe);
}

class CancelDuringPollProbe : public Node {
	FOUNDRY_CLASS(CancelDuringPollProbe, Node);

protected:
	void _notification(int p_what) {
		if (p_what != NOTIFICATION_PROCESS || cancel_attempted || wait_id.is_empty()) {
			return;
		}
		cancel_attempted = true;
		EditorAutomationCooperativeWaitHandle handle;
		cancel_succeeded = EditorAutomationWait::cancel_cooperative(wait_id, handle);
		cancel_status = handle.status;
	}

public:
	String wait_id;
	bool cancel_attempted = false;
	bool cancel_succeeded = false;
	EditorAutomationCooperativeWaitStatus cancel_status = EditorAutomationCooperativeWaitStatus::PENDING;
};

TEST_CASE("[Editor][Automation] cooperative cancel finalizes during frame-pump re-entry") {
	EditorAutomationWait::clear_all_cooperative();

	CancelDuringPollProbe *probe = memnew(CancelDuringPollProbe);
	SceneTree::get_singleton()->get_root()->add_child(probe);
	probe->set_process(true);

	Dictionary condition;
	condition["type"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Never Appears";
	condition["selector"] = selector;

	EditorAutomationWaitContext context;
	probe->wait_id = EditorAutomationWait::begin_cooperative(condition, 60.0, context);

	EditorAutomationCooperativeWaitHandle handle;
	REQUIRE(EditorAutomationWait::poll_cooperative(probe->wait_id, handle));
	CHECK(probe->cancel_attempted);
	CHECK(probe->cancel_succeeded);
	CHECK(probe->cancel_status == EditorAutomationCooperativeWaitStatus::CANCELLED);
	CHECK(handle.status == EditorAutomationCooperativeWaitStatus::CANCELLED);

	EditorAutomationWait::clear_all_cooperative();
	memdelete(probe);
}

} // namespace TestEditorAutomationWait
