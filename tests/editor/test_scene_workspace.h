/**************************************************************************/
/*  test_scene_workspace.h                                                */
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

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_workspace.h"
#include "editor/scene/editor_scene_tabs.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

class TestableEditorSceneTabs : public EditorSceneTabs {
public:
	using EditorSceneTabs::_resolve_tab_transfer_panes;
};

static TabBar *_find_first_tab_bar(Node *p_node) {
	if (!p_node) {
		return nullptr;
	}

	TabBar *tab_bar = Object::cast_to<TabBar>(p_node);
	if (tab_bar) {
		return tab_bar;
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		TabBar *child_tab_bar = _find_first_tab_bar(p_node->get_child(i));
		if (child_tab_bar) {
			return child_tab_bar;
		}
	}

	return nullptr;
}

TEST_CASE("[SceneTree][Editor] pane-model-mapping") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);

	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_scene_pane(c, 0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);

	const Vector<int> pane_0 = editor_data.get_pane_scene_indices(0);
	const Vector<int> pane_1 = editor_data.get_pane_scene_indices(1);
	REQUIRE(pane_0.size() == 2);
	REQUIRE(pane_1.size() == 1);
	CHECK(pane_0[0] == a);
	CHECK(pane_0[1] == c);
	CHECK(pane_1[0] == b);

	CHECK(editor_data.pane_tab_to_scene_index(0, 0) == a);
	CHECK(editor_data.pane_tab_to_scene_index(0, 1) == c);
	CHECK(editor_data.scene_index_to_pane_tab(c) == 1);
	CHECK(editor_data.scene_index_to_pane_tab(b) == 0);

	editor_data.remove_scene(c);
	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] pane-model-remove-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_scene_pane(c, 0);
	editor_data.set_focused_pane(0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);

	editor_data.remove_scene(a);
	CHECK(editor_data.get_pane_current_scene(0) == 1);
	CHECK(editor_data.get_edited_scene() == 1);

	editor_data.set_focused_pane(1);
	editor_data.remove_scene(0);
	CHECK(editor_data.get_pane_current_scene(1) == -1);
	CHECK(editor_data.get_edited_scene() == -1);

	editor_data.set_focused_pane(0);
	editor_data.remove_scene(0);
	CHECK(editor_data.get_edited_scene_count() == 0);
}

TEST_CASE("[SceneTree][Editor] pane-model-cross-move") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, -1);

	editor_data.set_scene_pane(b, 1);
	editor_data.set_pane_current_scene(1, b);

	CHECK(editor_data.get_pane_scene_indices(0) == Vector<int>{ a });
	CHECK(editor_data.get_pane_scene_indices(1) == Vector<int>{ b });
	CHECK(editor_data.get_pane_current_scene(1) == b);

	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] scene-tabs-transfer-signal-targets-receiver-pane") {
	EditorSceneTabs *previous_singleton = EditorSceneTabs::get_singleton();
	EditorSceneTabs *source_tabs = memnew(EditorSceneTabs(0));
	TabBar *source_bar = _find_first_tab_bar(source_tabs);
	REQUIRE(source_bar);

	int source_pane = -1;
	int target_pane = -1;
	CHECK(TestableEditorSceneTabs::_resolve_tab_transfer_panes(source_bar, 1, source_pane, target_pane));
	CHECK(source_pane == 0);
	CHECK(target_pane == 1);

	memdelete(source_tabs);
	EditorSceneTabs::set_focused_singleton(previous_singleton);
}

