/**************************************************************************/
/*  test_scene_workspace.h                                                */
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

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/groups_dock.h"
#include "editor/docks/groups_editor.h"
#include "editor/docks/history_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_workspace_leaf_content.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/scene/scene_tree_editor.h"
#include "editor/script/script_editor_controller.h"
#include "editor/themes/editor_scale.h"
#include "editor/workspace/scene_tab.h"
#include "editor/workspace/script_resource_tab.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_bar.h"
#include "editor/workspace/workspace_tab_registry.h"
#include "editor/workspace/workspace_tab_type.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

static WorkspacePane *get_leaf_pane(WorkspaceLeafNode *p_leaf) {
	return p_leaf ? p_leaf->get_workspace_pane() : nullptr;
}

static ScriptLeaf *get_leaf_script(WorkspaceLeafNode *p_leaf) {
	WorkspacePane *pane = get_leaf_pane(p_leaf);
	return pane ? pane->get_script_leaf() : nullptr;
}

static int count_workspace_tabs_of_type(EditorSceneWorkspace *p_workspace, const StringName &p_type) {
	if (!p_workspace) {
		return 0;
	}
	int count = 0;
	for (WorkspaceLeafNode *leaf : p_workspace->get_leaves()) {
		WorkspacePane *pane = get_leaf_pane(leaf);
		if (!pane) {
			continue;
		}
		for (int i = 0; i < pane->get_tab_count(); i++) {
			if (pane->get_tab(i).get_type_id() == p_type) {
				count++;
			}
		}
	}
	return count;
}

static String write_temp_workspace_text_file(const String &p_name, const String &p_source) {
	const String dir = OS::get_singleton()->get_cache_path().path_join("scene_workspace_scripts");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), String());
	da->make_dir_recursive(dir);
	const String path = dir.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V(file.is_null(), String());
	file->store_string(p_source);
	return path;
}

class RecordingTabType : public WorkspaceTabType {
public:
	int mount_count = 0;
	int unmount_count = 0;
	mutable int last_mounted_stable_id = -1;
	mutable int last_unmounted_stable_id = -1;
	// 1 if the tab was the currently-mounted one when request_close ran, else 0.
	mutable int last_close_mounted = -1;

	StringName type_id() const override { return StringName("recording"); }
	bool can_open(const String &p_resource) const override { return true; }
	WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const override {
		WorkspaceTab tab;
		tab.set_stable_id(p_stable_id);
		tab.set_type_id(type_id());
		tab.set_resource_key(p_resource);
		tab.set_title_cache(p_resource);
		return tab;
	}
	String get_title(const WorkspaceTab &p_tab) const override { return p_tab.get_title_cache(); }
	Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const override { return Ref<Texture2D>(); }
	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override {
		mount_count++;
		last_mounted_stable_id = p_tab.get_stable_id();
	}
	void unmount(WorkspaceTab &p_tab) override {
		unmount_count++;
		last_unmounted_stable_id = p_tab.get_stable_id();
	}
	void activate(WorkspaceTab &p_tab) override {}
	WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) override {
		last_close_mounted = (last_mounted_stable_id == p_tab.get_stable_id()) ? 1 : 0;
		return WorkspaceTabCloseResult::CLOSE;
	}
	Dictionary save_payload(const WorkspaceTab &p_tab) const override { return Dictionary(); }
	void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const override {}
};

class RecordingSceneTabType : public SceneTabType {
public:
	int requested_close_scene = -1;
	WorkspaceTabCloseResult close_result = WorkspaceTabCloseResult::DEFERRED;

protected:
	WorkspaceTabCloseResult request_editor_close(int p_scene_idx) override {
		requested_close_scene = p_scene_idx;
		return close_result;
	}
};

// Counts close requests and can be told which tri-state result to return, so
// tests can assert a move never routes through request_close and that a cancel
// leaves everything intact.
class PromptSpyTabType : public WorkspaceTabType {
public:
	int close_request_count = 0;
	WorkspaceTabCloseResult close_result = WorkspaceTabCloseResult::CLOSE;

	StringName type_id() const override { return StringName("prompt_spy"); }
	bool can_open(const String &p_resource) const override { return true; }
	WorkspaceTab make_tab(const String &p_resource, int p_stable_id) const override {
		WorkspaceTab tab;
		tab.set_stable_id(p_stable_id);
		tab.set_type_id(type_id());
		tab.set_resource_key(p_resource);
		tab.set_title_cache(p_resource);
		return tab;
	}
	String get_title(const WorkspaceTab &p_tab) const override { return p_tab.get_title_cache(); }
	Ref<Texture2D> get_icon(const WorkspaceTab &p_tab) const override { return Ref<Texture2D>(); }
	void mount(WorkspaceTab &p_tab, Control *p_chrome_host) override {}
	void unmount(WorkspaceTab &p_tab) override {}
	void activate(WorkspaceTab &p_tab) override {}
	WorkspaceTabCloseResult request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close = Callable()) override {
		close_request_count++;
		return close_result;
	}
	Dictionary save_payload(const WorkspaceTab &p_tab) const override { return Dictionary(); }
	void restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const override {}
};

class LeafRemovedTracker : public Object {
	FOUNDRY_CLASS(LeafRemovedTracker, Object);

public:
	int removed_leaf_id = -1;
	int successor_leaf_id = -1;

	void on_leaf_removed(int p_leaf_id, int p_successor_leaf_id) {
		removed_leaf_id = p_leaf_id;
		successor_leaf_id = p_successor_leaf_id;
	}
};

struct WorkspaceHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		workspace = EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data);
		host->add_child(workspace);
		workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		workspace->set_size(Size2(800, 600));
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(workspace);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tile-self-contained") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);

	REQUIRE(tile->get_scene_tabs() != nullptr);
	REQUIRE(tile->get_scene_tree_dock() != nullptr);
	REQUIRE(tile->get_inspector_dock() != nullptr);
	REQUIRE(tile->get_content_host() != nullptr);
	CHECK(tile->is_ancestor_of(tile->get_scene_tabs()));
	CHECK(tile->is_ancestor_of(tile->get_scene_tree_dock()));
	CHECK(tile->is_ancestor_of(tile->get_inspector_dock()));
	CHECK(tile->is_ancestor_of(tile->get_content_host()));
	CHECK(tile->get_scene_tabs()->get_tile_id() == tile->get_tile_id());

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *root = memnew(Node2D);
	root->set_name("TileScene");
	context->set_scene_root_node(root);
	Node2D *child = memnew(Node2D);
	child->set_name("TileChild");
	root->add_child(child);

	SceneTreeDock *tree_dock = tile->get_scene_tree_dock();
	InspectorDock *inspector_dock = tile->get_inspector_dock();

	tree_dock->set_scene_context(context);
	inspector_dock->set_scene_context(context);
	h.pump();

	CHECK(tree_dock->get_scene_context() == context);
	CHECK(inspector_dock->get_scene_context() == context);
	CHECK(String(tree_dock->get_scene_context()->get_scene_root_node()->get_name()) == "TileScene");

	context->activate(SceneTree::get_singleton()->get_root());
	Vector<Node *> select;
	select.push_back(child);
	tree_dock->set_selection(select);
	h.pump();
	CHECK(context->get_selection()->is_selected(child));

	EditorSceneContext *other_context = memnew(EditorSceneContext);
	Node2D *other_root = memnew(Node2D);
	other_context->set_scene_root_node(other_root);
	tree_dock->set_scene_context(other_context);
	inspector_dock->set_scene_context(other_context);
	h.pump();

	CHECK(tree_dock->get_scene_context() == other_context);
	CHECK(inspector_dock->get_scene_context() == other_context);
	CHECK(context->get_selection()->is_selected(child));

	tree_dock->set_scene_context(nullptr);
	inspector_dock->set_scene_context(nullptr);
	inspector_dock->update(nullptr);
	h.pump();

	CHECK(tree_dock->get_scene_context() == nullptr);
	CHECK(inspector_dock->get_scene_context() == nullptr);

	// Rebind to a fresh context after the previous binding was cleared.
	EditorSceneContext *replacement = memnew(EditorSceneContext);
	tree_dock->set_scene_context(replacement);
	inspector_dock->set_scene_context(replacement);
	h.pump();
	CHECK(tree_dock->get_scene_context() == replacement);

	context->deactivate();
	memdelete(context);
	memdelete(replacement);
	memdelete(other_context);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tree-update-skips-detached-root") {
	SceneTreeEditor *detached_editor = memnew(SceneTreeEditor(false, false, false));
	{
		ErrorDetector error_detector;
		detached_editor->update_warning();
		CHECK_FALSE(error_detector.has_error);
	}
	memdelete(detached_editor);

	Window *tree_root = SceneTree::get_singleton()->get_root();
	SceneTreeEditor *editor = memnew(SceneTreeEditor(false, false, false));
	tree_root->add_child(editor);

	Node2D *root = memnew(Node2D);
	root->set_name("DetachedRoot");
	tree_root->add_child(root);
	SceneTree::get_singleton()->set_edited_scene_root(root);

	tree_root->remove_child(root);
	ErrorDetector error_detector;
	editor->update_tree();
	CHECK_FALSE(error_detector.has_error);

	SceneTree::get_singleton()->set_edited_scene_root(nullptr);
	MessageQueue::get_singleton()->flush();
	memdelete(root);
	tree_root->remove_child(editor);
	memdelete(editor);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tile-isolation-across-leaves") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *child_a = memnew(Node2D);
	root_a->add_child(child_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *child_b = memnew(Node2D);
	root_b->add_child(child_b);

	context_a->activate(SceneTree::get_singleton()->get_root());
	context_b->activate(SceneTree::get_singleton()->get_root());

	tile_a->get_scene_tree_dock()->set_scene_context(context_a);
	tile_a->get_inspector_dock()->set_scene_context(context_a);
	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);

	Vector<Node *> select_a;
	select_a.push_back(child_a);
	tile_a->get_scene_tree_dock()->set_selection(select_a);
	h.pump();

	CHECK(context_a->get_selection()->is_selected(child_a));
	CHECK_FALSE(context_b->get_selection()->is_selected(child_b));

	Vector<Node *> select_b;
	select_b.push_back(child_b);
	tile_b->get_scene_tree_dock()->set_selection(select_b);
	h.pump();

	CHECK(context_b->get_selection()->is_selected(child_b));
	// Selecting in tile B must not alter tile A's bound context or selection.
	CHECK(context_a->get_selection()->is_selected(child_a));
	CHECK_FALSE(context_b->get_selection()->is_selected(child_a));
	CHECK(tile_a->get_scene_tree_dock()->get_scene_context() == context_a);
	CHECK(tile_b->get_scene_tree_dock()->get_scene_context() == context_b);

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tree-split-collapse") {
	WorkspaceHarness h;
	h.mount();

	CHECK(h.workspace->get_leaf_count() == 1);
	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	REQUIRE(first != nullptr);

	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	CHECK(h.workspace->get_leaf_count() == 2);

	WorkspaceLeafNode *third = h.workspace->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_FIRST);
	h.pump();
	REQUIRE(third != nullptr);
	CHECK(h.workspace->get_leaf_count() == 3);

	HashSet<int> ids;
	for (WorkspaceLeafNode *leaf : h.workspace->get_leaves()) {
		ids.insert(leaf->get_leaf_id());
	}
	CHECK(ids.size() == 3);

	const int third_id = third->get_leaf_id();

	// Collapse promotes the sibling and removes the single-child split.
	h.workspace->collapse(third);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 2);
	CHECK(h.workspace->get_leaf_by_id(third_id) == nullptr);

	h.workspace->collapse(second);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(first->get_parent() == h.workspace);

	// Collapsing the only leaf is a no-op.
	h.workspace->collapse(first);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);

	h.unmount();
}

static void replace_leaf_with_script(WorkspaceLeafNode *p_leaf, const String &p_title) {
	ERR_FAIL_NULL(p_leaf);
	WorkspacePane *old_pane = get_leaf_pane(p_leaf);
	ERR_FAIL_NULL(old_pane);
	WorkspaceLeafContent *previous = p_leaf->take_leaf_content();
	if (previous) {
		memdelete(previous->get_root_control());
	}
	WorkspacePane *pane = memnew(WorkspacePane);
	pane->setup(p_leaf->get_leaf_id(), old_pane->get_editor_selection(), old_pane->get_editor_data(), StringName("script"));
	pane->get_script_leaf()->set_tab_title(p_title);
	p_leaf->set_leaf_content(pane);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tree-move") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	REQUIRE(first->get_leaf_content() != nullptr);
	REQUIRE(second->get_leaf_content() != nullptr);
	CHECK(get_leaf_pane(first)->is_scene_pane());
	CHECK(get_leaf_pane(second)->is_scene_pane());

	replace_leaf_with_script(first, "pane_a");
	CHECK(get_leaf_pane(first)->is_script_pane());

	CHECK(h.workspace->move_content(first, second));
	CHECK(get_leaf_pane(first)->is_scene_pane());
	CHECK(get_leaf_pane(second)->is_script_pane());
	CHECK(second->get_leaf_content()->get_tab_title() == "pane_a");

	CHECK(h.workspace->move_content(first, second));
	CHECK(get_leaf_pane(first)->is_script_pane());
	CHECK(get_leaf_pane(second)->is_scene_pane());

	CHECK_FALSE(h.workspace->move_content(first, first));

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tree-persist") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspaceLeafNode *third = h.workspace->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(h.workspace->get_leaf_count() == 3);

	replace_leaf_with_script(second, "aux");
	h.workspace->set_focused_leaf(second->get_leaf_id());

	WorkspaceSplitNode *root_split = Object::cast_to<WorkspaceSplitNode>(h.workspace->get_child(0, false));
	REQUIRE(root_split != nullptr);
	root_split->set_split_offset(180);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	CHECK(int(config->get_value("Workspace", "node_count")) == 5);
	CHECK(int(config->get_value("Workspace", "focused_leaf_id")) == second->get_leaf_id());
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	const Vector<int> expected_leaf_ids = { first->get_leaf_id(), second->get_leaf_id(), third->get_leaf_id() };
	const int saved_focus = second->get_leaf_id();
	const String saved_aux = "aux";

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 3);
	for (int leaf_id : expected_leaf_ids) {
		CHECK(h2.workspace->get_leaf_by_id(leaf_id) != nullptr);
	}
	CHECK(h2.workspace->get_focused_leaf_id() == saved_focus);

	WorkspaceLeafNode *restored_script = nullptr;
	for (WorkspaceLeafNode *leaf : h2.workspace->get_leaves()) {
		if (WorkspacePane *pane = get_leaf_pane(leaf)) {
			if (pane->is_script_pane()) {
				restored_script = leaf;
			}
		}
	}
	REQUIRE(restored_script != nullptr);
	CHECK(restored_script->get_leaf_content()->get_tab_title() == saved_aux);
	CHECK(restored_script->get_leaf_id() == saved_focus);

	WorkspaceSplitNode *restored_root = Object::cast_to<WorkspaceSplitNode>(h2.workspace->get_child(0, false));
	REQUIRE(restored_root != nullptr);
	CHECK(restored_root->get_split_offset() == 180);

	WorkspaceLeafNode *fourth = h2.workspace->split(h2.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h2.pump();
	for (int leaf_id : expected_leaf_ids) {
		CHECK(fourth->get_leaf_id() != leaf_id);
	}

	h2.unmount();
}

