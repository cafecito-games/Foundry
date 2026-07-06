/**************************************************************************/
/*  editor_main_screen.h                                                  */
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

#include "scene/gui/panel_container.h"

class Button;
class ConfigFile;
class EditorPlugin;
class HBoxContainer;
class VBoxContainer;

/**
 * Main-screen switcher for the editor shell.
 *
 * Scene modes (2D/3D) live in scene_mode_vbox and are reparented into the
 * focused workspace tile. Script lives in app_screen_vbox at the workspace
 * level. Game lives in global_screen_vbox and overlays the workspace region
 * as the single app-level running-game screen (see docs/superpowers/specs/
 * 2026-07-04-multi-scene-tiled-workspace-design.md).
 */
class EditorMainScreen : public PanelContainer {
	FOUNDRY_CLASS(EditorMainScreen, PanelContainer);

public:
	enum EditorTable {
		EDITOR_2D = 0,
		EDITOR_3D,
		EDITOR_SCRIPT,
		EDITOR_GAME,
	};

	enum ScreenPlacement {
		SCREEN_SCENE_MODE,
		SCREEN_APP,
		SCREEN_GLOBAL,
	};

private:
	VBoxContainer *scene_mode_vbox = nullptr;
	VBoxContainer *app_screen_vbox = nullptr;
	VBoxContainer *global_screen_vbox = nullptr;
	EditorPlugin *selected_plugin = nullptr;

	HBoxContainer *button_hb = nullptr;
	Vector<Button *> buttons;
	Vector<EditorPlugin *> editor_table;
	HashMap<String, EditorPlugin *> main_editor_plugins;

	// Script has no toolbar tab (it opens as a workspace leaf), so its availability
	// is tracked here instead of via its hidden button's visibility.
	bool app_screen_enabled = true;

	int _get_current_main_editor() const;
	ScreenPlacement _get_plugin_placement(const String &p_plugin_name) const;

protected:
	void _notification(int p_what);

public:
	void set_button_container(HBoxContainer *p_button_hb);

	void save_layout_to_config(Ref<ConfigFile> p_config_file, const String &p_section) const;
	void load_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section);

	void set_button_enabled(int p_index, bool p_enabled);
	bool is_button_enabled(int p_index) const;

	void select_next();
	void select_prev();
	void select_by_name(const String &p_name);
	void select(int p_index);
	int get_selected_index() const;
	int get_plugin_index(EditorPlugin *p_editor) const;
	EditorPlugin *get_selected_plugin() const;
	EditorPlugin *get_plugin_by_name(const String &p_plugin_name) const;
	bool can_auto_switch_screens() const;
	bool is_scene_mode_selected() const;
	bool is_global_screen_selected() const;

	VBoxContainer *get_scene_mode_control() const;
	VBoxContainer *get_app_screen_control() const;
	VBoxContainer *get_global_screen_control() const;
	VBoxContainer *get_control() const;

	void add_main_plugin(EditorPlugin *p_editor);
	void remove_main_plugin(EditorPlugin *p_editor);

	EditorMainScreen();
};
