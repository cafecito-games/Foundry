#pragma once

#include "editor/editor_scene_mode.h"

#include "scene/gui/box_container.h"

class Button;
class ButtonGroup;

class EditorSceneModeSwitcher : public HBoxContainer {
	FOUNDRY_CLASS(EditorSceneModeSwitcher, HBoxContainer);

	Ref<ButtonGroup> mode_group;
	Button *button_2d = nullptr;
	Button *button_3d = nullptr;
	SceneEditorMode mode = SceneEditorMode::MODE_2D;
	bool mode_available = false;
	bool enabled_3d = true;

	void _on_mode_pressed(int p_mode);
	void _sync_state();

protected:
	static void _bind_methods();

public:
	void set_mode(SceneEditorMode p_mode);
	SceneEditorMode get_mode() const { return mode; }
	void set_mode_available(bool p_available);
	bool is_mode_available() const { return mode_available; }
	void set_3d_enabled(bool p_enabled);
	bool is_3d_enabled() const { return enabled_3d; }
	Button *get_2d_button() const { return button_2d; }
	Button *get_3d_button() const { return button_3d; }

	EditorSceneModeSwitcher();
};