static int add_test_scene(EditorData &p_data, int p_tile_id, Node2D *p_root = nullptr) {
	p_data.register_tile(p_tile_id);
	p_data.set_focused_tile_id(p_tile_id);
	const int idx = p_data.add_edited_scene(-1);
	if (p_data.get_scene_tile(idx) != p_tile_id) {
		p_data.set_scene_tile(idx, p_tile_id);
	}
	p_data.set_edited_scene(idx);
	if (p_root) {
		EditorSceneContext *context = p_data.get_scene_context(idx);
		context->set_scene_root_node(p_root);
	}
	return idx;
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] restored-script-leaf-associated-scenes-resolve-all-leaves") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	replace_leaf_with_script(leaf_a, "script_a");
	replace_leaf_with_script(leaf_b, "script_b");

	ScriptLeaf *script_a = get_leaf_script(leaf_a);
	ScriptLeaf *script_b = get_leaf_script(leaf_b);
	REQUIRE(script_a != nullptr);
	REQUIRE(script_b != nullptr);

	Node2D *stale_a = memnew(Node2D);
	stale_a->set_scene_file_path("res://scene_a.tscn");
	script_a->set_associated_scene_root(stale_a);
	memdelete(stale_a);

	Node2D *stale_b = memnew(Node2D);
	stale_b->set_scene_file_path("res://scene_b.tscn");
	script_b->set_associated_scene_root(stale_b);
	memdelete(stale_b);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, leaf_a->get_leaf_id(), root_a);
	h.editor_data.set_scene_path(scene_a, "res://scene_a.tscn");

	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, leaf_b->get_leaf_id(), root_b);
	h.editor_data.set_scene_path(scene_b, "res://scene_b.tscn");

	h.workspace->set_focused_leaf(leaf_a->get_leaf_id());
	h.workspace->resolve_script_leaf_associated_scenes(h.editor_data);

	CHECK(script_a->get_associated_scene_root() == root_a);
	CHECK(script_b->get_associated_scene_root() == root_b);

	h.unmount();
}

