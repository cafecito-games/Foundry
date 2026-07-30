/**************************************************************************/
/*  test_editor_automation_driver.h                                       */
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

#pragma once

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_input.h"
#include "editor/automation/editor_automation_mcp_contracts.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "core/input/input_event.h"
#include "core/input/shortcut.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/viewport.h"

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

	// A real user typing emits text_changed; semantic set_text must do the same
	// so reactive UIs (incremental search, live filters) respond.
	SIGNAL_WATCH(line_edit, SNAME("text_changed"));

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "set_text", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT);
	CHECK(line_edit->get_text() == "New Value");
	CHECK(result.events.has("text_changed"));

	Array expected_args;
	Array call_args;
	call_args.push_back("New Value");
	expected_args.push_back(call_args);
	SIGNAL_CHECK(SNAME("text_changed"), expected_args);
	SIGNAL_UNWATCH(line_edit, SNAME("text_changed"));

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

TEST_CASE("[Editor][Automation] act reconciles stale snapshot id across generations") {
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

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["id"] = button_element->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(second_snapshot, "click", target);
	CHECK(result.ok);
	CHECK(result.kind.is_empty());
	CHECK((bool)result.details["reconciled"]);
	CHECK(String(result.details["current_element_id"]) == String(result.element_id));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] freed control action returns freed_element") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Delete Me");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *button_element = find_element_by_role_and_name(first_snapshot, "button", "Delete Me");
	REQUIRE(button_element != nullptr);

	root->remove_child(button);
	memdelete(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["handle"] = button_element->handle;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(second_snapshot, "click", target);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "freed_element");

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

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "unsupported_action_name", target);
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

class AutomationDragSource : public Control {
	FOUNDRY_CLASS(AutomationDragSource, Control);

public:
	Variant get_drag_data(const Point2 &p_point) override {
		return "automation_payload";
	}
};

class AutomationDropTarget : public Control {
	FOUNDRY_CLASS(AutomationDropTarget, Control);

public:
	bool dropped = false;
	Variant dropped_data;

	bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override {
		return p_data.get_type() == Variant::STRING;
	}

	void drop_data(const Point2 &p_point, const Variant &p_data) override {
		dropped = true;
		dropped_data = p_data;
	}
};

class AutomationViewportClickTracker : public Control {
	FOUNDRY_CLASS(AutomationViewportClickTracker, Control);

public:
	bool clicked = false;

protected:
	void gui_input(const Ref<InputEvent> &p_event) override {
		Ref<InputEventMouseButton> mouse_button = p_event;
		if (mouse_button.is_valid() && mouse_button->is_pressed() && mouse_button->get_button_index() == MouseButton::LEFT) {
			clicked = true;
		}
	}
};

