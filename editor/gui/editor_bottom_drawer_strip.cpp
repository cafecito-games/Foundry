/**************************************************************************/
/*  editor_bottom_drawer_strip.cpp                                        */
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

#include "editor_bottom_drawer_strip.h"

#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/editor_version_button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/separator.h"

void EditorBottomDrawerStrip::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_THEME_CHANGED: {
			close_button->set_button_icon(get_editor_theme_icon(SNAME("Close")));
			// Titles, icons and metrics depend on the locale and theme, so
			// refresh the toggles.
			_rebuild_toggles();
		} break;
	}
}

void EditorBottomDrawerStrip::_rebuild_toggles() {
	for (Button *button : toggle_buttons) {
		toggles_hbox->remove_child(button);
		button->queue_free();
	}
	toggle_buttons.clear();
	toggle_tab_indices.clear();
	toggle_docks.clear();

	const int tab_count = bottom_panel->get_tab_count();
	for (int i = 0; i < tab_count; i++) {
		EditorDock *dock = Object::cast_to<EditorDock>(bottom_panel->get_tab_control(i));
		if (!dock) {
			continue;
		}

		Button *toggle = memnew(Button);
		toggle->set_theme_type_variation("FlatMenuButton");
		toggle->set_toggle_mode(true);
		toggle->set_focus_mode(Control::FOCUS_ACCESSIBILITY);

		toggle->connect(SceneStringName(pressed), callable_mp(this, &EditorBottomDrawerStrip::_toggle_pressed).bind(i));
		toggle->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomDrawerStrip::_toggle_gui_input).bind(i));

		// Keep the close button as the last child so toggles keep their tab order.
		toggles_hbox->add_child(toggle);
		toggles_hbox->move_child(toggle, toggle_buttons.size());
		toggle_buttons.push_back(toggle);
		toggle_tab_indices.push_back(i);
		toggle_docks.push_back(dock);

		_refresh_toggle((int)toggle_buttons.size() - 1);

		// Track title/icon/color/shortcut changes (e.g. the debugger's error
		// tint) the way the dock manager restyles tabs. The connection lives on
		// the dock and looks the toggle up dynamically, so it stays valid across
		// rebuilds and goes away with the dock.
		const Callable style_changed = callable_mp(this, &EditorBottomDrawerStrip::_dock_style_changed).bind(dock);
		if (!dock->is_connected("_tab_style_changed", style_changed)) {
			dock->connect("_tab_style_changed", style_changed);
		}
	}

	_update_active_states();
}

void EditorBottomDrawerStrip::_refresh_toggle(int p_toggle_index) {
	Button *toggle = toggle_buttons[p_toggle_index];
	EditorDock *dock = toggle_docks[p_toggle_index];

	toggle->set_text(dock->get_display_title());

	Ref<Texture2D> icon = dock->get_dock_icon();
	if (icon.is_null()) {
		const StringName icon_name = dock->get_icon_name();
		if (icon_name != StringName() && has_theme_icon(icon_name, EditorStringName(EditorIcons))) {
			icon = get_editor_theme_icon(icon_name);
		}
	}
	toggle->set_button_icon(icon);

	String tooltip;
	Ref<Shortcut> shortcut = dock->get_dock_shortcut();
	if (shortcut.is_valid() && shortcut->has_valid_event()) {
		tooltip = TTR(shortcut->get_name()) + " (" + shortcut->get_as_text() + ")";
	}
	toggle->set_tooltip_text(tooltip);

	// Mirror TabBar::set_font_color_override_all semantics: a transparent
	// title color means "no override".
	const Color title_color = dock->get_title_color();
	if (title_color.a > 0) {
		toggle->add_theme_color_override(SceneStringName(font_color), title_color);
		toggle->add_theme_color_override(SNAME("font_hover_color"), title_color);
		toggle->add_theme_color_override(SNAME("font_pressed_color"), title_color);
		toggle->add_theme_color_override(SNAME("font_hover_pressed_color"), title_color);
		toggle->add_theme_color_override(SNAME("font_focus_color"), title_color);
	} else {
		toggle->remove_theme_color_override(SceneStringName(font_color));
		toggle->remove_theme_color_override(SNAME("font_hover_color"));
		toggle->remove_theme_color_override(SNAME("font_pressed_color"));
		toggle->remove_theme_color_override(SNAME("font_hover_pressed_color"));
		toggle->remove_theme_color_override(SNAME("font_focus_color"));
	}
}

