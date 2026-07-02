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
#include "editor/gui/editor_toaster.h"
#include "editor/gui/editor_version_button.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/animation/tween.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"

void EditorBottomPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			layout_popup = get_popup();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			pin_button->set_button_icon(get_editor_theme_icon(SNAME("Pin")));
			expand_button->set_button_icon(get_editor_theme_icon(SNAME("ExpandBottomDock")));
			_update_drawer_geometry();
		} break;
	}
}

void EditorBottomPanel::_on_tab_changed(int p_idx) {
	pin_button->set_pressed_no_signal(_is_current_pinned());
	// Repaint first: it swaps the panel stylebox override between the open and
	// collapsed styles, which the drawer geometry depends on.
	_repaint();
	_update_drawer_geometry();
}

void EditorBottomPanel::_theme_changed() {
	int icon_width = get_theme_constant(SNAME("class_icon_size"), EditorStringName(Editor));
	int margin = bottom_hbox->get_minimum_size().width;
	if (get_popup()) {
		margin -= icon_width;
	}

	// Add margin to make space for the right side popup button.
	icon_spacer->set_custom_minimum_size(Vector2(icon_width, 0));

	// Need to get stylebox from EditorNode to update theme correctly.
	Ref<StyleBox> bottom_tabbar_style = EditorNode::get_singleton()->get_editor_theme()->get_stylebox(SNAME("tabbar_background"), SNAME("BottomPanel"))->duplicate();
	bottom_tabbar_style->set_content_margin(is_layout_rtl() ? SIDE_LEFT : SIDE_RIGHT, margin + bottom_tabbar_style->get_content_margin(is_layout_rtl() ? SIDE_RIGHT : SIDE_LEFT));
	add_theme_style_override("tabbar_background", bottom_tabbar_style);

	if (get_current_tab() == -1) {
		// Hide panel when not showing anything.
		remove_theme_style_override(SceneStringName(panel));
	} else {
		add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("BottomPanel"), EditorStringName(EditorStyles)));
	}
}

bool EditorBottomPanel::_is_current_pinned() const {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return false;
	}
	HashMap<String, bool>::ConstIterator E = dock_pinned.find(dock->get_effective_layout_key());
	return E ? E->value : pinned_by_default;
}

int EditorBottomPanel::_get_strip_height() const {
	int height = get_tab_bar()->get_combined_minimum_size().height;
	Ref<StyleBox> tabbar_style = get_theme_stylebox(SNAME("tabbar_background"));
	if (tabbar_style.is_valid()) {
		height += tabbar_style->get_minimum_size().height;
	}
	// TabContainer's minimum size also includes the panel stylebox, which
	// differs between the open and collapsed states; without it the anchored
	// rect falls below the minimum and the strip grows past the overlay edge.
	Ref<StyleBox> panel_style = get_theme_stylebox(SceneStringName(panel));
	if (panel_style.is_valid()) {
		height += panel_style->get_minimum_size().height;
	}
	return height;
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
		HashMap<String, int>::ConstIterator E = dock_offsets.find(dock->get_effective_layout_key());
		if (E) {
			stored = E->value;
		}
	}
	Control *area = get_parent_control();
	const int area_height = area ? area->get_size().height : stored + _get_strip_height();
	return BottomDrawerGeometry::clamp_body_height(stored, min_body, _get_strip_height(), area_height);
}

void EditorBottomPanel::_set_body_height(int p_height) {
	EditorDock *dock = Object::cast_to<EditorDock>(get_current_tab_control());
	if (!dock) {
		return;
	}
	Control *area = get_parent_control();
	const int area_height = area ? area->get_size().height : p_height + _get_strip_height();
	const int min_body = get_current_tab_control()->get_combined_minimum_size().height;
	dock_offsets[dock->get_effective_layout_key()] = BottomDrawerGeometry::clamp_body_height(p_height, min_body, _get_strip_height(), area_height);
	_update_drawer_geometry();
}

void EditorBottomPanel::update_drawer_geometry() {
	_update_drawer_geometry();
}

void EditorBottomPanel::_update_drawer_geometry() {
	Control *area = get_parent_control();
	if (!area) {
		return;
	}
	const bool open = get_current_tab() != -1;
	const int strip_height = _get_strip_height();
	const int area_height = area->get_size().height;
	const int body_height = open ? _get_body_height() : 0;

	const int drawer_height = BottomDrawerGeometry::drawer_height(open, drawer_expanded, strip_height, body_height, area_height);

	const bool pinned = _is_current_pinned();
	const int inset = BottomDrawerGeometry::workspace_inset(open, pinned, drawer_expanded, strip_height, body_height, area_height);
	VSplitContainer *top_split = EditorNode::get_top_split();
	if (top_split) {
		top_split->set_offset(SIDE_BOTTOM, -inset);
	}

	// A single logical open/close often produces several _update_drawer_geometry
	// calls within one frame (the tab change swaps the panel stylebox, which in
	// turn changes the strip height and re-fires geometry). Only (re)start the
	// tween when the target height actually changes; leave an in-flight tween
	// running when a settling call reports the same target, so the slide is not
	// snapped to its end on the same frame it began.
	const bool animate = !pinned && !grabber_dragging && is_inside_tree() && EDITOR_GET("interface/editor/animate_bottom_drawer");
	const bool target_changed = last_drawer_height != drawer_height;
	if (drawer_tween.is_valid() && (!animate || target_changed)) {
		drawer_tween->kill();
		drawer_tween.unref();
	}
	if (animate && target_changed) {
		drawer_tween = create_tween();
		drawer_tween->tween_method(callable_mp(this, &EditorBottomPanel::_set_drawer_top_offset), get_offset(SIDE_TOP), -(float)drawer_height, 0.15)->set_trans(Tween::TRANS_CUBIC)->set_ease(Tween::EASE_OUT);
	} else if (drawer_tween.is_null() || !drawer_tween->is_running()) {
		// No slide in flight: place the drawer (and grabber) at the target now.
		_set_drawer_top_offset(-drawer_height);
	}
	last_drawer_height = drawer_height;

	grabber->set_visible(open && !drawer_expanded);
}

