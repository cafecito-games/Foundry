#include "editor_scene_mode_switcher.h"

#include "scene/gui/base_button.h"
#include "scene/gui/button.h"

void EditorSceneModeSwitcher::_bind_methods() {
	ADD_SIGNAL(MethodInfo("mode_selected", PropertyInfo(Variant::INT, "mode")));
}

void EditorSceneModeSwitcher::_sync_state() {
	button_2d->set_disabled(!mode_available);
	button_3d->set_disabled(!mode_available || !enabled_3d);
	button_2d->set_pressed_no_signal(mode == SceneEditorMode::MODE_2D);
	button_3d->set_pressed_no_signal(mode == SceneEditorMode::MODE_3D);
	set_visible(enabled_3d);
}

void EditorSceneModeSwitcher::_on_mode_pressed(int p_mode) {
	const SceneEditorMode selected_mode = SceneEditorMode(p_mode);
	if (!mode_available || (selected_mode == SceneEditorMode::MODE_3D && !enabled_3d)) {
		_sync_state();
		return;
	}
	mode = selected_mode;
	_sync_state();
	emit_signal("mode_selected", int(mode));
}

void EditorSceneModeSwitcher::set_mode(SceneEditorMode p_mode) {
	mode = p_mode;
	_sync_state();
}

void EditorSceneModeSwitcher::set_mode_available(bool p_available) {
	mode_available = p_available;
	_sync_state();
}

void EditorSceneModeSwitcher::set_3d_enabled(bool p_enabled) {
	enabled_3d = p_enabled;
	if (!enabled_3d && mode == SceneEditorMode::MODE_3D) {
		mode = SceneEditorMode::MODE_2D;
	}
	_sync_state();
}

EditorSceneModeSwitcher::EditorSceneModeSwitcher() {
	set_theme_type_variation("SceneModeSwitcher");
	add_theme_constant_override("separation", 1);
	mode_group.instantiate();
	mode_group->set_allow_unpress(false);

	button_2d = memnew(Button("2D"));
	button_2d->set_toggle_mode(true);
	button_2d->set_button_group(mode_group);
	button_2d->set_theme_type_variation("SceneModeButton");
	button_2d->set_focus_mode(FOCUS_ACCESSIBILITY);
	button_2d->set_accessibility_name(TTRC("2D Scene Mode"));
	button_2d->connect(
			"pressed",
			callable_mp(this, &EditorSceneModeSwitcher::_on_mode_pressed).bind(int(SceneEditorMode::MODE_2D)));
	add_child(button_2d);

	button_3d = memnew(Button("3D"));
	button_3d->set_toggle_mode(true);
	button_3d->set_button_group(mode_group);
	button_3d->set_theme_type_variation("SceneModeButton");
	button_3d->set_focus_mode(FOCUS_ACCESSIBILITY);
	button_3d->set_accessibility_name(TTRC("3D Scene Mode"));
	button_3d->connect(
			"pressed",
			callable_mp(this, &EditorSceneModeSwitcher::_on_mode_pressed).bind(int(SceneEditorMode::MODE_3D)));
	add_child(button_3d);

	_sync_state();
}