void EditorBottomDrawerStrip::_dock_style_changed(EditorDock *p_dock) {
	const int64_t toggle_index = toggle_docks.find(p_dock);
	if (toggle_index < 0) {
		return;
	}
	_refresh_toggle((int)toggle_index);
}

void EditorBottomDrawerStrip::_toggle_pressed(int p_tab_index) {
	const int current = bottom_panel->get_current_tab();
	bottom_panel->set_current_tab(p_tab_index == current ? -1 : p_tab_index);
}

void EditorBottomDrawerStrip::_toggle_gui_input(const Ref<InputEvent> &p_event, int p_tab_index) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MouseButton::RIGHT) {
		return;
	}

	EditorDock *dock = Object::cast_to<EditorDock>(bottom_panel->get_tab_control(p_tab_index));
	if (!dock) {
		return;
	}

	EditorDockManager::get_singleton()->show_dock_context_popup(dock, get_screen_position() + get_local_mouse_position());
	accept_event();
}

void EditorBottomDrawerStrip::_close_pressed() {
	bottom_panel->hide_bottom_panel();
}

void EditorBottomDrawerStrip::_update_active_states() {
	const int current = bottom_panel->get_current_tab();

	int active_toggle = -1;
	for (uint32_t i = 0; i < toggle_buttons.size(); i++) {
		const bool active = toggle_tab_indices[i] == current;
		toggle_buttons[i]->set_pressed_no_signal(active);
		if (active) {
			active_toggle = (int)i;
		}
	}

	if (active_toggle == -1) {
		close_button->hide();
		return;
	}

	close_button->show();
	// Sit the close button immediately after the active toggle.
	toggles_hbox->move_child(close_button, active_toggle + 1);
}

void EditorBottomDrawerStrip::host_distraction_free_button(Button *p_button) {
	if (p_button->get_parent() != main_hbox) {
		if (p_button->get_parent()) {
			p_button->reparent(main_hbox);
		} else {
			main_hbox->add_child(p_button);
		}
	}
	// Keep the expand button as the strip's last control.
	main_hbox->move_child(p_button, main_hbox->get_child_count() - 2);
}

EditorBottomDrawerStrip::EditorBottomDrawerStrip(EditorBottomPanel *p_bottom_panel) {
	bottom_panel = p_bottom_panel;

	main_hbox = memnew(HBoxContainer);
	add_child(main_hbox);

	toggles_hbox = memnew(HBoxContainer);
	main_hbox->add_child(toggles_hbox);

	close_button = memnew(Button);
	close_button->set_theme_type_variation("FlatMenuButton");
	close_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	close_button->set_accessibility_name(TTRC("Close Bottom Drawer"));
	close_button->set_tooltip_text(TTRC("Close the open bottom drawer panel."));
	close_button->hide();
	close_button->connect(SceneStringName(pressed), callable_mp(this, &EditorBottomDrawerStrip::_close_pressed));
	toggles_hbox->add_child(close_button);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_hbox->add_child(spacer);

	EditorToaster *editor_toaster = memnew(EditorToaster);
	main_hbox->add_child(editor_toaster);

	main_hbox->add_child(memnew(VSeparator));

	EditorVersionButton *version_button = memnew(EditorVersionButton(EditorVersionButton::FORMAT_BASIC));
	// Fade out the version label to be less prominent, but still readable.
	version_button->set_self_modulate(Color(1, 1, 1, 0.65));
	version_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	main_hbox->add_child(version_button);

	// The pin and expand buttons are constructed and driven by EditorBottomPanel;
	// the strip only hosts them so they appear alongside the drawer controls.
	main_hbox->add_child(bottom_panel->get_pin_button());
	main_hbox->add_child(bottom_panel->get_expand_button());

	bottom_panel->get_tab_bar()->connect("tab_changed", callable_mp(this, &EditorBottomDrawerStrip::_update_active_states).unbind(1));
	bottom_panel->connect("child_order_changed", callable_mp(this, &EditorBottomDrawerStrip::_rebuild_toggles), CONNECT_DEFERRED);
}
