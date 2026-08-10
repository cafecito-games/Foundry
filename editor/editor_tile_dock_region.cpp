/**************************************************************************/
/*  editor_tile_dock_region.cpp                                           */
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

#include "editor_tile_dock_region.h"

#include "core/input/shortcut.h"
#include "core/object/object.h"
#include "editor/docks/editor_dock.h"
#include "editor/gui/side_rail_state.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/control.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/scene_tree.h"

static TileDockGapMap _tile_dock_gap_map_for_body(HSplitContainer *p_body, Control *p_center_host) {
	TileDockGapMap map;
	ERR_FAIL_NULL_V(p_body, map);
	ERR_FAIL_NULL_V(p_center_host, map);
	ERR_FAIL_COND_V(p_center_host->get_parent() != p_body, map);

	// Must match SplitContainer::_add_valid_child: non-internal, non-top-level,
	// visible Control children only (scene/gui/split_container.cpp). The body
	// always owns at least one INTERNAL_MODE_BACK dragger from construction.
	const int child_count = p_body->get_child_count(false);
	Vector<bool> child_visible;
	child_visible.resize_uninitialized(child_count);
	for (int i = 0; i < child_count; i++) {
		Control *child = Object::cast_to<Control>(p_body->get_child(i, false));
		child_visible.write[i] = child && !child->is_set_as_top_level() && child->is_visible();
	}
	return tile_dock_gap_map(child_visible, p_center_host->get_index(false));
}

String EditorTileDockRegion::layout_key_for_tile(const String &p_base_key, int p_tile_id) {
	if (p_tile_id <= 0) {
		return p_base_key;
	}
	return vformat("%s:%d", p_base_key, p_tile_id + 1);
}

void EditorTileDockRegion::attach(HSplitContainer *p_body, Control *p_center_host) {
	ERR_FAIL_NULL(p_body);
	ERR_FAIL_NULL(p_center_host);
	ERR_FAIL_COND(p_center_host->get_parent() != p_body);
	body = p_body;
	center_host = p_center_host;

	right_tabs = memnew(TabContainer);
	right_tabs->set_custom_minimum_size(Size2(180, 0) * EDSCALE);
	right_tabs->set_h_size_flags(Control::SIZE_FILL);
	right_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_tabs->set_use_hidden_tabs_for_min_size(true);
	right_tabs->set_tabs_visible(true);
	body->add_child(right_tabs);
}

void EditorTileDockRegion::place_left(EditorDock *p_dock) {
	ERR_FAIL_NULL(body);
	ERR_FAIL_NULL(p_dock);
	ERR_FAIL_NULL(center_host);

	const int center_index = center_host->get_index(false);
	body->add_child(p_dock);
	body->move_child(p_dock, center_index);
}

void EditorTileDockRegion::add_right(EditorDock *p_dock) {
	ERR_FAIL_NULL(right_tabs);
	ERR_FAIL_NULL(p_dock);
	right_tabs->add_child(p_dock);
	p_dock->set_v_size_flags(Control::SIZE_EXPAND_FILL);
}

Vector<EditorDock *> EditorTileDockRegion::get_side_docks(Side p_side) const {
	Vector<EditorDock *> docks;

	if (p_side == Side::RIGHT) {
		if (!right_tabs) {
			return docks;
		}
		for (int i = 0; i < right_tabs->get_tab_count(); i++) {
			EditorDock *dock = Object::cast_to<EditorDock>(right_tabs->get_tab_control(i));
			if (dock) {
				docks.push_back(dock);
			}
		}
		return docks;
	}

	// The ordered EditorDock children of body preceding center_host; place_left
	// admits more than the single dock shipping today.
	if (!body || !center_host) {
		return docks;
	}
	for (int i = 0; i < body->get_child_count(false); i++) {
		Node *child = body->get_child(i, false);
		if (child == center_host) {
			break;
		}
		EditorDock *dock = Object::cast_to<EditorDock>(child);
		if (dock) {
			docks.push_back(dock);
		}
	}
	return docks;
}