TEST_CASE("[Editor][Automation] drag routes through input and performs drop") {
	Window *root = memnew(Window);
	root->set_title("Drag Root");
	root->set_size(Size2i(500, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	AutomationDragSource *source = memnew(AutomationDragSource);
	source->set_name("DragSource");
	setup_visible_control(source, Size2(120, 40));
	root->add_child(source);

	AutomationDropTarget *drop_target_control = memnew(AutomationDropTarget);
	drop_target_control->set_name("DropTarget");
	setup_visible_control(drop_target_control, Size2(120, 40));
	drop_target_control->set_position(Vector2(300, 0));
	root->add_child(drop_target_control);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *source_element = find_element_by_role_and_name(snapshot, "control", "DragSource");
	const EditorAutomationElement *target_element = find_element_by_role_and_name(snapshot, "control", "DropTarget");
	REQUIRE(source_element != nullptr);
	REQUIRE(target_element != nullptr);

	Dictionary source_target;
	source_target["id"] = source_element->id;

	Dictionary options;
	Dictionary drop_target;
	drop_target["id"] = target_element->id;
	options["target"] = drop_target;
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "drag", source_target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_DRAG);
	CHECK(result.events.has("mouse_pressed"));
	CHECK(result.events.has("mouse_motion"));
	CHECK(result.events.has("mouse_released"));
	CHECK(drop_target_control->dropped);
	CHECK(String(drop_target_control->dropped_data) == "automation_payload");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] press_key with modifiers triggers shortcut") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Shortcut Target");
	setup_visible_control(button);
	root->add_child(button);

	PressTracker tracker;
	button->connect(SceneStringName(pressed), callable_mp(&tracker, &PressTracker::on_pressed));

	Ref<Shortcut> shortcut;
	shortcut.instantiate();
	Ref<InputEventKey> shortcut_key;
	shortcut_key.instantiate();
	shortcut_key->set_keycode(Key::S);
	shortcut_key->set_ctrl_pressed(true);
	Array shortcut_events;
	shortcut_events.push_back(shortcut_key);
	shortcut->set_events(shortcut_events);
	button->set_shortcut(shortcut);
	button->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary target;
	target["id"] = snapshot.get_focused_element_id();

	Dictionary options;
	options["key"] = "S";
	Array modifiers;
	modifiers.push_back("ctrl");
	options["modifiers"] = modifiers;
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "press_key", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_KEY);
	CHECK(tracker.pressed);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] type_text edits LineEdit through input events") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *line_edit = memnew(LineEdit);
	line_edit->set_name("TypedField");
	line_edit->set_text("old");
	setup_visible_control(line_edit);
	root->add_child(line_edit);
	line_edit->select_all();
	line_edit->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *field = find_element_by_role_and_name(snapshot, "text_field", "TypedField");
	REQUIRE(field != nullptr);

	Dictionary target;
	target["id"] = field->id;

	Dictionary options;
	options["text"] = "new";
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "type_text", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_TEXT);
	CHECK(line_edit->get_text() == "new");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] type_text edits CodeEdit and TextEdit through input events") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	CodeEdit *code_edit = memnew(CodeEdit);
	code_edit->set_name("CodeField");
	code_edit->set_text("old");
	setup_visible_control(code_edit, Size2(300, 120));
	root->add_child(code_edit);

	TextEdit *text_edit = memnew(TextEdit);
	text_edit->set_name("TextArea");
	text_edit->set_text("line");
	setup_visible_control(text_edit, Size2(300, 120));
	text_edit->set_position(Vector2(0, 140));
	root->add_child(text_edit);
	MessageQueue::get_singleton()->flush();

	EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *code_field_initial = find_element_by_role_and_name(snapshot, "code_editor", "CodeField");
	REQUIRE(code_field_initial != nullptr);

	Dictionary code_target;
	code_target["id"] = code_field_initial->id;
	Dictionary code_options;
	code_options["text"] = "new\nnext";
	code_options["route"] = "input";
	const EditorAutomationActionResult code_result = EditorAutomationDriver::perform(snapshot, "type_text", code_target, code_options);
	MessageQueue::get_singleton()->flush();
	CHECK(code_result.ok);
	CHECK(code_edit->get_text().contains("new"));
	CHECK(code_edit->get_text().contains("next"));

	code_edit->set_text("replace_me");
	code_edit->select_all();
	code_edit->grab_focus();
	MessageQueue::get_singleton()->flush();
	snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *code_field = find_element_by_role_and_name(snapshot, "code_editor", "CodeField");
	REQUIRE(code_field != nullptr);
	code_target["id"] = code_field->id;
	Dictionary replace_options;
	replace_options["text"] = "done";
	replace_options["route"] = "input";
	const EditorAutomationActionResult replace_result = EditorAutomationDriver::perform(snapshot, "type_text", code_target, replace_options);
	MessageQueue::get_singleton()->flush();
	CHECK(replace_result.ok);
	CHECK(code_edit->get_text() == "done");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] type_text backspace on TextEdit through input actions") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	TextEdit *text_edit = memnew(TextEdit);
	text_edit->set_name("BackspaceArea");
	text_edit->set_text("line!");
	setup_visible_control(text_edit, Size2(300, 120));
	root->add_child(text_edit);
	text_edit->set_caret_line(0);
	text_edit->set_caret_column(text_edit->get_text().length());
	text_edit->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *text_field = find_element_by_role_and_name(snapshot, "text_area", "BackspaceArea");
	REQUIRE(text_field != nullptr);

	Dictionary target;
	target["id"] = text_field->id;

	Dictionary options;
	options["text"] = String::chr(8);
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "type_text", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_TEXT);
	CHECK(result.events.has("action_pressed:ui_text_backspace"));
	CHECK(text_edit->get_text() == "line");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] viewport click uses relative coordinates") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	SubViewportContainer *viewport_container = memnew(SubViewportContainer);
	viewport_container->set_name("EditorViewport");
	viewport_container->set_stretch(true);
	viewport_container->set_stretch_shrink(1);
	viewport_container->set_custom_minimum_size(Size2(240, 180));
	setup_visible_control(viewport_container, Size2(240, 180));
	root->add_child(viewport_container);

	SubViewport *sub_viewport = memnew(SubViewport);
	sub_viewport->set_size(Vector2i(240, 180));
	sub_viewport->set_disable_input(false);
	viewport_container->add_child(sub_viewport);

	AutomationViewportClickTracker *tracker = memnew(AutomationViewportClickTracker);
	tracker->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	sub_viewport->add_child(tracker);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *viewport_element = find_element_by_role_and_name(snapshot, "viewport", "EditorViewport");
	REQUIRE(viewport_element != nullptr);

	Dictionary target;
	target["id"] = viewport_element->id;

	Dictionary options;
	options["x"] = 0.5;
	options["y"] = 0.5;
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_VIEWPORT_CLICK);
	CHECK(tracker->clicked);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] type_text edits popup LineEdit through input events") {
	Window *root = memnew(Window);
	root->set_title("Main Window");
	root->set_size(Size2i(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	Window *popup = memnew(Window);
	popup->set_title("Popup Input");
	popup->set_size(Size2i(360, 120));
	LineEdit *line_edit = memnew(LineEdit);
	line_edit->set_accessibility_name("PopupField");
	setup_visible_control(line_edit, Size2(300, 32));
	line_edit->set_position(Vector2(20, 20));
	popup->add_child(line_edit);
	root->add_child(popup);
	popup->popup_exclusive_centered(root);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *field = find_element_by_role_and_name(snapshot, "text_field", "PopupField");
	REQUIRE(field != nullptr);

	Dictionary target;
	target["id"] = field->id;

	Dictionary options;
	options["text"] = "Node";
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "type_text", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_TEXT);
	CHECK(line_edit->get_text() == "Node");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] popup action focuses owning window before click") {
	Window *root = memnew(Window);
	root->set_title("Main Window");
	root->set_size(Size2i(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	AcceptDialog *dialog = memnew(AcceptDialog);
	dialog->set_title("Popup Test");
	dialog->set_ok_button_text("Confirm Popup");
	root->add_child(dialog);
	dialog->popup_centered();
	MessageQueue::get_singleton()->flush();

	root->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *popup_button = find_element_by_role_and_name(snapshot, "button", "Confirm Popup");
	REQUIRE(popup_button != nullptr);
	CHECK(popup_button->metadata.has("window_object_id"));
	CHECK(popup_button->metadata.has("window_title"));

	Dictionary target;
	target["id"] = popup_button->id;

	Dictionary options;
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_MOUSE_CLICK);
	CHECK(result.events.has("mouse_pressed"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] resolve_position_in_bounds honors position array") {
	const Rect2i bounds(100, 50, 200, 80);
	Dictionary options;
	Array position;
	position.push_back(12);
	position.push_back(18);
	options["position"] = position;
	const Vector2 resolved = EditorAutomationInput::resolve_position_in_bounds(bounds, options);
	CHECK(resolved == Vector2(112, 68));
}

TEST_CASE("[Editor][Automation] drag resolves independent source and target points") {
	EditorAutomationInput::set_mouse_trace_enabled(true);

	Window *root = memnew(Window);
	root->set_title("Drag Position Root");
	root->set_size(Size2i(500, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	AutomationDragSource *source = memnew(AutomationDragSource);
	source->set_name("DragSource");
	setup_visible_control(source, Size2(120, 40));
	root->add_child(source);

	AutomationDropTarget *drop_target_control = memnew(AutomationDropTarget);
	drop_target_control->set_name("DropTarget");
	setup_visible_control(drop_target_control, Size2(120, 40));
	drop_target_control->set_position(Vector2(300, 0));
	root->add_child(drop_target_control);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *source_element = find_element_by_role_and_name(snapshot, "control", "DragSource");
	const EditorAutomationElement *target_element = find_element_by_role_and_name(snapshot, "control", "DropTarget");
	REQUIRE(source_element != nullptr);
	REQUIRE(target_element != nullptr);

	Dictionary source_options;
	Array source_position;
	source_position.push_back(10);
	source_position.push_back(8);
	source_options["position"] = source_position;
	const Vector2 expected_source = EditorAutomationInput::resolve_position_in_bounds(
			source_element->bounds, EditorAutomationInput::position_options_for_source(source_options));

	Dictionary target_options;
	target_options["target_anchor"] = "top_right";
	const Vector2 expected_target = EditorAutomationInput::resolve_position_in_bounds(
			target_element->bounds, EditorAutomationInput::position_options_for_target_element(target_options));

	Dictionary source_target;
	source_target["id"] = source_element->id;

	Dictionary options;
	Dictionary drop_target;
	drop_target["id"] = target_element->id;
	options["target"] = drop_target;
	options["route"] = "input";
	options["position"] = source_position;
	options["target_anchor"] = "top_right";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "drag", source_target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.events.has("mouse_pressed"));
	CHECK(result.events.has("mouse_released"));
	CHECK(EditorAutomationInput::get_mouse_trace_press_global().is_equal_approx(expected_source));
	CHECK(EditorAutomationInput::get_mouse_trace_release_global().is_equal_approx(expected_target));
	CHECK_FALSE(EditorAutomationInput::get_mouse_trace_press_global().is_equal_approx(expected_target));

	EditorAutomationInput::set_mouse_trace_enabled(false);
	memdelete(root);
}

TEST_CASE("[Editor][Automation] drag with target_point presses at source and releases at target") {
	EditorAutomationInput::set_mouse_trace_enabled(true);

	Window *root = memnew(Window);
	root->set_title("Drag Target Point Root");
	root->set_size(Size2i(500, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	AutomationDragSource *source = memnew(AutomationDragSource);
	source->set_name("DragSource");
	setup_visible_control(source, Size2(120, 40));
	root->add_child(source);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *source_element = find_element_by_role_and_name(snapshot, "control", "DragSource");
	REQUIRE(source_element != nullptr);

	const Vector2 expected_source = EditorAutomationInput::global_center_of_bounds(source_element->bounds);
	Array target_point;
	target_point.push_back(420);
	target_point.push_back(180);
	Dictionary target_point_options;
	target_point_options["target_point"] = target_point;
	const Vector2 expected_target = EditorAutomationInput::resolve_target_point(target_point_options);

	Dictionary source_target;
	source_target["id"] = source_element->id;

	Dictionary options;
	options["route"] = "input";
	options["target_point"] = target_point;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "drag", source_target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(EditorAutomationInput::get_mouse_trace_press_global().is_equal_approx(expected_source));
	CHECK(EditorAutomationInput::get_mouse_trace_release_global().is_equal_approx(expected_target));
	CHECK_FALSE(EditorAutomationInput::get_mouse_trace_press_global().is_equal_approx(expected_target));

	EditorAutomationInput::set_mouse_trace_enabled(false);
	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] act args schema declares honored positioning fields") {
	const Ref<EditorAutomationMCPJsonSchema> schema = EditorAutomationMCPActionArgs::schema();
	const Dictionary properties = schema->to_dictionary()["properties"];
	CHECK(properties.has("position"));
	CHECK(properties.has("anchor"));
	CHECK(properties.has("x"));
	CHECK(properties.has("y"));
	CHECK(properties.has("source"));
	CHECK(properties.has("target_position"));
	CHECK(properties.has("target_anchor"));
	CHECK(properties.has("target_x"));
	CHECK(properties.has("target_y"));
	CHECK(properties.has("target_point"));

	const Dictionary position_schema = properties["position"];
	CHECK_FALSE(String(position_schema.get("description", String())).is_empty());
}

TEST_CASE("[Editor][Automation] drag failure includes source and target diagnostics") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	AutomationDragSource *source = memnew(AutomationDragSource);
	source->set_name("DragSource");
	setup_visible_control(source);
	root->add_child(source);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *source_element = find_element_by_role_and_name(snapshot, "control", "DragSource");
	REQUIRE(source_element != nullptr);

	Dictionary target;
	target["id"] = source_element->id;

	Dictionary options;
	options["route"] = "input";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "drag", target, options);
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "invalid_parameter");
	CHECK(result.details.has("source_element"));
	CHECK(result.details.has("modal_stack"));

	memdelete(root);
}

