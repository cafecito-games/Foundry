/**************************************************************************/
/*  test_dock_scene_context_binding.h                                     */
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

#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/scene/scene_tree_editor.h"

#include "scene/2d/node_2d.h"
#include "scene/gui/button.h"
#include "scene/gui/menu_button.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

// Reads the InspectorDock's history-dependent chrome; declared as a friend of
// InspectorDock so the binding tests can assert button states.
class InspectorDockTestAccess {
public:
	static bool backward_disabled(InspectorDock *p_dock) { return p_dock->backward_button->is_disabled(); }
	static bool forward_disabled(InspectorDock *p_dock) { return p_dock->forward_button->is_disabled(); }
	static bool history_menu_disabled(InspectorDock *p_dock) { return p_dock->history_menu->is_disabled(); }
};

namespace TestDockSceneContextBinding {

TEST_CASE("[SceneTree][Editor] SceneTreeDock constructs without an EditorNode") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);

	// No EditorNode::get_singleton() exists here; construction must not
	// dereference it.
	SceneTreeDock *dock = memnew(SceneTreeDock(selection, editor_data));
	tree_root->add_child(dock);
	MessageQueue::get_singleton()->flush();

	CHECK(dock->get_scene_context() == nullptr);

	tree_root->remove_child(dock);
	memdelete(dock);
	memdelete(selection);
}

TEST_CASE("[SceneTree][Editor] InspectorDock constructs without an EditorNode") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorData editor_data;

	InspectorDock *dock = memnew(InspectorDock(editor_data));
	tree_root->add_child(dock);
	MessageQueue::get_singleton()->flush();

	CHECK(dock->get_scene_context() == nullptr);
	CHECK(dock->get_inspector() != nullptr);

	tree_root->remove_child(dock);
	memdelete(dock);
}

TEST_CASE("[SceneTree][Editor] SceneTreeDock binding follows the bound context's scene root") {
	EditorData editor_data;

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	Node2D *root_a = memnew(Node2D);
	root_a->set_name("SceneA");
	context_a->set_scene_root_node(root_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	root_b->set_name("SceneB");
	context_b->set_scene_root_node(root_b);

	EditorSelection *selection = memnew(EditorSelection);
	SceneTreeDock *dock = memnew(SceneTreeDock(selection, editor_data));

	dock->set_scene_context(context_b);
	MessageQueue::get_singleton()->flush();

	// The SceneTreeEditor's tree widget resolves the displayed scene through
	// the AnimationPlayerEditor editor plugin, which does not exist in a bare
	// unit test, so the binding is asserted through its scene-root source
	// (the same value _get_edited_scene_root() returns).
	CHECK(dock->get_scene_context() == context_b);
	REQUIRE(dock->get_scene_context()->get_scene_root_node() != nullptr);
	CHECK(String(dock->get_scene_context()->get_scene_root_node()->get_name()) == "SceneB");

	// Context A is left untouched by binding to B.
	CHECK(context_a->get_selection()->get_full_selected_node_list().is_empty());
	CHECK(context_a->get_history()->get_history_len() == 0);

	memdelete(dock);
	memdelete(selection);
	memdelete(context_a); // Frees root_a.
	memdelete(context_b); // Frees root_b.
}

TEST_CASE("[SceneTree][Editor] SceneTreeDock rebinding switches which selection it edits") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorData editor_data;

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	Node2D *root_a = memnew(Node2D);
	root_a->set_name("RootA");
	context_a->set_scene_root_node(root_a);
	Node2D *child_a = memnew(Node2D);
	child_a->set_name("ChildA");
	root_a->add_child(child_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	root_b->set_name("RootB");
	context_b->set_scene_root_node(root_b);
	Node2D *child_b = memnew(Node2D);
	child_b->set_name("ChildB");
	root_b->add_child(child_b);

	// Nodes must be inside the tree for the live EditorSelection to hold them.
	context_a->set_display_parent(tree_root, true);
	context_b->set_display_parent(tree_root, true);

	EditorSelection *selection = memnew(EditorSelection);
	SceneTreeDock *dock = memnew(SceneTreeDock(selection, editor_data));

	dock->set_scene_context(context_b);
	Vector<Node *> select_b;
	select_b.push_back(child_b);
	dock->set_selection(select_b);
	MessageQueue::get_singleton()->flush();

	CHECK(context_b->get_selection()->is_selected(child_b));
	CHECK(context_a->get_selection()->get_full_selected_node_list().is_empty());

	// Rebinding points selection edits at context A instead.
	dock->set_scene_context(context_a);
	Vector<Node *> select_a;
	select_a.push_back(child_a);
	dock->set_selection(select_a);
	MessageQueue::get_singleton()->flush();

	CHECK(context_a->get_selection()->is_selected(child_a));
	CHECK_FALSE(context_a->get_selection()->is_selected(child_b));

	memdelete(dock);
	memdelete(selection);
	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);
}