TEST_CASE("[SceneTree][Editor] pane-model-invalid-tab-is-ignored") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_pane_current_scene(0, a);

	ErrorDetector error_detector;
	CHECK(editor_data.pane_tab_to_scene_index(0, 1) == -1);
	CHECK_FALSE(error_detector.has_error);

	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] pane-model-moving-current-scene-updates-source-current") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 0);
	editor_data.set_focused_pane(0);
	editor_data.set_pane_current_scene(0, a);

	editor_data.set_scene_pane(a, 1);

	CHECK(editor_data.get_pane_current_scene(0) == b);
	CHECK(editor_data.get_edited_scene() == b);

	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] context-dual-attach") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	SubViewportContainer *container_a = memnew(SubViewportContainer);
	SubViewportContainer *container_b = memnew(SubViewportContainer);
	tree_root->add_child(container_a);
	tree_root->add_child(container_b);

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *scene_a = memnew(Node2D);
	Node2D *scene_b = memnew(Node2D);
	Node2D *child_a = memnew(Node2D);
	Node2D *child_b = memnew(Node2D);
	scene_a->add_child(child_a);
	scene_b->add_child(child_b);
	context_a->set_scene_root_node(scene_a);
	context_b->set_scene_root_node(scene_b);

	context_a->set_display_parent(container_a, true);
	context_b->set_display_parent(container_b, false);

	CHECK(context_a->get_viewport()->is_inside_tree());
	CHECK(context_b->get_viewport()->is_inside_tree());
	context_a->get_selection()->add_node(child_a);
	context_b->get_selection()->add_node(child_b);

	context_a->set_display_parent(container_b, false);
	CHECK(context_a->get_selection()->is_selected(child_a));

	context_a->set_display_parent(container_a, true);
	CHECK(context_a->get_selection()->is_selected(child_a));

	memdelete(context_a);
	memdelete(context_b);
	tree_root->remove_child(container_a);
	tree_root->remove_child(container_b);
	memdelete(container_a);
	memdelete(container_b);
}

TEST_CASE("[SceneTree][Editor] context-3d-heuristic") {
	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene_2d = memnew(Node2D);
	context->set_scene_root_node(scene_2d);
	CHECK_FALSE(context->scene_has_3d_content());

	Node3D *scene_3d = memnew(Node3D);
	context->set_scene_root_node(scene_3d);
	memdelete(scene_2d);
	CHECK(context->scene_has_3d_content());

	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] workspace-config-round-trip") {
	EditorData editor_data;
	Ref<ConfigFile> config;
	config.instantiate();

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(a, "res://pane0_a.tscn");
	editor_data.set_scene_path(b, "res://pane1_b.tscn");
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);
	editor_data.set_focused_pane(1);

	config->set_value("Workspace", "pane_count", 2);
	config->set_value("Workspace", "split_vertical", true);
	config->set_value("Workspace", "split_offset", 123);
	config->set_value("Workspace", "focused_pane", 1);
	config->set_value("Workspace", "pane_0_scenes", PackedStringArray{ "res://pane0_a.tscn" });
	config->set_value("Workspace", "pane_1_scenes", PackedStringArray{ "res://pane1_b.tscn" });
	config->set_value("Workspace", "pane_0_current", "res://pane0_a.tscn");
	config->set_value("Workspace", "pane_1_current", "res://pane1_b.tscn");

	CHECK(EditorSceneWorkspace::get_saved_pane_count(config) == 2);
	CHECK(EditorSceneWorkspace::get_saved_split_vertical(config));
	CHECK(EditorSceneWorkspace::get_saved_split_offset(config) == 123);
	CHECK(EditorSceneWorkspace::get_saved_focused_pane(config) == 1);
	CHECK(EditorSceneWorkspace::get_saved_pane_scenes(config, 0) == PackedStringArray{ "res://pane0_a.tscn" });
	CHECK(EditorSceneWorkspace::get_saved_pane_scenes(config, 1) == PackedStringArray{ "res://pane1_b.tscn" });
	CHECK(EditorSceneWorkspace::get_saved_pane_current(config, 0) == "res://pane0_a.tscn");
	CHECK(EditorSceneWorkspace::get_saved_pane_current(config, 1) == "res://pane1_b.tscn");
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->split_workspace(true);
	workspace->get_split()->set_split_offset(456);
	workspace->set_focused_pane(1);
	Ref<ConfigFile> saved_config;
	saved_config.instantiate();
	EditorSceneWorkspace::save_to_config(saved_config, editor_data, workspace);
	CHECK(EditorSceneWorkspace::get_saved_split_offset(saved_config) == 456);
	CHECK(saved_config->get_value("Workspace", "pane_1_current") == "res://pane1_b.tscn");
	memdelete(workspace);

	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] workspace-split-pane-sizes") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	VBoxContainer *host = memnew(VBoxContainer);
	host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	tree_root->add_child(host);

	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->set_custom_minimum_size(Size2(800, 600));
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	host->add_child(workspace);

	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	workspace->split_workspace(false);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	REQUIRE(workspace->is_split());
	REQUIRE(workspace->get_pane_count() == 2);

	EditorScenePane *pane_0 = workspace->get_pane(0);
	EditorScenePane *pane_1 = workspace->get_pane(1);
	CHECK(pane_0->get_size().x > 100);
	CHECK(pane_1->get_size().x > 100);

	memdelete(workspace);
	tree_root->remove_child(host);
	memdelete(host);
}

