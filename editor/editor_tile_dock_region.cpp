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
#include "editor/docks/editor_dock.h"
#include "editor/gui/side_rail_state.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/control.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"

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

void EditorTileDockRegion::focus_dock(EditorDock *p_dock) {
	ERR_FAIL_NULL(p_dock);
	TabContainer *tabs = Object::cast_to<TabContainer>(p_dock->get_parent());
	if (tabs) {
		tabs->set_current_tab(p_dock->get_index());
	}
	p_dock->grab_focus();
}

void EditorTileDockRegion::set_dock_enabled(EditorDock *p_dock, bool p_enabled) {
	ERR_FAIL_NULL(p_dock);
	p_dock->set_enabled(p_enabled);

	// A TabContainer child's own Control::visible tracks tab selection, not
	// availability: TabContainer::_repaint hides every non-current tab
	// regardless of caller intent, so a plain set_visible here would be
	// overwritten (or already be a no-op) for anything but the current tab.
	// set_tab_hidden is the primitive that actually removes a tab from the
	// available set without fighting that.
	if (right_tabs && p_dock->get_parent() == right_tabs) {
		const int tab_index = p_dock->get_index();
		right_tabs->set_tab_hidden(tab_index, !p_enabled);
		if (!p_enabled && right_tabs->get_current_tab() == tab_index) {
			for (int i = 0; i < right_tabs->get_tab_count(); i++) {
				if (!right_tabs->is_tab_hidden(i)) {
					right_tabs->set_current_tab(i);
					break;
				}
			}
		}
		return;
	}

	p_dock->set_visible(p_enabled);
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
	if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size()) {
		p_config->set_value(p_section, "tile_dock_hsplit_1", int(offsets[gap_map.left_center] / EDSCALE));
	}
	if (gap_map.center_right != TileDockGapMap::ABSENT && gap_map.center_right < offsets.size()) {
		p_config->set_value(p_section, "tile_dock_hsplit_2", int(offsets[gap_map.center_right] / EDSCALE));
	}

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

	// Per-side visibility (future tile_rail_* keys) must already match the
	// body's live show/hide state before this point; the gap map is resolved
	// from current visibility and will mis-apply offsets if restored later.
	const TileDockGapMap gap_map = _tile_dock_gap_map_for_body(body, center_host);
	PackedInt32Array offsets = body->get_split_offsets();
	const int desired_size = MAX(1, gap_map.gap_count);
	if (offsets.size() != desired_size) {
		offsets.resize_initialized(desired_size);
	}
	if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size() &&
			p_config->has_section_key(p_section, "tile_dock_hsplit_1")) {
		offsets.write[gap_map.left_center] = int(p_config->get_value(p_section, "tile_dock_hsplit_1")) * EDSCALE;
	}
	if (gap_map.center_right != TileDockGapMap::ABSENT && gap_map.center_right < offsets.size() &&
			p_config->has_section_key(p_section, "tile_dock_hsplit_2")) {
		offsets.write[gap_map.center_right] = int(p_config->get_value(p_section, "tile_dock_hsplit_2")) * EDSCALE;
	}
	if (offsets.size() > 0) {
		body->set_split_offsets(offsets);
	}

	if (right_tabs && p_config->has_section_key(p_section, "tile_dock_right_selected_tab_idx")) {
		const int tab_idx = int(p_config->get_value(p_section, "tile_dock_right_selected_tab_idx"));
		if (tab_idx >= 0 && tab_idx < right_tabs->get_tab_count()) {
			right_tabs->set_current_tab(tab_idx);
		}
	}

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
