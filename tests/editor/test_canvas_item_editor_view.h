/**************************************************************************/
/*  test_canvas_item_editor_view.h                                        */
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

#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/scene/canvas_item_editor_view.h"
#include "editor/scene/canvas_item_editor_view_state.h"

#include "scene/2d/node_2d.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestCanvasItemEditorView {

TEST_CASE("[Editor][canvas-view-bind] CanvasItemEditorView constructs without EditorNode") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();

	CHECK(view->get_bound_context() == nullptr);

	tree_root->remove_child(view);
	memdelete(view);
}

TEST_CASE("[Editor][canvas-view-bind] View pushes canvas transform to bound context viewport") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;
	view_state.zoom = 1.75;
	view_state.view_offset = Point2(120, 45);

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *scene_a = memnew(Node2D);
	Node2D *scene_b = memnew(Node2D);
	context_a->set_scene_root_node(scene_a);
	context_b->set_scene_root_node(scene_b);

	const Transform2D untouched_b(2.0, Vector2(17, 29));
	context_b->get_viewport()->set_global_canvas_transform(untouched_b);

	view->bind_context(context_a);
	CHECK(view->get_bound_context() == context_a);

	view->push_viewport_state();

	CanvasItemEditorViewMath::update_canvas_transform(view_state);
	CHECK(context_a->get_viewport()->get_global_canvas_transform().is_equal_approx(view_state.transform));
	CHECK(context_b->get_viewport()->get_global_canvas_transform().is_equal_approx(untouched_b));

	tree_root->remove_child(view);
	memdelete(view);
	memdelete(context_a);
	memdelete(context_b);
}

// #2062: a view kept a raw EditorSceneContext pointer and nothing invalidated it
// when the context was freed, so the viewport fan-out read through freed memory.
TEST_CASE("[Editor][canvas-view-bind] Freeing the bound context clears the view binding") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();

	EditorSceneContext *context = memnew(EditorSceneContext);
	context->set_scene_root_node(memnew(Node2D));
	view->bind_context(context);
	REQUIRE(view->get_bound_context() == context);

	memdelete(context);

	CHECK(view->get_bound_context() == nullptr);

	// The fan-out that crashed still visits the view; it must not read the context.
	Vector<CanvasItemEditorView *> views;
	views.push_back(view);
	const uint64_t before = view->test_update_viewport_invocations;
	CanvasItemEditorViewRouting::update_all_viewports(views);
	CHECK(view->test_update_viewport_invocations == before + 1);

	tree_root->remove_child(view);
	memdelete(view);
}

// Context ids are never reused, so a context allocated at the address of a freed
// one cannot resurrect a stale binding.
TEST_CASE("[Editor][canvas-view-bind] A replacement context does not resurrect a dead binding") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	const uint64_t id_a = context_a->get_context_id();
	view->bind_context(context_a);
	memdelete(context_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	CHECK(context_b->get_context_id() != id_a);
	CHECK(view->get_bound_context() == nullptr);

	view->bind_context(context_b);
	CHECK(view->get_bound_context() == context_b);

	tree_root->remove_child(view);
	memdelete(view);
	memdelete(context_b);
}

// The reverse teardown order: the view dies first, so the context must not keep a
// dangling registration to walk during its own destruction.
TEST_CASE("[Editor][canvas-view-bind] Destroying the view first leaves the context safe to free") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;

	EditorSceneContext *context = memnew(EditorSceneContext);

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();
	view->bind_context(context);

	tree_root->remove_child(view);
	memdelete(view);

	// Nothing left bound; freeing the context must not touch the freed view.
	context->detach_bound_canvas_views();
	memdelete(context);
}

// EditorNode::scene_context_about_to_be_removed() used to return early for every
// non-active context, which is exactly the multi-board case: a preview bound to a
// scene owned by another board. Detaching now runs for any removed context.
TEST_CASE("[Editor][canvas-view-bind] Removing a non-active scene detaches its preview binding") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	CanvasItemEditorViewState view_state;

	EditorData editor_data;
	const int active_index = editor_data.add_edited_scene(-1);
	const int background_index = editor_data.add_edited_scene(-1);
	editor_data.set_edited_scene(active_index);

	EditorSceneContext *background_context = editor_data.get_scene_context(background_index);
	REQUIRE(background_context != nullptr);
	REQUIRE(editor_data.get_active_scene_context() != background_context);

	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tree_root->add_child(view);
	MessageQueue::get_singleton()->flush();
	view->bind_context(background_context);
	REQUIRE(view->get_bound_context() == background_context);

	editor_data.remove_scene(background_index);

	CHECK(view->get_bound_context() == nullptr);

	Vector<CanvasItemEditorView *> views;
	views.push_back(view);
	CanvasItemEditorViewRouting::update_all_viewports(views);

	tree_root->remove_child(view);
	memdelete(view);
	editor_data.remove_scene(0);
}

} // namespace TestCanvasItemEditorView
