/**************************************************************************/
/*  editor_bottom_panel.h                                                 */
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

#include "editor/gui/bottom_drawer_layout.h"
#include "scene/gui/tab_container.h"

class Button;
class ConfigFile;
class EditorDock;
class Tween;

class EditorBottomPanel : public TabContainer {
	FOUNDRY_CLASS(EditorBottomPanel, TabContainer);

	Button *pin_button = nullptr;
	Button *expand_button = nullptr;
	Popup *layout_popup = nullptr;

	int previous_tab = -1;
	bool lock_panel_switching = false;
	bool drawer_expanded = false;
	bool grabber_dragging = false;
	int drag_start_body_height = 0;
	float drag_start_mouse_y = 0.0f;
	Control *grabber = nullptr;
	bool side_grabber_dragging = false;
	bool dragging_right_edge = false;
	int drag_start_island_width = 0;
	float drag_start_mouse_x = 0.0f;
	Control *left_grabber = nullptr;
	Control *right_grabber = nullptr;
	Ref<Tween> drawer_tween;
	float last_target_y = -1.0f;
	int drawer_current_x = 0;
	int drawer_current_width = 0;
	LocalVector<EditorDock *> bottom_docks;
	BottomDrawerLayoutState layout_state;

	LocalVector<Button *> legacy_buttons;
	void _on_button_visibility_changed(Button *p_button, EditorDock *p_dock);

	void _repaint();
	void _on_tab_changed(int p_idx);
	void _pin_button_toggled(bool p_pressed);
	void _expand_button_toggled(bool p_pressed);
	bool _is_current_pinned() const;
	int _get_drawer_area_height() const;
	int _get_body_height() const;
	int _get_island_width_override() const;
	void _set_body_height(int p_height);
	void _set_island_width(int p_width);
	void _reset_island_width();
	void _update_drawer_geometry();
	void _set_drawer_y(float p_y);
	void _update_side_grabber_geometry(float p_y, float p_height);
	void _hide_if_closed();
	void _grabber_input(const Ref<InputEvent> &p_event);
	void _side_grabber_input(const Ref<InputEvent> &p_event, bool p_right_edge);
	EditorDock *_get_dock_from_control(Control *p_control) const;

protected:
	void _notification(int p_what);
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

public:
	void save_layout_to_config(Ref<ConfigFile> p_config_file, const String &p_section) const;
	void load_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section);

	Button *add_item(String p_text, Control *p_item, const Ref<Shortcut> &p_shortcut = nullptr, bool p_at_front = false);
	void remove_item(Control *p_item);
	void make_item_visible(Control *p_item, bool p_visible = true, bool p_ignore_lock = false);
	void hide_bottom_panel();
	void toggle_last_opened_bottom_panel();
	void set_expanded(bool p_expanded);
	void set_switch_locked(bool p_locked);
	void _theme_changed();
	bool is_locked() const { return lock_panel_switching; }

	Button *get_pin_button() const { return pin_button; }
	Button *get_expand_button() const { return expand_button; }

	// The drawer's rect is fully manually driven and its height animates below
	// the content minimum while sliding (the bottom edge stays glued to the strip
	// line); reporting a zero minimum keeps Control::set_size from clamping the
	// rect back up mid-slide. Steady-state heights still respect the content
	// minimum via the body-height clamp.
	virtual Size2 get_minimum_size() const override { return Size2(); }

	void update_drawer_geometry();

	EditorBottomPanel();
	~EditorBottomPanel();
};