bool EditorTileDockRegion::find_dock_side(const EditorDock *p_dock, Side &r_side) const {
	if (!p_dock) {
		return false;
	}
	if (right_tabs && p_dock->get_parent() == right_tabs) {
		r_side = Side::RIGHT;
		return true;
	}
	if (get_side_docks(Side::LEFT).has(const_cast<EditorDock *>(p_dock))) {
		r_side = Side::LEFT;
		return true;
	}
	return false;
}

EditorDock *EditorTileDockRegion::_drawer_dock(Side p_side) const {
	return Object::cast_to<EditorDock>(ObjectDB::get_instance(sides[_side_index(p_side)].drawer_dock));
}

EditorDock *EditorTileDockRegion::get_drawer_dock(Side p_side) const {
	if (sides[_side_index(p_side)].mode != SideRailMode::RAILED) {
		return nullptr;
	}
	return _drawer_dock(p_side);
}

SideRailSideState EditorTileDockRegion::_pure_state(Side p_side) const {
	const SideState &stored = sides[_side_index(p_side)];
	const Vector<EditorDock *> docks = get_side_docks(p_side);

	SideRailSideState state;
	state.mode = stored.mode;
	state.drawer_dock = docks.find(Object::cast_to<EditorDock>(ObjectDB::get_instance(stored.drawer_dock)));
	state.last_drawer_dock = docks.find(Object::cast_to<EditorDock>(ObjectDB::get_instance(stored.last_drawer_dock)));
	return state;
}

void EditorTileDockRegion::_store_state(Side p_side, const SideRailSideState &p_state) {
	SideState &stored = sides[_side_index(p_side)];
	const Vector<EditorDock *> docks = get_side_docks(p_side);

	stored.mode = p_state.mode;
	stored.drawer_dock = p_state.drawer_dock >= 0 && p_state.drawer_dock < docks.size()
			? docks[p_state.drawer_dock]->get_instance_id()
			: ObjectID();
	stored.last_drawer_dock = p_state.last_drawer_dock >= 0 && p_state.last_drawer_dock < docks.size()
			? docks[p_state.last_drawer_dock]->get_instance_id()
			: ObjectID();
}

int EditorTileDockRegion::_shown_dock_index(Side p_side) const {
	const Vector<EditorDock *> docks = get_side_docks(p_side);

	if (p_side == Side::RIGHT) {
		if (!right_tabs || !right_tabs->is_visible()) {
			return -1;
		}
		// The tab stack's current-tab index survives its own tab becoming
		// disabled (set_tab_hidden does not move the selection unless the
		// side is DOCKED and there is another enabled tab to fall back to;
		// see set_dock_enabled), so a disabled current tab must still read as
		// "nothing shown" here, or entering RAILED on an empty side could
		// open the drawer on a dock that no longer has a toggle for it.
		const int index = docks.find(Object::cast_to<EditorDock>(right_tabs->get_current_tab_control()));
		if (index < 0 || !docks[index]->is_enabled()) {
			return -1;
		}
		return index;
	}

	for (int i = 0; i < docks.size(); i++) {
		if (docks[i]->is_enabled() && docks[i]->is_visible()) {
			return i;
		}
	}
	return -1;
}

bool EditorTileDockRegion::is_dock_shown(EditorDock *p_dock) const {
	Side side = Side::LEFT;
	if (!find_dock_side(p_dock, side) || !p_dock->is_enabled()) {
		return false;
	}

	const Vector<EditorDock *> docks = get_side_docks(side);
	const SideRailSideVisibility visibility = side_rail_side_visibility(_pure_state(side), docks.size());
	if (!visibility.side_shown) {
		return false;
	}
	if (!visibility.all_docks_shown) {
		return docks.find(p_dock) == visibility.single_dock;
	}
	if (side == Side::RIGHT) {
		return right_tabs && right_tabs->get_current_tab_control() == p_dock;
	}
	return true;
}