static void check_focus_invariant(const EditorData &p_data, const EditorSceneWorkspace *p_workspace) {
	const int focused_tile = p_data.get_focused_tile_id();
	CHECK(p_data.get_edited_scene() == p_data.get_tile_current_scene(focused_tile));
	if (p_workspace) {
		CHECK(focused_tile == p_workspace->get_focused_leaf_id());
	}
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] focus-invariant") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	h.editor_data.register_tile(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.pump();

	h.editor_data.set_tile_current_scene(tile_a, scene_a);
	h.editor_data.set_edited_scene(scene_a);
	check_focus_invariant(h.editor_data, h.workspace);

	WorkspaceLeafNode *second = h.workspace->split(h.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	const int tile_b = second->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.pump();

	h.workspace->set_focused_leaf(tile_b);
	h.editor_data.set_focused_tile_id(tile_b);
	h.editor_data.set_edited_scene(scene_c);
	check_focus_invariant(h.editor_data, h.workspace);

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_edited_scene(scene_b);
	check_focus_invariant(h.editor_data, h.workspace);

	const int collapsed_id = tile_b;
	WorkspaceLeafNode *survivor = h.workspace->get_focused_leaf();
	h.workspace->collapse(second);
	h.pump();
	h.editor_data.migrate_tile_scenes(collapsed_id, survivor->get_leaf_id());
	h.editor_data.unregister_tile(collapsed_id);
	check_focus_invariant(h.editor_data, h.workspace);
	CHECK(h.editor_data.get_tile_scene_indices(survivor->get_leaf_id()).has(scene_c));

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] collapse-focused-leaf-with-split-sibling-migrates-to-surviving-tile") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	WorkspaceLeafNode *leaf_c = h.workspace->split(leaf_b, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_c != nullptr);
	const int tile_c = leaf_c->get_leaf_id();
	h.editor_data.register_tile(tile_c);

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_tile_current_scene(tile_a, scene_a);
	h.editor_data.set_edited_scene(scene_a);
	check_focus_invariant(h.editor_data, h.workspace);

	LeafRemovedTracker tracker;
	h.workspace->connect("leaf_removed", callable_mp(&tracker, &LeafRemovedTracker::on_leaf_removed));

	h.workspace->collapse(leaf_a);
	h.pump();

	CHECK(tracker.removed_leaf_id == tile_a);
	CHECK(tracker.successor_leaf_id != tile_a);
	CHECK(h.workspace->get_leaf_by_id(tracker.successor_leaf_id) != nullptr);
	CHECK((tracker.successor_leaf_id == tile_b || tracker.successor_leaf_id == tile_c));

	if (tracker.successor_leaf_id != tile_a && h.workspace->get_leaf_by_id(tracker.successor_leaf_id)) {
		h.editor_data.migrate_tile_scenes(tile_a, tracker.successor_leaf_id);
		h.editor_data.unregister_tile(tile_a);
		CHECK(h.editor_data.get_tile_scene_indices(tracker.successor_leaf_id).has(scene_a));
	}

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] collapse-preserves-surface-relocated-to-survivor") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	// A shared singleton (the 2D/3D scene editors) is parked in the focused tile's
	// content host. When that tile collapses, EditorNode relocates the surface onto a
	// surviving scene tile first (via _focus_tile) so it is neither destroyed with the
	// freed leaf nor left orphaned. This models that relocation and checks the surface
	// survives, stays parented in the survivor, and is not freed by the collapse.
	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	ScenePaneTile *tile_a = leaf_a->get_pane_tile();
	ScenePaneTile *tile_b = leaf_b->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	Control *scene_mode = memnew(Control);
	tile_a->get_content_host()->add_child(scene_mode);
	const ObjectID scene_mode_id = scene_mode->get_instance_id();
	REQUIRE(leaf_a->is_ancestor_of(scene_mode));

	// Relocate the surface onto the surviving tile before collapsing the emptied one.
	scene_mode->get_parent()->remove_child(scene_mode);
	tile_b->get_content_host()->add_child(scene_mode);
	h.workspace->set_focused_leaf(leaf_b->get_leaf_id());

	h.workspace->collapse(leaf_a);
	h.pump();

	// The surface outlives the collapse and remains parented under the survivor, so it
	// is later freed with the workspace tree rather than leaked.
	REQUIRE(ObjectDB::get_instance(scene_mode_id) != nullptr);
	CHECK(tile_b->is_ancestor_of(scene_mode));

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tile-id-model") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.pump();

	CHECK(h.editor_data.get_edited_scenes()[scene_a].tile_id == tile_a);
	CHECK(h.editor_data.get_edited_scenes()[scene_b].tile_id == tile_a);
	CHECK(h.editor_data.get_tile_current_scene(tile_a) == scene_b);

	WorkspaceLeafNode *second = h.workspace->split(h.workspace->get_focused_leaf(), true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	const int tile_b = second->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.pump();

	CHECK(h.editor_data.get_edited_scenes()[scene_a].tile_id == tile_a);
	CHECK(h.editor_data.get_edited_scenes()[scene_c].tile_id == tile_b);
	CHECK(h.editor_data.get_tile_scene_indices(tile_a).size() == 2);
	CHECK(h.editor_data.get_tile_scene_indices(tile_b).size() == 1);

	h.workspace->move_content(h.workspace->get_leaf_by_id(tile_a), second);
	h.pump();
	CHECK(get_leaf_pane(second)->is_scene_pane());

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	const Vector<int> saved_tile_ids = { tile_a, tile_b };
	const int saved_focus = h.workspace->get_focused_leaf_id();

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	for (int tile_id : saved_tile_ids) {
		CHECK(h2.workspace->get_leaf_by_id(tile_id) != nullptr);
	}
	CHECK(h2.workspace->get_focused_leaf_id() == saved_focus);

	WorkspaceLeafNode *fourth = h2.workspace->split(h2.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h2.pump();
	for (int tile_id : saved_tile_ids) {
		CHECK(fourth->get_leaf_id() != tile_id);
	}
	CHECK(fourth->get_leaf_id() > tile_b);

	h2.unmount();
}

static void reparent_stand_in_main_screen(Control *p_main_screen, ScenePaneTile *p_tile) {
	ERR_FAIL_NULL(p_main_screen);
	ERR_FAIL_NULL(p_tile);
	Control *host = p_tile->get_content_host();
	ERR_FAIL_NULL(host);
	if (p_main_screen->get_parent() == host) {
		return;
	}
	if (p_main_screen->get_parent()) {
		p_main_screen->get_parent()->remove_child(p_main_screen);
	}
	host->add_child(p_main_screen);
	p_main_screen->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

static void attach_tile_scene_display(
		EditorData &p_data,
		EditorSceneWorkspace *p_workspace,
		SubViewportContainer *p_live_container,
		int p_focused_tile_id) {
	for (int i = 0; i < p_data.get_edited_scene_count(); i++) {
		EditorSceneContext *ctx = p_data.get_scene_context(i);
		const int tile_id = p_data.get_scene_tile(i);
		ScenePaneTile *tile = p_workspace->get_tile_by_id(tile_id);
		const bool is_tile_current = tile && p_data.get_tile_current_scene(tile_id) == i;

		if (!is_tile_current) {
			if (ctx->is_active()) {
				ctx->deactivate();
			}
			continue;
		}

		const bool is_focused_tile = tile_id == p_focused_tile_id;
		if (is_focused_tile) {
			tile->set_preview_mode(TilePreviewMode::FOCUSED_LIVE);
			ctx->set_display_parent(p_live_container, true, true);
			ctx->get_viewport()->set_update_mode(SubViewport::UPDATE_ALWAYS);
		} else if (ctx->scene_has_3d_content()) {
			tile->set_preview_mode(TilePreviewMode::LIVE_3D);
			SubViewportContainer *context_host = tile->get_context_viewport_host();
			ctx->set_display_parent(context_host, false);
			tile->bind_3d_preview_world(ctx->get_world_3d());
			tile->apply_3d_preview_camera_state(Dictionary());
			ctx->get_viewport()->set_update_mode(context_host->is_visible_in_tree() ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED);
			context_host->recalc_force_viewport_sizes();
			context_host->queue_redraw();
		} else {
			tile->set_preview_mode(TilePreviewMode::LIVE_2D);
			SubViewportContainer *preview = tile->get_preview_container();
			ctx->set_display_parent(preview, false);
			ctx->get_viewport()->set_update_mode(preview->is_visible_in_tree() ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED);
			preview->recalc_force_viewport_sizes();
			preview->queue_redraw();
		}
	}
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] reparent-render") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	const int tile_a_id = tile_a->get_tile_id();
	const int tile_b_id = tile_b->get_tile_id();
	h.editor_data.register_tile(tile_a_id);
	h.editor_data.register_tile(tile_b_id);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a_id, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b_id, root_b);
	h.pump();

	PanelContainer *main_screen = memnew(PanelContainer);
	main_screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	SubViewportContainer *live_container = memnew(SubViewportContainer);
	live_container->set_stretch(true);
	live_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	live_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen->add_child(live_container);

	auto check_focused_live = [&](int p_focused_tile_id, int p_focused_scene_idx, int p_preview_scene_idx, int p_preview_tile_id) {
		reparent_stand_in_main_screen(main_screen, h.workspace->get_tile_by_id(p_focused_tile_id));
		h.workspace->set_focused_leaf(p_focused_tile_id);
		h.editor_data.set_focused_tile_id(p_focused_tile_id);
		attach_tile_scene_display(h.editor_data, h.workspace, live_container, p_focused_tile_id);
		h.pump();

		EditorSceneContext *focused_ctx = h.editor_data.get_scene_context(p_focused_scene_idx);
		EditorSceneContext *preview_ctx = h.editor_data.get_scene_context(p_preview_scene_idx);
		ScenePaneTile *preview_tile = h.workspace->get_tile_by_id(p_preview_tile_id);

		CHECK(main_screen->get_parent() == h.workspace->get_tile_by_id(p_focused_tile_id)->get_content_host());
		CHECK(focused_ctx->get_viewport()->get_parent() == live_container);
		CHECK(focused_ctx->get_viewport()->get_update_mode() == SubViewport::UPDATE_ALWAYS);
		CHECK(focused_ctx->get_viewport()->is_inside_tree());
		CHECK(preview_ctx->get_viewport()->get_parent() == preview_tile->get_preview_container());
		CHECK(preview_ctx->get_viewport()->get_update_mode() == SubViewport::UPDATE_ALWAYS);
		CHECK(preview_ctx->get_viewport()->is_inside_tree());
	};

	check_focused_live(tile_a_id, scene_a, scene_b, tile_b_id);

	// Focus switch must keep both tiles rendering (no stale UPDATE_DISABLED black panes).
	check_focused_live(tile_b_id, scene_b, scene_a, tile_a_id);

	// Switch back to the first tile and verify the live path again.
	check_focused_live(tile_a_id, scene_a, scene_b, tile_b_id);

	for (int i = 0; i < h.editor_data.get_edited_scene_count(); i++) {
		h.editor_data.get_scene_context(i)->deactivate();
	}
	if (main_screen->get_parent()) {
		main_screen->get_parent()->remove_child(main_screen);
	}
	memdelete(main_screen);
	h.editor_data.clear_edited_scenes();

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] drop-region-select") {
	const Size2 pane(400, 300);

	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 150)) == EditorSceneWorkspace::DROP_CENTER);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 140)) == EditorSceneWorkspace::DROP_CENTER);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(190, 150)) == EditorSceneWorkspace::DROP_CENTER);

	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 150)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 150)) == EditorSceneWorkspace::DROP_RIGHT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 10)) == EditorSceneWorkspace::DROP_TOP);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 290)) == EditorSceneWorkspace::DROP_BOTTOM);

	// Near-corner: dominant axis wins (|dx| vs |dy|).
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 10)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 10)) == EditorSceneWorkspace::DROP_RIGHT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 290)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 290)) == EditorSceneWorkspace::DROP_RIGHT);

	// On the diagonal of a square pane, relative offsets break the tie.
	const Size2 square(400, 400);
	CHECK(EditorSceneWorkspace::drop_region_at(square, Point2(50, 50)) == EditorSceneWorkspace::DROP_LEFT);

	// Preview rects cover real halves / whole pane.
	const Rect2 left_preview = EditorSceneWorkspace::drop_preview_rect(pane, EditorSceneWorkspace::DROP_LEFT);
	CHECK(left_preview.size.x == doctest::Approx(pane.x * 0.5f));
	CHECK(left_preview.size.y == doctest::Approx(pane.y));

	const Rect2 center_preview = EditorSceneWorkspace::drop_preview_rect(pane, EditorSceneWorkspace::DROP_CENTER);
	CHECK(center_preview.size == pane);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] preview-camera-state") {
	ScenePaneTile *tile = memnew(ScenePaneTile);
	EditorSelection selection;
	EditorData editor_data;
	tile->setup(0, &selection, editor_data);

	Dictionary viewport_state;
	viewport_state["position"] = Vector3(1, 2, 3);
	viewport_state["x_rotation"] = 0.0;
	viewport_state["y_rotation"] = 0.0;
	viewport_state["distance"] = 8.0;
	tile->apply_3d_preview_camera_state(viewport_state);

	Camera3D *camera = tile->get_preview_3d_camera();
	REQUIRE(camera != nullptr);
	const Vector3 expected_origin = camera->get_transform().origin;
	CHECK(expected_origin.is_equal_approx(Vector3(1, 2, 11)));

	memdelete(tile);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] leaf-content-generic") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	REQUIRE(scene_leaf->get_leaf_content() != nullptr);
	CHECK(scene_leaf->get_leaf_content()->get_content_type() == StringName("pane"));
	CHECK(scene_leaf->get_pane_tile() != nullptr);
	CHECK(get_leaf_pane(scene_leaf)->is_scene_pane());

	WorkspaceLeafNode *script_leaf_node = h.workspace->split(scene_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(script_leaf_node != nullptr);
	replace_leaf_with_script(script_leaf_node, "ScriptPane");
	CHECK(script_leaf_node->get_leaf_content()->get_content_type() == StringName("pane"));
	CHECK(get_leaf_pane(script_leaf_node)->is_script_pane());
	CHECK(script_leaf_node->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(scene_leaf->get_leaf_content()->get_scene_context() == nullptr); // no open scene in harness

	CHECK(h.workspace->move_content(scene_leaf, script_leaf_node));
	CHECK(get_leaf_pane(scene_leaf)->is_script_pane());
	CHECK(get_leaf_pane(script_leaf_node)->is_scene_pane());
	CHECK(script_leaf_node->get_pane_tile() != nullptr);

	WorkspaceLeafNode *third = h.workspace->split(script_leaf_node, true, EditorSceneWorkspace::SPLIT_SIDE_FIRST);
	h.pump();
	REQUIRE(third != nullptr);
	CHECK(h.workspace->get_leaf_count() == 3);

	h.workspace->collapse(third);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 2);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	const int scene_leaf_id = scene_leaf->get_leaf_id();
	const int script_leaf_id = script_leaf_node->get_leaf_id();
	const String saved_script_title = scene_leaf->get_leaf_content()->get_tab_title();

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 2);
	WorkspaceLeafNode *restored_scene = h2.workspace->get_leaf_by_id(script_leaf_id);
	WorkspaceLeafNode *restored_script = h2.workspace->get_leaf_by_id(scene_leaf_id);
	REQUIRE(restored_scene != nullptr);
	REQUIRE(restored_script != nullptr);
	CHECK(get_leaf_pane(restored_scene)->is_scene_pane());
	CHECK(get_leaf_pane(restored_script)->is_script_pane());
	CHECK(restored_script->get_leaf_content()->get_tab_title() == saved_script_title);
	CHECK(restored_scene->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(restored_script->get_leaf_content()->get_scene_context() == nullptr);

	h2.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-hosts-mixed-tabs") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane->set_tab_registry(&registry);

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	WorkspaceTab scene_tab = scene_type->make_tab("res://a.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab("res://player.fs", registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);

	CHECK(pane->get_tab_count() == 2);
	CHECK(pane->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(pane->get_tab(1).get_type_id() == StringName("script"));

	ScenePaneTile *scene_tile = pane->get_scene_tile();
	REQUIRE(scene_tile != nullptr);
	pane->set_active_tab(1);
	CHECK(scene_tile->get_parent() == pane->get_chrome_host());
	CHECK(scene_tile->is_visible() == false);
	pane->set_active_tab(0);
	CHECK(scene_tile->is_visible());

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-active-tab-switch-mounts") {
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);

	WorkspacePane *pane = memnew(WorkspacePane);
	host->add_child(pane);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingTabType recording_type;
	registry.register_type(&recording_type);
	pane->set_tab_registry(&registry);

	WorkspaceTab first = recording_type.make_tab("first", registry.allocate_stable_id());
	WorkspaceTab second = recording_type.make_tab("second", registry.allocate_stable_id());
	pane->add_tab(first);
	pane->add_tab(second);
	CHECK(recording_type.mount_count == 1);
	CHECK(recording_type.last_mounted_stable_id == first.get_stable_id());

	pane->set_active_tab(1);
	CHECK(recording_type.unmount_count == 1);
	CHECK(recording_type.last_unmounted_stable_id == first.get_stable_id());
	CHECK(recording_type.mount_count == 2);
	CHECK(recording_type.last_mounted_stable_id == second.get_stable_id());

	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-remove-active-tab") {
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);

	WorkspacePane *pane = memnew(WorkspacePane);
	host->add_child(pane);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingTabType recording_type;
	registry.register_type(&recording_type);
	pane->set_tab_registry(&registry);

	WorkspaceTab first = recording_type.make_tab("first", registry.allocate_stable_id());
	WorkspaceTab second = recording_type.make_tab("second", registry.allocate_stable_id());
	WorkspaceTab third = recording_type.make_tab("third", registry.allocate_stable_id());
	pane->add_tab(first);
	pane->add_tab(second);
	pane->add_tab(third);
	CHECK(pane->get_active_tab_index() == 0);

	pane->remove_tab(0);
	CHECK(pane->get_tab_count() == 2);
	CHECK(pane->get_active_tab_index() == 0);
	CHECK(pane->get_tab(0).get_resource_key() == "second");
	CHECK(recording_type.mount_count >= 2);

	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-empty-placeholder") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);
	REQUIRE(pane->get_scene_tile() != nullptr);
	CHECK(pane->get_scene_tile()->get_scene_tabs()->is_visible());
	CHECK(pane->get_empty_placeholder()->is_visible());
	CHECK(pane->get_tab_strip()->is_visible() == false);
	CHECK(pane->get_scene_context() == nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane->set_tab_registry(&registry);

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);
	WorkspaceTab scene_tab = scene_type->make_tab("res://only.tscn", registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	CHECK(pane->get_empty_placeholder()->is_visible() == false);
	CHECK(pane->get_tab_strip()->is_visible());

	pane->remove_tab(0);
	CHECK(pane->get_tab_count() == 0);
	CHECK(pane->get_empty_placeholder()->is_visible());
	CHECK(pane->get_tab_strip()->is_visible() == false);
	CHECK(pane->get_scene_context() == nullptr);

	Ref<ConfigFile> config;
	config.instantiate();
	pane->save_layout(config, "PaneEmpty");
	pane->load_layout(config, "PaneEmpty");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-scene-only-layout-renders") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);

	Node2D *root = memnew(Node2D);
	const int scene_idx = add_test_scene(h.editor_data, leaf->get_leaf_id(), root);
	h.editor_data.set_scene_path(scene_idx, "res://pane_scene.tscn");
	h.workspace->sync_scene_tabs_from_editor_data();
	h.pump();

	CHECK(pane->get_leaf_id() == leaf->get_leaf_id());
	CHECK(pane->get_scene_tile() != nullptr);
	CHECK(pane->get_scene_tile()->get_tile_id() == leaf->get_leaf_id());
	CHECK(Math::is_equal_approx(pane->get_scene_tile()->get_anchor(SIDE_LEFT), real_t(Control::ANCHOR_BEGIN)));
	CHECK(Math::is_equal_approx(pane->get_scene_tile()->get_anchor(SIDE_TOP), real_t(Control::ANCHOR_BEGIN)));
	CHECK(Math::is_equal_approx(pane->get_scene_tile()->get_anchor(SIDE_RIGHT), real_t(Control::ANCHOR_END)));
	CHECK(Math::is_equal_approx(pane->get_scene_tile()->get_anchor(SIDE_BOTTOM), real_t(Control::ANCHOR_END)));
	for (int i = 0; i < 4 && pane->get_chrome_host()->get_size().is_zero_approx(); i++) {
		h.pump();
	}
	CHECK(pane->get_chrome_host()->get_size().x > 0);
	CHECK(pane->get_chrome_host()->get_size().y > 0);
	CHECK(pane->get_scene_tile()->get_rect().is_equal_approx(Rect2(Point2(), pane->get_chrome_host()->get_size())));
	h.pump();

	CHECK(pane->get_scene_context() != nullptr);
	CHECK(String(h.editor_data.get_scene_path(scene_idx)) == "res://pane_scene.tscn");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tab-open-reveal") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);

	Node2D *root = memnew(Node2D);
	const int scene = add_test_scene(h.editor_data, leaf->get_leaf_id(), root);
	h.editor_data.set_scene_path(scene, "res://already_open.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	h.pump();
	CHECK(pane->get_tab_count() == 1);
	CHECK(pane->get_tab(0).get_resource_key() == "res://already_open.tscn");
	REQUIRE(pane->get_scene_tile() != nullptr);
	CHECK(pane->get_tab_strip()->is_visible());
	CHECK_FALSE(pane->get_scene_tile()->get_scene_tabs()->is_visible());
	CHECK(pane->get_chrome_host()->get_size().x > 0);
	CHECK(pane->get_chrome_host()->get_size().y > 0);
	CHECK(pane->get_scene_tile()->get_rect().is_equal_approx(Rect2(Point2(), pane->get_chrome_host()->get_size())));
	CHECK(pane->get_scene_tile()->get_content_host()->get_size().x > 0);
	CHECK(pane->get_scene_tile()->get_content_host()->get_size().y > 0);

	CHECK(h.workspace->focus_scene_tab(scene));
	h.workspace->sync_scene_tabs_from_editor_data();
	h.pump();
	CHECK(pane->get_tab_count() == 1);
	CHECK(pane->get_scene_tile()->get_rect().is_equal_approx(Rect2(Point2(), pane->get_chrome_host()->get_size())));

	WorkspaceTab found;
	WorkspaceTabLocation location;
	CHECK(WorkspacePane::get_shared_tab_registry().find_canonical(StringName("scene"), SceneTabType::resource_key_for_scene(h.editor_data, scene), found, location));
	CHECK(location.pane_id == leaf->get_leaf_id());
	CHECK(location.tab_index == 0);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] passive-pane-sync-keeps-pending-scene-current") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, leaf->get_leaf_id(), root_a);
	h.editor_data.set_scene_path(scene_a, "res://scene_a.tscn");
	h.workspace->sync_scene_tabs_from_editor_data();
	h.pump();
	REQUIRE(pane->get_tab_count() == 1);
	CHECK(h.editor_data.get_edited_scene() == scene_a);

	const int pending_scene = h.editor_data.add_edited_scene(-1);
	REQUIRE(pending_scene != scene_a);
	CHECK(h.editor_data.get_edited_scene() == pending_scene);

	pane->sync_from_editor_data();
	CHECK(h.editor_data.get_edited_scene() == pending_scene);
	CHECK(h.editor_data.get_tile_current_scene(leaf->get_leaf_id()) == pending_scene);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tab-activate-sets-current") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_id = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_id, root_a);
	h.editor_data.set_scene_path(scene_a, "res://scene_a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_id, root_b);
	h.editor_data.set_scene_path(scene_b, "res://scene_b.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);
	REQUIRE(pane->get_tab_count() == 2);

	pane->set_active_tab(0);
	CHECK(h.editor_data.get_tile_current_scene(tile_id) == scene_a);
	CHECK(h.editor_data.get_edited_scene() == scene_a);
	check_focus_invariant(h.editor_data, h.workspace);

	pane->set_active_tab(1);
	CHECK(h.editor_data.get_tile_current_scene(tile_id) == scene_b);
	CHECK(h.editor_data.get_edited_scene() == scene_b);
	check_focus_invariant(h.editor_data, h.workspace);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tab-move-keeps-edit-state") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	REQUIRE(leaf_a != nullptr);
	const int tile_a = leaf_a->get_leaf_id();

	Node2D *root = memnew(Node2D);
	Node2D *selected = memnew(Node2D);
	root->add_child(selected);
	const int scene = add_test_scene(h.editor_data, tile_a, root);
	h.editor_data.set_scene_path(scene, "res://move_me.tscn");
	Vector<ObjectID> selected_ids;
	selected_ids.push_back(selected->get_instance_id());
	h.editor_data.get_scene_context(scene)->set_selected_node_ids(selected_ids);
	EditorUndoRedoManager::get_singleton()->set_history_as_unsaved(h.editor_data.get_scene_history_id(scene));

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	h.workspace->sync_scene_tabs_from_editor_data();
	REQUIRE(h.workspace->handle_scene_drop(scene, leaf_b, EditorSceneWorkspace::DROP_CENTER) == leaf_b);
	CHECK(h.workspace->focus_scene_tab(scene));

	CHECK(h.editor_data.get_scene_tile(scene) == tile_b);
	CHECK(h.editor_data.get_edited_scene_root(scene) == root);
	CHECK(h.editor_data.get_scene_context(scene)->get_selected_node_ids().has(selected->get_instance_id()));
	CHECK(EditorUndoRedoManager::get_singleton()->is_history_unsaved(h.editor_data.get_scene_history_id(scene)));

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tab-close-runs-flow") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingSceneTabType scene_type;
	registry.register_type(&scene_type);
	pane->set_tab_registry(&registry);

	Node2D *root = memnew(Node2D);
	const int scene = add_test_scene(h.editor_data, leaf->get_leaf_id(), root);
	h.editor_data.set_scene_path(scene, "res://dirty_close.tscn");

	pane->sync_scene_tabs_from_editor_data();
	REQUIRE(pane->get_tab_count() == 1);

	const WorkspaceTabCloseResult result = pane->request_close_tab(0);
	CHECK(result == WorkspaceTabCloseResult::DEFERRED);
	CHECK(scene_type.requested_close_scene == scene);
	CHECK(pane->get_tab_count() == 1);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] scene-tab-order-matches-editordata") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_id = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_id, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_id, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");
	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_id, root_c);
	h.editor_data.set_scene_path(scene_c, "res://c.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);
	REQUIRE(pane->get_tab_count() == 3);

	pane->move_tab(0, 2);

	Vector<String> pane_keys;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		pane_keys.push_back(pane->get_tab(i).get_resource_key());
	}

	Vector<String> editor_data_keys;
	for (int scene_idx : h.editor_data.get_tile_scene_indices(tile_id)) {
		editor_data_keys.push_back(SceneTabType::resource_key_for_scene(h.editor_data, scene_idx));
	}

	CHECK(pane_keys == editor_data_keys);
	CHECK(pane_keys[0] == "res://b.tscn");
	CHECK(pane_keys[1] == "res://c.tscn");
	CHECK(pane_keys[2] == "res://a.tscn");

	h.unmount();
}

class TileHistoryDockTestAccess {
public:
	static void refresh(HistoryDock *p_dock) { p_dock->refresh_history(); }
	static String newest_action_name(HistoryDock *p_dock) {
		if (p_dock->action_list->get_item_count() <= 1) {
			return String();
		}
		return p_dock->action_list->get_item_text(0);
	}
};

class TileConnectionsDockTestAccess {
public:
	static Object *selected_object(SignalsDock *p_dock) {
		return p_dock->connections ? p_dock->connections->selected_object : nullptr;
	}
};

