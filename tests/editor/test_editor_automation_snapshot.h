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

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/item_list.h"
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

TEST_CASE("[Editor][Automation] dialog action buttons (Window internal children) are captured") {
	// AcceptDialog adds its OK/custom buttons via an internal buttons HBox. The
	// snapshot must descend into Window internal children so dialogs can be
	// confirmed via automation.
	AcceptDialog *dialog = memnew(AcceptDialog);
	dialog->set_title("Confirm");
	dialog->set_ok_button_text("Create");
	SceneTree::get_singleton()->get_root()->add_child(dialog);
	dialog->set_visible(true);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(dialog);
	const EditorAutomationElement *ok_button = find_element_by_role_and_name(snapshot, "button", "Create");
	CHECK(ok_button != nullptr);

	memdelete(dialog);
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
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Save Scene");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Save Scene";
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(result.match_indices.size() == 1);
	CHECK(snapshot.get_element(result.match_indices[0]).name == "Save Scene");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] selector role only is ambiguous") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *first = memnew(Button);
	first->set_text("First");
	setup_visible_control(first);
	root->add_child(first);

	Button *second = memnew(Button);
	second->set_text("Second");
	setup_visible_control(second);
	root->add_child(second);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary selector;
	selector["role"] = "button";
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(result.status == EditorAutomationSelectorStatus::AMBIGUOUS);
	CHECK(result.error_kind == "ambiguous_selector");
	CHECK(result.candidates.size() == 2);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] within selector disambiguates duplicate button names") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(root);

	PanelContainer *scene_dock = memnew(PanelContainer);
	scene_dock->set_accessibility_name("Scene");
	setup_visible_control(scene_dock, Size2(280, 200));
	root->add_child(scene_dock);

	Button *scene_button = memnew(Button);
	scene_button->set_text("Add Child Node");
	setup_visible_control(scene_button);
	scene_dock->add_child(scene_button);

	PanelContainer *import_dock = memnew(PanelContainer);
	import_dock->set_accessibility_name("Import");
	setup_visible_control(import_dock, Size2(280, 200));
	root->add_child(import_dock);

	Button *import_button = memnew(Button);
	import_button->set_text("Add Child Node");
	setup_visible_control(import_button);
	import_dock->add_child(import_button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

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
	CHECK(match.path == String(root->get_path_to(scene_button)));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] name_contains and text_contains partial matching") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_accessibility_name("SaveSceneButton");
	button->set_text("Save Scene As...");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary name_selector;
	name_selector["role"] = "button";
	name_selector["name_contains"] = "SaveScene";
	const EditorAutomationSelectorResult name_result = EditorAutomationSelector::resolve(snapshot, name_selector);
	CHECK(name_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(name_result.match_indices.size() == 1);
	CHECK(snapshot.get_element(name_result.match_indices[0]).name == "SaveSceneButton");

	Dictionary text_selector;
	text_selector["text_contains"] = "Save Scene";
	const EditorAutomationSelectorResult text_result = EditorAutomationSelector::resolve(snapshot, text_selector);
	CHECK(text_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(text_result.match_indices.size() == 1);
	CHECK(snapshot.get_element(text_result.match_indices[0]).text == "Save Scene As...");

	Dictionary miss_selector;
	miss_selector["name_contains"] = "DoesNotExist";
	const EditorAutomationSelectorResult miss_result = EditorAutomationSelector::resolve(snapshot, miss_selector);
	CHECK(miss_result.status == EditorAutomationSelectorStatus::NO_MATCH);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] case-insensitive matching for exact and contains fields") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Add Child Node");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	// Default (case-sensitive) mismatch on casing must fail.
	Dictionary sensitive_selector;
	sensitive_selector["role"] = "button";
	sensitive_selector["text"] = "add child node";
	const EditorAutomationSelectorResult sensitive_result = EditorAutomationSelector::resolve(snapshot, sensitive_selector);
	CHECK(sensitive_result.status == EditorAutomationSelectorStatus::NO_MATCH);

	// Case-insensitive exact match.
	Dictionary exact_selector;
	exact_selector["role"] = "button";
	exact_selector["text"] = "add child node";
	exact_selector["case_sensitive"] = false;
	const EditorAutomationSelectorResult exact_result = EditorAutomationSelector::resolve(snapshot, exact_selector);
	CHECK(exact_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(exact_result.match_indices.size() == 1);

	// Case-insensitive contains match.
	Dictionary contains_selector;
	contains_selector["text_contains"] = "CHILD";
	contains_selector["case_sensitive"] = false;
	const EditorAutomationSelectorResult contains_result = EditorAutomationSelector::resolve(snapshot, contains_selector);
	CHECK(contains_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(contains_result.match_indices.size() == 1);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] nth/index disambiguation across repeated labels") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *first = memnew(Button);
	first->set_accessibility_name("FirstAdd");
	first->set_text("Add");
	setup_visible_control(first);
	root->add_child(first);

	Button *second = memnew(Button);
	second->set_accessibility_name("SecondAdd");
	second->set_text("Add");
	setup_visible_control(second);
	root->add_child(second);

	Button *third = memnew(Button);
	third->set_accessibility_name("ThirdAdd");
	third->set_text("Add");
	setup_visible_control(third);
	root->add_child(third);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	// Without a disambiguator, repeated labels stay ambiguous with ordered candidates.
	Dictionary ambiguous_selector;
	ambiguous_selector["role"] = "button";
	ambiguous_selector["text"] = "Add";
	const EditorAutomationSelectorResult ambiguous_result = EditorAutomationSelector::resolve(snapshot, ambiguous_selector);
	CHECK(ambiguous_result.status == EditorAutomationSelectorStatus::AMBIGUOUS);
	REQUIRE(ambiguous_result.candidates.size() == 3);
	const Dictionary first_candidate = ambiguous_result.candidates[0];
	CHECK((int)first_candidate["nth"] == 0);
	CHECK((int)first_candidate["index"] == 0);
	CHECK(first_candidate.has("score"));
	CHECK(first_candidate.has("reason"));
	CHECK(first_candidate.has("handle"));

	// `nth` selects deterministically in stable snapshot order.
	Dictionary nth_selector;
	nth_selector["role"] = "button";
	nth_selector["text"] = "Add";
	nth_selector["nth"] = 1;
	const EditorAutomationSelectorResult nth_result = EditorAutomationSelector::resolve(snapshot, nth_selector);
	CHECK(nth_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(nth_result.match_indices.size() == 1);
	CHECK(snapshot.get_element(nth_result.match_indices[0]).name == "SecondAdd");

	// `index` is a synonym for `nth`.
	Dictionary index_selector;
	index_selector["role"] = "button";
	index_selector["text"] = "Add";
	index_selector["index"] = 2;
	const EditorAutomationSelectorResult index_result = EditorAutomationSelector::resolve(snapshot, index_selector);
	CHECK(index_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(index_result.match_indices.size() == 1);
	CHECK(snapshot.get_element(index_result.match_indices[0]).name == "ThirdAdd");

	// Negative index counts from the end.
	Dictionary negative_selector;
	negative_selector["role"] = "button";
	negative_selector["text"] = "Add";
	negative_selector["nth"] = -1;
	const EditorAutomationSelectorResult negative_result = EditorAutomationSelector::resolve(snapshot, negative_selector);
	CHECK(negative_result.status == EditorAutomationSelectorStatus::OK);
	CHECK(snapshot.get_element(negative_result.match_indices[0]).name == "ThirdAdd");

	// Out-of-range disambiguator reports a machine-readable error with candidates.
	Dictionary oob_selector;
	oob_selector["role"] = "button";
	oob_selector["text"] = "Add";
	oob_selector["nth"] = 7;
	const EditorAutomationSelectorResult oob_result = EditorAutomationSelector::resolve(snapshot, oob_selector);
	CHECK(oob_result.status == EditorAutomationSelectorStatus::NO_MATCH);
	CHECK(oob_result.error_kind == "index_out_of_range");
	CHECK(oob_result.candidates.size() == 3);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] visible_only and enabled_only filtering") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *visible_enabled = memnew(Button);
	visible_enabled->set_accessibility_name("VisibleEnabled");
	visible_enabled->set_text("Action");
	setup_visible_control(visible_enabled);
	root->add_child(visible_enabled);

	Button *disabled = memnew(Button);
	disabled->set_accessibility_name("DisabledButton");
	disabled->set_text("Action");
	setup_visible_control(disabled);
	disabled->set_disabled(true);
	root->add_child(disabled);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	// Both buttons match text; enabled_only narrows to the enabled one.
	Dictionary enabled_selector;
	enabled_selector["role"] = "button";
	enabled_selector["text"] = "Action";
	enabled_selector["enabled_only"] = true;
	const EditorAutomationSelectorResult enabled_result = EditorAutomationSelector::resolve(snapshot, enabled_selector);
	CHECK(enabled_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(enabled_result.match_indices.size() == 1);
	CHECK(snapshot.get_element(enabled_result.match_indices[0]).name == "VisibleEnabled");

	// enabled_only:false is a no-op filter, so the selector stays ambiguous.
	Dictionary noop_selector;
	noop_selector["role"] = "button";
	noop_selector["text"] = "Action";
	noop_selector["enabled_only"] = false;
	const EditorAutomationSelectorResult noop_result = EditorAutomationSelector::resolve(snapshot, noop_selector);
	CHECK(noop_result.status == EditorAutomationSelectorStatus::AMBIGUOUS);

	// visible_only narrows to the visible one after hiding a control.
	disabled->set_disabled(false);
	visible_enabled->set_visible(false);
	MessageQueue::get_singleton()->flush();
	const EditorAutomationSnapshot visible_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	Dictionary visible_selector;
	visible_selector["role"] = "button";
	visible_selector["text"] = "Action";
	visible_selector["visible_only"] = true;
	const EditorAutomationSelectorResult visible_result = EditorAutomationSelector::resolve(visible_snapshot, visible_selector);
	CHECK(visible_result.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(visible_result.match_indices.size() == 1);
	CHECK(visible_snapshot.get_element(visible_result.match_indices[0]).name == "DisabledButton");

	memdelete(root);
}

TEST_CASE("[Editor][Automation] stale snapshot id reconciles for live controls") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Run");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *button_element = find_element_by_role_and_name(first_snapshot, "button", "Run");
	REQUIRE(button_element != nullptr);
	CHECK_FALSE(button_element->handle.is_empty());

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationSelectorResult stale_result = EditorAutomationSelector::resolve_by_id(second_snapshot, button_element->id);
	CHECK(stale_result.status == EditorAutomationSelectorStatus::OK);
	CHECK(stale_result.reconciled);
	CHECK(stale_result.snapshot_generation == second_snapshot.get_generation());
	CHECK(stale_result.current_element_id != button_element->id);

	const EditorAutomationSelectorResult handle_result = EditorAutomationSelector::resolve_by_handle(second_snapshot, button_element->handle);
	CHECK(handle_result.status == EditorAutomationSelectorStatus::OK);
	CHECK_FALSE(handle_result.reconciled);

	Dictionary action_target;
	action_target["id"] = button_element->id;
	const EditorAutomationActionResult action_result = EditorAutomationDriver::perform(second_snapshot, "click", action_target);
	CHECK(action_result.ok);
	CHECK((bool)action_result.details["reconciled"]);
	CHECK((uint64_t)action_result.details["snapshot_generation"] == second_snapshot.get_generation());

	memdelete(root);
}

TEST_CASE("[Editor][Automation] freed control stale handle fails with machine-readable error") {
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
	const String durable_handle = button_element->handle;

	root->remove_child(button);
	memdelete(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);

	const EditorAutomationSelectorResult id_result = EditorAutomationSelector::resolve_by_id(second_snapshot, stale_id);
	CHECK(id_result.status == EditorAutomationSelectorStatus::STALE_ID);
	CHECK(id_result.error_kind == "freed_object");
	CHECK(id_result.candidates.size() > 0);

	const EditorAutomationSelectorResult handle_result = EditorAutomationSelector::resolve_by_handle(second_snapshot, durable_handle);
	CHECK(handle_result.status == EditorAutomationSelectorStatus::STALE_ID);
	CHECK(handle_result.error_kind == "freed_object");

	Dictionary action_target;
	action_target["id"] = stale_id;
	const EditorAutomationActionResult action_result = EditorAutomationDriver::perform(second_snapshot, "click", action_target);
	CHECK_FALSE(action_result.ok);
	CHECK(action_result.kind == "freed_element");
	CHECK(action_result.candidates.size() > 0);

	memdelete(root);
}

TEST_CASE("[Editor][Automation] hidden control stale handle fails with visibility diagnostic") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Hide Me");
	setup_visible_control(button);
	root->add_child(button);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *button_element = find_element_by_role_and_name(first_snapshot, "button", "Hide Me");
	REQUIRE(button_element != nullptr);
	const String stale_id = button_element->id;

	button->set_visible(false);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve_by_id(second_snapshot, stale_id);
	CHECK(result.status == EditorAutomationSelectorStatus::STALE_ID);
	CHECK(result.error_kind == "element_not_visible");
	CHECK(result.candidates.size() > 0);

	const Dictionary diagnostic = result.candidates[0];
	CHECK(diagnostic.has("object_id"));
	CHECK(diagnostic.has("node_class"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] virtual element durable handle reconciliation") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	TabContainer *tab_container = memnew(TabContainer);
	tab_container->set_name("MainTabs");
	setup_visible_control(tab_container, Size2(300, 200));
	root->add_child(tab_container);

	Control *scene_tab = memnew(Control);
	scene_tab->set_name("SceneTab");
	tab_container->add_child(scene_tab);
	Control *import_tab = memnew(Control);
	import_tab->set_name("ImportTab");
	tab_container->add_child(import_tab);
	tab_container->set_tab_title(0, "Scene");
	tab_container->set_tab_title(1, "Import");
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot first_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *tabs = find_element_by_role_and_name(first_snapshot, "tab_list", "MainTabs");
	REQUIRE(tabs != nullptr);
	REQUIRE(tabs->children.size() >= 2);

	const EditorAutomationElement &scene_tab_element = first_snapshot.get_element(tabs->children[0]);
	CHECK(scene_tab_element.role == "tab");
	CHECK_FALSE(scene_tab_element.handle.is_empty());

	const EditorAutomationSnapshot second_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationSelectorResult reconciled = EditorAutomationSelector::resolve_by_id(second_snapshot, scene_tab_element.id);
	CHECK(reconciled.status == EditorAutomationSelectorStatus::OK);
	CHECK(reconciled.reconciled);

	ItemList *item_list = memnew(ItemList);
	item_list->set_name("Files");
	setup_visible_control(item_list, Size2(280, 120));
	root->add_child(item_list);
	item_list->add_item("Alpha");
	item_list->add_item("Beta");
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot list_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationElement *list_element = find_element_by_role_and_name(list_snapshot, "list", "Files");
	REQUIRE(list_element != nullptr);
	REQUIRE(list_element->children.size() >= 2);
	const EditorAutomationElement &beta_item = list_snapshot.get_element(list_element->children[1]);
	CHECK(beta_item.role == "list_item");
	const String beta_handle = beta_item.handle;

	item_list->remove_item(1);
	MessageQueue::get_singleton()->flush();

	const EditorAutomationSnapshot changed_snapshot = EditorAutomationSnapshot::capture_from_node(root);
	const EditorAutomationSelectorResult unavailable = EditorAutomationSelector::resolve_by_handle(changed_snapshot, beta_handle);
	CHECK(unavailable.status == EditorAutomationSelectorStatus::STALE_ID);
	CHECK(unavailable.error_kind == "virtual_element_unavailable");
	CHECK(unavailable.candidates.size() > 0);
	const Dictionary diagnostic = unavailable.candidates[0];
	CHECK(diagnostic.has("durable_key_strategy"));

	memdelete(root);
}

} // namespace TestEditorAutomationSnapshot
