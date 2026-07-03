/**************************************************************************/
/*  editor_bottom_panel.cpp                                               */
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

#include "editor_bottom_panel.h"

#include "editor/debugger/editor_debugger_node.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/gui/bottom_drawer_geometry.h"
#include "editor/gui/editor_bottom_drawer_strip.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/animation/tween.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/split_container.h"

void EditorBottomPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			layout_popup = get_popup();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			pin_button->set_button_icon(get_editor_theme_icon(SNAME("Pin")));
			expand_button->set_button_icon(get_editor_theme_icon(SNAME("ExpandBottomDock")));
			// Refresh the stylebox override first: a theme regeneration replaces
			// the island/panel styleboxes, and the geometry below measures the
			// applied stylebox's chrome. _theme_changed only touches the override
			// when it differs, so this cannot recurse.
			_theme_changed();
			_update_drawer_geometry();
		} break;
	}
}

void EditorBottomPanel::_on_tab_changed(int p_idx) {
	pin_button->set_pressed_no_signal(_is_current_pinned());
	// Repaint first: it swaps the panel stylebox override between the open and
	// collapsed styles, which the drawer geometry depends on. _repaint short-
	// circuits when switching between two already-open tabs, so refresh the
	// stylebox here too in case the new tab differs in pin state (island vs
	// square).
	_repaint();
	_theme_changed();
	_update_drawer_geometry();
}

void EditorBottomPanel::_theme_changed() {
	// The tab bar is hidden in favor of the status strip, so only the panel
	// stylebox needs swapping between the collapsed, floating-island and pinned
	// states. Unpinned open drawers render as a rounded, bordered island; pinned
	// ones keep the square panel style flush over the workspace column.
	//
	// This runs from NOTIFICATION_THEME_CHANGED, and applying an override from
	// there re-fires the same notification, so only touch the override when the
	// applied stylebox actually differs; the re-entrant pass then resolves the
	// same stylebox and no-ops, converging after at most one extra pass.
	if (get_current_tab() == -1) {
		// Hide panel when not showing anything.
		if (has_theme_stylebox_override(SceneStringName(panel))) {
			remove_theme_style_override(SceneStringName(panel));
		}
		return;
	}
	const Ref<StyleBox> desired_style = _is_current_pinned()
			? get_theme_stylebox(SNAME("BottomPanel"), EditorStringName(EditorStyles))
			: get_theme_stylebox(SNAME("BottomDrawerIsland"), EditorStringName(EditorStyles));
	if (!has_theme_stylebox_override(SceneStringName(panel)) || get_theme_stylebox(SceneStringName(panel)) != desired_style) {
		add_theme_style_override(SceneStringName(panel), desired_style);
	}
}

bool EditorBottomPanel::_is_current_pinned() const {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return false;
	}
	return layout_state.is_pinned(dock->get_effective_layout_key());
}

int EditorBottomPanel::_get_drawer_area_height() const {
	// The drawer region is gui_base's rect above the status strip. The strip is
	// a separate control, so it contributes nothing to this height.
	Control *base = get_parent_control();
	EditorBottomDrawerStrip *strip = EditorNode::get_bottom_drawer_strip();
	if (!base || !strip) {
		return 0;
	}
	return MAX(0, int(strip->get_global_rect().position.y - base->get_global_rect().position.y));
}

int EditorBottomPanel::_get_body_height() const {
	Control *tab_control = get_current_tab_control();
	if (!tab_control) {
		return 0;
	}
	const int min_body = tab_control->get_combined_minimum_size().height;
	int stored = min_body;
	EditorDock *dock = Object::cast_to<EditorDock>(tab_control);
	if (dock) {
		stored = layout_state.get_offset(dock->get_effective_layout_key(), min_body);
	}
	return BottomDrawerGeometry::clamp_body_height(stored, min_body, 0, _get_drawer_area_height());
}

int EditorBottomPanel::_get_island_width_override() const {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return 0;
	}
	return layout_state.get_width(dock->get_effective_layout_key());
}

void EditorBottomPanel::_set_body_height(int p_height) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return;
	}
	const int min_body = get_current_tab_control()->get_combined_minimum_size().height;
	layout_state.set_offset(dock->get_effective_layout_key(), BottomDrawerGeometry::clamp_body_height(p_height, min_body, 0, _get_drawer_area_height()));
	_update_drawer_geometry();
}

