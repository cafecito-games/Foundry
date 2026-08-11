#pragma once

#include "core/string/string_name.h"

enum class SceneEditorMode {
	MODE_2D,
	MODE_3D,
};

inline StringName scene_editor_mode_to_name(SceneEditorMode p_mode) {
	return p_mode == SceneEditorMode::MODE_3D ? StringName("3d") : StringName("2d");
}

inline bool scene_editor_mode_from_name(const StringName &p_name, SceneEditorMode &r_mode) {
	if (p_name == StringName("2d")) {
		r_mode = SceneEditorMode::MODE_2D;
		return true;
	}
	if (p_name == StringName("3d")) {
		r_mode = SceneEditorMode::MODE_3D;
		return true;
	}
	return false;
}