class TreeActivateTracker : public Object {
	FOUNDRY_CLASS(TreeActivateTracker, Object);

public:
	bool activated = false;

	void on_activated() {
		activated = true;
	}
};

class ItemListActivateTracker : public Object {
	FOUNDRY_CLASS(ItemListActivateTracker, Object);

public:
	int activated_index = -1;

	void on_activated(int p_index) {
		activated_index = p_index;
	}
};

class LineEditSubmitTracker : public Object {
	FOUNDRY_CLASS(LineEditSubmitTracker, Object);

public:
	String submitted_text;

	void on_submitted(const String &p_text) {
		submitted_text = p_text;
	}
};

static const EditorAutomationElement *find_virtual_element(const EditorAutomationSnapshot &p_snapshot, const String &p_role, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

TEST_CASE("[Editor][Automation] tree item select expand collapse and activate") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Tree *tree = memnew(Tree);
	tree->set_name("TestTree");
	setup_visible_control(tree, Size2(200, 200));
	root->add_child(tree);

	TreeItem *root_item = tree->create_item();
	TreeItem *parent_item = tree->create_item(root_item);
	parent_item->set_text(0, "Parent");
	TreeItem *child_item = tree->create_item(parent_item);
	child_item->set_text(0, "Child");
	parent_item->set_collapsed(true);

	TreeActivateTracker tracker;
	tree->connect("item_activated", callable_mp(&tracker, &TreeActivateTracker::on_activated));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *parent = find_virtual_element(snapshot, "tree_item", "Parent");
	const EditorAutomationElement *child = find_virtual_element(snapshot, "tree_item", "Child");
	REQUIRE(parent != nullptr);
	REQUIRE(child != nullptr);
	CHECK(parent->actions.has("select"));
	CHECK(parent->actions.has("activate"));
	CHECK(parent->actions.has("expand"));
	CHECK(String(parent->metadata.get("label", String())) == "Parent");
	CHECK((bool)parent->metadata.get("expanded", true) == false);
	CHECK((bool)parent->metadata.get("has_children", false) == true);

	Dictionary parent_target;
	parent_target["id"] = parent->id;

	const EditorAutomationActionResult expand_result = EditorAutomationDriver::perform(snapshot, "expand", parent_target, Dictionary());
	CHECK(expand_result.ok);
	CHECK(expand_result.route == EditorAutomationActionRouteNames::SEMANTIC_SELECT);
	CHECK(expand_result.events.has("expanded"));
	CHECK(!parent_item->is_collapsed());

	const EditorAutomationActionResult collapse_result = EditorAutomationDriver::perform(snapshot, "collapse", parent_target, Dictionary());
	CHECK(collapse_result.ok);
	CHECK(collapse_result.events.has("collapsed"));
	CHECK(parent_item->is_collapsed());

	const EditorAutomationActionResult expand_again = EditorAutomationDriver::perform(snapshot, "expand", parent_target, Dictionary());
	CHECK(expand_again.ok);
	CHECK(!parent_item->is_collapsed());

	Dictionary child_target;
	child_target["id"] = child->id;
	const EditorAutomationActionResult select_result = EditorAutomationDriver::perform(snapshot, "select", child_target, Dictionary());
	CHECK(select_result.ok);
	CHECK(child_item->is_selected(0));

	const EditorAutomationActionResult activate_result = EditorAutomationDriver::perform(snapshot, "activate", child_target, Dictionary());
	CHECK(activate_result.ok);
	CHECK(activate_result.route == EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE);
	CHECK(activate_result.events.has("activated"));
	CHECK(tracker.activated);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] item list select and activate") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	ItemList *item_list = memnew(ItemList);
	item_list->set_name("TestList");
	setup_visible_control(item_list, Size2(200, 200));
	root->add_child(item_list);
	item_list->add_item("Alpha");
	item_list->add_item("Beta");

	ItemListActivateTracker tracker;
	item_list->connect("item_activated", callable_mp(&tracker, &ItemListActivateTracker::on_activated));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *beta_item = find_virtual_element(snapshot, "list_item", "Beta");
	REQUIRE(beta_item != nullptr);
	CHECK(beta_item->actions.has("activate"));
	CHECK((int)beta_item->metadata.get("index", -1) == 1);
	CHECK(String(beta_item->metadata.get("label", String())) == "Beta");

	Dictionary target;
	target["id"] = beta_item->id;

	const EditorAutomationActionResult select_result = EditorAutomationDriver::perform(snapshot, "select", target, Dictionary());
	CHECK(select_result.ok);
	CHECK(item_list->is_selected(1));

	const EditorAutomationActionResult activate_result = EditorAutomationDriver::perform(snapshot, "activate", target, Dictionary());
	CHECK(activate_result.ok);
	CHECK(activate_result.route == EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE);
	CHECK(activate_result.events.has("activated"));
	CHECK(tracker.activated_index == 1);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] submit on LineEdit emits text_submitted") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *line_edit = memnew(LineEdit);
	line_edit->set_name("Search");
	line_edit->set_text("Node2D");
	setup_visible_control(line_edit);
	root->add_child(line_edit);

	LineEditSubmitTracker tracker;
	line_edit->connect(SceneStringName(text_submitted), callable_mp(&tracker, &LineEditSubmitTracker::on_submitted));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *field = find_element_by_role_and_name(snapshot, "text_field", "Search");
	REQUIRE(field != nullptr);
	CHECK(field->actions.has("submit"));

	Dictionary target;
	target["id"] = field->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "submit", target, Dictionary());
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_SUBMIT);
	CHECK(result.events.has("text_submitted"));
	CHECK(String(result.details.get("submitted_text", String())) == "Node2D");
	CHECK(tracker.submitted_text == "Node2D");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] scroll action reports whether scrolling occurred") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	ScrollContainer *scroll_container = memnew(ScrollContainer);
	scroll_container->set_name("ScrollArea");
	setup_visible_control(scroll_container, Size2(120, 80));
	root->add_child(scroll_container);

	VBoxContainer *content = memnew(VBoxContainer);
	for (int i = 0; i < 20; i++) {
		Label *label = memnew(Label);
		label->set_text(vformat("Row %d", i));
		content->add_child(label);
	}
	scroll_container->add_child(content);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *scroll_element = find_element_by_role_and_name(snapshot, "control", "ScrollArea");
	REQUIRE(scroll_element != nullptr);
	CHECK(scroll_element->actions.has("scroll"));

	Dictionary target;
	target["id"] = scroll_element->id;

	Dictionary options;
	options["direction"] = "down";

	const int before = scroll_container->get_v_scroll();
	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "scroll", target, options);
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_SCROLL);
	CHECK((bool)result.details.get("scrolled", false));
	CHECK(result.events.has("scrolled"));
	CHECK(scroll_container->get_v_scroll() > before);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] selector matches tree items by metadata label") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Tree *tree = memnew(Tree);
	setup_visible_control(tree, Size2(200, 200));
	root->add_child(tree);

	TreeItem *root_item = tree->create_item();
	TreeItem *child_item = tree->create_item(root_item);
	child_item->set_text(0, "ChildNode");
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary selector;
	selector["role"] = "tree_item";
	Dictionary metadata;
	metadata["label"] = "ChildNode";
	selector["metadata"] = metadata;

	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(selector_result.status == EditorAutomationSelectorStatus::OK);
	CHECK(selector_result.match_indices.size() == 1);

	memdelete(root);
}