void EditorBottomPanel::_set_island_width(int p_width) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return;
	}
	Control *base = get_parent_control();
	if (!base) {
		return;
	}
	const int window_width = int(base->get_global_rect().size.width);
	layout_state.set_width(dock->get_effective_layout_key(), BottomDrawerGeometry::clamp_island_width(p_width, window_width, 480 * EDSCALE, 48 * EDSCALE));
	_update_drawer_geometry();
}

void EditorBottomPanel::_reset_island_width() {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return;
	}
	layout_state.erase_width(dock->get_effective_layout_key());
	_update_drawer_geometry();
}

void EditorBottomPanel::update_drawer_geometry() {
	_update_drawer_geometry();
}

void EditorBottomPanel::_update_drawer_geometry() {
	Control *base = get_parent_control();
	EditorBottomDrawerStrip *strip = EditorNode::get_bottom_drawer_strip();
	VSplitContainer *top_split = EditorNode::get_top_split();
	if (!base || !strip || !top_split) {
		return;
	}

	const bool open = get_current_tab() != -1;
	const bool pinned = _is_current_pinned();

	// The drawer occupies gui_base's region above the status strip; positions are
	// expressed in gui_base coordinates. All rects come from global space so the
	// math is independent of the panel's own (manually driven) rect.
	const Rect2 base_rect = base->get_global_rect();
	const int area_height = _get_drawer_area_height();
	const int window_width = int(base_rect.size.width);

	int height = 0;
	if (open) {
		// The drawer's total height is the stored body height plus the panel
		// stylebox chrome around the dock content, so the content region keeps
		// exactly the stored height.
		int chrome_height = 0;
		Ref<StyleBox> panel_style = get_theme_stylebox(SceneStringName(panel));
		if (panel_style.is_valid()) {
			chrome_height = panel_style->get_minimum_size().height;
		}
		const int body_height = _get_body_height();
		height = BottomDrawerGeometry::island_height(drawer_expanded, body_height + chrome_height, area_height, 24 * EDSCALE);

		int width = 0;
		int x = 0;
		if (pinned) {
			// Pinned: align flush over the center workspace column.
			const Rect2 column_rect = top_split->get_global_rect();
			x = int(column_rect.position.x - base_rect.position.x);
			width = int(column_rect.size.width);
		} else {
			// Unpinned: a horizontally centered floating island.
			width = BottomDrawerGeometry::island_width_with_override(window_width, 480 * EDSCALE, 48 * EDSCALE, _get_island_width_override());
			x = BottomDrawerGeometry::island_x(window_width, width);
		}
		drawer_current_x = x;
		drawer_current_width = width;
	}
	const bool show_side_grabbers = open && !pinned;
	left_grabber->set_visible(show_side_grabbers);
	right_grabber->set_visible(show_side_grabbers);
	grabber->set_visible(open && !drawer_expanded);

	// Only a pinned, non-expanded open drawer reserves workspace space. The strip
	// is a separate control outside center_overlay, so it contributes nothing to
	// the inset.
	const int inset = (open && pinned && !drawer_expanded) ? height : 0;
	top_split->set_offset(SIDE_BOTTOM, -inset);

	// The slide animates the island's y: open lifts it from the strip line up to
	// (area_height - height); close slides it back down to the strip line and
	// then hides the panel. A single logical open/close can fire several geometry
	// updates in one frame (stylebox swaps, deferred layout), so only (re)start
	// the tween when the target y actually changes; leave an in-flight tween
	// running on a same-target settling call so the slide is not snapped to its
	// end on the same frame it began.
	const float target_y = open ? float(area_height - height) : float(area_height);
	// A close only animates when the panel is actually on screen; a closed,
	// already-hidden drawer (e.g. at startup) settles instantly with no slide.
	const bool animate = !pinned && !grabber_dragging && !side_grabber_dragging && is_inside_tree() && (open || is_visible()) && bool(EDITOR_GET("interface/editor/animate_bottom_drawer"));
	const bool target_changed = last_target_y != target_y;
	if (drawer_tween.is_valid() && (!animate || target_changed)) {
		drawer_tween->kill();
		drawer_tween.unref();
	}

	if (open && !is_visible()) {
		// Seed an opening drawer at the strip line so it rises into view.
		_set_drawer_y(area_height);
		show();
	}

	if (animate && target_changed) {
		drawer_tween = create_tween();
		drawer_tween->tween_method(callable_mp(this, &EditorBottomPanel::_set_drawer_y), get_position().y, target_y, 0.15)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
		if (!open) {
			drawer_tween->tween_callback(callable_mp(this, &EditorBottomPanel::_hide_if_closed));
		}
	} else if (drawer_tween.is_null() || !drawer_tween->is_running()) {
		// No slide in flight: place the drawer (and grabber) at the target now.
		_set_drawer_y(target_y);
		if (!open) {
			hide();
		}
	}
	last_target_y = target_y;
}

