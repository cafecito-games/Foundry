/**************************************************************************/
/*  test_script_editor_views.h                                            */
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
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_view.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"
#include "scene/resources/text_file.h"

#include "tests/editor/test_scene_workspace.h"
#include "tests/test_macros.h"

namespace TestScriptEditorViews {

struct ScriptControllerHarness {
	Control *host = nullptr;
	ScriptEditorController *controller = nullptr;

	void mount() {
		host = memnew(Control);
		SceneTree::get_singleton()->get_root()->add_child(host);
		controller = memnew(ScriptEditorController);
		controller->init_global_services(host);
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(controller);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
	}
};

static Ref<TextFile> make_text_file(const String &p_path, const String &p_source) {
	Ref<TextFile> text_file;
	text_file.instantiate();
	text_file->set_path(p_path);
	text_file->set_text(p_source);
	return text_file;
}

TEST_CASE("[Editor][two-scripts-two-leaves] Distinct script leaves keep independent tabs") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	ScriptLeaf *leaf_a = memnew(ScriptLeaf);
	ScriptLeaf *leaf_b = memnew(ScriptLeaf);
	h.host->add_child(leaf_a);
	h.host->add_child(leaf_b);
	h.pump();

	ScriptEditorView *view_a = h.controller->create_view_for_leaf(leaf_a);
	ScriptEditorView *view_b = h.controller->create_view_for_leaf(leaf_b);
	REQUIRE(view_a != nullptr);
	REQUIRE(view_b != nullptr);
	CHECK(view_a != view_b);

	Ref<TextFile> script_a = make_text_file("res://alpha.txt", "alpha one");
	Ref<TextFile> script_b = make_text_file("res://beta.txt", "beta two");
	CHECK(view_a->edit(script_a, true));
	CHECK(view_b->edit(script_b, true));
	h.pump();

	CHECK(view_a->get_tab_container()->get_tab_count() == 1);
	CHECK(view_b->get_tab_container()->get_tab_count() == 1);
	CHECK(view_a->get_open_editor_for_path("res://alpha.txt") != nullptr);
	CHECK(view_b->get_open_editor_for_path("res://beta.txt") != nullptr);
	CHECK(view_a->get_open_editor_for_path("res://beta.txt") == nullptr);
	CHECK(view_b->get_open_editor_for_path("res://alpha.txt") == nullptr);

	h.controller->set_focused_view(view_a);
	String path_a;
	int line_a = 0;
	int column_a = 0;
	CHECK(h.controller->get_current_script_view_state(path_a, line_a, column_a));
	CHECK(path_a == "res://alpha.txt");

	h.controller->set_focused_view(view_b);
	String path_b;
	int line_b = 0;
	int column_b = 0;
	CHECK(h.controller->get_current_script_view_state(path_b, line_b, column_b));
	CHECK(path_b == "res://beta.txt");

	h.host->remove_child(leaf_a);
	h.host->remove_child(leaf_b);
	memdelete(leaf_a);
	memdelete(leaf_b);
	h.unmount();
}

TEST_CASE("[Editor][script-view-parity] Single script leaf preserves open/edit state") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	WorkspaceLeafNode *script_leaf_node = h.workspace->open_script_leaf(scene_leaf, "res://player.fs", true);
	h.pump();
	REQUIRE(script_leaf_node != nullptr);

	ScriptLeaf *script_leaf = Object::cast_to<ScriptLeaf>(script_leaf_node->get_leaf_content()->get_root_control());
	REQUIRE(script_leaf != nullptr);
	ScriptEditorView *view = script_leaf->get_script_editor_view();
	REQUIRE(view != nullptr);

	Ref<TextFile> player = make_text_file("res://player.fs", "func run(): pass");
	Ref<TextFile> helper = make_text_file("res://helper.fs", "func help(): pass");
	CHECK(view->edit(player, true));
	CHECK(view->edit(helper, true));
	h.pump();
	CHECK(view->get_tab_container()->get_tab_count() == 2);

	Ref<ConfigFile> config;
	config.instantiate();
	script_leaf->save_layout(config, "leaf");

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[Editor][focused-view-global-actions] Global controller routes to focused view") {
	ScriptControllerHarness h;
	h.mount();
	h.pump();

	ScriptLeaf *leaf_a = memnew(ScriptLeaf);
	ScriptLeaf *leaf_b = memnew(ScriptLeaf);
	h.host->add_child(leaf_a);
	h.host->add_child(leaf_b);
	h.pump();

	ScriptEditorView *view_a = h.controller->create_view_for_leaf(leaf_a);
	ScriptEditorView *view_b = h.controller->create_view_for_leaf(leaf_b);

	Ref<TextFile> script_a = make_text_file("res://focused_a.txt", "a");
	Ref<TextFile> script_b = make_text_file("res://focused_b.txt", "b");
	view_a->edit(script_a, false);
	view_b->edit(script_b, false);
	h.pump();

	h.controller->set_focused_view(view_b);
	String path;
	int line = 0;
	int column = 0;
	CHECK(h.controller->get_current_script_view_state(path, line, column));
	CHECK(path == "res://focused_b.txt");

	h.controller->save_current_script();
	h.controller->set_focused_view(view_a);
	CHECK(h.controller->get_current_script_view_state(path, line, column));
	CHECK(path == "res://focused_a.txt");

	h.host->remove_child(leaf_a);
	h.host->remove_child(leaf_b);
	memdelete(leaf_a);
	memdelete(leaf_b);
	h.unmount();
}