bool EditorTileDockRegion::_update_side_visibility(Side p_side) {
	const Vector<EditorDock *> docks = get_side_docks(p_side);
	const SideRailSideVisibility visibility = side_rail_side_visibility(_pure_state(p_side), docks.size());
	bool changed = false;

	if (p_side == Side::LEFT) {
		for (int i = 0; i < docks.size(); i++) {
			const bool shown = visibility.side_shown && docks[i]->is_enabled() &&
					(visibility.all_docks_shown || i == visibility.single_dock);
			if (docks[i]->is_visible() != shown) {
				docks[i]->set_visible(shown);
				changed = true;
			}
		}
		return changed;
	}

	if (!right_tabs) {
		return false;
	}
	// Selecting first and showing second keeps the tab stack from ever being
	// briefly visible on the wrong tab.
	if (visibility.single_dock >= 0 && visibility.single_dock < docks.size()) {
		const int tab_index = right_tabs->get_tab_idx_from_control(docks[visibility.single_dock]);
		if (tab_index >= 0 && right_tabs->get_current_tab() != tab_index) {
			right_tabs->set_current_tab(tab_index);
		}
	}
	if (right_tabs->is_visible() != visibility.side_shown) {
		right_tabs->set_visible(visibility.side_shown);
		changed = true;
	}
	return changed;
}

void EditorTileDockRegion::_sync_remembered_gaps() {
	if (!body) {
		return;
	}
	const TileDockGapMap gap_map = _tile_dock_gap_map_for_body(body, center_host);
	const PackedInt32Array offsets = body->get_split_offsets();
	if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size()) {
		remembered_gap[GAP_LEFT_CENTER] = offsets[gap_map.left_center];
		has_remembered_gap[GAP_LEFT_CENTER] = true;
	}
	if (gap_map.center_right != TileDockGapMap::ABSENT && gap_map.center_right < offsets.size()) {
		remembered_gap[GAP_CENTER_RIGHT] = offsets[gap_map.center_right];
		has_remembered_gap[GAP_CENTER_RIGHT] = true;
	}
}

void EditorTileDockRegion::_reapply_gaps() {
	if (!body) {
		return;
	}
	const TileDockGapMap gap_map = _tile_dock_gap_map_for_body(body, center_host);
	PackedInt32Array offsets = body->get_split_offsets();
	const int desired_size = MAX(1, gap_map.gap_count);
	if (offsets.size() != desired_size) {
		offsets.resize_initialized(desired_size);
	}
	if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size() &&
			has_remembered_gap[GAP_LEFT_CENTER]) {
		offsets.write[gap_map.left_center] = remembered_gap[GAP_LEFT_CENTER];
	}
	if (gap_map.center_right != TileDockGapMap::ABSENT && gap_map.center_right < offsets.size() &&
			has_remembered_gap[GAP_CENTER_RIGHT]) {
		offsets.write[gap_map.center_right] = remembered_gap[GAP_CENTER_RIGHT];
	}
	if (offsets.size() > 0) {
		body->set_split_offsets(offsets);
	}
}

void EditorTileDockRegion::_apply_side(Side p_side) {
	// Order matters, and is the constraint hazard 4 asks to be recorded.
	// SplitContainer::_on_child_visibility_changed runs synchronously and
	// rewrites split_offsets from the surviving children's desired sizes
	// (scene/gui/split_container.cpp), both dropping the collapsing side's
	// entry and re-aliasing the entries that remain. So the live widths must
	// be captured before the show/hide, and re-derived by gap identity after
	// it; doing either on the other side of the visibility change reads or
	// writes the wrong gap.
	_sync_remembered_gaps();
	if (_update_side_visibility(p_side)) {
		_reapply_gaps();
	}
	_notify_side_changed(p_side);
}

void EditorTileDockRegion::_notify_side_changed(Side p_side) const {
	const Callable &callback = sides[_side_index(p_side)].changed_callback;
	if (callback.is_valid()) {
		callback.call();
	}
}

void EditorTileDockRegion::set_side_changed_callback(Side p_side, const Callable &p_callback) {
	sides[_side_index(p_side)].changed_callback = p_callback;
}