class MenuItemPressTracker : public Object {
	FOUNDRY_CLASS(MenuItemPressTracker, Object);

public:
	int last_id = -1;

	void on_id_pressed(int p_id) {
		last_id = p_id;
	}
};

// #1090: a MenuButton's attached PopupMenu items must be reachable through the
// snapshot even while the popup is hidden, so choose_menu_item can activate them
// without first having to synthesize a popup open.
TEST_CASE("[Editor][Automation] menu button popup items are selectable while hidden") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Go To");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Look Up Symbol", 10);
	popup->add_item("Go to Line", 11);

	MenuItemPressTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &MenuItemPressTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	// The popup is never opened; its items must still be present in the snapshot.
	CHECK_FALSE(popup->is_visible());

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *look_up = find_virtual_element(snapshot, "menu_item", "Look Up Symbol");
	REQUIRE(look_up != nullptr);
	CHECK(look_up->actions.has("choose_menu_item"));
	CHECK(look_up->actions.has("activate"));

	Dictionary target;
	target["id"] = look_up->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_SELECT);
	CHECK(tracker.last_id == 10);

	memdelete(root);
}

static int count_role(const EditorAutomationSnapshot &p_snapshot, const String &p_role) {
	int count = 0;
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		if (p_snapshot.get_element(i).role == p_role) {
			count++;
		}
	}
	return count;
}

