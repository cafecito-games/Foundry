/**************************************************************************/
/*  editor_scene_tabs.h                                                   */
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

#include "scene/gui/margin_container.h"

class Button;
class HBoxContainer;
class MenuButton;
class Panel;
class PanelContainer;
class PopupMenu;
#include "editor/scene/editor_scene_tab_bar.h"
class TextureRect;

class EditorSceneTabs : public MarginContainer {
	FOUNDRY_CLASS(EditorSceneTabs, MarginContainer);

	inline static EditorSceneTabs *focused_singleton = nullptr;

public:
	enum {
		SCENE_SHOW_IN_FILESYSTEM = 1000, // Prevents conflicts with EditorNode options.
		SCENE_RUN,
		SCENE_CLOSE_OTHERS,
		SCENE_CLOSE_RIGHT,
	};

	// Shared across every tile strip so scene tabs can be dragged between tiles.
	// Cross-strip drops are mediated by EditorSceneTabBar / the drop overlay.
	static constexpr int SCENE_TABS_REARRANGE_GROUP = 100;

private:
	int tile_id = 0;

	PanelContainer *tabbar_panel = nullptr;
	HBoxContainer *tabbar_container = nullptr;

	EditorSceneTabBar *scene_tabs = nullptr;
	PopupMenu *scene_tabs_context_menu = nullptr;
	MenuButton *scene_list = nullptr;
	Button *scene_tab_add = nullptr;
	Control *scene_tab_add_ph = nullptr;

	Panel *tab_preview_panel = nullptr;
	TextureRect *tab_preview = nullptr;

	int last_hovered_tab = -1;

	void _scene_tab_changed(int p_tab);
	void _scene_tab_script_edited(int p_tab);
	void _scene_tab_closed(int p_tab);
	void _scene_tab_hovered(int p_tab);
	void _scene_tab_exit();
	void _scene_tab_input(const Ref<InputEvent> &p_input);
	void _scene_tabs_resized();

	void _update_tab_titles();
	void _reposition_active_tab(int p_to_index);
	void _update_context_menu();
	void _custom_menu_option(int p_option);
	void _update_scene_list();

	void _tab_preview_done(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, int p_tab);

	void _global_menu_scene(const Variant &p_tag);
	void _global_menu_new_window(const Variant &p_tag);

	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

protected:
	void _notification(int p_what);
	virtual void unhandled_key_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	static EditorSceneTabs *get_singleton() { return focused_singleton; }
	static void set_focused_singleton(EditorSceneTabs *p_tabs) { focused_singleton = p_tabs; }

	int get_tile_id() const { return tile_id; }
	TabBar *get_tab_bar() const { return scene_tabs; }

	void add_extra_button(Button *p_button);

	void set_current_tab(int p_tab);
	int get_current_tab() const;

	void update_scene_tabs();

	EditorSceneTabs(int p_tile_id = 0);
};
