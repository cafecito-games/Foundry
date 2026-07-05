/**************************************************************************/
/*  test_canvas_item_editor_secondary_view.h                              */
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

#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/scene/canvas_item_editor_view.h"
#include "editor/scene/canvas_item_editor_view_state.h"

#include "scene/gui/control.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestCanvasItemEditorSecondaryView {

TEST_CASE("[SceneTree][Editor] tile-preview-mode-prefers-canvas-view-host") {
	Control *host = memnew(Control);
	host->set_custom_minimum_size(Size2(640, 480));
	SceneTree::get_singleton()->get_root()->add_child(host);

	EditorData editor_data;
	EditorSelection *selection = memnew(EditorSelection);
	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data);
	host->add_child(workspace);
	workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	host->set_size(Size2(640, 480));
	workspace->set_size(Size2(640, 480));
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	ScenePaneTile *tile = workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
	REQUIRE(tile->get_preview_container() != nullptr);

	tile->set_preview_mode(TilePreviewMode::LIVE_2D);
	CHECK(tile->get_preview_container()->is_visible());

	CanvasItemEditorViewState view_state;
	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, view_state));
	tile->set_canvas_view(view);
	tile->set_preview_mode(TilePreviewMode::LIVE_2D);

	CHECK_FALSE(tile->get_preview_container()->is_visible());

	tile->set_canvas_view(nullptr);
	tile->set_preview_mode(TilePreviewMode::LIVE_2D);
	CHECK(tile->get_preview_container()->is_visible());

	memdelete(view);
	memdelete(workspace);
	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
	memdelete(selection);
}

} // namespace TestCanvasItemEditorSecondaryView