void EditorTileDockRegion::press_rail_toggle(EditorDock *p_dock) {
	ERR_FAIL_NULL(p_dock);
	Side side = Side::LEFT;
	if (!find_dock_side(p_dock, side)) {
		return;
	}
	const int pressed_index = get_side_docks(side).find(p_dock);
	if (pressed_index < 0) {
		return;
	}

	const SideRailToggleResult result = side_rail_press_toggle(_pure_state(side), pressed_index, _shown_dock_index(side));
	if (result.action == SideRailToggleAction::FOCUS_DOCK) {
		focus_dock(p_dock);
		return;
	}
	_store_state(side, result.state);
	_apply_side(side);
}

void EditorTileDockRegion::close_drawer(Side p_side) {
	_store_state(p_side, side_rail_close_drawer(_pure_state(p_side), _shown_dock_index(p_side)));
	_apply_side(p_side);
}

void EditorTileDockRegion::toggle_side_mode(Side p_side) {
	_store_state(p_side, side_rail_toggle_mode(_pure_state(p_side), _shown_dock_index(p_side)));
	_apply_side(p_side);
}

void EditorTileDockRegion::set_side_mode(Side p_side, SideRailMode p_mode) {
	if (get_side_mode(p_side) == p_mode) {
		return;
	}
	toggle_side_mode(p_side);
}

void EditorTileDockRegion::focus_dock(EditorDock *p_dock) {
	ERR_FAIL_NULL(p_dock);

	Side side = Side::LEFT;
	if (find_dock_side(p_dock, side) && get_side_mode(side) == SideRailMode::RAILED) {
		// A railed side shows at most one dock, so focusing one has to open the
		// drawer on it; otherwise the focus lands on a hidden control.
		_store_state(side, side_rail_focus_dock(_pure_state(side), get_side_docks(side).find(p_dock)));
		_apply_side(side);
	}

	// Node::get_index() counts the TabContainer's internal TabBar, so it is one
	// past every tab index; get_tab_idx_from_control is the only mapping that
	// agrees with set_current_tab.
	TabContainer *tabs = Object::cast_to<TabContainer>(p_dock->get_parent());
	if (tabs) {
		const int tab_index = tabs->get_tab_idx_from_control(p_dock);
		if (tab_index >= 0) {
			tabs->set_current_tab(tab_index);
		}
	}
	// Production EditorDock roots are layout containers and commonly keep the
	// default FOCUS_NONE. In that case, leave focus on the activating control.
	const Control::FocusMode focus_mode = p_dock->get_focus_mode_with_override();
	const bool focus_visible = p_dock->is_visible_in_tree();
	const bool accessibility_focus = focus_visible && focus_mode == Control::FOCUS_ACCESSIBILITY &&
			p_dock->get_tree()->is_accessibility_enabled();
	if (focus_visible &&
			(focus_mode == Control::FOCUS_ALL || focus_mode == Control::FOCUS_CLICK || accessibility_focus)) {
		p_dock->grab_focus();
	}
}

void EditorTileDockRegion::set_dock_enabled(EditorDock *p_dock, bool p_enabled) {
	ERR_FAIL_NULL(p_dock);
	p_dock->set_enabled(p_enabled);

	Side side = Side::LEFT;
	if (!find_dock_side(p_dock, side)) {
		p_dock->set_visible(p_enabled);
		return;
	}

	if (side == Side::RIGHT && right_tabs) {
		// A TabContainer child's own Control::visible tracks tab selection, not
		// availability: TabContainer::_repaint hides every non-current tab
		// regardless of caller intent, so a plain set_visible here would be
		// overwritten (or already be a no-op) for anything but the current tab.
		// set_tab_hidden is the primitive that actually removes a tab from the
		// available set without fighting that.
		const int tab_index = right_tabs->get_tab_idx_from_control(p_dock);
		if (tab_index >= 0) {
			right_tabs->set_tab_hidden(tab_index, !p_enabled);
			// On a railed side the drawer, not the tab stack, decides what is
			// shown, so moving the selection here would silently reopen a
			// closed drawer on an arbitrary dock.
			if (!p_enabled && get_side_mode(side) == SideRailMode::DOCKED && right_tabs->get_current_tab() == tab_index) {
				for (int i = 0; i < right_tabs->get_tab_count(); i++) {
					if (!right_tabs->is_tab_hidden(i)) {
						right_tabs->set_current_tab(i);
						break;
					}
				}
			}
		}
	}

	if (!p_enabled) {
		_store_state(side, side_rail_drawer_dock_unavailable(_pure_state(side), get_side_docks(side).find(p_dock)));
	}

	_apply_side(side);
}