static int _right_tab_index(TabContainer *p_tabs, EditorDock *p_dock) {
	if (!p_tabs || !p_dock) {
		return -1;
	}
	for (int i = 0; i < p_tabs->get_tab_count(); i++) {
		if (p_tabs->get_tab_control(i) == p_dock) {
			return i;
		}
	}
	return -1;
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tile-dock-region") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(tile_a->get_signals_dock() != nullptr);
	CHECK(tile_a->get_groups_dock() != nullptr);
	CHECK(tile_a->get_history_dock() != nullptr);
	CHECK(tile_a->is_ancestor_of(tile_a->get_signals_dock()));
	CHECK(tile_a->is_ancestor_of(tile_a->get_groups_dock()));
	CHECK(tile_a->is_ancestor_of(tile_a->get_history_dock()));

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	context_a->set_history_id(42);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *node_a = memnew(Node2D);
	root_a->add_child(node_a);
	node_a->add_to_group("tile_a_group", false);

	ur_manager->create_action_for_history("Tile A action", 42);
	ur_manager->add_do_method(node_a, "set_name", "RenamedA");
	ur_manager->commit_action();

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(99);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);
	node_b->add_to_group("tile_b_group", false);

	ur_manager->create_action_for_history("Tile B action", 99);
	ur_manager->add_do_method(node_b, "set_name", "RenamedB");
	ur_manager->commit_action();

	tile_a->get_scene_tree_dock()->set_scene_context(context_a);
	tile_a->get_inspector_dock()->set_scene_context(context_a);
	tile_a->get_signals_dock()->set_scene_context(context_a);
	tile_a->get_groups_dock()->set_scene_context(context_a);
	tile_a->get_history_dock()->set_scene_context(context_a);

	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);
	tile_b->get_signals_dock()->set_scene_context(context_b);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	tile_b->get_history_dock()->set_scene_context(context_b);

	tile_a->get_signals_dock()->show();
	tile_a->get_groups_dock()->show();
	tile_a->get_history_dock()->show();
	tile_b->get_signals_dock()->show();
	tile_b->get_groups_dock()->show();
	tile_b->get_history_dock()->show();
	h.pump();

	// GroupsEditor defers group-tree rebuilds until visible; rebind after show().
	tile_a->get_groups_dock()->set_scene_context(context_a);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	h.pump();

	tile_a->get_signals_dock()->set_object(node_a);
	context_a->activate(tree_root);
	Vector<Node *> select_a;
	select_a.push_back(node_a);
	tile_a->get_groups_dock()->set_selection(select_a);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	TileHistoryDockTestAccess::refresh(tile_a->get_history_dock());
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A action");

	tile_b->get_signals_dock()->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	tile_b->get_groups_dock()->set_selection(select_b);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_b->get_signals_dock()) == node_b);
	CHECK(tile_b->get_groups_dock()->get_scene_context() == context_b);
	TileHistoryDockTestAccess::refresh(tile_b->get_history_dock());
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_b->get_history_dock()) == "Tile B action");

	// Tile B edits must not bleed into tile A's bound panels.
	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A action");

	TabContainer *right_tabs = tile_a->get_dock_region()->get_right_tabs();
	REQUIRE(right_tabs != nullptr);
	const int history_tab = _right_tab_index(right_tabs, tile_a->get_history_dock());
	REQUIRE(history_tab >= 0);
	right_tabs->set_current_tab(history_tab);

	HSplitContainer *body = tile_a->get_dock_region()->get_body();
	REQUIRE(body != nullptr);
	h.host->set_size(Size2(900, 600));
	h.pump();
	PackedInt32Array offsets = body->get_split_offsets();
	if (offsets.size() < 2) {
		offsets.resize(2);
	}
	offsets.write[0] = 220;
	offsets.write[1] = -240;
	body->set_split_offsets(offsets);
	h.pump();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	const String layout_section = EditorSceneWorkspace::leaf_layout_section(tile_a->get_tile_id());
	CHECK(config->has_section_key(layout_section, "tile_dock_right"));
	CHECK(int(config->get_value(layout_section, "tile_dock_hsplit_1")) == 220);
	CHECK(int(config->get_value(layout_section, "tile_dock_hsplit_2")) == -240);
	CHECK(int(config->get_value(layout_section, "tile_dock_right_selected_tab_idx")) == history_tab);

	const int tile_a_id = tile_a->get_tile_id();
	const int tile_b_id = tile_b->get_tile_id();

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	ScenePaneTile *restored_a = h2.workspace->get_tile_by_id(tile_a_id);
	ScenePaneTile *restored_b = h2.workspace->get_tile_by_id(tile_b_id);
	REQUIRE(restored_a != nullptr);
	REQUIRE(restored_b != nullptr);

	TabContainer *restored_tabs = restored_a->get_dock_region()->get_right_tabs();
	REQUIRE(restored_tabs != nullptr);
	const int restored_history_tab = _right_tab_index(restored_tabs, restored_a->get_history_dock());
	REQUIRE(restored_history_tab >= 0);
	CHECK(restored_tabs->get_current_tab() == restored_history_tab);

	HSplitContainer *restored_body = restored_a->get_dock_region()->get_body();
	REQUIRE(restored_body != nullptr);
	h2.host->set_size(Size2(900, 600));
	h2.pump();
	PackedInt32Array restored_offsets = restored_body->get_split_offsets();
	REQUIRE(restored_offsets.size() >= 2);
	CHECK(restored_offsets[0] == 220 * EDSCALE);
	CHECK(restored_offsets[1] == -240 * EDSCALE);

	h2.unmount();
	memdelete(ur_manager);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] script-leaf-open") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 0);

	const String player_path = write_temp_workspace_text_file("script_leaf_player.txt", "func run(): pass\n");
	const String enemy_path = write_temp_workspace_text_file("script_leaf_enemy.txt", "func chase(): pass\n");
	REQUIRE_FALSE(player_path.is_empty());
	REQUIRE_FALSE(enemy_path.is_empty());

	// Opening a script from the scene tile creates a script workspace tab beside it.
	WorkspaceLeafNode *script_leaf_node = h.workspace->open_script_leaf(scene_leaf, player_path);
	h.pump();
	REQUIRE(script_leaf_node != nullptr);
	CHECK(script_leaf_node != scene_leaf);
	CHECK(h.workspace->get_leaf_count() == 2);
	REQUIRE(script_leaf_node->get_leaf_content() != nullptr);
	CHECK(script_leaf_node->get_leaf_content()->get_content_type() == StringName("pane"));
	CHECK(get_leaf_pane(script_leaf_node)->is_script_pane());
	REQUIRE(get_leaf_pane(script_leaf_node)->get_tab_count() == 1);
	CHECK(get_leaf_pane(script_leaf_node)->get_tab(0).get_type_id() == StringName("script"));
	CHECK(get_leaf_pane(script_leaf_node)->get_tab(0).get_resource_key() == player_path);
	CHECK(script_leaf_node->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(h.workspace->get_script_leaf() == script_leaf_node);
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 1);

	// Opening a different script in a forced new leaf creates a second script tab host.
	WorkspaceLeafNode *second = h.workspace->open_script_leaf(scene_leaf, enemy_path, true);
	h.pump();
	CHECK(second != script_leaf_node);
	CHECK(h.workspace->get_leaf_count() == 3);
	CHECK(h.workspace->get_script_leaves().size() == 2);
	REQUIRE(get_leaf_pane(second) != nullptr);
	REQUIRE(get_leaf_pane(second)->get_tab_count() == 1);
	CHECK(get_leaf_pane(second)->get_tab(0).get_type_id() == StringName("script"));
	CHECK(get_leaf_pane(second)->get_tab(0).get_resource_key() == enemy_path);
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 2);

	// Each script leaf round-trips through persistence with its own tabs.
	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	const int scene_leaf_id = scene_leaf->get_leaf_id();
	const int enemy_leaf_id = second->get_leaf_id();

	h.unmount();
	memdelete(controller);

	WorkspaceHarness h2;
	h2.mount();
	h2.pump();
	ScriptEditorController *controller2 = memnew(ScriptEditorController);
	controller2->init_global_services(h2.host);
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 3);
	CHECK(h2.workspace->get_leaf_by_id(scene_leaf_id) != nullptr);
	CHECK(h2.workspace->get_script_leaves().size() == 2);
	WorkspaceLeafNode *restored_enemy = h2.workspace->get_leaf_by_id(enemy_leaf_id);
	REQUIRE(restored_enemy != nullptr);
	WorkspacePane *restored_enemy_pane = get_leaf_pane(restored_enemy);
	REQUIRE(restored_enemy_pane != nullptr);
	REQUIRE(restored_enemy_pane->get_tab_count() == 1);
	CHECK(restored_enemy_pane->get_tab(0).get_type_id() == StringName("script"));
	CHECK(restored_enemy_pane->get_tab(0).get_resource_key() == enemy_path);

	h2.unmount();
	memdelete(controller2);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] open-script-creates-workspace-tab") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 0);

	const String script_path = write_temp_workspace_text_file("workspace_open_script.txt", "func run(): pass\n");
	REQUIRE_FALSE(script_path.is_empty());

	WorkspaceLeafNode *script_host = h.workspace->open_script_leaf(scene_leaf, script_path);
	h.pump();
	REQUIRE(script_host != nullptr);

	WorkspacePane *pane = get_leaf_pane(script_host);
	REQUIRE(pane != nullptr);
	CHECK(pane->get_tab_count() == 1);
	if (pane->get_tab_count() == 1) {
		CHECK(pane->get_active_tab_index() == 0);
		CHECK(pane->get_tab(0).get_type_id() == StringName("script"));
		CHECK(pane->get_tab(0).get_resource_key() == script_path);
	}
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 1);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] reopening-script-tab-refreshes-associated-scene") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_a = h.workspace->get_focused_leaf();
	REQUIRE(scene_a != nullptr);
	WorkspaceLeafNode *scene_b = h.workspace->split(scene_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(scene_b != nullptr);

	Node2D *root_a = memnew(Node2D);
	root_a->set_scene_file_path("res://scene_a.tscn");
	const int scene_idx_a = add_test_scene(h.editor_data, scene_a->get_leaf_id(), root_a);
	h.editor_data.set_scene_path(scene_idx_a, "res://scene_a.tscn");
	Node2D *root_b = memnew(Node2D);
	root_b->set_scene_file_path("res://scene_b.tscn");
	const int scene_idx_b = add_test_scene(h.editor_data, scene_b->get_leaf_id(), root_b);
	h.editor_data.set_scene_path(scene_idx_b, "res://scene_b.tscn");

	const String script_path = write_temp_workspace_text_file("workspace_association.txt", "script association\n");
	REQUIRE_FALSE(script_path.is_empty());

	WorkspaceLeafNode *script_host = h.workspace->open_script_leaf(scene_a, script_path);
	h.pump();
	REQUIRE(script_host != nullptr);
	WorkspacePane *script_pane = get_leaf_pane(script_host);
	REQUIRE(script_pane != nullptr);
	REQUIRE(script_pane->get_tab_count() == 1);
	REQUIRE(script_pane->get_tab(0).get_type_id() == StringName("script"));
	ScriptResourceTabType *script_type = static_cast<ScriptResourceTabType *>(WorkspacePane::get_shared_tab_registry().find_type(StringName("script")));
	REQUIRE(script_type != nullptr);
	ScriptLeaf *script_leaf = script_type->get_mounted_script_leaf(script_pane->get_tab(0).get_stable_id());
	REQUIRE(script_leaf != nullptr);
	CHECK(script_leaf->get_associated_scene_root() == root_a);

	WorkspaceLeafNode *reused_host = h.workspace->open_script_leaf(scene_b, script_path);
	h.pump();
	CHECK(reused_host == script_host);
	CHECK(count_workspace_tabs_of_type(h.workspace, StringName("script")) == 1);
	CHECK(script_type->get_mounted_script_leaf(script_pane->get_tab(0).get_stable_id()) == script_leaf);
	CHECK(script_leaf->get_associated_scene_root() == root_b);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	const int scene_b_id = scene_b->get_leaf_id();
	const int script_host_id = script_host->get_leaf_id();

	h.unmount();
	memdelete(controller);

	WorkspaceHarness h2;
	h2.mount();
	h2.pump();

	ScriptEditorController *controller2 = memnew(ScriptEditorController);
	controller2->init_global_services(h2.host);
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspaceLeafNode *restored_scene_b = h2.workspace->get_leaf_by_id(scene_b_id);
	REQUIRE(restored_scene_b != nullptr);
	Node2D *restored_root_b = memnew(Node2D);
	restored_root_b->set_scene_file_path("res://scene_b.tscn");
	const int restored_scene_idx_b = add_test_scene(h2.editor_data, scene_b_id, restored_root_b);
	h2.editor_data.set_scene_path(restored_scene_idx_b, "res://scene_b.tscn");
	h2.workspace->resolve_script_leaf_associated_scenes(h2.editor_data);

	WorkspaceLeafNode *restored_script_host = h2.workspace->get_leaf_by_id(script_host_id);
	REQUIRE(restored_script_host != nullptr);
	WorkspacePane *restored_script_pane = get_leaf_pane(restored_script_host);
	REQUIRE(restored_script_pane != nullptr);
	REQUIRE(restored_script_pane->get_tab_count() == 1);
	REQUIRE(restored_script_pane->get_tab(0).get_type_id() == StringName("script"));
	ScriptLeaf *restored_script_leaf = script_type->get_mounted_script_leaf(restored_script_pane->get_tab(0).get_stable_id());
	REQUIRE(restored_script_leaf != nullptr);
	CHECK(restored_script_leaf->get_associated_scene_path() == "res://scene_b.tscn");
	CHECK(restored_script_leaf->get_associated_scene_root() == restored_root_b);

	h2.unmount();
	memdelete(controller2);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] script-leaf-collapse-keeps-scene-focus") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_a = h.workspace->get_focused_leaf();
	REQUIRE(scene_a != nullptr);

	// Two scene tiles, with a script leaf opened beside the second one, so that
	// scene_b's collapse successor is the (non-scene) script leaf.
	WorkspaceLeafNode *scene_b = h.workspace->split(scene_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(scene_b != nullptr);
	WorkspaceLeafNode *script_leaf = h.workspace->open_script_leaf(scene_b, "res://a.fs");
	h.pump();
	REQUIRE(script_leaf != nullptr);
	CHECK(script_leaf->get_pane_tile() == nullptr);

	// Collapsing the focused scene tile promotes the script leaf structurally, but
	// focus must land on a scene tile rather than the non-focusable script leaf.
	h.workspace->set_focused_leaf(scene_b->get_leaf_id());
	h.workspace->collapse(scene_b);
	h.pump();

	WorkspaceLeafNode *focused = h.workspace->get_focused_leaf();
	REQUIRE(focused != nullptr);
	CHECK(focused->get_pane_tile() != nullptr);
	CHECK(get_leaf_pane(focused)->is_scene_pane());
	CHECK(h.workspace->get_script_leaf() == script_leaf);

	h.unmount();
}

static SignalsDock *_focused_signals_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_signals_dock() : nullptr;
}

static GroupsDock *_focused_groups_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_groups_dock() : nullptr;
}

static HistoryDock *_focused_history_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_history_dock() : nullptr;
}

static SceneTreeDock *_focused_scene_tree_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_scene_tree_dock() : nullptr;
}

static InspectorDock *_focused_inspector_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_inspector_dock() : nullptr;
}