TEST_CASE("[Editor][script-view-persistence] Multi script leaves round-trip per-leaf tabs") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);

	WorkspaceLeafNode *script_a = h.workspace->open_script_leaf(scene_leaf, "res://a.fs", true);
	WorkspaceLeafNode *script_b = h.workspace->open_script_leaf(scene_leaf, "res://b.fs", true);
	h.pump();
	REQUIRE(script_a != nullptr);
	REQUIRE(script_b != nullptr);
	CHECK(script_a != script_b);
	CHECK(h.workspace->get_script_leaves().size() == 2);

	ScriptLeaf *leaf_content_a = Object::cast_to<ScriptLeaf>(script_a->get_leaf_content()->get_root_control());
	ScriptLeaf *leaf_content_b = Object::cast_to<ScriptLeaf>(script_b->get_leaf_content()->get_root_control());
	REQUIRE(leaf_content_a != nullptr);
	REQUIRE(leaf_content_b != nullptr);

	Ref<TextFile> file_a = make_text_file("res://a.fs", "a");
	Ref<TextFile> file_b = make_text_file("res://b.fs", "b");
	leaf_content_a->get_script_editor_view()->edit(file_a, true);
	leaf_content_b->get_script_editor_view()->edit(file_b, true);
	h.pump();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	const int script_a_id = script_a->get_leaf_id();
	const int script_b_id = script_b->get_leaf_id();

	h.unmount();
	memdelete(controller);

	TestSceneWorkspace::WorkspaceHarness h2;
	h2.mount();
	h2.pump();
	ScriptEditorController *controller2 = memnew(ScriptEditorController);
	controller2->init_global_services(h2.host);
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_script_leaves().size() == 2);
	WorkspaceLeafNode *restored_a = h2.workspace->get_leaf_by_id(script_a_id);
	WorkspaceLeafNode *restored_b = h2.workspace->get_leaf_by_id(script_b_id);
	REQUIRE(restored_a != nullptr);
	REQUIRE(restored_b != nullptr);

	ScriptLeaf *restored_leaf_a = Object::cast_to<ScriptLeaf>(restored_a->get_leaf_content()->get_root_control());
	ScriptLeaf *restored_leaf_b = Object::cast_to<ScriptLeaf>(restored_b->get_leaf_content()->get_root_control());
	REQUIRE(restored_leaf_a != nullptr);
	REQUIRE(restored_leaf_b != nullptr);
	CHECK(restored_leaf_a->get_script_path() == "res://a.fs");
	CHECK(restored_leaf_b->get_script_path() == "res://b.fs");
	REQUIRE(restored_leaf_a->get_script_editor_view() != nullptr);
	REQUIRE(restored_leaf_b->get_script_editor_view() != nullptr);
	CHECK(restored_leaf_a->get_script_editor_view()->get_open_editor_for_path("res://a.fs") != nullptr);
	CHECK(restored_leaf_b->get_script_editor_view()->get_open_editor_for_path("res://b.fs") != nullptr);

	h2.unmount();
	memdelete(controller2);
}

} // namespace TestScriptEditorViews