void EditorBottomPanel::_set_drawer_y(float p_y) {
	// Derive the height from y so the bottom edge stays glued to the strip line
	// throughout the slide: the drawer emerges from the strip instead of sliding
	// over it. At rest this equals the computed target height by construction
	// (target_y = area_height - height). Requires the zero minimum size reported
	// by get_minimum_size(); Control::set_size clamps to the minimum otherwise
	// and would extend the rect below the strip.
	const float height = MAX(0.0f, float(_get_drawer_area_height()) - p_y);
	set_position(Point2(drawer_current_x, p_y));
	set_size(Size2(drawer_current_width, height));

	// Track the grabber to the panel's animated top edge instead of the final
	// target, so it slides with the drawer rather than jumping ahead of it.
	Control *base = get_parent_control();
	if (!base) {
		return;
	}
	const int grabber_height = 6 * EDSCALE;
	grabber->set_size(Vector2(get_size().width, grabber_height));
	grabber->set_global_position(base->get_global_position() + Vector2(drawer_current_x, p_y));
	_update_side_grabber_geometry(p_y, height);
}

void EditorBottomPanel::_update_side_grabber_geometry(float p_y, float p_height) {
	Control *base = get_parent_control();
	if (!base) {
		return;
	}
	const int grabber_width = 6 * EDSCALE;
	const int top_grabber_height = 6 * EDSCALE;
	const bool top_grabber_visible = grabber->is_visible();
	const float side_y = top_grabber_visible ? p_y + top_grabber_height : p_y;
	const float side_height = top_grabber_visible ? MAX(0.0f, p_height - top_grabber_height) : p_height;

	left_grabber->set_size(Vector2(grabber_width, side_height));
	right_grabber->set_size(Vector2(grabber_width, side_height));

	const Vector2 base_pos = base->get_global_position();
	left_grabber->set_global_position(base_pos + Vector2(drawer_current_x, side_y));
	right_grabber->set_global_position(base_pos + Vector2(drawer_current_x + drawer_current_width - grabber_width, side_y));
}

void EditorBottomPanel::_hide_if_closed() {
	if (get_current_tab() == -1) {
		hide();
	}
}

void EditorBottomPanel::_grabber_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			grabber_dragging = true;
			drag_start_mouse_y = grabber->get_global_position().y + mb->get_position().y;
			drag_start_body_height = _get_body_height();
		} else {
			grabber_dragging = false;
			EditorNode::get_singleton()->save_editor_layout_delayed();
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && grabber_dragging) {
		const float mouse_y = grabber->get_global_position().y + mm->get_position().y;
		_set_body_height(drag_start_body_height + int(drag_start_mouse_y - mouse_y));
	}
}

void EditorBottomPanel::_side_grabber_input(const Ref<InputEvent> &p_event, bool p_right_edge) {
	Control *edge_grabber = p_right_edge ? right_grabber : left_grabber;
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_double_click()) {
			_reset_island_width();
			EditorNode::get_singleton()->save_editor_layout_delayed();
			return;
		}
		if (mb->is_pressed()) {
			side_grabber_dragging = true;
			dragging_right_edge = p_right_edge;
			drag_start_mouse_x = edge_grabber->get_global_position().x + mb->get_position().x;
			drag_start_island_width = drawer_current_width;
		} else {
			side_grabber_dragging = false;
			EditorNode::get_singleton()->save_editor_layout_delayed();
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && side_grabber_dragging && dragging_right_edge == p_right_edge) {
		Control *base = get_parent_control();
		if (!base) {
			return;
		}
		const float mouse_x = edge_grabber->get_global_position().x + mm->get_position().x;
		const int mouse_delta = int(mouse_x - drag_start_mouse_x);
		const int window_width = int(base->get_global_rect().size.width);
		_set_island_width(BottomDrawerGeometry::island_width_from_edge_drag(drag_start_island_width, mouse_delta, p_right_edge, window_width, 480 * EDSCALE, 48 * EDSCALE));
	}
}