class TileInspectorDockTestAccess {
public:
	static String info_button_text(InspectorDock *p_dock) {
		return p_dock && p_dock->info ? p_dock->info->get_text() : String();
	}
};

TEST_CASE("[SceneWorkspace][SceneTree][Editor] focused-scenetree-inspector-accessor") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(_focused_scene_tree_dock(h.workspace) == tile_a->get_scene_tree_dock());
	CHECK(_focused_inspector_dock(h.workspace) == tile_a->get_inspector_dock());

	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	CHECK(_focused_scene_tree_dock(h.workspace) == tile_b->get_scene_tree_dock());
	CHECK(_focused_inspector_dock(h.workspace) == tile_b->get_inspector_dock());

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);

	SceneTreeDock *scene_tree = _focused_scene_tree_dock(h.workspace);
	InspectorDock *inspector = _focused_inspector_dock(h.workspace);
	REQUIRE(scene_tree == tile_b->get_scene_tree_dock());
	REQUIRE(inspector == tile_b->get_inspector_dock());

	scene_tree->set_filter("FocusedTileFilter");
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	scene_tree->set_selection(select_b);
	inspector->set_info("FocusedTileInfo", "Focused tile inspector message", false);
	h.pump();

	CHECK(scene_tree->get_filter() == "FocusedTileFilter");
	CHECK(context_b->get_selection()->is_selected(node_b));
	CHECK(TileInspectorDockTestAccess::info_button_text(inspector) == "FocusedTileInfo");

	context_b->deactivate();
	memdelete(context_b);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] focused-dock-accessor") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(_focused_signals_dock(h.workspace) == tile_a->get_signals_dock());
	CHECK(_focused_groups_dock(h.workspace) == tile_a->get_groups_dock());
	CHECK(_focused_history_dock(h.workspace) == tile_a->get_history_dock());

	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	CHECK(_focused_signals_dock(h.workspace) == tile_b->get_signals_dock());
	CHECK(_focused_groups_dock(h.workspace) == tile_b->get_groups_dock());
	CHECK(_focused_history_dock(h.workspace) == tile_b->get_history_dock());

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(77);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	ur_manager->create_action_for_history("Focused tile action", 77);
	ur_manager->add_do_method(node_b, "set_name", "FocusedB");
	ur_manager->commit_action();

	tile_b->get_signals_dock()->set_scene_context(context_b);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	tile_b->get_history_dock()->set_scene_context(context_b);

	SignalsDock *signals = _focused_signals_dock(h.workspace);
	GroupsDock *groups = _focused_groups_dock(h.workspace);
	HistoryDock *history = _focused_history_dock(h.workspace);
	REQUIRE(signals == tile_b->get_signals_dock());
	REQUIRE(groups == tile_b->get_groups_dock());
	REQUIRE(history == tile_b->get_history_dock());

	signals->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	groups->set_selection(select_b);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(signals) == node_b);
	CHECK(groups->get_scene_context() == context_b);
	TileHistoryDockTestAccess::refresh(history);
	CHECK(TileHistoryDockTestAccess::newest_action_name(history) == "Focused tile action");

	context_b->deactivate();
	memdelete(context_b);

	h.unmount();
	memdelete(ur_manager);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] stale-singleton-guard") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(tile_a->get_signals_dock() != tile_b->get_signals_dock());
	CHECK(tile_a->get_groups_dock() != tile_b->get_groups_dock());
	CHECK(tile_a->get_history_dock() != tile_b->get_history_dock());

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	context_a->set_history_id(11);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *node_a = memnew(Node2D);
	root_a->add_child(node_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(22);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	ur_manager->create_action_for_history("Tile A stale guard", 11);
	ur_manager->add_do_method(node_a, "set_name", "A");
	ur_manager->commit_action();
	ur_manager->create_action_for_history("Tile B stale guard", 22);
	ur_manager->add_do_method(node_b, "set_name", "B");
	ur_manager->commit_action();

	for (ScenePaneTile *tile : { tile_a, tile_b }) {
		EditorSceneContext *ctx = tile == tile_a ? context_a : context_b;
		tile->get_signals_dock()->set_scene_context(ctx);
		tile->get_groups_dock()->set_scene_context(ctx);
		tile->get_history_dock()->set_scene_context(ctx);
		tile->get_signals_dock()->show();
		tile->get_groups_dock()->show();
		tile->get_history_dock()->show();
	}
	h.pump();

	// Seed tile A through the focused-tile accessor path while tile A is focused.
	tile_a->get_signals_dock()->set_object(node_a);
	context_a->activate(tree_root);
	Vector<Node *> select_a;
	select_a.push_back(node_a);
	tile_a->get_groups_dock()->set_selection(select_a);
	TileHistoryDockTestAccess::refresh(tile_a->get_history_dock());
	tile_a->get_scene_tree_dock()->set_filter("TileAFilter");
	tile_a->get_inspector_dock()->set_info("TileAInfo", "Tile A inspector message", false);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A stale guard");
	CHECK(tile_a->get_scene_tree_dock()->get_filter() == "TileAFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_a->get_inspector_dock()) == "TileAInfo");

	// Focus tile B and edit only through focused-tile accessors.
	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	SignalsDock *focused_signals = _focused_signals_dock(h.workspace);
	GroupsDock *focused_groups = _focused_groups_dock(h.workspace);
	HistoryDock *focused_history = _focused_history_dock(h.workspace);
	SceneTreeDock *focused_scene_tree = _focused_scene_tree_dock(h.workspace);
	InspectorDock *focused_inspector = _focused_inspector_dock(h.workspace);
	REQUIRE(focused_signals == tile_b->get_signals_dock());
	REQUIRE(focused_groups == tile_b->get_groups_dock());
	REQUIRE(focused_history == tile_b->get_history_dock());
	REQUIRE(focused_scene_tree == tile_b->get_scene_tree_dock());
	REQUIRE(focused_inspector == tile_b->get_inspector_dock());

	focused_signals->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	focused_groups->set_selection(select_b);
	TileHistoryDockTestAccess::refresh(focused_history);
	focused_scene_tree->set_filter("TileBFilter");
	focused_inspector->set_info("TileBInfo", "Tile B inspector message", false);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_b->get_signals_dock()) == node_b);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_b->get_history_dock()) == "Tile B stale guard");
	CHECK(tile_b->get_scene_tree_dock()->get_filter() == "TileBFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_b->get_inspector_dock()) == "TileBInfo");

	// Tile A must remain unchanged — a global singleton repoint would have overwritten this.
	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A stale guard");
	CHECK(tile_a->get_scene_tree_dock()->get_filter() == "TileAFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_a->get_inspector_dock()) == "TileAInfo");

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();
	memdelete(ur_manager);
}

static WorkspaceTab add_script_tab(WorkspacePane *p_pane, const String &p_path) {
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	ERR_FAIL_NULL_V(script_type, WorkspaceTab());
	WorkspaceTab tab = script_type->make_tab(p_path, registry.allocate_stable_id());
	p_pane->add_tab(tab);
	return tab;
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] empty-script-tab-pane-collapses-after-center-drop") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(h.host);

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	WorkspacePane *scene_pane = get_leaf_pane(scene_leaf);
	REQUIRE(scene_pane != nullptr);

	WorkspaceLeafNode *script_leaf = h.workspace->split_with_content(scene_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND, StringName("script"));
	h.pump();
	REQUIRE(script_leaf != nullptr);
	WorkspacePane *script_pane = get_leaf_pane(script_leaf);
	REQUIRE(script_pane != nullptr);
	REQUIRE(script_pane->is_script_pane());
	ScriptLeaf *bridge_leaf = script_pane->get_script_leaf();
	REQUIRE(bridge_leaf != nullptr);
	REQUIRE(bridge_leaf->get_script_editor_view() != nullptr);

	const String move_path = write_temp_workspace_text_file("move.txt", "move\n");
	REQUIRE(!move_path.is_empty());
	add_script_tab(script_pane, move_path);
	h.pump();
	REQUIRE(script_pane->get_tab_count() == 1);
	REQUIRE(h.workspace->get_leaf_count() == 2);

	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(script_leaf->get_leaf_id(), 0, scene_leaf, EditorSceneWorkspace::DROP_CENTER);
	h.pump();

	REQUIRE(dest == scene_leaf);
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(scene_leaf->get_parent() == h.workspace);
	CHECK(Object::cast_to<WorkspaceLeafNode>(h.workspace->get_child(0, false)) == scene_leaf);
	CHECK(scene_pane->get_tab_count() == 1);
	CHECK(scene_pane->get_tab(0).get_type_id() == StringName("script"));
	CHECK(scene_pane->get_tab(0).get_resource_key() == move_path);

	h.unmount();
	memdelete(controller);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tab-move-center") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_a->get_tab_count() == 2);
	REQUIRE(pane_b->get_tab_count() == 0);

	// Center-drop scene_a's tab from pane A into pane B.
	const int move_index = pane_a->find_scene_tab_index(scene_a);
	REQUIRE(move_index >= 0);
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(tile_a, move_index, leaf_b, EditorSceneWorkspace::DROP_CENTER);
	CHECK(dest == leaf_b);

	// Appended to the destination and removed from the source.
	CHECK(pane_b->get_tab_count() == 1);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://a.tscn");
	CHECK(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "res://b.tscn");
	CHECK(h.editor_data.get_scene_tile(scene_a) == tile_b);
	CHECK(h.editor_data.get_scene_tile(scene_b) == tile_a);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tab-move-edge-splits") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);

	// Two script tabs so moving one out does not empty (and collapse) the source.
	add_script_tab(pane_a, "res://keep.fs");
	add_script_tab(pane_a, "res://move.fs");
	REQUIRE(pane_a->get_tab_count() == 2);
	const int move_index = 1;

	// Edge-drop onto the right of pane A splits it horizontally, new pane on the
	// second (right) side, and the tab moves into that new pane.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), move_index, leaf_a, EditorSceneWorkspace::DROP_RIGHT);
	h.pump();
	REQUIRE(dest != nullptr);
	CHECK(dest != leaf_a);
	CHECK(h.workspace->get_leaf_count() == 2);

	SplitContainer *sc = Object::cast_to<SplitContainer>(dest->get_parent());
	REQUIRE(sc != nullptr);
	WorkspaceSplitNode *split_node = Object::cast_to<WorkspaceSplitNode>(sc->get_parent());
	REQUIRE(split_node != nullptr);
	CHECK(split_node->is_vertical() == false); // DROP_RIGHT => horizontal split.
	CHECK(sc->get_child(1, false) == dest); // Second side hosts the new pane.

	WorkspacePane *dest_pane = get_leaf_pane(dest);
	REQUIRE(dest_pane != nullptr);
	CHECK(dest_pane->get_tab_count() == 1);
	CHECK(dest_pane->get_tab(0).get_resource_key() == "res://move.fs");
	CHECK(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "res://keep.fs");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tab-reorder-within-pane") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_id = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_id, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_id, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");
	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_id, root_c);
	h.editor_data.set_scene_path(scene_c, "res://c.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane->get_tab_count() == 3);

	// Intra-pane reorder does not add or remove panes.
	pane->move_tab(0, 2);
	CHECK(h.workspace->get_leaf_count() == 1);

	Vector<String> pane_keys;
	for (int i = 0; i < pane->get_tab_count(); i++) {
		pane_keys.push_back(pane->get_tab(i).get_resource_key());
	}
	CHECK(pane_keys[0] == "res://b.tscn");
	CHECK(pane_keys[1] == "res://c.tscn");
	CHECK(pane_keys[2] == "res://a.tscn");

	// The scene tab order for a scene tab reorder tracks EditorData membership.
	Vector<String> editor_data_keys;
	for (int scene_idx : h.editor_data.get_tile_scene_indices(tile_id)) {
		editor_data_keys.push_back(SceneTabType::resource_key_for_scene(h.editor_data, scene_idx));
	}
	CHECK(pane_keys == editor_data_keys);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] move-last-tab-collapses") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b->get_tab_count() == 1);

	// Focus the pane we are about to empty so the collapse must redirect focus.
	h.workspace->set_focused_leaf(tile_b);
	h.editor_data.set_focused_tile_id(tile_b);

	const int move_index = pane_b->find_scene_tab_index(scene_b);
	REQUIRE(move_index >= 0);
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(tile_b, move_index, leaf_a, EditorSceneWorkspace::DROP_CENTER);
	CHECK(dest == leaf_a);

	// Emptied source collapses on the deferred pass; focus lands on a scene tile.
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(h.workspace->get_leaf_by_id(tile_b) == nullptr);
	WorkspaceLeafNode *focused = h.workspace->get_focused_leaf();
	REQUIRE(focused != nullptr);
	CHECK(focused->get_pane_tile() != nullptr);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] close-last-tab-final-pane-empty") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	WorkspacePane *pane = get_leaf_pane(leaf);
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingTabType recording_type;
	registry.register_type(&recording_type);
	pane->set_tab_registry(&registry);

	WorkspaceTab only = recording_type.make_tab("only", registry.allocate_stable_id());
	pane->add_tab(only);
	REQUIRE(pane->get_tab_count() == 1);
	REQUIRE(h.workspace->get_leaf_count() == 1);

	// Closing the last tab in the only pane leaves the empty placeholder, no
	// collapse, no crash.
	ErrorDetector error_detector;
	const WorkspaceTabCloseResult result = pane->request_close_tab(0);
	h.pump();
	CHECK(result == WorkspaceTabCloseResult::CLOSE);
	CHECK(pane->get_tab_count() == 0);
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(pane->get_empty_placeholder()->is_visible());
	CHECK(pane->get_tab_strip()->is_visible() == false);
	CHECK_FALSE(error_detector.has_error);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] drag-never-prompts") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	// Script (generic) move must never route through request_close.
	{
		WorkspaceHarness h;
		h.mount();
		h.pump();

		WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
		WorkspacePane *pane_a = get_leaf_pane(leaf_a);

		WorkspaceTabRegistry registry;
		registry.reset_stable_id_counter();
		registry.clear_canonical_index();
		PromptSpyTabType spy_type;
		registry.register_type(&spy_type);
		pane_a->set_tab_registry(&registry);

		WorkspaceTab dirty = spy_type.make_tab("res://dirty.fs", registry.allocate_stable_id());
		WorkspaceTab keep = spy_type.make_tab("res://keep.fs", registry.allocate_stable_id());
		pane_a->add_tab(dirty);
		pane_a->add_tab(keep);

		WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
		h.pump();
		get_leaf_pane(leaf_b)->set_tab_registry(&registry);

		h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), 0, leaf_b, EditorSceneWorkspace::DROP_CENTER);
		h.pump();
		CHECK(spy_type.close_request_count == 0);
		CHECK(get_leaf_pane(leaf_b)->get_tab_count() == 1);

		h.unmount();
	}

	// A dirty scene move must not close the scene or clear its unsaved state.
	{
		WorkspaceHarness h;
		h.mount();
		h.pump();

		const int tile_a = h.workspace->get_focused_leaf_id();
		Node2D *root = memnew(Node2D);
		const int scene = add_test_scene(h.editor_data, tile_a, root);
		h.editor_data.set_scene_path(scene, "res://dirty.tscn");
		EditorUndoRedoManager::get_singleton()->set_history_as_unsaved(h.editor_data.get_scene_history_id(scene));

		WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
		WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
		h.pump();
		h.editor_data.register_tile(leaf_b->get_leaf_id());
		h.workspace->sync_scene_tabs_from_editor_data();

		const int move_index = get_leaf_pane(leaf_a)->find_scene_tab_index(scene);
		REQUIRE(move_index >= 0);
		h.workspace->handle_tab_drop(tile_a, move_index, leaf_b, EditorSceneWorkspace::DROP_CENTER);

		CHECK(h.editor_data.get_edited_scene_count() == 1); // Not closed.
		CHECK(EditorUndoRedoManager::get_singleton()->is_history_unsaved(h.editor_data.get_scene_history_id(scene)));

		h.unmount();
	}
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] close-cancel-no-collapse") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	REQUIRE(h.workspace->get_leaf_count() == 2);

	WorkspacePane *pane_a = get_leaf_pane(leaf_a);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	PromptSpyTabType spy_type;
	spy_type.close_result = WorkspaceTabCloseResult::CANCEL;
	registry.register_type(&spy_type);
	pane_a->set_tab_registry(&registry);

	WorkspaceTab only = spy_type.make_tab("res://keep.fs", registry.allocate_stable_id());
	pane_a->add_tab(only);
	REQUIRE(pane_a->get_tab_count() == 1);

	// A cancel from request_close leaves the pane and its tab intact.
	const WorkspaceTabCloseResult result = pane_a->request_close_tab(0);
	h.pump();
	CHECK(result == WorkspaceTabCloseResult::CANCEL);
	CHECK(spy_type.close_request_count == 1);
	CHECK(pane_a->get_tab_count() == 1);
	CHECK(h.workspace->get_leaf_count() == 2);
	CHECK(h.workspace->get_leaf_by_id(leaf_a->get_leaf_id()) != nullptr);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-move-no-crash") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ErrorDetector error_detector;

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	h.editor_data.register_tile(leaf_b->get_leaf_id());
	h.workspace->sync_scene_tabs_from_editor_data();

	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);

	// Mixed content: a scene tab and script tabs share pane A.
	add_script_tab(pane_a, "res://one.fs");
	add_script_tab(pane_a, "res://two.fs");
	CHECK(pane_a->get_tab_count() >= 2);

	// Move a script tab to pane B's edge (splits), then move the scene into pane B.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), pane_a->get_tab_count() - 1, leaf_b, EditorSceneWorkspace::DROP_BOTTOM);
	h.pump();
	REQUIRE(dest != nullptr);

	const int scene_move_index = pane_a->find_scene_tab_index(scene_a);
	if (scene_move_index >= 0) {
		h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), scene_move_index, leaf_b, EditorSceneWorkspace::DROP_CENTER);
		h.pump();
	}

	// Close a remaining tab in pane B, then let any collapse run.
	if (pane_b->get_tab_count() > 0) {
		pane_b->request_close_tab(0);
		h.pump();
	}

	CHECK(h.workspace->get_leaf_count() >= 1);
	CHECK_FALSE(error_detector.has_error);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] script-tab-mount-hides-legacy-bridge") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);

	// A script-created pane carries the legacy script_leaf bridge. Mounting a real
	// script resource tab into it must not also show that bridge: the tab type owns
	// its own surface, so showing the bridge would stack two script surfaces for
	// one active tab.
	WorkspaceLeafNode *script_leaf_node = h.workspace->open_script_leaf(scene_leaf, "res://player.fs");
	h.pump();
	REQUIRE(script_leaf_node != nullptr);
	WorkspacePane *pane = get_leaf_pane(script_leaf_node);
	REQUIRE(pane != nullptr);
	REQUIRE(pane->is_script_pane());
	ScriptLeaf *legacy_bridge = pane->get_script_leaf();
	REQUIRE(legacy_bridge != nullptr);

	add_script_tab(pane, "res://enemy.fs");
	h.pump();

	REQUIRE(pane->get_tab_count() == 2);
	REQUIRE(pane->get_active_tab_index() == 0);
	CHECK(pane->get_tab(0).get_resource_key() == "res://player.fs");
	CHECK(pane->get_tab(1).get_resource_key() == "res://enemy.fs");
	CHECK(legacy_bridge->is_visible() == false);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] tab-move-into-nonempty-pane-activates") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);

	add_script_tab(pane_a, "res://keep.fs");
	add_script_tab(pane_a, "res://move.fs");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, "res://existing.fs");
	REQUIRE(pane_b->get_tab_count() == 1);
	REQUIRE(pane_b->get_active_tab_index() == 0);

	// Center-drop "move.fs" into the already-populated pane B. add_tab only
	// auto-activates the first tab in a pane, so without an explicit activation the
	// moved tab would sit inactive/unmounted behind "existing.fs". A scene move
	// makes the moved scene current; a script move must do the same.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(leaf_a->get_leaf_id(), 1, leaf_b, EditorSceneWorkspace::DROP_CENTER);
	h.pump();
	CHECK(dest == leaf_b);
	REQUIRE(pane_b->get_tab_count() == 2);
	CHECK(pane_b->get_tab(1).get_resource_key() == "res://move.fs");
	CHECK(pane_b->get_active_tab_index() == 1);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] pane-insert-tab-at-index") {
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);

	WorkspacePane *pane = memnew(WorkspacePane);
	host->add_child(pane);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingTabType recording_type;
	registry.register_type(&recording_type);
	pane->set_tab_registry(&registry);

	WorkspaceTab first = recording_type.make_tab("first", registry.allocate_stable_id());
	WorkspaceTab second = recording_type.make_tab("second", registry.allocate_stable_id());
	pane->add_tab(first);
	pane->add_tab(second);
	REQUIRE(pane->get_tab_count() == 2);
	REQUIRE(pane->get_active_tab_index() == 0);

	// Insert ahead of the active tab: order becomes [mid, first, second] and the
	// active index shifts right to stay on "first".
	WorkspaceTab mid = recording_type.make_tab("mid", registry.allocate_stable_id());
	pane->insert_tab(0, mid);
	REQUIRE(pane->get_tab_count() == 3);
	CHECK(pane->get_tab(0).get_resource_key() == "mid");
	CHECK(pane->get_tab(1).get_resource_key() == "first");
	CHECK(pane->get_tab(2).get_resource_key() == "second");
	CHECK(pane->get_active_tab_index() == 1);

	// The inserted tab's canonical location tracks its index.
	WorkspaceTab found;
	WorkspaceTabLocation location;
	REQUIRE(registry.find_canonical(StringName("recording"), "mid", found, location));
	CHECK(location.tab_index == 0);
	REQUIRE(registry.find_canonical(StringName("recording"), "second", found, location));
	CHECK(location.tab_index == 2);

	// An out-of-range index clamps to the end.
	WorkspaceTab tail = recording_type.make_tab("tail", registry.allocate_stable_id());
	pane->insert_tab(999, tail);
	REQUIRE(pane->get_tab_count() == 4);
	CHECK(pane->get_tab(3).get_resource_key() == "tail");

	host->remove_child(pane);
	memdelete(pane);
	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-drop-inserts-generic-tab-at-index") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	add_script_tab(pane_a, "res://keep.fs");
	add_script_tab(pane_a, "res://move.fs");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, "res://x.fs");
	add_script_tab(pane_b, "res://y.fs");
	REQUIRE(pane_b->get_tab_count() == 2);

	// Drop "move.fs" (pane A index 1) into pane B at the hovered index 1: the strip
	// path lands it between "x" and "y" instead of appending like the overlay path.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_strip_drop(leaf_a->get_leaf_id(), 1, leaf_b->get_leaf_id(), 1);
	h.pump();
	CHECK(dest == leaf_b);
	REQUIRE(pane_b->get_tab_count() == 3);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://x.fs");
	CHECK(pane_b->get_tab(1).get_resource_key() == "res://move.fs");
	CHECK(pane_b->get_tab(2).get_resource_key() == "res://y.fs");
	CHECK(pane_b->get_active_tab_index() == 1);
	REQUIRE(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "res://keep.fs");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-drop-inserts-scene-tab-at-index") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);
	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.editor_data.set_scene_path(scene_c, "res://c.tscn");
	Node2D *root_d = memnew(Node2D);
	const int scene_d = add_test_scene(h.editor_data, tile_b, root_d);
	h.editor_data.set_scene_path(scene_d, "res://d.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_a->get_tab_count() == 2);
	REQUIRE(pane_b->get_tab_count() == 2);

	const int move_index = pane_a->find_scene_tab_index(scene_a);
	REQUIRE(move_index >= 0);
	// Insert scene_a's tab into pane B at index 1: [c, a, d]. Scene ownership moves
	// to tile B and the scene-tab order tracks the reordered EditorData membership.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_strip_drop(tile_a, move_index, tile_b, 1);
	h.pump();
	CHECK(dest == leaf_b);
	REQUIRE(pane_b->get_tab_count() == 3);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://c.tscn");
	CHECK(pane_b->get_tab(1).get_resource_key() == "res://a.tscn");
	CHECK(pane_b->get_tab(2).get_resource_key() == "res://d.tscn");
	// Ownership moved: pane A keeps only scene_b. (scene_a is a positional index
	// that the reorder above invalidates, so ownership is verified by tab identity.)
	REQUIRE(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "res://b.tscn");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-drop-scene-tab-into-mixed-pane") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);
	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.editor_data.set_scene_path(scene_c, "res://c.tscn");
	Node2D *root_d = memnew(Node2D);
	const int scene_d = add_test_scene(h.editor_data, tile_b, root_d);
	h.editor_data.set_scene_path(scene_d, "res://d.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	// Make pane B a mixed pane: scene tabs [c, d] plus a trailing script tab. A synced
	// pane groups its scene tabs ahead of script tabs.
	add_script_tab(pane_b, "res://helper.fs");
	REQUIRE(pane_b->get_tab_count() == 3);

	const int move_index = pane_a->find_scene_tab_index(scene_a);
	REQUIRE(move_index >= 0);
	// Drop scene_a at strip index 2 (past both scene tabs, before the script tab). The
	// full-strip index must translate to scene ordinal 2 so the reorder goes through
	// EditorData (not a visual-only pane reorder), landing scene order [c, d, a].
	WorkspaceLeafNode *dest = h.workspace->handle_tab_strip_drop(tile_a, move_index, tile_b, 2);
	h.pump();
	CHECK(dest == leaf_b);
	REQUIRE(pane_b->get_tab_count() == 4);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://c.tscn");
	CHECK(pane_b->get_tab(1).get_resource_key() == "res://d.tscn");
	CHECK(pane_b->get_tab(2).get_resource_key() == "res://a.tscn");
	CHECK(pane_b->get_tab(3).get_resource_key() == "res://helper.fs");

	// EditorData scene order for tile B is canonical [c, d, a] -- not left stale.
	Vector<String> tile_b_order;
	for (int scene_idx : h.editor_data.get_tile_scene_indices(tile_b)) {
		tile_b_order.push_back(SceneTabType::resource_key_for_scene(h.editor_data, scene_idx));
	}
	REQUIRE(tile_b_order.size() == 3);
	CHECK(tile_b_order[0] == "res://c.tscn");
	CHECK(tile_b_order[1] == "res://d.tscn");
	CHECK(tile_b_order[2] == "res://a.tscn");
	REQUIRE(pane_a->get_tab_count() == 1);
	CHECK(pane_a->get_tab(0).get_resource_key() == "res://b.tscn");

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-drop-rejects-scene-tab-onto-script-pane") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.editor_data.set_scene_path(scene_b, "res://b.tscn");
	h.workspace->sync_scene_tabs_from_editor_data();

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a->get_tab_count() == 2);

	// A script-only sibling pane cannot host a scene tab (it has no scene tile), so
	// the move is refused and nothing changes rather than dropping the scene.
	WorkspaceLeafNode *leaf_b = h.workspace->split_with_content(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND, StringName("script"));
	h.pump();
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	REQUIRE(pane_b->is_script_pane());

	const int move_index = pane_a->find_scene_tab_index(scene_a);
	REQUIRE(move_index >= 0);
	WorkspaceLeafNode *dest = h.workspace->handle_tab_strip_drop(tile_a, move_index, leaf_b->get_leaf_id(), 0);
	CHECK(dest == nullptr);
	CHECK(pane_a->get_tab_count() == 2);
	CHECK(h.editor_data.get_scene_tile(scene_a) == tile_a);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-drop-collapses-emptied-source") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	add_script_tab(pane_a, "res://solo.fs");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, "res://existing.fs");
	REQUIRE(h.workspace->get_leaf_count() == 2);

	const int leaf_a_id = leaf_a->get_leaf_id();
	// Moving the source pane's only tab out empties it, so it collapses on the
	// deferred pass and the workspace is left with the destination pane.
	WorkspaceLeafNode *dest = h.workspace->handle_tab_strip_drop(leaf_a_id, 0, leaf_b->get_leaf_id(), 0);
	h.pump();
	CHECK(dest == leaf_b);
	REQUIRE(pane_b->get_tab_count() == 2);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://solo.fs");
	CHECK(pane_b->get_tab(1).get_resource_key() == "res://existing.fs");
	CHECK(pane_b->get_active_tab_index() == 0);
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(h.workspace->get_leaf_by_id(leaf_a_id) == nullptr);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] strip-tab-bar-shared-rearrange-group") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	add_script_tab(pane_a, "res://keep.fs");
	add_script_tab(pane_a, "res://move.fs");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, "res://existing.fs");
	h.pump();

	WorkspaceTabBar *bar_a = Object::cast_to<WorkspaceTabBar>(pane_a->get_tab_strip());
	WorkspaceTabBar *bar_b = Object::cast_to<WorkspaceTabBar>(pane_b->get_tab_strip());
	REQUIRE(bar_a != nullptr);
	REQUIRE(bar_b != nullptr);

	// Every pane's strip shares one rearrange group so a tab dragged from one strip
	// is accepted by another (TabBar rejects cross-bar drops otherwise), and each
	// strip carries a back-pointer to its owning pane so drop_data can resolve the
	// source and destination panes.
	CHECK(bar_a->get_tabs_rearrange_group() == WorkspaceTabBar::WORKSPACE_TABS_REARRANGE_GROUP);
	CHECK(bar_a->get_tabs_rearrange_group() == bar_b->get_tabs_rearrange_group());
	CHECK(bar_a->get_drag_to_rearrange_enabled());
	CHECK(bar_b->get_drag_to_rearrange_enabled());
	CHECK(bar_a->get_pane() == pane_a);
	CHECK(bar_b->get_pane() == pane_b);

	// The cross-pane move is mediated by EditorNode (like the rosette overlay drop),
	// which the workspace harness does not create. A synthesized cross-pane drop must
	// therefore be a safe no-op rather than crashing or half-applying the move; the
	// model-level move itself is covered by the handle_tab_strip_drop tests above.
	Dictionary drag;
	drag["type"] = "tab";
	drag["tab_type"] = "tab_bar_tab";
	drag["tab_index"] = 1;
	drag["from_path"] = bar_a->get_path();

	// Control::drop_data is public and virtual; the call dispatches to the override.
	Control *drop_target = bar_b;
	drop_target->drop_data(Point2(100000, 0), drag);
	h.pump();

	CHECK(pane_a->get_tab_count() == 2);
	CHECK(pane_b->get_tab_count() == 1);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] inactive-tab-close-mounts-before-prompt") {
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);

	WorkspacePane *pane = memnew(WorkspacePane);
	host->add_child(pane);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	RecordingTabType recording_type;
	registry.register_type(&recording_type);
	pane->set_tab_registry(&registry);

	WorkspaceTab first = recording_type.make_tab("first", registry.allocate_stable_id());
	WorkspaceTab second = recording_type.make_tab("second", registry.allocate_stable_id());
	pane->add_tab(first);
	pane->add_tab(second);
	// "first" is active and mounted; "second" is inactive and never mounted.
	REQUIRE(pane->get_active_tab_index() == 0);

	// Closing the inactive "second" tab must mount it first so its type inspects a
	// live surface (e.g. a dirty script running its save/discard prompt) instead of
	// being asked to close while unmounted, which would bypass the prompt.
	const WorkspaceTabCloseResult result = pane->request_close_tab(1);
	CHECK(result == WorkspaceTabCloseResult::CLOSE);
	CHECK(recording_type.last_close_mounted == 1);
	CHECK(pane->get_tab_count() == 1);
	CHECK(pane->get_tab(0).get_resource_key() == "first");

	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