static String _dock_layout_names(TabContainer *p_tabs) {
	if (!p_tabs) {
		return String();
	}
	PackedStringArray names;
	for (int i = 0; i < p_tabs->get_tab_count(); i++) {
		EditorDock *dock = Object::cast_to<EditorDock>(p_tabs->get_tab_control(i));
		if (dock) {
			names.push_back(dock->get_effective_layout_key());
		}
	}
	return String(",").join(names);
}

static void _apply_dock_tab_order(TabContainer *p_tabs, const String &p_names_csv) {
	if (!p_tabs || p_names_csv.is_empty()) {
		return;
	}
	Vector<String> names = p_names_csv.split(",");
	for (int target_idx = 0; target_idx < names.size(); target_idx++) {
		const String &name = names[target_idx];
		for (int i = 0; i < p_tabs->get_tab_count(); i++) {
			EditorDock *dock = Object::cast_to<EditorDock>(p_tabs->get_tab_control(i));
			if (dock && dock->get_effective_layout_key() == name) {
				p_tabs->move_child(dock, target_idx);
				break;
			}
		}
	}
}

void EditorTileDockRegion::save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(body);

	const TileDockGapMap gap_map = _tile_dock_gap_map_for_body(body, center_host);
	PackedInt32Array offsets = body->get_split_offsets();
	// A gap that does not currently exist has no live width to write. Its
	// remembered width is written instead when the side collapsed through this
	// region, so a layout saved while a side is railed still round-trips that
	// side's width; with nothing remembered the stored key is left untouched.
	if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size()) {
		p_config->set_value(p_section, "tile_dock_hsplit_1", int(offsets[gap_map.left_center] / EDSCALE));
	} else if (has_remembered_gap[GAP_LEFT_CENTER]) {
		p_config->set_value(p_section, "tile_dock_hsplit_1", int(remembered_gap[GAP_LEFT_CENTER] / EDSCALE));
	}
	if (gap_map.center_right != TileDockGapMap::ABSENT && gap_map.center_right < offsets.size()) {
		p_config->set_value(p_section, "tile_dock_hsplit_2", int(offsets[gap_map.center_right] / EDSCALE));
	} else if (has_remembered_gap[GAP_CENTER_RIGHT]) {
		p_config->set_value(p_section, "tile_dock_hsplit_2", int(remembered_gap[GAP_CENTER_RIGHT] / EDSCALE));
	}

	p_config->set_value(p_section, "tile_rail_left", sides[_side_index(Side::LEFT)].mode == SideRailMode::RAILED);
	p_config->set_value(p_section, "tile_rail_right", sides[_side_index(Side::RIGHT)].mode == SideRailMode::RAILED);
	const EditorDock *left_drawer = get_drawer_dock(Side::LEFT);
	const EditorDock *right_drawer = get_drawer_dock(Side::RIGHT);
	p_config->set_value(p_section, "tile_drawer_dock_left", left_drawer ? left_drawer->get_effective_layout_key() : String());
	p_config->set_value(p_section, "tile_drawer_dock_right", right_drawer ? right_drawer->get_effective_layout_key() : String());

	const String right_names = _dock_layout_names(right_tabs);
	if (!right_names.is_empty()) {
		p_config->set_value(p_section, "tile_dock_right", right_names);
	}
	if (right_tabs && right_tabs->get_current_tab() >= 0) {
		p_config->set_value(p_section, "tile_dock_right_selected_tab_idx", right_tabs->get_current_tab());
	}

	for (int i = 0; i < body->get_child_count(); i++) {
		EditorDock *dock = Object::cast_to<EditorDock>(body->get_child(i));
		if (dock) {
			const String dock_section = p_section + "/" + dock->get_effective_layout_key();
			Ref<ConfigFile> dock_config = p_config;
			dock->save_layout_to_config(dock_config, dock_section);
		}
	}
	if (right_tabs) {
		for (int i = 0; i < right_tabs->get_tab_count(); i++) {
			EditorDock *dock = Object::cast_to<EditorDock>(right_tabs->get_tab_control(i));
			if (dock) {
				const String dock_section = p_section + "/" + dock->get_effective_layout_key();
				Ref<ConfigFile> dock_config = p_config;
				dock->save_layout_to_config(dock_config, dock_section);
			}
		}
	}
}