static int count_role_with_bounds(const EditorAutomationSnapshot &p_snapshot, const String &p_role) {
	int count = 0;
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.bounds.size.y > 0) {
			count++;
		}
	}
	return count;
}

// #1090 follow-up: activating a hidden MenuButton item opens the menu as a user
// would, so the MenuButton must not be left stuck pressed / processing after the
// menu closes -- neither on a successful activation nor on a refusal.
TEST_CASE("[Editor][Automation] hidden menu activation leaves the button unpressed") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("File");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Save", 1);
	popup->add_item("Blocked", 2);
	popup->set_item_disabled(1, true);

	MenuItemPressTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &MenuItemPressTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	const EditorAutomationElement *save_item = find_virtual_element(snapshot, "menu_item", "Save");
	REQUIRE(save_item != nullptr);
	Dictionary save_target;
	save_target["id"] = save_item->id;
	const EditorAutomationActionResult save_result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", save_target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK(save_result.ok);
	CHECK(tracker.last_id == 1);
	// The menu closed, so the button is not stuck pressed.
	CHECK_FALSE(menu_button->is_pressed());
	CHECK_FALSE(popup->is_visible());

	const EditorAutomationElement *blocked_item = find_virtual_element(snapshot, "menu_item", "Blocked");
	REQUIRE(blocked_item != nullptr);
	Dictionary blocked_target;
	blocked_target["id"] = blocked_item->id;
	const EditorAutomationActionResult blocked_result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", blocked_target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK_FALSE(blocked_result.ok);
	CHECK(blocked_result.kind == "element_disabled");
	// Even a refused activation must restore the button state.
	CHECK_FALSE(menu_button->is_pressed());
	CHECK_FALSE(popup->is_visible());

	memdelete(root);
}

// #1090 follow-up: a submenu row opens a child menu rather than emitting an id.
// It is exposed for visibility but not as selectable, and activating it is
// refused instead of firing the parent's id_pressed path.
TEST_CASE("[Editor][Automation] menu button submenu rows are not directly selectable") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("File");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Save", 1);
	PopupMenu *submenu = memnew(PopupMenu);
	submenu->set_name("RecentSubmenu");
	submenu->add_item("Recent File", 100);
	popup->add_submenu_node_item("Open Recent", submenu, 2);

	MenuItemPressTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &MenuItemPressTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *submenu_item = find_virtual_element(snapshot, "menu_item", "Open Recent");
	REQUIRE(submenu_item != nullptr);
	CHECK((bool)submenu_item->metadata.get("has_submenu", false));
	// A submenu row advertises no direct-selection actions.
	CHECK_FALSE(submenu_item->actions.has("choose_menu_item"));
	CHECK_FALSE(submenu_item->actions.has("activate"));

	Dictionary target;
	target["id"] = submenu_item->id;
	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "unsupported_action");
	// The parent's id_pressed path must not fire.
	CHECK(tracker.last_id == -1);
	CHECK_FALSE(menu_button->is_pressed());

	memdelete(root);
}

// #1089: snapshotting a Tree whose row list is enormous (the Search Help dialog
// holds the whole class database) must stay bounded. Bounds are derived from a
// single anchor row plus O(1) per-row heights, so every on-screen row is emitted
// with correct, monotonically increasing bounds without the O(n^2) hang.
TEST_CASE("[Editor][Automation] tree rows all emitted with correct incremental bounds") {
	// A plain Control root does not resize its children, so the tree keeps the
	// explicit height below and every row lays out on-screen with real bounds.
	Control *root = memnew(Control);
	root->set_size(Size2(400, 4100));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Tree *tree = memnew(Tree);
	tree->set_name("HugeTree");
	setup_visible_control(tree, Size2(200, 4000));
	root->add_child(tree);

	TreeItem *tree_root = tree->create_item();
	const int total_rows = 120;
	for (int i = 0; i < total_rows; i++) {
		TreeItem *item = tree->create_item(tree_root);
		item->set_text(0, vformat("Row %d", i));
	}
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	// Every row is present and, because the tall viewport fits them all, every row
	// carries on-screen bounds derived from the incremental offset walk.
	CHECK(count_role(snapshot, "tree_item") == total_rows);
	CHECK(count_role_with_bounds(snapshot, "tree_item") == total_rows);

	// Bounds increase monotonically down the tree, matching the incremental offset
	// accumulation (the fix must not collapse rows onto one origin).
	const EditorAutomationElement *first_row = find_virtual_element(snapshot, "tree_item", "Row 0");
	const EditorAutomationElement *mid_row = find_virtual_element(snapshot, "tree_item", "Row 60");
	const EditorAutomationElement *last_row = find_virtual_element(snapshot, "tree_item", "Row 119");
	REQUIRE(first_row != nullptr);
	REQUIRE(mid_row != nullptr);
	REQUIRE(last_row != nullptr);
	CHECK(mid_row->bounds.position.y > first_row->bounds.position.y);
	CHECK(last_row->bounds.position.y > mid_row->bounds.position.y);
	// Row spacing tracks the row height rather than collapsing.
	CHECK(mid_row->bounds.position.y - first_row->bounds.position.y >= first_row->bounds.size.y * 2);

	memdelete(root);
}

