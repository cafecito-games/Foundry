/**************************************************************************/
/*  editor_side_rail_strip.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "editor_side_rail_strip.h"

#include "editor/docks/editor_dock.h"
#include "editor/editor_string_names.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/gui/dock_tooltip.h"
#include "editor/gui/editor_side_rail_button.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/resources/font.h"

void EditorSideRailStrip::_rebuild_toggles() {
	for (EditorSideRailButton *button : toggle_buttons) {
		toggles_vbox->remove_child(button);
		button->queue_free();
	}
	toggle_buttons.clear();
	toggle_docks.clear();

	const Vector<EditorDock *> source_docks = dock_region->get_side_docks(side);
	for (EditorDock *dock : source_docks) {
		// EditorDock::is_enabled(), not Control::is_visible(): a right-side
		// dock's own visibility tracks TabContainer tab selection, not
		// availability, so it stays false for every non-current tab whether
		// or not that tab is enabled. set_dock_enabled hides a disabled dock
		// rather than removing it, so enabled-state changes on docks that
		// never appear as toggles must still rebuild the rail when they
		// later become enabled again.
		const Callable enabled_changed_callable = callable_mp(this, &EditorSideRailStrip::rebuild_toggles);
		if (!dock->is_connected("enabled_changed", enabled_changed_callable)) {
			dock->connect("enabled_changed", enabled_changed_callable, CONNECT_DEFERRED);
		}

		if (!dock->is_enabled()) {
			continue;
		}

		EditorSideRailButton *toggle = memnew(EditorSideRailButton);
		toggle->set_label_visible(label_mode == SideRailLabelMode::LABELLED);
		toggle->connect(SceneStringName(pressed), callable_mp(this, &EditorSideRailStrip::_toggle_pressed).bind(dock));

		toggles_vbox->add_child(toggle);
		toggle_buttons.push_back(toggle);
		toggle_docks.push_back(dock);

		_refresh_toggle((int)toggle_buttons.size() - 1);

		const Callable style_changed = callable_mp(this, &EditorSideRailStrip::_dock_style_changed).bind(dock);
		if (!dock->is_connected("_tab_style_changed", style_changed)) {
			dock->connect("_tab_style_changed", style_changed);
		}
	}

	if (close_button->get_parent() == toggles_vbox) {
		toggles_vbox->move_child(close_button, toggle_buttons.size());
	}

	_apply_label_mode();
	_update_active_states();
}

void EditorSideRailStrip::_refresh_toggle(int p_toggle_index) {
	EditorSideRailButton *toggle = toggle_buttons[p_toggle_index];
	EditorDock *dock = toggle_docks[p_toggle_index];

	toggle->set_rail_label(dock->get_display_title());

	Ref<Texture2D> icon = dock->get_dock_icon();
	if (icon.is_null()) {
		const StringName icon_name = dock->get_icon_name();
		if (icon_name != StringName() && has_theme_icon(icon_name, EditorStringName(EditorIcons))) {
			icon = get_editor_theme_icon(icon_name);
		}
	}
	toggle->set_rail_icon(icon);

	toggle->set_tooltip_text(dock_tooltip_with_shortcut_fallback(dock));

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

void EditorSideRailStrip::_dock_style_changed(EditorDock *p_dock) {
	const int64_t toggle_index = toggle_docks.find(p_dock);
	if (toggle_index < 0) {
		return;
	}
	_refresh_toggle((int)toggle_index);
}

void EditorSideRailStrip::_toggle_pressed(EditorDock *p_dock) {
	dock_region->press_rail_toggle(p_dock);
}

void EditorSideRailStrip::_close_pressed() {
	// Mirrors EditorBottomDrawerStrip::_close_pressed's
	// bottom_panel->hide_bottom_panel(): the side stops showing a dock. From
	// DOCKED that also collapses the side, which is the same transition as
	// pressing the shown dock's own toggle.
	dock_region->close_drawer(side);
}

void EditorSideRailStrip::_expand_pressed() {
	dock_region->set_side_mode(side, SideRailMode::DOCKED);
}

void EditorSideRailStrip::_update_active_states() {
	int active_toggle = -1;
	for (uint32_t i = 0; i < toggle_docks.size(); i++) {
		const bool shown = dock_region->is_dock_shown(toggle_docks[i]);
		toggle_buttons[i]->set_pressed_no_signal(shown);
		// A docked left side can show several docks at once; the close button
		// sits after the first of them.
		if (shown && active_toggle == -1) {
			active_toggle = (int)i;
		}
	}

	expand_button->set_visible(dock_region->get_side_mode(side) == SideRailMode::RAILED);

	if (active_toggle == -1) {
		close_button->hide();
		return;
	}

	close_button->show();
	// Sit the close button immediately after the active toggle.
	toggles_vbox->move_child(close_button, active_toggle + 1);
}

Size2 EditorSideRailStrip::get_minimum_size() const {
	const Size2 base = PanelContainer::get_minimum_size();
	if (!toggle_buttons.is_empty() || (expand_button && expand_button->is_visible())) {
		// A toggle or the expand button already gives PanelContainer's own
		// computation a real child to measure; nothing to floor.
		return base;
	}

	const Ref<Font> font = get_theme_font(SceneStringName(font));
	if (font.is_null()) {
		return base;
	}
	const int font_size = get_theme_font_size(SceneStringName(font_size));
	const real_t floor_extent = font->get_height(font_size) + 2 * EDSCALE;
	return base.max(Size2(floor_extent, floor_extent));
}

void EditorSideRailStrip::_apply_label_mode() {
	if (toggle_buttons.is_empty()) {
		return;
	}

	Vector<real_t> labelled_heights;
	labelled_heights.resize(toggle_buttons.size());
	for (uint32_t i = 0; i < toggle_buttons.size(); i++) {
		labelled_heights.write[i] = toggle_buttons[i]->get_labelled_minimum_size().height;
	}
	const SideRailFitThresholds thresholds = side_rail_fit_thresholds(labelled_heights);
	const SideRailLabelMode new_mode = side_rail_fit_label_mode(label_mode, get_size().height, thresholds);
	if (new_mode == label_mode) {
		return;
	}
	label_mode = new_mode;
	for (EditorSideRailButton *toggle : toggle_buttons) {
		toggle->set_label_visible(label_mode == SideRailLabelMode::LABELLED);
	}
}

void EditorSideRailStrip::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_THEME_CHANGED: {
			close_button->set_button_icon(get_editor_theme_icon(SNAME("Close")));
			expand_button->set_button_icon(get_editor_theme_icon(side == Side::LEFT ? SNAME("Forward") : SNAME("Back")));
			// Titles, icons and metrics depend on the locale and theme, so
			// refresh the toggles.
			_rebuild_toggles();
		} break;

		case NOTIFICATION_RESIZED: {
			_apply_label_mode();
		} break;
	}
}

void EditorSideRailStrip::rebuild_toggles() {
	_rebuild_toggles();
}

EditorSideRailStrip::EditorSideRailStrip(Side p_side, EditorTileDockRegion *p_dock_region) {
	side = p_side;
	dock_region = p_dock_region;
	ERR_FAIL_NULL(dock_region);

	if (side == Side::RIGHT) {
		active_source_tabs = dock_region->get_right_tabs();
	}

	// A stylebox type variation, not an override: an override applied from
	// NOTIFICATION_THEME_CHANGED re-fires the same notification and recurses
	// infinitely (see EditorBottomDrawerStrip).
	set_theme_type_variation(side == Side::LEFT ? "SideRailStripLeft" : "SideRailStripRight");

	main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	toggles_vbox = memnew(VBoxContainer);
	toggles_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(toggles_vbox);

	close_button = memnew(Button);
	close_button->set_theme_type_variation("FlatMenuButton");
	close_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	close_button->set_accessibility_name(TTRC("Close Drawer"));
	close_button->set_tooltip_text(TTRC("Close the open tile drawer."));
	close_button->hide();
	close_button->connect(SceneStringName(pressed), callable_mp(this, &EditorSideRailStrip::_close_pressed));
	toggles_vbox->add_child(close_button);

	// The far end of the rail; only meaningful while the side is RAILED.
	expand_button = memnew(Button);
	expand_button->set_theme_type_variation("FlatMenuButton");
	expand_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	expand_button->set_accessibility_name(TTRC("Expand Tile Drawer"));
	expand_button->set_tooltip_text(TTRC("Restore this side of the tile to its full width."));
	expand_button->hide();
	expand_button->connect(SceneStringName(pressed), callable_mp(this, &EditorSideRailStrip::_expand_pressed));
	main_vbox->add_child(expand_button);

	dock_region->set_side_changed_callback(side, callable_mp(this, &EditorSideRailStrip::_update_active_states));

	if (active_source_tabs) {
		active_source_tabs->connect("tab_changed", callable_mp(this, &EditorSideRailStrip::_update_active_states).unbind(1));
	}

	Node *mirrored_source = side == Side::RIGHT ? static_cast<Node *>(dock_region->get_right_tabs()) : static_cast<Node *>(dock_region->get_body());
	if (mirrored_source) {
		mirrored_source->connect("child_order_changed", callable_mp(this, &EditorSideRailStrip::rebuild_toggles), CONNECT_DEFERRED);
	}

	callable_mp(this, &EditorSideRailStrip::rebuild_toggles).call_deferred();
}
