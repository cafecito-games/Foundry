/**************************************************************************/
/*  test_canvas_item_editor_focused_accessor.h                            */
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

#include "editor/scene/canvas_item_editor_view.h"
#include "editor/scene/canvas_item_editor_view_state.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestCanvasItemEditorFocusedAccessor {

TEST_CASE("[Editor][canvas-focused-accessor] Focused view accessors return the first owned view") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	CanvasItemEditorViewState focused_state;
	CanvasItemEditorViewState other_state;
	focused_state.zoom = 1.25;
	focused_state.view_offset = Point2(40, 12);
	other_state.zoom = 3.5;
	other_state.view_offset = Point2(99, 88);

	CanvasItemEditorView *focused_view = memnew(CanvasItemEditorView(nullptr, focused_state));
	CanvasItemEditorView *other_view = memnew(CanvasItemEditorView(nullptr, other_state));
	tree_root->add_child(focused_view);
	tree_root->add_child(other_view);
	MessageQueue::get_singleton()->flush();

	Vector<CanvasItemEditorView *> views;
	views.push_back(focused_view);
	views.push_back(other_view);

	CanvasItemEditorViewMath::update_canvas_transform(focused_state);
	CanvasItemEditorViewMath::update_canvas_transform(other_state);

	CHECK(CanvasItemEditorViewRouting::get_focused_view(views) == focused_view);
	CHECK(CanvasItemEditorViewRouting::get_canvas_transform(views).is_equal_approx(focused_state.transform));
	CHECK(CanvasItemEditorViewRouting::get_viewport_control(views) == focused_view->get_viewport_control());

	tree_root->remove_child(focused_view);
	tree_root->remove_child(other_view);
	memdelete(focused_view);
	memdelete(other_view);
}

TEST_CASE("[Editor][canvas-focused-accessor] update_viewport redraws every owned view") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	CanvasItemEditorViewState state_a;
	CanvasItemEditorViewState state_b;
	CanvasItemEditorView *view_a = memnew(CanvasItemEditorView(nullptr, state_a));
	CanvasItemEditorView *view_b = memnew(CanvasItemEditorView(nullptr, state_b));
	tree_root->add_child(view_a);
	tree_root->add_child(view_b);
	MessageQueue::get_singleton()->flush();

	Vector<CanvasItemEditorView *> views;
	views.push_back(view_a);
	views.push_back(view_b);

	CHECK(view_a->test_update_viewport_invocations == 0);
	CHECK(view_b->test_update_viewport_invocations == 0);

	CanvasItemEditorViewRouting::update_all_viewports(views);

	CHECK(view_a->test_update_viewport_invocations == 1);
	CHECK(view_b->test_update_viewport_invocations == 1);

	tree_root->remove_child(view_a);
	tree_root->remove_child(view_b);
	memdelete(view_a);
	memdelete(view_b);
}

TEST_CASE("[Editor][config-not-scene-scoped] Scene geometry state round-trips zoom and pan only") {
	CanvasItemEditorViewState state;
	state.zoom = 2.0;
	state.view_offset = Point2(16, -8);

	const Dictionary saved = CanvasItemEditorSceneGeometryState::to_dict(state);
	CHECK(saved.has("zoom"));
	CHECK(saved.has("ofs"));
	CHECK_FALSE(saved.has("snap_node_parent"));
	CHECK_FALSE(saved.has("grid_visibility"));
	CHECK_FALSE(saved.has("show_rulers"));

	CanvasItemEditorViewState restored;
	CanvasItemEditorSceneGeometryState::apply(restored, saved);
	CHECK(restored.zoom == state.zoom);
	CHECK(restored.view_offset == state.view_offset);
}

TEST_CASE("[Editor][config-not-scene-scoped] Legacy global config keys in scene state are ignored") {
	CanvasItemEditorViewState state;
	state.zoom = 1.0;
	state.view_offset = Point2(5, 7);

	Dictionary legacy = CanvasItemEditorSceneGeometryState::to_dict(state);
	legacy["snap_node_parent"] = false;
	legacy["grid_visibility"] = 2;
	legacy["show_rulers"] = false;
	legacy["grid_offset"] = Vector2(123, 456);
	legacy["smart_snap_active"] = false;

	CanvasItemEditorViewState restored;
	restored.zoom = 4.0;
	restored.view_offset = Point2(1, 2);
	CanvasItemEditorSceneGeometryState::apply(restored, legacy);

	CHECK(restored.zoom == state.zoom);
	CHECK(restored.view_offset == state.view_offset);
	CHECK(CanvasItemEditorSceneGeometryState::is_geometry_key("zoom"));
	CHECK(CanvasItemEditorSceneGeometryState::is_geometry_key("ofs"));
	CHECK(CanvasItemEditorSceneGeometryState::is_geometry_key("show_zoom_control"));
	CHECK_FALSE(CanvasItemEditorSceneGeometryState::is_geometry_key("snap_node_parent"));
}

} // namespace TestCanvasItemEditorFocusedAccessor