void EditorBottomPanel::shortcut_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (k.is_null() || !k->is_action_pressed(SNAME("ui_cancel"), false, true)) {
		return;
	}
	if (get_current_tab() == -1 || _is_current_pinned()) {
		return;
	}
	Control *focus_owner = get_viewport()->gui_get_focus_owner();
	if (!focus_owner || !is_ancestor_of(focus_owner)) {
		return;
	}
	hide_bottom_panel();
	get_viewport()->set_input_as_handled();
}

void EditorBottomPanel::_repaint() {
	bool panel_collapsed = get_current_tab() == -1;

	if (panel_collapsed && get_popup()) {
		set_popup(nullptr);
	} else if (!panel_collapsed && !get_popup()) {
		set_popup(layout_popup);
	}
	if (!panel_collapsed && (previous_tab != -1)) {
		return;
	}
	previous_tab = get_current_tab();

	pin_button->set_visible(!panel_collapsed);
	expand_button->set_visible(!panel_collapsed);
	if (expand_button->is_pressed()) {
		_expand_button_toggled(!panel_collapsed);
	} else {
		_theme_changed();
	}
}

void EditorBottomPanel::save_layout_to_config(Ref<ConfigFile> p_config_file, const String &p_section) const {
	layout_state.save_to_config(p_config_file, p_section);
}

void EditorBottomPanel::load_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
	layout_state.load_from_config(p_config_file, p_section);
	pin_button->set_pressed_no_signal(_is_current_pinned());

	_update_drawer_geometry();
}

void EditorBottomPanel::make_item_visible(Control *p_item, bool p_visible, bool p_ignore_lock) {
	// Don't allow changing tabs involuntarily when tabs are locked.
	if (!p_ignore_lock && lock_panel_switching && get_current_tab() != -1) {
		return;
	}

	EditorDock *dock = _get_dock_from_control(p_item);
	ERR_FAIL_NULL(dock);
	dock->set_visible(p_visible);
}

void EditorBottomPanel::hide_bottom_panel() {
	set_current_tab(-1);
}

void EditorBottomPanel::toggle_last_opened_bottom_panel() {
	set_current_tab(get_current_tab() == -1 ? get_previous_tab() : -1);
}

void EditorBottomPanel::_pin_button_toggled(bool p_pressed) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (dock) {
		layout_state.set_pinned(dock->get_effective_layout_key(), p_pressed);
	}
	// Swap between the island and square panel styles before repositioning.
	_theme_changed();
	_update_drawer_geometry();
	EditorNode::get_singleton()->save_editor_layout_delayed();
}

void EditorBottomPanel::set_switch_locked(bool p_locked) {
	lock_panel_switching = p_locked;
}

void EditorBottomPanel::set_expanded(bool p_expanded) {
	expand_button->set_pressed(p_expanded);
}

void EditorBottomPanel::_expand_button_toggled(bool p_pressed) {
	drawer_expanded = p_pressed;
	_update_drawer_geometry();

	Button *distraction_free = EditorNode::get_singleton()->get_distraction_free_button();
	distraction_free->set_meta("_scene_tabs_owned", !p_pressed);
	EditorNode::get_singleton()->update_distraction_free_button_theme();
	if (p_pressed) {
		EditorNode::get_bottom_drawer_strip()->host_distraction_free_button(distraction_free);
	} else {
		distraction_free->get_parent()->remove_child(distraction_free);
		EditorSceneTabs::get_singleton()->add_extra_button(distraction_free);
	}
	_theme_changed();
}

EditorDock *EditorBottomPanel::_get_dock_from_control(Control *p_control) const {
	return Object::cast_to<EditorDock>(p_control->get_parent());
}

Button *EditorBottomPanel::add_item(String p_text, Control *p_item, const Ref<Shortcut> &p_shortcut, bool p_at_front) {
	EditorDock *dock = memnew(EditorDock);
	dock->add_child(p_item);
	dock->set_title(p_text);
	dock->set_dock_shortcut(p_shortcut);
	dock->set_global(false);
	dock->set_transient(true);
	dock->set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	dock->set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL);
	EditorDockManager::get_singleton()->add_dock(dock);
	bottom_docks.push_back(dock);

	p_item->show(); // Compatibility in case it was hidden.

	// Still return a dummy button for compatibility reasons.
	Button *tb = memnew(Button);
	tb->set_toggle_mode(true);
	tb->connect(SceneStringName(visibility_changed), callable_mp(this, &EditorBottomPanel::_on_button_visibility_changed).bind(tb, dock));
	legacy_buttons.push_back(tb);
	return tb;
}

