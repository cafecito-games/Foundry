/**************************************************************************/
/*  test_editor_scene_mode_switcher.h                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "editor/gui/editor_scene_mode_switcher.h"

#include "scene/gui/button.h"
#include "scene/scene_string_names.h"

#include "tests/test_macros.h"

namespace TestEditorSceneModeSwitcher {

static int selected_count = 0;
static int selected_mode = -1;

static void record_selected_mode(int p_mode) {
	selected_count++;
	selected_mode = p_mode;
}

TEST_CASE("[Editor][SceneModeSwitcher] reflects mode and emits one user selection") {
	EditorSceneModeSwitcher *switcher = memnew(EditorSceneModeSwitcher);
	CHECK_FALSE(switcher->is_mode_available());
	CHECK(switcher->get_2d_button()->is_disabled());
	CHECK(switcher->get_3d_button()->is_disabled());

	selected_count = 0;
	selected_mode = -1;
	switcher->connect(SNAME("mode_selected"), callable_mp_static(&record_selected_mode));

	switcher->set_mode_available(true);
	switcher->set_mode(SceneEditorMode::MODE_2D);
	CHECK(switcher->get_2d_button()->is_pressed());
	CHECK_FALSE(switcher->get_3d_button()->is_pressed());
	CHECK(selected_count == 0);

	switcher->get_3d_button()->emit_signal(SceneStringName(pressed));
	CHECK(selected_count == 1);
	CHECK(selected_mode == int(SceneEditorMode::MODE_3D));

	memdelete(switcher);
}

TEST_CASE("[Editor][SceneModeSwitcher] hides when 3D is unavailable") {
	EditorSceneModeSwitcher *switcher = memnew(EditorSceneModeSwitcher);
	switcher->set_mode_available(true);
	switcher->set_3d_enabled(false);
	CHECK_FALSE(switcher->is_visible());
	switcher->set_3d_enabled(true);
	CHECK(switcher->is_visible());
	memdelete(switcher);
}

} // namespace TestEditorSceneModeSwitcher