// #1089 follow-up: rows inside a collapsed branch are still visited by
// get_next_in_tree and pass is_visible_in_tree(), but they are not drawn. They
// must not advance the running row offset (which would push later siblings down)
// nor claim on-screen bounds.
TEST_CASE("[Editor][Automation] collapsed branch does not shift sibling bounds") {
	Control *root = memnew(Control);
	root->set_size(Size2(400, 600));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Tree *tree = memnew(Tree);
	tree->set_name("CollapseTree");
	setup_visible_control(tree, Size2(200, 500));
	root->add_child(tree);

	TreeItem *tree_root = tree->create_item();
	TreeItem *branch = tree->create_item(tree_root);
	branch->set_text(0, "Branch");
	TreeItem *hidden_child = tree->create_item(branch);
	hidden_child->set_text(0, "Hidden Child");
	TreeItem *hidden_child2 = tree->create_item(branch);
	hidden_child2->set_text(0, "Hidden Child 2");
	branch->set_collapsed(true);
	TreeItem *sibling = tree->create_item(tree_root);
	sibling->set_text(0, "Sibling");
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	const EditorAutomationElement *branch_element = find_virtual_element(snapshot, "tree_item", "Branch");
	const EditorAutomationElement *sibling_element = find_virtual_element(snapshot, "tree_item", "Sibling");
	const EditorAutomationElement *hidden_element = find_virtual_element(snapshot, "tree_item", "Hidden Child");
	REQUIRE(branch_element != nullptr);
	REQUIRE(sibling_element != nullptr);
	// Hidden rows stay present (selectable) even though collapsed.
	REQUIRE(hidden_element != nullptr);

	// Drawn rows carry bounds; the hidden collapsed child does not.
	CHECK(branch_element->bounds.size.y > 0);
	CHECK(sibling_element->bounds.size.y > 0);
	CHECK(hidden_element->bounds.size.y == 0);

	// The sibling sits one row below the branch, not three: the two hidden
	// descendants must not have pushed it down.
	CHECK(sibling_element->bounds.position.y > branch_element->bounds.position.y);
	CHECK(sibling_element->bounds.position.y - branch_element->bounds.position.y <= branch_element->bounds.size.y + 1);

	memdelete(root);
}

class DisabledMenuTracker : public Object {
	FOUNDRY_CLASS(DisabledMenuTracker, Object);

public:
	int fired_count = 0;

	void on_id_pressed(int p_id) {
		fired_count++;
	}
};

// #1090 follow-up: a disabled MenuButton command is exposed for visibility but
// must not be activatable, since a user could not select it either.
TEST_CASE("[Editor][Automation] disabled menu button items are not activatable") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Debug");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Enabled Command", 1);
	popup->add_item("Disabled Command", 2);
	popup->set_item_disabled(1, true);

	DisabledMenuTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &DisabledMenuTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *disabled_item = find_virtual_element(snapshot, "menu_item", "Disabled Command");
	REQUIRE(disabled_item != nullptr);
	// The disabled command is visible but reported as not enabled.
	CHECK_FALSE(disabled_item->enabled);

	Dictionary target;
	target["id"] = disabled_item->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "element_disabled");
	CHECK(tracker.fired_count == 0);

	memdelete(root);
}

class AboutToPopupDisabler : public Object {
	FOUNDRY_CLASS(AboutToPopupDisabler, Object);

public:
	PopupMenu *popup = nullptr;
	int disable_index = -1;

	void on_about_to_popup() {
		if (popup != nullptr && disable_index >= 0) {
			popup->set_item_disabled(disable_index, true);
		}
	}
};

// #1090 follow-up: editor menus commonly update/disable entries in the
// MenuButton about_to_popup signal. Exposing hidden popup items must still honor
// that: activating an item fires about_to_popup first, so a command the live
// menu would disable cannot be triggered through the stale snapshot.
TEST_CASE("[Editor][Automation] menu button activation refreshes via about_to_popup") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Edit");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Refreshed Command", 7);

	AboutToPopupDisabler disabler;
	disabler.popup = popup;
	disabler.disable_index = 0;
	menu_button->connect("about_to_popup", callable_mp(&disabler, &AboutToPopupDisabler::on_about_to_popup));

	DisabledMenuTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &DisabledMenuTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *item = find_virtual_element(snapshot, "menu_item", "Refreshed Command");
	REQUIRE(item != nullptr);
	// The snapshot captured the item before about_to_popup ran, so it still looks
	// enabled...
	CHECK(item->enabled);

	Dictionary target;
	target["id"] = item->id;

	// ...but activation fires about_to_popup, which disables it, so the command is
	// refused rather than triggered.
	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "element_disabled");
	CHECK(tracker.fired_count == 0);

	memdelete(root);
}

