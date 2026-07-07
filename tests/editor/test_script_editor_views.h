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
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/gui/code_editor.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/script/script_editor_view.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_registry.h"
#include "editor/workspace/workspace_tab_type.h"

#include "scene/gui/code_edit.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
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

static String write_temp_text_file(const String &p_name, const String &p_source) {
	const String dir = OS::get_singleton()->get_cache_path().path_join("script_view_persist");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), String());
	da->make_dir_recursive(dir);
	const String path = dir.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V(file.is_null(), String());
	file->store_string(p_source);
	return path;
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

	ScriptLeaf *script_leaf = TestSceneWorkspace::get_leaf_script(script_leaf_node);
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

	WorkspaceLeafNode *script_a = h.workspace->open_script_leaf(scene_leaf, String(), true);
	WorkspaceLeafNode *script_b = h.workspace->open_script_leaf(scene_leaf, String(), true);
	h.pump();
	REQUIRE(script_a != nullptr);
	REQUIRE(script_b != nullptr);
	CHECK(script_a != script_b);
	CHECK(h.workspace->get_script_leaves().size() == 2);

	ScriptLeaf *leaf_content_a = TestSceneWorkspace::get_leaf_script(script_a);
	ScriptLeaf *leaf_content_b = TestSceneWorkspace::get_leaf_script(script_b);
	REQUIRE(leaf_content_a != nullptr);
	REQUIRE(leaf_content_b != nullptr);

	const String path_a = write_temp_text_file("a.txt", "a");
	const String path_b = write_temp_text_file("b.txt", "b");
	REQUIRE_FALSE(path_a.is_empty());
	REQUIRE_FALSE(path_b.is_empty());
	Ref<TextFile> file_a = make_text_file(path_a, "a");
	Ref<TextFile> file_b = make_text_file(path_b, "b");
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

	ScriptLeaf *restored_leaf_a = TestSceneWorkspace::get_leaf_script(restored_a);
	ScriptLeaf *restored_leaf_b = TestSceneWorkspace::get_leaf_script(restored_b);
	REQUIRE(restored_leaf_a != nullptr);
	REQUIRE(restored_leaf_b != nullptr);
	CHECK(restored_leaf_a->get_script_path() == path_a);
	CHECK(restored_leaf_b->get_script_path() == path_b);
	REQUIRE(restored_leaf_a->get_script_editor_view() != nullptr);
	REQUIRE(restored_leaf_b->get_script_editor_view() != nullptr);
	CHECK(restored_leaf_a->get_script_editor_view()->get_open_editor_for_path(path_a) != nullptr);
	CHECK(restored_leaf_b->get_script_editor_view()->get_open_editor_for_path(path_b) != nullptr);

	h2.unmount();
	memdelete(controller2);
}

static ConfirmationDialog *find_erase_confirm(ScriptEditorView *p_view) {
	for (int i = 0; i < p_view->get_child_count(); i++) {
		if (ConfirmationDialog *dialog = Object::cast_to<ConfirmationDialog>(p_view->get_child(i))) {
			return dialog;
		}
	}
	return nullptr;
}

static ScriptLeaf *find_mounted_script_leaf(WorkspacePane *p_pane) {
	Control *host = p_pane ? p_pane->get_chrome_host() : nullptr;
	if (!host) {
		return nullptr;
	}
	for (int i = 0; i < host->get_child_count(false); i++) {
		if (ScriptLeaf *leaf = Object::cast_to<ScriptLeaf>(host->get_child(i, false))) {
			return leaf;
		}
	}
	return nullptr;
}

static WorkspaceTab open_script_tab(WorkspacePane *p_pane, WorkspaceTabRegistry &p_registry, const String &p_path) {
	WorkspaceTabType *script_type = p_registry.find_type(StringName("script"));
	WorkspaceTab tab = script_type->make_tab(p_path, p_registry.allocate_stable_id());
	p_pane->add_tab(tab);
	return tab;
}

