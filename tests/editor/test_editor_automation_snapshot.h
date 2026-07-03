/**************************************************************************/
/*  test_editor_automation_snapshot.h                                      */
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

#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationSnapshot {

static const EditorAutomationElement *find_element_by_role_and_name(const EditorAutomationSnapshot &p_snapshot, const String &p_role, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

static void setup_visible_control(Control *p_control, const Size2 &p_size = Size2(120, 32)) {
	p_control->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	p_control->set_size(p_size);
	p_control->set_visible(true);
}

TEST_CASE("[Editor][Automation] synthetic control tree snapshot") {
	Window *window = memnew(Window);
	window->set_title("Automation Root");
	window->set_size(Size2i(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(window);
	MessageQueue::get_singleton()->flush();

	PanelContainer *scene_dock = memnew(PanelContainer);
	scene_dock->set_name("SceneDock");
	scene_dock->set_accessibility_name("Scene");
	setup_visible_control(scene_dock, Size2(300, 400));
	window->add_child(scene_dock);

	Button *add_button = memnew(Button);
	add_button->set_name("AddChildButton");
	add_button->set_text("Add Child Node");
	setup_visible_control(add_button);
	scene_dock->add_child(add_button);

	LineEdit *name_field = memnew(LineEdit);
	name_field->set_name("NameField");
	name_field->set_text("Player");
	setup_visible_control(name_field);
	scene_dock->add_child(name_field);

	CheckBox *visible_checkbox = memnew(CheckBox);
	visible_checkbox->set_name("VisibleCheck");
	visible_checkbox->set_text("Visible");
	visible_checkbox->set_pressed(true);
	setup_visible_control(visible_checkbox);
	scene_dock->add_child(visible_checkbox);

	TabContainer *tab_container = memnew(TabContainer);
	tab_container->set_name("MainTabs");
	setup_visible_control(tab_container, Size2(300, 200));
	window->add_child(tab_container);

	Control *scene_tab = memnew(Control);
	scene_tab->set_name("SceneTab");
	tab_container->add_child(scene_tab);
	Control *import_tab = memnew(Control);
	import_tab->set_name("ImportTab");
	tab_container->add_child(import_tab);
	tab_container->set_tab_title(0, "Scene");
	tab_container->set_tab_title(1, "Import");

	name_field->grab_focus();
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);
	CHECK(snapshot.get_generation() > 0);

	const Dictionary serialized = snapshot.to_dictionary();
	CHECK(serialized.has("generation"));
	CHECK(serialized.has("focused_element_id"));
	CHECK(serialized.has("roots"));
	CHECK(serialized["roots"].get_type() == Variant::ARRAY);

	const EditorAutomationElement *button = find_element_by_role_and_name(snapshot, "button", "Add Child Node");
	REQUIRE(button != nullptr);
	CHECK(button->class_name == "Button");
	CHECK(button->visible);
	CHECK(button->enabled);
	CHECK_FALSE(button->focused);
	CHECK(button->actions.has("click"));
	CHECK(button->actions.has("focus"));
	CHECK(button->bounds.size.x > 0);
	CHECK(button->bounds.size.y > 0);

	const EditorAutomationElement *line_edit = find_element_by_role_and_name(snapshot, "text_field", "NameField");
	REQUIRE(line_edit != nullptr);
	CHECK(line_edit->text == "Player");
	CHECK(line_edit->focused);
	CHECK(line_edit->actions.has("set_text"));
	CHECK(line_edit->actions.has("type_text"));

	const EditorAutomationElement *checkbox = find_element_by_role_and_name(snapshot, "checkbox", "Visible");
	REQUIRE(checkbox != nullptr);
	CHECK(checkbox->pressed);
	CHECK(checkbox->actions.has("click"));

	const EditorAutomationElement *tabs = find_element_by_role_and_name(snapshot, "tab_list", "MainTabs");
	REQUIRE(tabs != nullptr);
	CHECK(tabs->children.size() >= 2);

	bool found_scene_tab = false;
	for (int child_index : tabs->children) {
		const EditorAutomationElement &tab = snapshot.get_element(child_index);
		if (tab.role == "tab" && tab.name == "Scene") {
			found_scene_tab = true;
			CHECK(tab.selected);
		}
	}
	CHECK(found_scene_tab);

	memdelete(window);
}

TEST_CASE("[Editor][Automation] selector role and name returns one match") {
	Window *window = memnew(Window);
	window->set_size(Size2i(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(window);

	Button *button = memnew(Button);
	button->set_text("Save Scene");
	setup_visible_control(button);
	window->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Save Scene";
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(result.match_indices.size() == 1);
	CHECK(snapshot.get_element(result.match_indices[0]).name == "Save Scene");

	memdelete(window);
}

TEST_CASE("[Editor][Automation] selector role only is ambiguous") {
	Window *window = memnew(Window);
	window->set_size(Size2i(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(window);

	Button *first = memnew(Button);
	first->set_text("First");
	setup_visible_control(first);
	window->add_child(first);

	Button *second = memnew(Button);
	second->set_text("Second");
	setup_visible_control(second);
	window->add_child(second);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);

	Dictionary selector;
	selector["role"] = "button";
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(result.status == EditorAutomationSelectorStatus::AMBIGUOUS);
	CHECK(result.error_kind == "ambiguous_selector");
	CHECK(result.candidates.size() == 2);

	memdelete(window);
}

TEST_CASE("[Editor][Automation] within selector disambiguates duplicate button names") {
	Window *window = memnew(Window);
	window->set_size(Size2i(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(window);

	PanelContainer *scene_dock = memnew(PanelContainer);
	scene_dock->set_accessibility_name("Scene");
	setup_visible_control(scene_dock, Size2(280, 200));
	window->add_child(scene_dock);

	Button *scene_button = memnew(Button);
	scene_button->set_text("Add Child Node");
	setup_visible_control(scene_button);
	scene_dock->add_child(scene_button);

	PanelContainer *import_dock = memnew(PanelContainer);
	import_dock->set_accessibility_name("Import");
	setup_visible_control(import_dock, Size2(280, 200));
	window->add_child(import_dock);

	Button *import_button = memnew(Button);
	import_button->set_text("Add Child Node");
	setup_visible_control(import_button);
	import_dock->add_child(import_button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);

	Dictionary within;
	within["role"] = "control";
	within["name"] = "Scene";

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Add Child Node";
	selector["within"] = within;

	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(result.match_indices.size() == 1);
	const EditorAutomationElement &match = snapshot.get_element(result.match_indices[0]);
	CHECK(match.name == "Add Child Node");
	CHECK(match.path == window->get_path_to(scene_button));

	memdelete(window);
}

TEST_CASE("[Editor][Automation] stale snapshot id lookup fails") {
	Window *window = memnew(Window);
	window->set_size(Size2i(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(window);

	Button *button = memnew(Button);
	button->set_text("Run");
	setup_visible_control(button);
	window->add_child(button);
	MessageQueue::get_singleton()->flush();

	EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(window);
	const EditorAutomationElement *button_element = find_element_by_role_and_name(first_snapshot, "button", "Run");
	REQUIRE(button_element != nullptr);
	const String stale_id = button_element->id;

	EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(window);
	const EditorAutomationSelectorResult stale_result = EditorAutomationSelector::resolve_by_id(second_snapshot, stale_id);
	CHECK(stale_result.status == EditorAutomationSelectorStatus::STALE_ID);
	CHECK(stale_result.error_kind == "stale_snapshot_id");
	CHECK_FALSE(stale_result.message.is_empty());
	CHECK(stale_result.candidates.size() > 0);

	const EditorAutomationSelectorResult current_result = EditorAutomationSelector::resolve_by_id(second_snapshot, button_element->id);
	CHECK(current_result.status == EditorAutomationSelectorStatus::STALE_ID);

	const EditorAutomationElement *fresh_button = find_element_by_role_and_name(second_snapshot, "button", "Run");
	REQUIRE(fresh_button != nullptr);
	const EditorAutomationSelectorResult valid_result = EditorAutomationSelector::resolve_by_id(second_snapshot, fresh_button->id);
	CHECK(valid_result.status == EditorAutomationSelectorStatus::OK);

	memdelete(window);
}

} // namespace TestEditorAutomationSnapshot