// #1090 follow-up: some editor menus connect their rebuild/disable handlers to
// the PopupMenu's own about_to_popup rather than the MenuButton's. Activation
// must fire both, so a command the popup-signal handler disables is refused.
TEST_CASE("[Editor][Automation] menu button activation honors popup about_to_popup") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Search");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Popup Signal Command", 3);

	AboutToPopupDisabler disabler;
	disabler.popup = popup;
	disabler.disable_index = 0;
	// Connect to the PopupMenu's signal, not the MenuButton's.
	popup->connect("about_to_popup", callable_mp(&disabler, &AboutToPopupDisabler::on_about_to_popup));

	DisabledMenuTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &DisabledMenuTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *item = find_virtual_element(snapshot, "menu_item", "Popup Signal Command");
	REQUIRE(item != nullptr);

	Dictionary target;
	target["id"] = item->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "element_disabled");
	CHECK(tracker.fired_count == 0);

	memdelete(root);
}

class AboutToPopupRenamer : public Object {
	FOUNDRY_CLASS(AboutToPopupRenamer, Object);

public:
	PopupMenu *popup = nullptr;
	int index = -1;
	String new_text;

	void on_about_to_popup() {
		if (popup != nullptr && index >= 0) {
			popup->set_item_text(index, new_text);
		}
	}
};

// #1090 follow-up: a menu rebuilt on about_to_popup can reorder/rename entries,
// so the snapshot's numeric index may no longer map to the requested command.
// Activation detects the mismatch and refuses rather than firing the wrong one.
TEST_CASE("[Editor][Automation] menu button activation refuses a stale index after rebuild") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Go To");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Original Command", 4);

	AboutToPopupRenamer renamer;
	renamer.popup = popup;
	renamer.index = 0;
	renamer.new_text = "Different Command";
	menu_button->connect("about_to_popup", callable_mp(&renamer, &AboutToPopupRenamer::on_about_to_popup));

	DisabledMenuTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &DisabledMenuTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *item = find_virtual_element(snapshot, "menu_item", "Original Command");
	REQUIRE(item != nullptr);

	Dictionary target;
	target["id"] = item->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "stale_element");
	CHECK(tracker.fired_count == 0);

	memdelete(root);
}

// #1090 follow-up: a disabled MenuButton cannot be opened, so its commands are
// exposed as not enabled and cannot be activated.
TEST_CASE("[Editor][Automation] disabled menu button exposes inert commands") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Debug");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Command In Disabled Menu", 9);
	menu_button->set_disabled(true);

	DisabledMenuTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &DisabledMenuTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *item = find_virtual_element(snapshot, "menu_item", "Command In Disabled Menu");
	REQUIRE(item != nullptr);
	CHECK_FALSE(item->enabled);

	Dictionary target;
	target["id"] = item->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "choose_menu_item", target, Dictionary());
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "element_disabled");
	CHECK(tracker.fired_count == 0);

	memdelete(root);
}

class RightClickCaptureControl : public Control {
	FOUNDRY_CLASS(RightClickCaptureControl, Control);

public:
	int right_click_count = 0;

protected:
	void gui_input(const Ref<InputEvent> &p_event) override {
		Ref<InputEventMouseButton> mouse_button = p_event;
		if (mouse_button.is_valid() && mouse_button->is_pressed() && mouse_button->get_button_index() == MouseButton::RIGHT) {
			right_click_count++;
		}
	}
};

