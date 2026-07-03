/**************************************************************************/
/*  editor_scene_workspace.h                                              */
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

#include "scene/gui/box_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"

class ConfigFile;
class Control;
class EditorData;
class EditorSceneTabs;
class Label;
class PanelContainer;
class TextureRect;
class VBoxContainer;

class EditorScenePane : public VBoxContainer {
	FOUNDRY_CLASS(EditorScenePane, VBoxContainer);

	int pane_index = 0;
	EditorSceneTabs *scene_tabs = nullptr;
	Control *content_host = nullptr;
	SubViewportContainer *preview_container = nullptr;
	PanelContainer *preview_placeholder = nullptr;
	Label *preview_placeholder_label = nullptr;
	TextureRect *preview_placeholder_icon = nullptr;
	PanelContainer *focus_frame = nullptr;

	void _pane_gui_input(const Ref<InputEvent> &p_event);
	void _pane_focus_entered();
	void _fit_content_child(Control *p_child);

protected:
	void _notification(int p_what);

public:
	int get_pane_index() const { return pane_index; }
	EditorSceneTabs *get_scene_tabs() const { return scene_tabs; }
	Control *get_content_host() const { return content_host; }
	SubViewportContainer *get_preview_container() const { return preview_container; }

	void set_focused_visual(bool p_focused);
	void set_preview_mode(bool p_show_live_preview, bool p_show_3d_placeholder, const String &p_scene_name, const Ref<Texture2D> &p_icon);

	void setup(int p_pane_index);

	EditorScenePane();
};

class EditorSceneWorkspace : public Control {
	FOUNDRY_CLASS(EditorSceneWorkspace, Control);

	static inline const char *WORKSPACE_CONFIG_SECTION = "Workspace";

	SplitContainer *split = nullptr;
	Vector<EditorScenePane *> panes;
	int focused_pane = 0;
	bool split_vertical = false;

	void _create_pane(int p_index);
	void _configure_pane_layout(EditorScenePane *p_pane, bool p_in_split);
	void _ensure_split_offset();
	void _on_pane_focus_requested(int p_pane);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorSceneWorkspace *create_single_pane_workspace();

	void split_workspace(bool p_vertical);
	void unsplit_workspace();
	void set_focused_pane(int p_pane);
	int get_focused_pane() const { return focused_pane; }
	EditorScenePane *get_pane(int p_pane) const;
	int get_pane_count() const { return panes.size(); }
	bool is_split() const { return panes.size() > 1; }
	bool is_split_vertical() const { return split_vertical; }
	SplitContainer *get_split() const { return split; }

	void update_focus_visuals();

	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config);
	static int get_saved_pane_count(const Ref<ConfigFile> &p_config);
	static bool get_saved_split_vertical(const Ref<ConfigFile> &p_config);
	static int get_saved_split_offset(const Ref<ConfigFile> &p_config);
	static int get_saved_focused_pane(const Ref<ConfigFile> &p_config);
	static PackedStringArray get_saved_pane_scenes(const Ref<ConfigFile> &p_config, int p_pane);
	static String get_saved_pane_current(const Ref<ConfigFile> &p_config, int p_pane);

	EditorSceneWorkspace();
};