static String make_existing_resource_file(const String &p_name) {
	const String dir = OS::get_singleton()->get_cache_path().path_join("workspace_persist");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	ERR_FAIL_COND_V(da.is_null(), String());
	da->make_dir_recursive(dir);
	const String path = dir.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V(file.is_null(), String());
	file->store_string("func run(): pass\n");
	return path;
}

static void remove_resource_file(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (da.is_valid()) {
		da->remove(p_path);
	}
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-mixed-pane-roundtrip") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("mixed_roundtrip.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	WorkspaceTab scene_tab = scene_type->make_tab("res://level.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);
	REQUIRE(pane->get_tab_count() == 2);

	const int scene_stable = scene_tab.get_stable_id();
	const int script_stable = script_tab.get_stable_id();
	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	REQUIRE(restored->get_tab_count() == 2);

	CHECK(restored->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(restored->get_tab(0).get_resource_key() == "res://level.tscn");
	CHECK(restored->get_tab(0).get_stable_id() == scene_stable);

	CHECK(restored->get_tab(1).get_type_id() == StringName("script"));
	CHECK(restored->get_tab(1).get_resource_key() == script_path);
	CHECK(restored->get_tab(1).get_stable_id() == script_stable);

	h2.unmount();
	remove_resource_file(script_path);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-active-and-focused") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String path_a = make_existing_resource_file("active_a.fs");
	const String path_b = make_existing_resource_file("active_b.fs");
	const String path_c = make_existing_resource_file("active_c.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	add_script_tab(pane_a, path_a);
	add_script_tab(pane_a, path_b);
	pane_a->set_active_tab(1);
	REQUIRE(pane_a->get_active_tab_index() == 1);

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, path_c);

	h.workspace->set_focused_leaf(leaf_b->get_leaf_id());
	const int leaf_a_id = leaf_a->get_leaf_id();
	const int leaf_b_id = leaf_b->get_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_focused_leaf_id() == leaf_b_id);

	WorkspacePane *restored_a = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_a_id));
	WorkspacePane *restored_b = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_b_id));
	REQUIRE(restored_a != nullptr);
	REQUIRE(restored_b != nullptr);
	CHECK(restored_a->get_tab_count() == 2);
	CHECK(restored_a->get_active_tab_index() == 1);
	CHECK(restored_b->get_tab_count() == 1);
	CHECK(restored_b->get_active_tab_index() == 0);

	h2.unmount();
	remove_resource_file(path_a);
	remove_resource_file(path_b);
	remove_resource_file(path_c);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-script-tab-payload") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("payload.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	// A scene tab is the active tab so the script tab stays unmounted; that keeps
	// the payload we set below intact instead of being overwritten by a live
	// (empty) surface capture on mount.
	WorkspaceTab scene_tab = scene_type->make_tab("res://payload_scene.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	Dictionary payload;
	payload["script_path"] = script_path;
	payload["view_layout"] = "[view]\ncaret_line=12\nscroll_position=40\nfolded_lines=[3, 7]\n";
	script_tab.set_payload(payload);

	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);
	REQUIRE(pane->get_active_tab_index() == 0);

	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	REQUIRE(restored->get_tab_count() == 2);
	const WorkspaceTab &restored_script = restored->get_tab(1);
	CHECK(restored_script.get_type_id() == StringName("script"));
	const Dictionary &restored_payload = restored_script.get_payload();
	REQUIRE(restored_payload.has("view_layout"));
	const String restored_layout = restored_payload["view_layout"];
	CHECK(restored_layout.contains("caret_line=12"));
	CHECK(restored_layout.contains("scroll_position=40"));

	h2.unmount();
	remove_resource_file(script_path);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-missing-resource-graceful") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("missing.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTab scene_tab = scene_type->make_tab("res://survives.tscn", registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	add_script_tab(pane, script_path);
	REQUIRE(pane->get_tab_count() == 2);

	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	// The backing script is deleted between sessions, so its tab must be dropped
	// with a diagnostic while the rest of the layout restores cleanly.
	remove_resource_file(script_path);

	ErrorDetector error_detector;
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(error_detector.has_error);

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	CHECK(restored->get_tab_count() == 1);
	CHECK(restored->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(restored->get_tab(0).get_resource_key() == "res://survives.tscn");

	h2.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-unknown-type-skipped") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	// Persist with a registry that knows a "recording" type; restore uses the
	// shared registry which does not, so that tab has no registered type.
	WorkspaceTabRegistry save_registry;
	RecordingTabType ghost_type;
	save_registry.register_type(&ghost_type);
	pane->set_tab_registry(&save_registry);

	WorkspaceTabType *scene_type = save_registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);
	WorkspaceTab scene_tab = scene_type->make_tab("res://keep.tscn", save_registry.allocate_stable_id());
	WorkspaceTab ghost_tab = ghost_type.make_tab("res://ghost.dat", save_registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(ghost_tab);
	REQUIRE(pane->get_tab_count() == 2);

	const int leaf_id = h.workspace->get_focused_leaf_id();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	ErrorDetector error_detector;
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(error_detector.has_error);

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	CHECK(restored->get_tab_count() == 1);
	CHECK(restored->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(restored->get_tab(0).get_resource_key() == "res://keep.tscn");

	h2.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-focus-invariant-holds") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("focus_invariant.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	const int tile_a = leaf_a->get_leaf_id();
	h.editor_data.register_tile(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_tile_current_scene(tile_a, scene_a);
	h.editor_data.set_edited_scene(scene_a);

	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	// The focused pane's active tab is a script tab, but the pane is a scene pane
	// and still exposes a scene tile the editor focus can fall back to.
	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTab scene_tab = scene_type->make_tab("res://focus_scene.tscn", registry.allocate_stable_id());
	pane_a->add_tab(scene_tab);
	add_script_tab(pane_a, script_path);
	pane_a->set_active_tab(1);
	REQUIRE(pane_a->get_active_tab_index() == 1);

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	h.editor_data.register_tile(leaf_b->get_leaf_id());
	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	check_focus_invariant(h.editor_data, h.workspace);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	// Restore into the same workspace so editor_data (and its scene state) is kept.
	h.workspace->restore_from_config(config);
	h.pump();

	// Mirror EditorNode's post-restore focus re-assertion: the editor's focused
	// tile follows the restored focused pane, which must resolve to a scene tile.
	h.editor_data.set_focused_tile_id(h.workspace->get_focused_leaf_id());

	CHECK(h.workspace->get_focused_leaf_id() == tile_a);
	WorkspacePane *restored_a = get_leaf_pane(h.workspace->get_leaf_by_id(tile_a));
	REQUIRE(restored_a != nullptr);
	CHECK(restored_a->get_active_tab_index() == 1);
	CHECK(restored_a->get_tab(1).get_type_id() == StringName("script"));
	CHECK(h.workspace->get_focused_tile() != nullptr);
	check_focus_invariant(h.editor_data, h.workspace);

	h.unmount();
	remove_resource_file(script_path);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-nonfocused-scene-tab-keeps-focus") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	const int tile_a = leaf_a->get_leaf_id();
	h.editor_data.register_tile(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b, root_b);

	h.workspace->sync_scene_tabs_from_editor_data();
	h.workspace->set_focused_leaf(tile_b);
	h.editor_data.set_focused_tile_id(tile_b);
	h.editor_data.set_edited_scene(scene_b);
	REQUIRE(get_leaf_pane(leaf_a)->find_scene_tab_index(scene_a) >= 0);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	// Restore into the same workspace so editor_data keeps both scenes; the
	// non-focused pane's restored scene tab must not activate and steal focus.
	h.workspace->restore_from_config(config);
	h.pump();

	CHECK(h.workspace->get_focused_leaf_id() == tile_b);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] persist-restore-reserves-stable-ids") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("reserve_ids.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	WorkspaceTab scene_tab = scene_type->make_tab("res://reserve.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);
	const int max_stable = MAX(scene_tab.get_stable_id(), script_tab.get_stable_id());

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	// A fresh process starts the shared id counter below the persisted ids; restore
	// must reserve past them so the next allocation cannot collide with a restored tab.
	WorkspacePane::get_shared_tab_registry().reset_stable_id_counter(0);
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(WorkspacePane::get_shared_tab_registry().allocate_stable_id() > max_stable);

	h2.unmount();
	remove_resource_file(script_path);
}

// Mimics _load_open_scenes_from_config: a scene reopened on startup lands on the
// current focused (startup) tile and carries no per-scene tile record, so a fresh
// session assigns it a new history id unrelated to the persisted tab payload.
static int add_startup_scene(EditorData &p_data, const String &p_path, Node2D *p_root = nullptr) {
	const int idx = p_data.add_edited_scene(-1);
	p_data.set_scene_path(idx, p_path);
	if (p_root) {
		EditorSceneContext *context = p_data.get_scene_context(idx);
		context->set_scene_root_node(p_root);
	}
	return idx;
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] restart-restores-multipane-scene-ownership") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	// Session 1: two panes, each owning a distinct saved scene; focus on tile_b.
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	const int tile_a = leaf_a->get_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://scene_a.tscn");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b, root_b);
	h.editor_data.set_scene_path(scene_b, "res://scene_b.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	h.workspace->set_focused_leaf(tile_b);
	h.editor_data.set_focused_tile_id(tile_b);
	h.editor_data.set_edited_scene(scene_b);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	h.unmount();

	// Session 2: a fresh EditorData reopens the scenes before the workspace tree is
	// restored, so both land on the startup tile 0 (mirrors _load_open_scenes_from_config).
	// Reopen in reverse order so the fresh scene_history_ids no longer line up with the
	// persisted tab payloads -- ownership must resolve by path, not the stale history id.
	WorkspaceHarness h2;
	h2.mount();
	h2.pump();

	h2.editor_data.register_tile(0);
	h2.editor_data.set_focused_tile_id(0);
	Node2D *reopened_b_root = memnew(Node2D);
	const int reopened_b = add_startup_scene(h2.editor_data, "res://scene_b.tscn", reopened_b_root);
	Node2D *reopened_a_root = memnew(Node2D);
	const int reopened_a = add_startup_scene(h2.editor_data, "res://scene_a.tscn", reopened_a_root);

	h2.workspace->restore_from_config(config);

	// Decoupled ownership restoration: push each restored scene tab's tile ownership
	// into EditorData before the tab sync runs, without claiming focus for non-focused panes.
	h2.workspace->restore_scene_tile_ownership_from_tabs();

	CHECK(h2.editor_data.get_scene_tile(reopened_a) == tile_a);
	CHECK(h2.editor_data.get_scene_tile(reopened_b) == tile_b);
	CHECK(h2.editor_data.get_tile_scene_indices(tile_a).has(reopened_a));
	CHECK(h2.editor_data.get_tile_scene_indices(tile_b).has(reopened_b));

	// The focused tile has a current scene immediately, before any tab sync runs.
	CHECK(h2.editor_data.get_tile_current_scene(tile_b) == reopened_b);

	h2.editor_data.set_focused_tile_id(h2.workspace->get_focused_leaf_id());
	h2.workspace->sync_scene_tabs_from_editor_data();
	h2.pump();

	CHECK(h2.workspace->get_focused_leaf_id() == tile_b);
	WorkspacePane *restored_a = get_leaf_pane(h2.workspace->get_leaf_by_id(tile_a));
	WorkspacePane *restored_b = get_leaf_pane(h2.workspace->get_leaf_by_id(tile_b));
	REQUIRE(restored_a != nullptr);
	REQUIRE(restored_b != nullptr);
	CHECK(restored_a->find_scene_tab_index(reopened_a) >= 0);
	CHECK(restored_b->find_scene_tab_index(reopened_b) >= 0);

	h2.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] restart-restores-mixed-pane-scene-ownership") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("mixed_restart.fs");

	// Session 1: a non-focused pane holds a scene tab and a script tab; focus stays
	// on the first pane so the mixed pane never activates.
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	const int tile_a = leaf_a->get_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://mixed_a.tscn");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b, root_b);
	h.editor_data.set_scene_path(scene_b, "res://mixed_b.tscn");

	h.workspace->sync_scene_tabs_from_editor_data();
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);
	add_script_tab(pane_b, script_path);
	REQUIRE(pane_b->get_tab_count() == 2);

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_edited_scene(scene_a);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	h.unmount();

	// Session 2: fresh EditorData reopens both scenes onto the startup tile 0.
	WorkspaceHarness h2;
	h2.mount();
	h2.pump();

	h2.editor_data.register_tile(0);
	h2.editor_data.set_focused_tile_id(0);
	Node2D *reopened_a_root = memnew(Node2D);
	const int reopened_a = add_startup_scene(h2.editor_data, "res://mixed_a.tscn", reopened_a_root);
	Node2D *reopened_b_root = memnew(Node2D);
	const int reopened_b = add_startup_scene(h2.editor_data, "res://mixed_b.tscn", reopened_b_root);

	h2.workspace->restore_from_config(config);
	h2.workspace->restore_scene_tile_ownership_from_tabs();

	CHECK(h2.editor_data.get_scene_tile(reopened_a) == tile_a);
	CHECK(h2.editor_data.get_scene_tile(reopened_b) == tile_b);

	h2.editor_data.set_focused_tile_id(h2.workspace->get_focused_leaf_id());
	h2.workspace->sync_scene_tabs_from_editor_data();
	h2.pump();

	WorkspacePane *restored_b = get_leaf_pane(h2.workspace->get_leaf_by_id(tile_b));
	REQUIRE(restored_b != nullptr);
	// The mixed pane keeps both its scene tab and its script tab.
	CHECK(restored_b->find_scene_tab_index(reopened_b) >= 0);
	bool has_script_tab = false;
	for (int i = 0; i < restored_b->get_tab_count(); i++) {
		if (restored_b->get_tab(i).get_type_id() == StringName("script") && restored_b->get_tab(i).get_resource_key() == script_path) {
			has_script_tab = true;
			break;
		}
	}
	CHECK(has_script_tab);

	h2.unmount();
	remove_resource_file(script_path);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] restart-ignores-stale-unsaved-scene-tab") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	// Session 1: pane_a owns a saved scene; the non-focused pane_b owns an unsaved
	// (pathless) scene whose only stable identity is a session-local history id.
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	const int tile_a = leaf_a->get_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://stale_a.tscn");

	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	Node2D *root_b = memnew(Node2D);
	const int scene_b_unsaved = add_test_scene(h.editor_data, tile_b, root_b);
	const int stale_history_id = h.editor_data.get_scene_history_id(scene_b_unsaved);

	h.workspace->sync_scene_tabs_from_editor_data();
	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_edited_scene(scene_a);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	h.unmount();

	// Session 2: the unsaved scene is not reopened. Its history id is recycled onto a
	// fresh blank startup scene, so a history-id-based resolver would move that blank
	// scene into pane_b. Ownership must resolve by path only and leave it on tile 0.
	WorkspaceHarness h2;
	h2.mount();
	h2.pump();

	h2.editor_data.register_tile(0);
	h2.editor_data.set_focused_tile_id(0);
	Node2D *reopened_a_root = memnew(Node2D);
	const int reopened_a = add_startup_scene(h2.editor_data, "res://stale_a.tscn", reopened_a_root);
	Node2D *blank_root = memnew(Node2D);
	const int blank_scene = h2.editor_data.add_edited_scene(-1);
	h2.editor_data.get_scene_context(blank_scene)->set_scene_root_node(blank_root);
	REQUIRE(h2.editor_data.get_scene_history_id(blank_scene) == stale_history_id);

	h2.workspace->restore_from_config(config);
	h2.workspace->restore_scene_tile_ownership_from_tabs();

	// The saved scene is restored to its pane; the blank scene is never claimed by the
	// stale unsaved tab and stays on the startup tile.
	CHECK(h2.editor_data.get_scene_tile(reopened_a) == tile_a);
	CHECK(h2.editor_data.get_scene_tile(blank_scene) == 0);
	CHECK_FALSE(h2.editor_data.get_tile_scene_indices(tile_b).has(blank_scene));

	h2.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-pane-two-tabs") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	pane->set_tab_registry(&registry);

	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	WorkspaceTab scene_tab = scene_type->make_tab("res://level.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab("res://player.fs", registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);

	// A scene tab and a script tab coexist in one pane and stay individually
	// addressable by their type and resource key.
	REQUIRE(pane->get_tab_count() == 2);
	CHECK(pane->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(pane->get_tab(0).get_resource_key() == "res://level.tscn");
	CHECK(pane->get_tab(1).get_type_id() == StringName("script"));
	CHECK(pane->get_tab(1).get_resource_key() == "res://player.fs");

	ScenePaneTile *scene_tile = pane->get_scene_tile();
	REQUIRE(scene_tile != nullptr);

	// Switching tabs mounts the right chrome: the scene tab shows the scene tile,
	// the script tab hides it so the script surface owns the chrome host.
	pane->set_active_tab(0);
	CHECK(pane->get_active_tab_index() == 0);
	CHECK(scene_tile->is_visible());

	pane->set_active_tab(1);
	CHECK(pane->get_active_tab_index() == 1);
	CHECK(scene_tile->is_visible() == false);

	pane->set_active_tab(0);
	CHECK(scene_tile->is_visible());

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-move-script-between-panes") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int leaf_a_id = h.workspace->get_focused_leaf_id();
	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(leaf_a_id);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int leaf_b_id = leaf_b->get_leaf_id();
	h.editor_data.register_tile(leaf_b_id);

	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_b != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);

	const String mover_path = "res://mover.fs";
	const String keep_path = "res://keep.fs";

	// Pane A hosts a scene tab plus the script tab we will move; pane B keeps its
	// own script tab so it never empties out from under us.
	WorkspaceTab scene_tab = scene_type->make_tab("res://level.tscn", registry.allocate_stable_id());
	pane_a->add_tab(scene_tab);
	add_script_tab(pane_a, mover_path);
	add_script_tab(pane_b, keep_path);
	REQUIRE(pane_a->get_tab_count() == 2);
	REQUIRE(pane_b->get_tab_count() == 1);

	auto index_of_key = [](WorkspacePane *p_pane, const String &p_key) -> int {
		for (int i = 0; i < p_pane->get_tab_count(); i++) {
			if (p_pane->get_tab(i).get_resource_key() == p_key) {
				return i;
			}
		}
		return -1;
	};
	auto pane_has_key = [&index_of_key](WorkspacePane *p_pane, const String &p_key) -> bool {
		return index_of_key(p_pane, p_key) >= 0;
	};

	// Move the script tab from pane A into the sibling pane B (center drop merges).
	const int mover_index = index_of_key(pane_a, mover_path);
	REQUIRE(mover_index >= 0);
	WorkspaceLeafNode *dest = h.workspace->handle_tab_drop(leaf_a_id, mover_index, leaf_b, EditorSceneWorkspace::DROP_CENTER);
	h.pump();
	REQUIRE(dest != nullptr);

	CHECK_FALSE(pane_has_key(pane_a, mover_path));
	CHECK(pane_has_key(pane_a, "res://level.tscn"));
	CHECK(pane_has_key(pane_b, mover_path));
	CHECK(pane_has_key(pane_b, keep_path));

	// Now split the moved script tab out of pane B into a brand-new pane (edge drop).
	const int leaf_count_before = h.workspace->get_leaf_count();
	const int mover_index_b = index_of_key(pane_b, mover_path);
	REQUIRE(mover_index_b >= 0);
	WorkspaceLeafNode *new_leaf = h.workspace->handle_tab_drop(leaf_b_id, mover_index_b, leaf_b, EditorSceneWorkspace::DROP_RIGHT);
	h.pump();
	REQUIRE(new_leaf != nullptr);
	CHECK(h.workspace->get_leaf_count() == leaf_count_before + 1);

	WorkspacePane *new_pane = get_leaf_pane(new_leaf);
	REQUIRE(new_pane != nullptr);
	CHECK(pane_has_key(new_pane, mover_path));
	CHECK_FALSE(pane_has_key(pane_b, mover_path));
	CHECK(pane_has_key(pane_b, keep_path));

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-close-dirty-and-collapse") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	REQUIRE(leaf_a != nullptr);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	h.editor_data.register_tile(leaf_b->get_leaf_id());

	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_b != nullptr);

	// Pane B hosts a dirty tab whose close defers, modelling the save/discard prompt.
	WorkspaceTabRegistry registry;
	registry.reset_stable_id_counter();
	registry.clear_canonical_index();
	PromptSpyTabType prompt_type;
	registry.register_type(&prompt_type);
	pane_b->set_tab_registry(&registry);

	prompt_type.close_result = WorkspaceTabCloseResult::DEFERRED;
	WorkspaceTab dirty = prompt_type.make_tab("res://dirty.fs", registry.allocate_stable_id());
	pane_b->add_tab(dirty);
	REQUIRE(pane_b->get_active_tab_index() == 0);

	// Closing the dirty tab defers (prompts) and keeps the tab in place.
	CHECK(pane_b->request_close_active_tab() == WorkspaceTabCloseResult::DEFERRED);
	CHECK(prompt_type.close_request_count == 1);
	CHECK(pane_b->get_tab_count() == 1);

	// Resolving the prompt (discard) lets the close proceed and empties the pane.
	prompt_type.close_result = WorkspaceTabCloseResult::CLOSE;
	CHECK(pane_b->request_close_active_tab() == WorkspaceTabCloseResult::CLOSE);
	CHECK(prompt_type.close_request_count == 2);
	CHECK(pane_b->get_tab_count() == 0);
	CHECK(pane_b->get_empty_placeholder()->is_visible());
	CHECK(pane_b->get_tab_strip()->is_visible() == false);

	// Collapsing the emptied pane keeps a focused scene tile in the surviving leaf.
	h.workspace->set_focused_leaf(leaf_a->get_leaf_id());
	h.workspace->collapse(leaf_b);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);

	WorkspaceLeafNode *survivor = h.workspace->get_focused_leaf();
	REQUIRE(survivor != nullptr);
	CHECK(get_leaf_pane(survivor)->is_scene_pane());
	// The final surviving pane persists its empty placeholder (it has no tabs yet).
	CHECK(get_leaf_pane(survivor)->get_empty_placeholder()->is_visible());

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-persist-roundtrip") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	const String script_path = make_existing_resource_file("mixed_named_roundtrip.fs");

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspacePane *pane = get_leaf_pane(h.workspace->get_focused_leaf());
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	WorkspaceTab scene_tab = scene_type->make_tab("res://mixed.tscn", registry.allocate_stable_id());
	WorkspaceTab script_tab = script_type->make_tab(script_path, registry.allocate_stable_id());
	pane->add_tab(scene_tab);
	pane->add_tab(script_tab);
	pane->set_active_tab(1);
	REQUIRE(pane->get_tab_count() == 2);

	const int leaf_id = h.workspace->get_focused_leaf_id();
	const int active_before = pane->get_active_tab_index();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	WorkspacePane *restored = get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_id));
	REQUIRE(restored != nullptr);
	REQUIRE(restored->get_tab_count() == 2);

	// The mixed layout round-trips: tab order, types, resource keys, and active index.
	CHECK(restored->get_tab(0).get_type_id() == StringName("scene"));
	CHECK(restored->get_tab(0).get_resource_key() == "res://mixed.tscn");
	CHECK(restored->get_tab(1).get_type_id() == StringName("script"));
	CHECK(restored->get_tab(1).get_resource_key() == script_path);
	CHECK(restored->get_restored_active_tab_index() == active_before);

	h2.unmount();
	remove_resource_file(script_path);
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] mixed-collapse-focused-pane-with-split-sibling-no-crash") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	ErrorDetector error_detector;

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	h.editor_data.register_tile(leaf_b->get_leaf_id());
	WorkspaceLeafNode *leaf_c = h.workspace->split(leaf_b, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_c != nullptr);
	h.editor_data.register_tile(leaf_c->get_leaf_id());

	// The focused pane mixes a scene tab and script tabs; its sibling is a split.
	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	REQUIRE(pane_a != nullptr);
	h.workspace->sync_scene_tabs_from_editor_data();
	add_script_tab(pane_a, "res://mixed_one.fs");
	add_script_tab(pane_a, "res://mixed_two.fs");

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);

	LeafRemovedTracker tracker;
	h.workspace->connect("leaf_removed", callable_mp(&tracker, &LeafRemovedTracker::on_leaf_removed));

	// Collapsing the focused mixed pane whose sibling is itself a split must promote a
	// surviving leaf without crashing or leaving a stale focus.
	h.workspace->collapse(leaf_a);
	h.pump();

	CHECK(tracker.removed_leaf_id == tile_a);
	CHECK(tracker.successor_leaf_id != tile_a);
	CHECK(h.workspace->get_leaf_by_id(tracker.successor_leaf_id) != nullptr);
	CHECK(h.workspace->get_leaf_count() >= 1);
	CHECK(h.workspace->get_focused_leaf() != nullptr);
	CHECK_FALSE(error_detector.has_error);

	h.unmount();
}