TEST_CASE("[Editor][script-tab-canonical-reveal] Opening a script twice reveals the single tab") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	WorkspacePane *pane_a = TestSceneWorkspace::get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = TestSceneWorkspace::get_leaf_pane(leaf_b);
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_b != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane_a->set_tab_registry(&registry);
	pane_b->set_tab_registry(&registry);

	const String path = write_temp_text_file("canon.txt", "one\ntwo\n");
	REQUIRE_FALSE(path.is_empty());

	open_script_tab(pane_a, registry, path);
	h.pump();
	CHECK(pane_a->get_tab_count() == 1);

	// Opening the same script again must resolve to the existing tab via the
	// canonical (type_id, resource_key) index, wherever that tab currently lives.
	WorkspaceTab existing;
	WorkspaceTabLocation location;
	REQUIRE(registry.find_canonical(StringName("script"), path, existing, location));
	CHECK(location.pane_id == pane_a->get_leaf_id());

	// Even though we "target" pane B, the reveal lands on pane A's existing tab.
	WorkspacePane *holder = (location.pane_id == pane_a->get_leaf_id()) ? pane_a : pane_b;
	holder->set_active_tab(location.tab_index);
	h.pump();

	CHECK(pane_a->get_tab_count() == 1);
	CHECK(pane_b->get_tab_count() == 0);
	CHECK(pane_a->get_active_tab_index() == location.tab_index);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[Editor][script-feature-gate] Controller exposes a script-feature toggle") {
	ScriptControllerHarness h;
	h.mount();

	// The script feature is enabled by default; the editor gates script reveal on
	// this flag instead of on a hidden main-screen button.
	CHECK(h.controller->is_feature_enabled());

	h.controller->set_feature_enabled(false);
	CHECK_FALSE(h.controller->is_feature_enabled());

	h.controller->set_feature_enabled(true);
	CHECK(h.controller->is_feature_enabled());

	h.unmount();
}