void EditorBottomPanel::_set_drawer_top_offset(float p_offset) {
	set_offset(SIDE_TOP, p_offset);

	// Track the grabber to the panel's animated top edge instead of the final
	// target, so it slides with the drawer rather than jumping ahead of it.
	Control *area = get_parent_control();
	if (!area) {
		return;
	}
	const int grabber_height = 6 * EDSCALE;
	grabber->set_size(Vector2(area->get_size().width, grabber_height));
	grabber->set_global_position(area->get_global_position() + Vector2(0, area->get_size().height + p_offset));
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
	Dictionary offsets;
	for (const KeyValue<String, int> &E : dock_offsets) {
		offsets[E.key] = E.value;
	}
	p_config_file->set_value(p_section, "bottom_panel_offsets", offsets);

	Dictionary pinned;
	for (const KeyValue<String, bool> &E : dock_pinned) {
		pinned[E.key] = E.value;
	}
	p_config_file->set_value(p_section, "bottom_panel_pinned", pinned);
	p_config_file->set_value(p_section, "bottom_panel_pinned_by_default", pinned_by_default);
}

void EditorBottomPanel::load_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
	const Dictionary offsets = p_config_file->get_value(p_section, "bottom_panel_offsets", Dictionary());
	const LocalVector<Variant> offset_list = offsets.get_key_list();

	for (const Variant &v : offset_list) {
		dock_offsets[v] = BottomDrawerGeometry::body_height_from_stored(offsets[v], 0);
	}

	// Layouts written before the drawer existed lack the key and keep the
	// familiar in-flow behavior for every panel.
	pinned_by_default = p_config_file->get_value(p_section, "bottom_panel_pinned_by_default", true);
	const Dictionary pinned = p_config_file->get_value(p_section, "bottom_panel_pinned", Dictionary());
	const LocalVector<Variant> pinned_list = pinned.get_key_list();
	for (const Variant &v : pinned_list) {
		dock_pinned[v] = pinned[v];
	}
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
		dock_pinned[dock->get_effective_layout_key()] = p_pressed;
	}
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
		distraction_free->reparent(bottom_hbox);
		bottom_hbox->move_child(distraction_free, -2);
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
	set_deselect_enabled(true);
	set_process_shortcut_input(true);

	grabber = memnew(Control);
	grabber->set_name("DrawerGrabber");
	// Top-level so the TabContainer base does not treat the grabber as a tab page.
	// Its rect is driven manually from _update_drawer_geometry to track the drawer's top edge.
	grabber->set_as_top_level(true);
	add_child(grabber, false, Node::INTERNAL_MODE_BACK);
	grabber->set_default_cursor_shape(Control::CURSOR_VSIZE);
	grabber->hide();
	grabber->connect(SceneStringName(gui_input), callable_mp(this, &EditorBottomPanel::_grabber_input));

	bottom_hbox = memnew(HBoxContainer);
	bottom_hbox->set_mouse_filter(MOUSE_FILTER_IGNORE);
	bottom_hbox->set_anchors_and_offsets_preset(Control::PRESET_RIGHT_WIDE);
	get_tab_bar()->add_child(bottom_hbox);

	icon_spacer = memnew(Control);
	icon_spacer->set_mouse_filter(MOUSE_FILTER_IGNORE);
	bottom_hbox->add_child(icon_spacer);

	bottom_hbox->add_child(memnew(VSeparator));

	editor_toaster = memnew(EditorToaster);
	bottom_hbox->add_child(editor_toaster);

	EditorVersionButton *version_btn = memnew(EditorVersionButton(EditorVersionButton::FORMAT_BASIC));
	// Fade out the version label to be less prominent, but still readable.
	version_btn->set_self_modulate(Color(1, 1, 1, 0.65));
	version_btn->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	bottom_hbox->add_child(version_btn);

	// Add a dummy control node for horizontal spacing.
	Control *h_spacer = memnew(Control);
	bottom_hbox->add_child(h_spacer);

	pin_button = memnew(Button);
	bottom_hbox->add_child(pin_button);
	pin_button->hide();
	pin_button->set_theme_type_variation("BottomPanelButton");
	pin_button->set_toggle_mode(true);
	pin_button->set_accessibility_name(TTRC("Dock Bottom Drawer"));
	pin_button->set_tooltip_text(TTRC("Dock the bottom drawer, pushing the workspace up instead of covering it."));
	pin_button->connect(SceneStringName(toggled), callable_mp(this, &EditorBottomPanel::_pin_button_toggled));

	expand_button = memnew(Button);
	bottom_hbox->add_child(expand_button);
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