// Issue #1063: a persisted layout can restore a non-default leaf whose tabs no
// longer resolve to any content, leaving a phantom empty (uncollapsed) pane that
// violates the "empty non-default panes collapse" invariant. reconcile_empty_leaves()
// is the defensive self-heal run after a restore settles.

TEST_CASE("[SceneWorkspace][SceneTree][Editor] reconcile-empty-leaves-collapses-non-default-only") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	// The sole leaf is the default pane and must never be collapsed, even empty.
	h.workspace->reconcile_empty_leaves();
	CHECK(h.workspace->get_leaf_count() == 1);

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int leaf_a_id = leaf_a->get_leaf_id();
	const int leaf_b_id = leaf_b->get_leaf_id();

	// leaf_a owns a scene (non-empty); leaf_b is an empty scene pane.
	Node2D *root = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, leaf_a_id, root);
	h.editor_data.set_scene_path(scene_a, "res://a.tscn");
	h.workspace->set_focused_leaf(leaf_a_id);
	h.workspace->sync_scene_tabs_from_editor_data();
	h.pump();

	REQUIRE(get_leaf_pane(leaf_a)->get_tab_count() == 1);
	REQUIRE(get_leaf_pane(leaf_b)->get_tab_count() == 0);

	h.workspace->reconcile_empty_leaves();
	h.pump();

	// Only the empty non-default leaf collapses; the populated leaf survives intact.
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(h.workspace->get_leaf_by_id(leaf_b_id) == nullptr);
	WorkspaceLeafNode *survivor = h.workspace->get_leaf_by_id(leaf_a_id);
	REQUIRE(survivor != nullptr);
	CHECK(get_leaf_pane(survivor)->get_tab_count() == 1);

	h.unmount();
}

