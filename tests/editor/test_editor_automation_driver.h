/**************************************************************************/
/*  test_editor_automation_driver.h                                       */
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

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationDriver {

class PressTracker : public Object {
	FOUNDRY_CLASS(PressTracker, Object);

public:
	bool pressed = false;

	void on_pressed() {
		pressed = true;
	}
};

class KeyCaptureControl : public Control {
	FOUNDRY_CLASS(KeyCaptureControl, Control);

public:
	int key_press_count = 0;
	Key last_key = Key::NONE;

protected:
	void gui_input(const Ref<InputEvent> &p_event) override {
		Ref<InputEventKey> key_event = p_event;
		if (key_event.is_valid() && key_event->is_pressed() && !key_event->is_echo()) {
			key_press_count++;
			last_key = key_event->get_keycode();
		}
	}
};

static void setup_visible_control(Control *p_control, const Size2 &p_size = Size2(120, 32)) {
	p_control->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	p_control->set_size(p_size);
	p_control->set_visible(true);
}

static const EditorAutomationElement *find_element_by_role_and_name(const EditorAutomationSnapshot &p_snapshot, const String &p_role, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

TEST_CASE("[Editor][Automation] selector resolution followed by click on Button") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Run Test");
	setup_visible_control(button);
	root->add_child(button);

	PressTracker tracker;
	button->connect(SceneStringName(pressed), callable_mp(&tracker, &PressTracker::on_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Run Test";

	Dictionary options;
	options["route"] = "semantic";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_CLICK);
	CHECK(result.element_id == find_element_by_role_and_name(snapshot, "button", "Run Test")->id);
	CHECK(result.events.has("pressed"));
	CHECK(tracker.pressed);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] set_text on LineEdit") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *line_edit = memnew(LineEdit);
	line_edit->set_name("NameField");
	line_edit->set_text("Old");
	setup_visible_control(line_edit);
	root->add_child(line_edit);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *field = find_element_by_role_and_name(snapshot, "text_field", "NameField");
	REQUIRE(field != nullptr);

	Dictionary target;
	target["id"] = field->id;

	Dictionary options;
	options["text"] = "New Value";
	options["route"] = "semantic";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "set_text", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT);
	CHECK(line_edit->get_text() == "New Value");
	CHECK(result.events.has("text_changed"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] focus changes focus and reports focused element") {
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
	MessageQueue::get_singleton()->flush();

	EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *second_field = find_element_by_role_and_name(snapshot, "text_field", "SecondField");
	REQUIRE(second_field != nullptr);

	Dictionary target;
	target["id"] = second_field->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "focus", target);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_FOCUS);
	CHECK(second->has_focus());
	CHECK_FALSE(first->has_focus());
	CHECK(result.focus == second_field->id);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] stale element ID rejection") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Save");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *button_element = find_element_by_role_and_name(first_snapshot, "button", "Save");
	REQUIRE(button_element != nullptr);
	const String stale_id = button_element->id;

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["id"] = stale_id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(second_snapshot, "click", target);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "stale_element");
	CHECK_FALSE(result.message.is_empty());

	memdelete(root);
}

TEST_CASE("[Editor][Automation] ambiguous selector rejection before action dispatch") {
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
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Duplicate";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "ambiguous_selector");
	CHECK(result.candidates.size() == 2);
	CHECK_FALSE(first->is_pressed());
	CHECK_FALSE(second->is_pressed());

	memdelete(root);
}

TEST_CASE("[Editor][Automation] unsupported action error shape") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Action");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Action";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "drag", target);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "unsupported_action");
	CHECK_FALSE(result.message.is_empty());

	const Dictionary serialized = result.to_dictionary();
	CHECK_FALSE((bool)serialized["ok"]);
	CHECK(serialized.has("kind"));
	CHECK(serialized.has("message"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] click route preference semantic input and auto") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Route Test");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Route Test";

	Dictionary semantic_options;
	semantic_options["route"] = "semantic";
	const EditorAutomationActionResult semantic_result = EditorAutomationDriver::perform(snapshot, "click", target, semantic_options);
	CHECK(semantic_result.ok);
	CHECK(semantic_result.route == EditorAutomationActionRouteNames::SEMANTIC_CLICK);

	button->set_pressed(false);
	MessageQueue::get_singleton()->flush();

	Dictionary input_options;
	input_options["route"] = "input";
	const EditorAutomationActionResult input_result = EditorAutomationDriver::perform(snapshot, "click", target, input_options);
	CHECK(input_result.ok);
	CHECK(input_result.route == EditorAutomationActionRouteNames::INPUT_MOUSE_CLICK);
	CHECK(input_result.events.has("mouse_pressed"));

	Dictionary auto_options;
	auto_options["route"] = "auto";
	const EditorAutomationActionResult auto_result = EditorAutomationDriver::perform(snapshot, "click", target, auto_options);
	CHECK(auto_result.ok);
	CHECK(auto_result.route == EditorAutomationActionRouteNames::SEMANTIC_CLICK);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] type_text routes through input events") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *line_edit = memnew(LineEdit);
	line_edit->set_name("TypedField");
	setup_visible_control(line_edit);
	root->add_child(line_edit);
	line_edit->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *field = find_element_by_role_and_name(snapshot, "text_field", "TypedField");
	REQUIRE(field != nullptr);

	Dictionary target;
	target["id"] = field->id;

	Dictionary options;
	options["text"] = "ab";
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "type_text", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_TEXT);
	CHECK(result.events.size() >= 2);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] press_key routes through focused viewport") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	KeyCaptureControl *capture = memnew(KeyCaptureControl);
	capture->set_name("Capture");
	capture->set_focus_mode(Control::FOCUS_ALL);
	setup_visible_control(capture, Size2(200, 100));
	root->add_child(capture);
	capture->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["id"] = snapshot.get_focused_element_id();

	Dictionary options;
	options["key"] = "A";
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "press_key", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_KEY);
	CHECK(result.events.has("key_pressed"));
	MessageQueue::get_singleton()->flush();
	CHECK(capture->key_press_count >= 1);
	CHECK(capture->last_key == Key::A);

	memdelete(root);
}

} // namespace TestEditorAutomationDriver