TEST_CASE("[SceneTree][Editor] SceneTreeDock rebinding clears stale selected tree items") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorData editor_data;

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *child_a = memnew(Node2D);
	root_a->add_child(child_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);

	context_a->set_display_parent(tree_root, true);

	EditorSelection *selection = memnew(EditorSelection);
	SceneTreeDock *dock = memnew(SceneTreeDock(selection, editor_data));

	dock->set_scene_context(context_a);
	dock->set_selected(child_a);
	CHECK(dock->get_tree_editor()->get_selected() == child_a);

	context_a->deactivate();
	dock->set_scene_context(context_b);

	CHECK(dock->get_tree_editor()->get_selected() == nullptr);

	memdelete(dock);
	memdelete(selection);
	memdelete(context_a);
	memdelete(context_b);
}

TEST_CASE("[SceneTree][Editor] SceneTreeEditor ignores detached and empty-path selections") {
	SceneTreeEditor *scene_tree = memnew(SceneTreeEditor(false, false, false));

	Node2D *detached = memnew(Node2D);
	ErrorDetector detached_error_detector;
	scene_tree->set_selected(detached);
	CHECK_FALSE(detached_error_detector.has_error);
	CHECK(scene_tree->get_selected() == nullptr);
	memdelete(detached);

	TreeItem *root_item = scene_tree->get_scene_tree()->create_item();
	root_item->set_text(0, "Root");
	ErrorDetector empty_path_error_detector;
	scene_tree->set_filter("");
	CHECK_FALSE(empty_path_error_detector.has_error);

	memdelete(scene_tree);
}

TEST_CASE("[SceneTree][Editor] SceneTreeEditor skips detached edited scene roots") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	Node *previous_edited_scene_root = SceneTree::get_singleton()->get_edited_scene_root();

	SceneTreeEditor *scene_tree = memnew(SceneTreeEditor(false, false, false));
	tree_root->add_child(scene_tree);

	Node2D *detached_root = memnew(Node2D);
	SceneTree::get_singleton()->set_edited_scene_root(detached_root);

	ErrorDetector error_detector;
	scene_tree->update_tree();
	CHECK_FALSE(error_detector.has_error);

	SceneTree::get_singleton()->set_edited_scene_root(previous_edited_scene_root);
	tree_root->remove_child(scene_tree);
	memdelete(scene_tree);
	memdelete(detached_root);
}

TEST_CASE("[SceneTree][Editor] SceneTreeDock destructor tolerates deleted bound selection") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	SceneTreeDock *dock = memnew(SceneTreeDock(selection, editor_data));

	EditorSceneContext *context = memnew(EditorSceneContext);
	dock->set_scene_context(context);

	memdelete(context);
	memdelete(dock);
	memdelete(selection);
}