// #1091: open_context_menu synthesizes a right-click so a control's context menu
// (e.g. the Inspector class-category "Open Documentation") can be reached.
TEST_CASE("[Editor][Automation] open_context_menu synthesizes a right click on the element") {
	Window *root = memnew(Window);
	root->set_title("Context Menu Root");
	root->set_size(Size2i(500, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	root->set_visible(true);
	MessageQueue::get_singleton()->flush();

	RightClickCaptureControl *target_control = memnew(RightClickCaptureControl);
	target_control->set_name("CategoryRow");
	target_control->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	setup_visible_control(target_control, Size2(200, 40));
	root->add_child(target_control);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *element = find_element_by_role_and_name(snapshot, "control", "CategoryRow");
	REQUIRE(element != nullptr);

	Dictionary target;
	target["id"] = element->id;

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "open_context_menu", target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::INPUT_CONTEXT_MENU);
	CHECK(result.events.has("mouse_pressed"));
	CHECK(result.events.has("mouse_released"));
	CHECK(target_control->right_click_count >= 1);

	// right_click is an accepted alias for the same behavior.
	const EditorAutomationActionResult alias_result = EditorAutomationDriver::perform(snapshot, "right_click", target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK(alias_result.ok);
	CHECK(target_control->right_click_count >= 2);

	memdelete(root);
}

// A MenuButton whose entries are built lazily in about_to_popup: its PopupMenu is
// empty until the menu is opened. This mirrors editor menus such as SceneTreeDock's
// options button, which populate themselves only when the popup is about to show.
class LazyMenuPopulator : public Object {
	FOUNDRY_CLASS(LazyMenuPopulator, Object);

public:
	PopupMenu *popup = nullptr;
	int build_count = 0;

	void on_about_to_popup() {
		if (popup == nullptr) {
			return;
		}
		popup->clear();
		popup->add_item("Lazy One", 101);
		popup->add_item("Lazy Two", 102);
		build_count++;
	}
};

// #1095: a MenuButton whose menu is populated only in about_to_popup is empty
// while hidden, so the snapshot exposes no menu_item targets. A semantic click
// must open the menu the way a user would (show_popup fires about_to_popup and
// populates the entries) and leave it open so a follow-up observe_ui captures the
// freshly-built items as normal menu_item elements that choose_menu_item can drive.
TEST_CASE("[Editor][Automation] clicking a MenuButton opens and captures its lazily-built menu") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Options");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	LazyMenuPopulator populator;
	populator.popup = popup;
	menu_button->connect("about_to_popup", callable_mp(&populator, &LazyMenuPopulator::on_about_to_popup));

	MenuItemPressTracker tracker;
	popup->connect("id_pressed", callable_mp(&tracker, &MenuItemPressTracker::on_id_pressed));
	MessageQueue::get_singleton()->flush();

	// While the menu was never opened, its lazy entries do not exist yet, so the
	// snapshot cannot expose any menu_item for it.
	CHECK(popup->get_item_count() == 0);
	const EditorAutomationSnapshot before = EditorAutomationSnapshot::capture_from_node(root);
	CHECK(find_virtual_element(before, "menu_item", "Lazy One") == nullptr);

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Options";
	Dictionary options;
	options["route"] = "semantic";

	const EditorAutomationActionResult click = EditorAutomationDriver::perform(before, "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(click.ok);
	CHECK(click.route == EditorAutomationActionRouteNames::SEMANTIC_CLICK);
	// The click opened the menu, which populated it via about_to_popup and stays open.
	CHECK(populator.build_count == 1);
	CHECK(popup->is_visible());
	CHECK(popup->get_item_count() == 2);

	// Re-observing now captures the open popup's freshly-built items as menu_item
	// elements that carry the choose_menu_item action.
	const EditorAutomationSnapshot after = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *lazy_one = find_virtual_element(after, "menu_item", "Lazy One");
	REQUIRE(lazy_one != nullptr);
	CHECK(lazy_one->actions.has("choose_menu_item"));
	CHECK(find_virtual_element(after, "menu_item", "Lazy Two") != nullptr);

	// And choose_menu_item on the captured item fires its command.
	Dictionary choose_target;
	choose_target["id"] = lazy_one->id;
	const EditorAutomationActionResult choose = EditorAutomationDriver::perform(after, "choose_menu_item", choose_target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK(choose.ok);
	CHECK(tracker.last_id == 101);

	memdelete(root);
}

// #1095: activate on a MenuButton follows the same open-the-menu path as click, so
// menu-only buttons are driveable through either semantic verb.
TEST_CASE("[Editor][Automation] activating a MenuButton opens its lazily-built menu") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Options");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	LazyMenuPopulator populator;
	populator.popup = popup;
	menu_button->connect("about_to_popup", callable_mp(&populator, &LazyMenuPopulator::on_about_to_popup));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	Dictionary target;
	target["role"] = "button";
	target["name"] = "Options";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "activate", target, Dictionary());
	MessageQueue::get_singleton()->flush();
	CHECK(result.ok);
	CHECK(result.route == EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE);
	CHECK(populator.build_count == 1);
	CHECK(popup->is_visible());
	CHECK(popup->get_item_count() == 2);

	popup->hide();
	memdelete(root);
}

// #1095: a disabled MenuButton cannot be opened by a user, so a semantic click is
// refused and its menu is neither populated nor shown.
TEST_CASE("[Editor][Automation] clicking a disabled MenuButton is refused") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Options");
	menu_button->set_disabled(true);
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	LazyMenuPopulator populator;
	populator.popup = popup;
	menu_button->connect("about_to_popup", callable_mp(&populator, &LazyMenuPopulator::on_about_to_popup));
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);
	Dictionary target;
	target["role"] = "button";
	target["name"] = "Options";
	Dictionary options;
	options["route"] = "semantic";

	const EditorAutomationActionResult result = EditorAutomationDriver::perform(snapshot, "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK_FALSE(result.ok);
	CHECK(result.kind == "element_disabled");
	CHECK(populator.build_count == 0);
	CHECK_FALSE(popup->is_visible());

	memdelete(root);
}

// #1095: a semantic MenuButton click reproduces a real press -- it toggles the
// menu (opens when hidden, closes when already open) and still fires the inherited
// pressed signal, rather than only force-opening the popup.
TEST_CASE("[Editor][Automation] semantic MenuButton click toggles the menu and fires pressed") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Options");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Alpha", 1);

	PressTracker press_tracker;
	menu_button->connect(SceneStringName(pressed), callable_mp(&press_tracker, &PressTracker::on_pressed));
	MessageQueue::get_singleton()->flush();

	Dictionary target;
	target["role"] = "button";
	target["name"] = "Options";
	Dictionary options;
	options["route"] = "semantic";

	// The first click opens the menu and fires the inherited pressed signal.
	const EditorAutomationActionResult open = EditorAutomationDriver::perform(EditorAutomationSnapshot::capture_from_node(root), "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(open.ok);
	CHECK(popup->is_visible());
	CHECK(press_tracker.pressed);
	CHECK(open.events.has("pressed"));
	CHECK(open.events.has("popup_shown"));

	// A second click closes it again, matching MenuButton::pressed()'s toggle.
	const EditorAutomationActionResult close = EditorAutomationDriver::perform(EditorAutomationSnapshot::capture_from_node(root), "click", target, options);
	MessageQueue::get_singleton()->flush();
	CHECK(close.ok);
	CHECK_FALSE(popup->is_visible());
	CHECK(close.events.has("popup_hidden"));

	memdelete(root);
}

// #1095: once a MenuButton's popup is open (visible), the snapshot walks it as a
// real menu node so its items surface as menu_item elements exactly once -- under
// the menu, not duplicated as virtual children of the MenuButton.
TEST_CASE("[Editor][Automation] visible MenuButton popup is captured as a menu node without duplicates") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	MenuButton *menu_button = memnew(MenuButton);
	menu_button->set_text("Options");
	setup_visible_control(menu_button);
	root->add_child(menu_button);

	PopupMenu *popup = menu_button->get_popup();
	popup->add_item("Alpha", 1);
	popup->add_item("Beta", 2);
	MessageQueue::get_singleton()->flush();

	// Open the menu so its popup becomes visible.
	menu_button->show_popup();
	MessageQueue::get_singleton()->flush();
	REQUIRE(popup->is_visible());

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	// The now-visible popup is present as a menu element in the walk.
	const EditorAutomationElement *menu_element = find_element_by_role_and_name(snapshot, "menu", popup->get_name());
	REQUIRE(menu_element != nullptr);

	// Its items appear exactly once each -- captured under the walked popup rather
	// than also duplicated as MenuButton virtual children.
	int alpha_count = 0;
	for (int i = 0; i < snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = snapshot.get_element(i);
		if (element.role == "menu_item" && element.name == "Alpha") {
			alpha_count++;
		}
	}
	CHECK(alpha_count == 1);

	popup->hide();
	memdelete(root);
}

} // namespace TestEditorAutomationDriver
