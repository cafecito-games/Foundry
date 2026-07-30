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

} // namespace TestCanvasItemEditorView