void EditorTileDockRegion::load_layout(const Ref<ConfigFile> &p_config, const String &p_section) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(body);

	if (p_config->has_section_key(p_section, "tile_dock_right")) {
		_apply_dock_tab_order(right_tabs, p_config->get_value(p_section, "tile_dock_right"));
	}

	// Restored before the rail state so that a railed side's drawer dock, not
	// the stored tab index, decides what that side ends up showing.
	if (right_tabs && p_config->has_section_key(p_section, "tile_dock_right_selected_tab_idx")) {
		const int tab_idx = int(p_config->get_value(p_section, "tile_dock_right_selected_tab_idx"));
		if (tab_idx >= 0 && tab_idx < right_tabs->get_tab_count()) {
			right_tabs->set_current_tab(tab_idx);
		}
	}

	// Absent keys mean DOCKED with no drawer dock, which is the behavior of a
	// layout written before rails existed.
	const Side side_order[2] = { Side::LEFT, Side::RIGHT };
	const char *rail_keys[2] = { "tile_rail_left", "tile_rail_right" };
	const char *drawer_keys[2] = { "tile_drawer_dock_left", "tile_drawer_dock_right" };
	for (int i = 0; i < 2; i++) {
		const Side side = side_order[i];
		SideState &stored = sides[_side_index(side)];
		stored.mode = p_config->has_section_key(p_section, rail_keys[i]) && bool(p_config->get_value(p_section, rail_keys[i]))
				? SideRailMode::RAILED
				: SideRailMode::DOCKED;
		stored.drawer_dock = ObjectID();
		stored.last_drawer_dock = ObjectID();

		const String drawer_key = p_config->has_section_key(p_section, drawer_keys[i])
				? String(p_config->get_value(p_section, drawer_keys[i]))
				: String();
		if (drawer_key.is_empty()) {
			continue;
		}
		// A drawer dock that no longer exists, or that is disabled in this
		// session, loads as a closed drawer rather than an error.
		for (EditorDock *dock : get_side_docks(side)) {
			if (dock->is_enabled() && dock->get_effective_layout_key() == drawer_key) {
				stored.drawer_dock = dock->get_instance_id();
				stored.last_drawer_dock = stored.drawer_dock;
				break;
			}
		}
	}
	_update_side_visibility(Side::LEFT);
	_update_side_visibility(Side::RIGHT);

	// The gap map is resolved from the body's live visible-child list, so the
	// per-side visibility above has to be applied first or the offsets land on
	// the wrong gaps.
	for (int i = 0; i < GAP_MAX; i++) {
		const char *key = i == GAP_LEFT_CENTER ? "tile_dock_hsplit_1" : "tile_dock_hsplit_2";
		has_remembered_gap[i] = p_config->has_section_key(p_section, key);
		remembered_gap[i] = has_remembered_gap[i] ? int(p_config->get_value(p_section, key)) * EDSCALE : 0;
	}
	_reapply_gaps();

	_notify_side_changed(Side::LEFT);
	_notify_side_changed(Side::RIGHT);

	for (int i = 0; i < body->get_child_count(); i++) {
		EditorDock *dock = Object::cast_to<EditorDock>(body->get_child(i));
		if (dock) {
			const String dock_section = p_section + "/" + dock->get_effective_layout_key();
			dock->load_layout_from_config(p_config, dock_section);
		}
	}
	if (right_tabs) {
		for (int i = 0; i < right_tabs->get_tab_count(); i++) {
			EditorDock *dock = Object::cast_to<EditorDock>(right_tabs->get_tab_control(i));
			if (dock) {
				const String dock_section = p_section + "/" + dock->get_effective_layout_key();
				dock->load_layout_from_config(p_config, dock_section);
			}
		}
	}
}