TEST_CASE("[Editor][script-tab-in-scene-pane] Script and scene tabs coexist in one pane") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspacePane *pane = TestSceneWorkspace::get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);
	ScenePaneTile *scene_tile = pane->get_scene_tile();
	REQUIRE(scene_tile != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane->set_tab_registry(&registry);

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);
	WorkspaceTab scene_tab = scene_type->make_tab("res://level.tscn", registry.allocate_stable_id());
	pane->add_tab(scene_tab);

	const String path = write_temp_text_file("mixed.txt", "func run(): pass");
	REQUIRE_FALSE(path.is_empty());
	open_script_tab(pane, registry, path);
	h.pump();

	CHECK(pane->get_tab_count() == 2);
	CHECK(pane->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(pane->get_tab(1).get_type_id() == StringName("script"));

	// Activate the script tab: script chrome mounts, scene chrome hides.
	pane->set_active_tab(1);
	h.pump();
	CHECK(scene_tile->is_visible() == false);
	ScriptLeaf *mounted = find_mounted_script_leaf(pane);
	REQUIRE(mounted != nullptr);
	CHECK(mounted->get_script_path() == path);

	// Activate the scene tab: scene chrome shows, the script surface is released.
	pane->set_active_tab(0);
	h.pump();
	CHECK(scene_tile->is_visible());
	CHECK(find_mounted_script_leaf(pane) == nullptr);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[Editor][script-tab-move-between-panes] Moving a script tab keeps its state and never prompts") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	WorkspacePane *pane_a = TestSceneWorkspace::get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = TestSceneWorkspace::get_leaf_pane(leaf_b);
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_b != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane_a->set_tab_registry(&registry);
	pane_b->set_tab_registry(&registry);

	const String path = write_temp_text_file("move.txt", "line0\nline1\nline2\nline3\n");
	REQUIRE_FALSE(path.is_empty());
	open_script_tab(pane_a, registry, path);
	h.pump();

	// Place the caret on a non-default line, then confirm the pre-move state.
	ScriptLeaf *leaf_before = find_mounted_script_leaf(pane_a);
	REQUIRE(leaf_before != nullptr);
	ScriptEditorView *view_before = leaf_before->get_script_editor_view();
	REQUIRE(view_before != nullptr);
	TabContainer *tabs_before = view_before->get_tab_container();
	REQUIRE(tabs_before->get_tab_count() == 1);
	ScriptEditorBase *se_before = Object::cast_to<ScriptEditorBase>(tabs_before->get_tab_control(0));
	REQUIRE(se_before != nullptr);
	se_before->get_code_editor()->get_text_editor()->set_caret_line(2);
	h.pump();
	String pre_path;
	int pre_line = -1;
	int pre_column = -1;
	REQUIRE(view_before->get_current_script_view_state(pre_path, pre_line, pre_column));
	CHECK(pre_path == path);
	// Non-trivial caret so the round-trip assertion below is meaningful.
	CHECK(pre_line > 0);

	// Move the tab to the other pane using the generic move primitives.
	WorkspaceTab moved = pane_a->take_tab(0);
	pane_b->add_tab(moved);
	h.pump();

	CHECK(pane_a->get_tab_count() == 0);
	CHECK(pane_b->get_tab_count() == 1);

	// The moved tab re-mounts in pane B with its caret/scroll/fold state intact.
	ScriptLeaf *leaf_after = find_mounted_script_leaf(pane_b);
	REQUIRE(leaf_after != nullptr);
	ScriptEditorView *view_after = leaf_after->get_script_editor_view();
	REQUIRE(view_after != nullptr);
	CHECK(view_after->get_open_editor_for_path(path) != nullptr);
	String post_path;
	int post_line = -1;
	int post_column = -1;
	REQUIRE(view_after->get_current_script_view_state(post_path, post_line, post_column));
	CHECK(post_path == path);
	CHECK(post_line == pre_line);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[Editor][script-tab-dirty-close-prompts] Closing a dirty script tab routes to the save prompt") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspacePane *pane = TestSceneWorkspace::get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane->set_tab_registry(&registry);

	const String path = write_temp_text_file("dirty.txt", "content\n");
	REQUIRE_FALSE(path.is_empty());
	open_script_tab(pane, registry, path);
	h.pump();

	ScriptLeaf *leaf = find_mounted_script_leaf(pane);
	REQUIRE(leaf != nullptr);
	ScriptEditorView *view = leaf->get_script_editor_view();
	REQUIRE(view != nullptr);
	TabContainer *tabs = view->get_tab_container();
	REQUIRE(tabs->get_tab_count() == 1);
	ScriptEditorBase *se = Object::cast_to<ScriptEditorBase>(tabs->get_tab_control(0));
	REQUIRE(se != nullptr);

	// Introduce an unsaved edit so the close path must prompt.
	se->get_code_editor()->get_text_editor()->insert_text_at_caret("dirty");
	h.pump();
	REQUIRE(se->is_unsaved());

	ConfirmationDialog *prompt = find_erase_confirm(view);
	REQUIRE(prompt != nullptr);

	// Closing a dirty tab defers to the existing save/discard/cancel prompt.
	WorkspaceTabCloseResult result = pane->request_close_active_tab();
	CHECK(result == WorkspaceTabCloseResult::DEFERRED);
	CHECK(pane->get_tab_count() == 1);

	// Cancel keeps the tab.
	prompt->emit_signal(SNAME("canceled"));
	h.pump();
	CHECK(pane->get_tab_count() == 1);

	// Re-request, then Discard: the workspace tab and its canonical entry go away.
	result = pane->request_close_active_tab();
	CHECK(result == WorkspaceTabCloseResult::DEFERRED);
	prompt->emit_signal(SNAME("custom_action"), "discard");
	h.pump();
	CHECK(pane->get_tab_count() == 0);
	WorkspaceTab discarded;
	WorkspaceTabLocation discarded_location;
	CHECK_FALSE(registry.find_canonical(StringName("script"), path, discarded, discarded_location));

	// Save path also removes the workspace tab once the prompt confirms.
	const String save_path = write_temp_text_file("dirty_save.txt", "content\n");
	REQUIRE_FALSE(save_path.is_empty());
	open_script_tab(pane, registry, save_path);
	h.pump();
	ScriptLeaf *save_leaf = find_mounted_script_leaf(pane);
	REQUIRE(save_leaf != nullptr);
	ScriptEditorView *save_view = save_leaf->get_script_editor_view();
	REQUIRE(save_view != nullptr);
	ScriptEditorBase *save_se = Object::cast_to<ScriptEditorBase>(save_view->get_tab_container()->get_tab_control(0));
	REQUIRE(save_se != nullptr);
	save_se->get_code_editor()->get_text_editor()->insert_text_at_caret("dirty");
	h.pump();
	REQUIRE(save_se->is_unsaved());

	ConfirmationDialog *save_prompt = find_erase_confirm(save_view);
	REQUIRE(save_prompt != nullptr);
	result = pane->request_close_active_tab();
	CHECK(result == WorkspaceTabCloseResult::DEFERRED);
	save_prompt->emit_signal(SNAME("confirmed"));
	h.pump();
	CHECK(pane->get_tab_count() == 0);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[Editor][script-tab-focus-routes-controller] Focusing a script tab routes the controller to its view") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	WorkspacePane *pane_a = TestSceneWorkspace::get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = TestSceneWorkspace::get_leaf_pane(leaf_b);
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_b != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane_a->set_tab_registry(&registry);
	pane_b->set_tab_registry(&registry);

	const String path_a = write_temp_text_file("focus_a.txt", "a\n");
	const String path_b = write_temp_text_file("focus_b.txt", "b\n");
	REQUIRE_FALSE(path_a.is_empty());
	REQUIRE_FALSE(path_b.is_empty());
	open_script_tab(pane_a, registry, path_a);
	open_script_tab(pane_b, registry, path_b);
	h.pump();

	// Focusing pane A's script tab makes the global controller resolve to its view.
	pane_a->on_focus_entered();
	String path;
	int line = 0;
	int column = 0;
	REQUIRE(controller->get_current_script_view_state(path, line, column));
	CHECK(path == path_a);

	// Focusing pane B's script tab moves the controller's active view accordingly.
	pane_b->on_focus_entered();
	REQUIRE(controller->get_current_script_view_state(path, line, column));
	CHECK(path == path_b);

	h.unmount();
	memdelete(controller);
}

} // namespace TestScriptEditorViews
