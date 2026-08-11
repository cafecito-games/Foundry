#pragma once

#include "editor/gui/editor_scene_mode_switcher.h"

#include "scene/gui/button.h"

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

	switcher->set_mode_available(true);
	switcher->set_mode(SceneEditorMode::MODE_2D);
	CHECK(switcher->get_2d_button()->is_pressed());
	CHECK_FALSE(switcher->get_3d_button()->is_pressed());

	selected_count = 0;
	selected_mode = -1;
	switcher->connect("mode_selected", callable_mp_static(&record_selected_mode));
	switcher->get_3d_button()->emit_signal("pressed");
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