void EditorBottomPanel::remove_item(Control *p_item) {
	EditorDock *dock = _get_dock_from_control(p_item);
	ERR_FAIL_NULL_MSG(dock, vformat("Cannot remove unknown dock \"%s\" from the bottom panel.", p_item->get_name()));

	int item_idx = bottom_docks.find(dock);
	ERR_FAIL_COND(item_idx == -1);

	bottom_docks.remove_at(item_idx);

	legacy_buttons[item_idx]->queue_free();
	legacy_buttons.remove_at(item_idx);

	EditorDockManager::get_singleton()->remove_dock(dock);
	dock->remove_child(p_item);
	dock->queue_free();
}

void EditorBottomPanel::_on_button_visibility_changed(Button *p_button, EditorDock *p_dock) {
	if (p_button->is_visible()) {
		p_dock->open();
	} else {
		p_dock->close();
	}
}

EditorBottomPanel::EditorBottomPanel() {
	get_tab_bar()->connect("tab_changed", callable_mp(this, &EditorBottomPanel::_on_tab_changed));
	set_tabs_position(TabPosition::POSITION_BOTTOM);
	set_tabs_visible(false);
	set_deselect_enabled(true);
	set_process_shortcut_input(true);
	// The slide animates the panel's height below its content minimum, squeezing
	// the current dock; clipping hides the squeezed content past the bottom edge
	// so the drawer emerges from the strip cleanly.
	set_clip_contents(true);
	// The drawer starts collapsed: it is hidden until a tab is opened, and the
	// status strip is its collapsed representation.
	hide();

	grabber = memnew(Control);
	grabber->set_name("DrawerGrabber");
	// Top-level so the TabContainer base does not treat the grabber as a tab page.
	// Its rect is driven manually from _update_drawer_geometry to track the drawer's top edge.
	grabber->set_as_top_level(true);
	add_child(grabber, false, Node::INTERNAL_MODE_BACK);
	grabber->set_default_cursor_shape(Control::CURSOR_VSIZE);
	grabber->hide();
	grabber->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomPanel::_grabber_input));

	left_grabber = memnew(Control);
	left_grabber->set_name("DrawerLeftGrabber");
	left_grabber->set_as_top_level(true);
	add_child(left_grabber, false, Node::INTERNAL_MODE_BACK);
	left_grabber->set_default_cursor_shape(Control::CURSOR_HSIZE);
	left_grabber->hide();
	left_grabber->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomPanel::_side_grabber_input).bind(false));

	right_grabber = memnew(Control);
	right_grabber->set_name("DrawerRightGrabber");
	right_grabber->set_as_top_level(true);
	add_child(right_grabber, false, Node::INTERNAL_MODE_BACK);
	right_grabber->set_default_cursor_shape(Control::CURSOR_HSIZE);
	right_grabber->hide();
	right_grabber->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomPanel::_side_grabber_input).bind(true));

	// The pin and expand buttons are hosted by EditorBottomDrawerStrip, which
	// reparents them into itself; they are created here without a parent so the
	// panel keeps owning all of their toggle logic.
	pin_button = memnew(Button);
	pin_button->hide();
	pin_button->set_theme_type_variation("BottomPanelButton");
	pin_button->set_toggle_mode(true);
	pin_button->set_accessibility_name(TTRC("Dock Bottom Drawer"));
	pin_button->set_tooltip_text(TTRC("Dock the bottom drawer, pushing the workspace up instead of covering it."));
	pin_button->connect(SceneStringName(toggled), callable_mp(this, &EditorBottomPanel::_pin_button_toggled));

	expand_button = memnew(Button);
	expand_button->hide();
	expand_button->set_theme_type_variation("BottomPanelButton");
	expand_button->set_toggle_mode(true);
	expand_button->set_accessibility_name(TTRC("Expand Bottom Panel"));
	expand_button->set_shortcut(ED_SHORTCUT_AND_COMMAND("editor/bottom_panel_expand", TTRC("Expand Bottom Panel"), KeyModifierMask::SHIFT | Key::F12));
	expand_button->connect(SceneStringName(toggled), callable_mp(this, &EditorBottomPanel::_expand_button_toggled));

	callable_mp(this, &EditorBottomPanel::_repaint).call_deferred();
}

EditorBottomPanel::~EditorBottomPanel() {
	for (Button *b : legacy_buttons) {
		memdelete(b);
	}
}