TEST_CASE("[SceneTree][Editor] InspectorDock history follows the bound context") {
	EditorData editor_data;

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);

	Node2D *obj1 = memnew(Node2D);
	Node2D *obj2 = memnew(Node2D);
	context_b->get_history()->add_object(obj1->get_instance_id());
	context_b->get_history()->add_object(obj2->get_instance_id());

	InspectorDock *dock = memnew(InspectorDock(editor_data));

	dock->set_scene_context(context_b);
	dock->update(nullptr);

	// B has two entries and sits at the end of its history.
	CHECK_FALSE(InspectorDockTestAccess::backward_disabled(dock));
	CHECK(InspectorDockTestAccess::forward_disabled(dock));
	CHECK_FALSE(InspectorDockTestAccess::history_menu_disabled(dock));

	// A has an empty history: both navigation buttons and the menu are off.
	dock->set_scene_context(context_a);
	dock->update(nullptr);

	CHECK(InspectorDockTestAccess::backward_disabled(dock));
	CHECK(InspectorDockTestAccess::forward_disabled(dock));
	CHECK(InspectorDockTestAccess::history_menu_disabled(dock));

	memdelete(dock);
	memdelete(obj1);
	memdelete(obj2);
	memdelete(context_a);
	memdelete(context_b);
}

TEST_CASE("[SceneTree][Editor] Docks tolerate a null context binding") {
	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);

	SceneTreeDock *scene_dock = memnew(SceneTreeDock(selection, editor_data));
	InspectorDock *inspector_dock = memnew(InspectorDock(editor_data));

	// Bind to a real context first, then to nullptr, to exercise the unbind
	// path (disconnecting the old selection, clearing history chrome).
	EditorSceneContext *context = memnew(EditorSceneContext);
	scene_dock->set_scene_context(context);
	inspector_dock->set_scene_context(context);

	scene_dock->set_scene_context(nullptr);
	inspector_dock->set_scene_context(nullptr);
	inspector_dock->update(nullptr);
	MessageQueue::get_singleton()->flush();

	CHECK(scene_dock->get_scene_context() == nullptr);
	CHECK(inspector_dock->get_scene_context() == nullptr);
	// With no context there is no history: navigation chrome is disabled.
	CHECK(InspectorDockTestAccess::backward_disabled(inspector_dock));
	CHECK(InspectorDockTestAccess::forward_disabled(inspector_dock));
	CHECK(InspectorDockTestAccess::history_menu_disabled(inspector_dock));

	memdelete(scene_dock);
	memdelete(inspector_dock);
	memdelete(selection);
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] Colliding dock layout keys are uniquified") {
	// The full EditorDockManager needs an EditorNode to construct, so the
	// uniquify rule is exercised through its static helper directly.
	Vector<String> taken;
	taken.push_back("Scene");
	CHECK(EditorDockManager::uniquify_layout_key("Scene", taken) == "Scene:2");

	taken.push_back("Scene:2");
	CHECK(EditorDockManager::uniquify_layout_key("Scene", taken) == "Scene:3");

	// A free key is returned unchanged.
	CHECK(EditorDockManager::uniquify_layout_key("Inspector", taken) == "Inspector");

	// Two docks sharing a name end up with distinct effective layout keys.
	EditorDock *dock_a = memnew(EditorDock);
	dock_a->set_layout_key("Dupe");
	EditorDock *dock_b = memnew(EditorDock);
	dock_b->set_layout_key("Dupe");

	Vector<String> registered;
	registered.push_back(dock_a->get_effective_layout_key());
	dock_b->set_layout_key(EditorDockManager::uniquify_layout_key(dock_b->get_effective_layout_key(), registered));

	CHECK(dock_a->get_effective_layout_key() != dock_b->get_effective_layout_key());
	CHECK(dock_b->get_effective_layout_key() == "Dupe:2");

	memdelete(dock_a);
	memdelete(dock_b);
}

} // namespace TestDockSceneContextBinding
