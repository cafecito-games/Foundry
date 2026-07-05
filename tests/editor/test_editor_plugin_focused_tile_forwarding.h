/**************************************************************************/
/*  test_editor_plugin_focused_tile_forwarding.h                          */
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

#include "editor/scene/canvas_item_editor_view.h"
#include "editor/scene/canvas_item_editor_view_state.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorPluginFocusedTileForwarding {

TEST_CASE("[Editor][plugin-forwarding] Primary canvas view accepts plugin forwarding") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	CanvasItemEditorViewState state;
	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, state));
	Control *host = memnew(Control);
	tree_root->add_child(host);
	host->add_child(view);
	view->build_ui(host, true);

	CHECK(view->is_plugin_forwarding_target());

	host->remove_child(view);
	tree_root->remove_child(host);
	memdelete(view);
	memdelete(host);
}

TEST_CASE("[Editor][plugin-forwarding] Secondary canvas view rejects plugin forwarding") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	CanvasItemEditorViewState state;
	CanvasItemEditorView *view = memnew(CanvasItemEditorView(nullptr, state));
	Control *host = memnew(Control);
	tree_root->add_child(host);
	host->add_child(view);
	view->build_ui(host, false);

	CHECK_FALSE(view->is_plugin_forwarding_target());

	host->remove_child(view);
	tree_root->remove_child(host);
	memdelete(view);
	memdelete(host);
}

} // namespace TestEditorPluginFocusedTileForwarding