TEST_CASE("[SceneTree][Editor] workspace-fits-overlay-to-focused-pane-without-reparenting") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	Control *host = memnew(Control);
	host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	tree_root->add_child(host);

	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	workspace->set_custom_minimum_size(Size2(800, 600));
	host->add_child(workspace);

	Control *overlay = memnew(Control);
	overlay->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	host->add_child(overlay);
	Node *overlay_parent = overlay->get_parent();

	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	workspace->split_workspace(false);
	workspace->set_focused_pane(1);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	workspace->fit_overlay_to_focused_pane(overlay);

	EditorScenePane *focused_pane = workspace->get_pane(1);
	const Rect2 expected_rect = focused_pane->get_content_host()->get_global_rect();
	CHECK(overlay->get_parent() == overlay_parent);
	CHECK(overlay->get_global_position().is_equal_approx(expected_rect.position));
	CHECK(overlay->get_size().is_equal_approx(expected_rect.size));

	memdelete(workspace);
	host->remove_child(overlay);
	memdelete(overlay);
	tree_root->remove_child(host);
	memdelete(host);
}

TEST_CASE("[SceneTree][Editor] workspace-unsplit-resets-focus-to-remaining-pane") {
	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->split_workspace(false);
	workspace->set_focused_pane(1);

	workspace->unsplit_workspace();

	CHECK(workspace->get_pane_count() == 1);
	CHECK(workspace->get_focused_pane() == 0);

	memdelete(workspace);
}

TEST_CASE("[SceneTree][Editor] workspace-pane-click-requests-focus-even-when-child-handles-gui-input") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	Control *host = memnew(Control);
	host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	tree_root->add_child(host);

	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	workspace->set_custom_minimum_size(Size2(800, 600));
	host->add_child(workspace);

	workspace->split_workspace(false);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	workspace->connect("pane_focus_requested", callable_mp(workspace, &EditorSceneWorkspace::set_focused_pane));
	SIGNAL_WATCH(workspace, "pane_focus_requested");
	const Point2 click_position = workspace->get_pane(1)->get_global_rect().get_center();
	SEND_GUI_MOUSE_BUTTON_EVENT(click_position, MouseButton::LEFT, MouseButtonMask::LEFT, Key::NONE);

	Array expected_emission;
	expected_emission.push_back(1);
	Array expected;
	expected.push_back(expected_emission);
	SIGNAL_CHECK("pane_focus_requested", expected);
	SEND_GUI_MOUSE_BUTTON_RELEASED_EVENT(click_position, MouseButton::LEFT, MouseButtonMask::NONE, Key::NONE);
	SIGNAL_UNWATCH(workspace, "pane_focus_requested");

	memdelete(workspace);
	tree_root->remove_child(host);
	memdelete(host);
}

} // namespace TestSceneWorkspace
