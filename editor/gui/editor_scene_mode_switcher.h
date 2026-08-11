/**************************************************************************/
/*  editor_scene_mode_switcher.h                                          */
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