TEST_CASE("[SceneWorkspace][SceneTree][Editor] restore-then-reconcile-drops-all-tabs-dropped-leaf") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int leaf_a_id = leaf_a->get_leaf_id();
	const int leaf_b_id = leaf_b->get_leaf_id();

	// A save-side registry that also knows a type the shared restore registry does
	// not, so leaf_b's only tab is dropped on restore and leaf_b loads empty. leaf_a
	// keeps a scene tab (scene tabs survive load unconditionally) so it stays valid.
	WorkspaceTabRegistry save_registry;
	save_registry.reset_stable_id_counter();
	PromptSpyTabType unresolved_type;
	save_registry.register_type(&unresolved_type);
	WorkspaceTabType *scene_type = save_registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);

	WorkspacePane *pane_a = get_leaf_pane(leaf_a);
	WorkspacePane *pane_b = get_leaf_pane(leaf_b);
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_b != nullptr);
	pane_a->set_tab_registry(&save_registry);
	pane_b->set_tab_registry(&save_registry);

	pane_a->add_tab(scene_type->make_tab("res://keep.tscn", save_registry.allocate_stable_id()));
	pane_b->add_tab(unresolved_type.make_tab("res://gone", save_registry.allocate_stable_id()));
	REQUIRE(pane_a->get_tab_count() == 1);
	REQUIRE(pane_b->get_tab_count() == 1);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	h.unmount();

	ErrorDetector error_detector;
	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	// leaf_b's only tab was unresolvable and dropped with a diagnostic, leaving it a
	// phantom empty non-default pane; leaf_a restored its scene tab.
	CHECK(error_detector.has_error);
	REQUIRE(h2.workspace->get_leaf_count() == 2);
	REQUIRE(get_leaf_pane(h2.workspace->get_leaf_by_id(leaf_b_id))->get_tab_count() == 0);

	// The restore self-heal collapses the emptied leaf.
	h2.workspace->reconcile_empty_leaves();
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 1);
	CHECK(h2.workspace->get_leaf_by_id(leaf_b_id) == nullptr);
	WorkspaceLeafNode *survivor = h2.workspace->get_leaf_by_id(leaf_a_id);
	REQUIRE(survivor != nullptr);
	CHECK(get_leaf_pane(survivor)->get_tab_count() == 1);

	h2.unmount();
}

} // namespace TestSceneWorkspace
