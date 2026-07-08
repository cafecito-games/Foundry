/**************************************************************************/
/*  test_editor_data.h                                                    */
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

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestEditorData {

// Closing the only scene in the focused tile while another tile still holds a
// scene must never transiently repoint current_edited_scene to the -1 sentinel:
// anything that runs during the removal window (scene_closed handlers, plugin
// notifications) would otherwise read an out-of-bounds current scene and log a
// transient "current_edited_scene = -1 is out of bounds" error. Regression for
// the close-transition follow-up to the docked-tile close hardening (#1042).
TEST_CASE("[EditorData][Editor] remove-current-scene-keeps-current-valid-while-scenes-remain") {
	EditorData editor_data;

	// Tile 0 holds a scene.
	editor_data.set_focused_tile_id(0);
	const int scene_in_first_tile = editor_data.add_edited_scene(-1);

	// A second (docked) tile holds another scene and is the focused/current one.
	editor_data.register_tile(1);
	editor_data.set_focused_tile_id(1);
	const int scene_in_docked_tile = editor_data.add_edited_scene(-1);

	REQUIRE(editor_data.get_edited_scene_count() == 2);
	REQUIRE(editor_data.get_edited_scene() == scene_in_docked_tile);
	REQUIRE(editor_data.get_focused_tile_id() == 1);

	ErrorDetector error_detector;

	// Remove the docked tile's only scene. The first tile's scene survives.
	editor_data.remove_scene(scene_in_docked_tile);

	// A scene still exists, so current must reference it, never -1.
	CHECK(editor_data.get_edited_scene_count() == 1);
	CHECK(editor_data.get_edited_scene() >= 0);
	CHECK(editor_data.get_edited_scene() < editor_data.get_edited_scene_count());
	CHECK(editor_data.get_edited_scene() == scene_in_first_tile);
	CHECK_FALSE(error_detector.has_error);
}

// The opposite guard: removing the last remaining scene legitimately leaves no
// current scene (the script-only/empty state), so the repoint must still land on
// -1 rather than being forced onto a stale index.
TEST_CASE("[EditorData][Editor] remove-last-scene-leaves-no-current-scene") {
	EditorData editor_data;

	editor_data.set_focused_tile_id(0);
	const int only_scene = editor_data.add_edited_scene(-1);
	REQUIRE(editor_data.get_edited_scene_count() == 1);

	ErrorDetector error_detector;

	editor_data.remove_scene(only_scene);

	CHECK(editor_data.get_edited_scene_count() == 0);
	CHECK(editor_data.get_edited_scene() == -1);
	CHECK(editor_data.get_scene_path(-1).is_empty());
	CHECK_FALSE(editor_data.is_scene_changed(-1));
	CHECK(editor_data.get_scene_root_script(editor_data.get_edited_scene()).is_null());
	CHECK(editor_data.get_edited_scene_live_edit_root().is_empty());
	CHECK_FALSE(error_detector.has_error);
}

} // namespace TestEditorData
