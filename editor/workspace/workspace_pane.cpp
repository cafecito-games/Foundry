/**************************************************************************/
/*  workspace_pane.cpp                                                    */
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

#include "workspace_pane.h"

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_string_names.h"
#include "editor/editor_tile_drop_overlay.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/workspace/scene_tab.h"
#include "editor/workspace/workspace_tab_type.h"
#include "scene/gui/label.h"
#include "scene/resources/3d/world_3d.h"

WorkspaceTabRegistry &WorkspacePane::get_shared_tab_registry() {
	static WorkspaceTabRegistry shared_registry;
	return shared_registry;
}

void WorkspacePane::_on_tab_strip_changed(int p_index) {
	if (suppress_tab_strip_callback) {
		return;
	}
	set_active_tab(p_index);
}

void WorkspacePane::_on_tab_strip_rearranged(int p_to_index) {
	if (suppress_tab_strip_callback) {
		return;
	}
	move_tab(active_tab_index, p_to_index);
}

void WorkspacePane::_on_tab_strip_close_pressed(int p_index) {
	if (suppress_tab_strip_callback) {
		return;
	}
	ERR_FAIL_INDEX(p_index, tabs.size());
	// Every close routes through the tab type's tri-state request_close; the pane
	// only drops the tab (and possibly collapses) once the flow resolves to CLOSE.
	request_close_tab(p_index);
}

void WorkspacePane::_bind_tab_strip() {
	if (!tab_strip) {
		return;
	}
	if (!tab_strip->is_connected(SNAME("tab_changed"), callable_mp(this, &WorkspacePane::_on_tab_strip_changed))) {
		tab_strip->connect(SNAME("tab_changed"), callable_mp(this, &WorkspacePane::_on_tab_strip_changed));
	}
	if (!tab_strip->is_connected(SNAME("active_tab_rearranged"), callable_mp(this, &WorkspacePane::_on_tab_strip_rearranged))) {
		tab_strip->connect(SNAME("active_tab_rearranged"), callable_mp(this, &WorkspacePane::_on_tab_strip_rearranged));
	}
	if (!tab_strip->is_connected(SNAME("tab_close_pressed"), callable_mp(this, &WorkspacePane::_on_tab_strip_close_pressed))) {
		tab_strip->connect(SNAME("tab_close_pressed"), callable_mp(this, &WorkspacePane::_on_tab_strip_close_pressed));
	}
}

void WorkspacePane::_collapse_self_if_empty() {
	if (!tabs.is_empty() || has_bridge_content() || !workspace) {
		return;
	}
	// The final pane in the workspace stays visible with its empty placeholder.
	if (workspace->get_leaf_count() <= 1) {
		return;
	}
	workspace->collapse_if_empty_deferred(leaf_id);
}

void WorkspacePane::_sync_tab_strip() {
	if (!tab_strip) {
		return;
	}
	tab_strip->set_block_signals(true);
	tab_strip->clear_tabs();
	for (int i = 0; i < tabs.size(); i++) {
		WorkspaceTabType *type = tab_registry ? tab_registry->find_type(tabs[i].get_type_id()) : nullptr;
		const String title = type ? type->get_title(tabs[i]) : tabs[i].get_title_cache();
		tab_strip->add_tab(title);
	}
	if (active_tab_index >= 0 && active_tab_index < tab_strip->get_tab_count()) {
		tab_strip->set_current_tab(active_tab_index);
	}
	tab_strip->set_visible(!tabs.is_empty());
	tab_strip->set_block_signals(false);
}

void WorkspacePane::_refresh_canonical_locations() {
	if (!tab_registry) {
		return;
	}
	for (int i = 0; i < tabs.size(); i++) {
		WorkspaceTabLocation location;
		location.pane_id = leaf_id;
		location.tab_index = i;
		tab_registry->set_canonical(tabs[i], location);
	}
}

void WorkspacePane::_fit_chrome_child(Control *p_child) {
	if (!p_child) {
		return;
	}
	p_child->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_child->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_child->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

void WorkspacePane::_detach_ephemeral_chrome() {
	if (!chrome_host) {
		return;
	}
	mounted_tab_stable_id = -1;
	for (int i = chrome_host->get_child_count(false) - 1; i >= 0; i--) {
		Node *child = chrome_host->get_child(i, false);
		if (child == scene_tile || child == script_leaf || child == drop_overlay) {
			continue;
		}
		chrome_host->remove_child(child);
		memdelete(child);
	}
}

void WorkspacePane::_set_bridge_visibility(bool p_scene_visible, bool p_script_visible) {
	if (scene_tile) {
		scene_tile->set_visible(p_scene_visible);
	}
	if (script_leaf) {
		script_leaf->set_visible(p_script_visible);
	}
}

bool WorkspacePane::_has_legacy_scene_content() const {
	if (!scene_tile || !editor_data || !is_scene_pane()) {
		return false;
	}
	return !editor_data->get_tile_scene_indices(leaf_id).is_empty();
}

bool WorkspacePane::_has_legacy_script_content() const {
	if (!script_leaf || !is_script_pane()) {
		return false;
	}
	return !script_leaf->get_script_path().is_empty() || script_leaf->get_script_editor_view() != nullptr;
}

void WorkspacePane::_unmount_active_tab() {
	if (mounted_tab_stable_id < 0) {
		return;
	}
	if (active_tab_index >= 0 && active_tab_index < tabs.size()) {
		WorkspaceTab &tab = tabs.write[active_tab_index];
		if (WorkspaceTabType *type = tab_registry ? tab_registry->find_type(tab.get_type_id()) : nullptr) {
			type->unmount(tab);
		}
	}
	mounted_tab_stable_id = -1;
}

void WorkspacePane::_mount_scene_bridge() {
	ERR_FAIL_NULL(chrome_host);
	ERR_FAIL_NULL(scene_tile);
	_detach_ephemeral_chrome();
	_set_bridge_visibility(true, false);
	_fit_chrome_child(scene_tile);
	if (scene_tile->get_scene_tabs()) {
		scene_tile->get_scene_tabs()->show();
	}
}

void WorkspacePane::_mount_script_bridge() {
	ERR_FAIL_NULL(chrome_host);
	ERR_FAIL_NULL(script_leaf);
	_detach_ephemeral_chrome();
	_set_bridge_visibility(false, true);
}

void WorkspacePane::_mount_active_tab(bool p_activate) {
	ERR_FAIL_NULL(chrome_host);
	ERR_FAIL_NULL(tab_registry);
	ERR_FAIL_COND(active_tab_index < 0 || active_tab_index >= tabs.size());

	WorkspaceTab &tab = tabs.write[active_tab_index];
	WorkspaceTabType *type = tab_registry->find_type(tab.get_type_id());
	ERR_FAIL_NULL(type);

	if (mounted_tab_stable_id == tab.get_stable_id()) {
		if (p_activate) {
			type->activate(tab);
		}
		return;
	}

	_unmount_active_tab();
	_detach_ephemeral_chrome();
	_set_bridge_visibility(false, false);

	type->mount(tab, chrome_host);
	mounted_tab_stable_id = tab.get_stable_id();

	// The scene tab type has no surface of its own; it bridges to the pane's
	// shared scene tile, so mounting a scene tab shows that tile. Every other tab
	// type (script resource tabs included) owns and mounts its own surface under
	// the chrome host, so the legacy script_leaf bridge is never shown here --
	// showing it would stack a second script surface on top of the tab-owned one.
	if (tab.get_type_id() == StringName("scene") && scene_tile) {
		scene_tile->show();
		_fit_chrome_child(scene_tile);
	}

	if (p_activate) {
		type->activate(tab);
	}
}

WorkspaceTabType *WorkspacePane::_active_tab_type() const {
	if (active_tab_index < 0 || active_tab_index >= tabs.size() || !tab_registry) {
		return nullptr;
	}
	return tab_registry->find_type(tabs[active_tab_index].get_type_id());
}

const WorkspaceTab *WorkspacePane::_active_tab() const {
	if (active_tab_index < 0 || active_tab_index >= tabs.size()) {
		return nullptr;
	}
	return &tabs[active_tab_index];
}

WorkspaceTab *WorkspacePane::_active_tab_mut() {
	if (active_tab_index < 0 || active_tab_index >= tabs.size()) {
		return nullptr;
	}
	return &tabs.write[active_tab_index];
}

void WorkspacePane::_update_pane_state(bool p_activate) {
	const bool has_tabs = !tabs.is_empty();
	const bool legacy_scene_bridge = !has_tabs && _has_legacy_scene_content();
	const bool legacy_script_bridge = !has_tabs && _has_legacy_script_content();
	const bool show_chrome = has_tabs || legacy_scene_bridge || legacy_script_bridge;

	if (empty_placeholder) {
		empty_placeholder->set_visible(!show_chrome);
	}
	if (chrome_host) {
		chrome_host->set_visible(show_chrome);
	}
	if (tab_strip) {
		tab_strip->set_visible(has_tabs);
	}

	if (has_tabs && active_tab_index >= 0) {
		_mount_active_tab(p_activate);
	} else if (legacy_scene_bridge) {
		_mount_scene_bridge();
	} else if (legacy_script_bridge) {
		_mount_script_bridge();
	} else {
		_unmount_active_tab();
		_detach_ephemeral_chrome();
		_set_bridge_visibility(false, false);
	}
}

void WorkspacePane::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (empty_placeholder) {
				for (int i = 0; i < empty_placeholder->get_child_count(false); i++) {
					Label *label = Object::cast_to<Label>(empty_placeholder->get_child(i, false));
					if (label) {
						label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("font_color"), EditorStringName(Editor)));
					}
				}
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			if (has_pending_active_tab) {
				// A restored layout populated the tab records while this pane was
				// detached. Now that the pane is in the tree, mount the persisted
				// active tab (creating any live surface it needs) deferred, after
				// its children finish entering.
				callable_mp(this, &WorkspacePane::_apply_pending_active_tab).call_deferred();
			}
		} break;
	}
}

void WorkspacePane::_apply_pending_active_tab() {
	if (!has_pending_active_tab) {
		return;
	}
	has_pending_active_tab = false;
	const int index = pending_active_tab_index;
	pending_active_tab_index = -1;
	// Only the restored focused pane runs activation side effects; a non-focused
	// pane activating a scene tab would claim workspace focus and overwrite the
	// restored focused_leaf_id.
	const bool activate = workspace && workspace->get_focused_leaf_id() == leaf_id;
	if (index >= 0 && index < tabs.size()) {
		set_active_tab(index, activate);
	} else {
		_update_pane_state(activate);
	}
}

void WorkspacePane::setup(int p_leaf_id, EditorSelection *p_editor_selection, EditorData *p_editor_data, const StringName &p_initial_content_type) {
	leaf_id = p_leaf_id;
	editor_selection = p_editor_selection;
	editor_data = p_editor_data;
	initial_content_type = p_initial_content_type;

	if (!tab_registry) {
		tab_registry = &get_shared_tab_registry();
	}

	ERR_FAIL_NULL(chrome_host);

	if (drop_overlay) {
		drop_overlay->set_owning_pane_id(leaf_id);
	}

	if (is_scene_pane()) {
		scene_tile = memnew(ScenePaneTile);
		scene_tile->setup(leaf_id, editor_selection, *editor_data);
		chrome_host->add_child(scene_tile);
		_fit_chrome_child(scene_tile);
	} else if (is_script_pane()) {
		script_leaf = memnew(ScriptLeaf);
		chrome_host->add_child(script_leaf);
		_fit_chrome_child(script_leaf);
	}

	_update_pane_state();
}

void WorkspacePane::set_tab_registry(WorkspaceTabRegistry *p_registry) {
	tab_registry = p_registry ? p_registry : &get_shared_tab_registry();
}

void WorkspacePane::sync_from_editor_data() const {
	const_cast<WorkspacePane *>(this)->_update_pane_state(false);
}

void WorkspacePane::sync_scene_tabs_from_editor_data(bool p_activate) {
	if (!editor_data || !is_scene_pane()) {
		_update_pane_state(p_activate);
		return;
	}

	const Vector<int> tile_scenes = editor_data->get_tile_scene_indices(leaf_id);
	bool had_scene_tabs = false;
	for (const WorkspaceTab &tab : tabs) {
		if (tab.get_type_id() == StringName("scene")) {
			had_scene_tabs = true;
			break;
		}
	}
	if (tile_scenes.is_empty() && !had_scene_tabs) {
		_update_pane_state(p_activate);
		return;
	}

	const int previous_active_history = (active_tab_index >= 0 && active_tab_index < tabs.size() && tabs[active_tab_index].get_type_id() == StringName("scene") && tabs[active_tab_index].get_payload().has("scene_history_id")) ? int(tabs[active_tab_index].get_payload()["scene_history_id"]) : -1;

	HashMap<int, WorkspaceTab> existing_scene_tabs_by_history;
	HashMap<String, WorkspaceTab> existing_scene_tabs_by_key;
	Vector<WorkspaceTab> non_scene_tabs;
	for (const WorkspaceTab &tab : tabs) {
		if (tab.get_type_id() != StringName("scene")) {
			non_scene_tabs.push_back(tab);
			continue;
		}
		if (tab.get_payload().has("scene_history_id")) {
			existing_scene_tabs_by_history[int(tab.get_payload()["scene_history_id"])] = tab;
		}
		existing_scene_tabs_by_key[tab.get_resource_key()] = tab;
		if (tab_registry) {
			tab_registry->remove_canonical(tab.get_type_id(), tab.get_resource_key());
		}
	}

	_unmount_active_tab();
	active_tab_index = -1;

	Vector<WorkspaceTab> rebuilt_tabs;
	for (int scene_idx : tile_scenes) {
		const int history_id = editor_data->get_scene_history_id(scene_idx);
		const String key = SceneTabType::resource_key_for_scene(*editor_data, scene_idx);
		int stable_id = tab_registry ? tab_registry->allocate_stable_id() : scene_idx;
		if (WorkspaceTab *existing_history_tab = existing_scene_tabs_by_history.getptr(history_id)) {
			stable_id = existing_history_tab->get_stable_id();
		} else if (WorkspaceTab *existing_key_tab = existing_scene_tabs_by_key.getptr(key)) {
			stable_id = existing_key_tab->get_stable_id();
		}
		rebuilt_tabs.push_back(SceneTabType::make_tab_for_scene(*editor_data, scene_idx, stable_id));
	}
	for (const WorkspaceTab &tab : non_scene_tabs) {
		rebuilt_tabs.push_back(tab);
	}
	tabs = rebuilt_tabs;

	const int current_scene = editor_data->get_tile_current_scene(leaf_id);
	if (current_scene >= 0) {
		const String current_key = SceneTabType::resource_key_for_scene(*editor_data, current_scene);
		for (int i = 0; i < tabs.size(); i++) {
			if (tabs[i].get_type_id() == StringName("scene") && tabs[i].get_resource_key() == current_key) {
				active_tab_index = i;
				break;
			}
		}
	}
	if (active_tab_index < 0 && previous_active_history >= 0) {
		for (int i = 0; i < tabs.size(); i++) {
			if (tabs[i].get_type_id() == StringName("scene") && tabs[i].get_payload().has("scene_history_id") && int(tabs[i].get_payload()["scene_history_id"]) == previous_active_history) {
				active_tab_index = i;
				break;
			}
		}
	}
	if (active_tab_index < 0 && !tabs.is_empty()) {
		active_tab_index = 0;
	}

	_refresh_canonical_locations();
	_sync_tab_strip();
	// Only the focused pane activates its scene tab; activation reparents the
	// single shared scene editor into this tile, so activating every pane on a
	// bulk sync would thrash that heavy reparent between tiles.
	_update_pane_state(p_activate);
}

void WorkspacePane::add_tab(const WorkspaceTab &p_tab) {
	ERR_FAIL_COND(!p_tab.is_valid());
	tabs.push_back(p_tab);

	WorkspaceTabLocation location;
	location.pane_id = leaf_id;
	location.tab_index = tabs.size() - 1;
	if (tab_registry) {
		tab_registry->insert_canonical(p_tab, location);
	}

	_sync_tab_strip();

	if (tabs.size() == 1) {
		set_active_tab(0);
	} else {
		_update_pane_state();
	}
}

void WorkspacePane::remove_tab(int p_index) {
	ERR_FAIL_INDEX(p_index, tabs.size());

	const bool removed_active = active_tab_index == p_index;
	if (removed_active) {
		_unmount_active_tab();
		active_tab_index = -1;
	} else if (active_tab_index > p_index) {
		active_tab_index--;
	}

	if (tab_registry) {
		tab_registry->remove_canonical(tabs[p_index].get_type_id(), tabs[p_index].get_resource_key());
	}
	tabs.remove_at(p_index);

	if (tab_registry) {
		for (int i = p_index; i < tabs.size(); i++) {
			WorkspaceTabLocation location;
			location.pane_id = leaf_id;
			location.tab_index = i;
			tab_registry->remove_canonical(tabs[i].get_type_id(), tabs[i].get_resource_key());
			tab_registry->insert_canonical(tabs[i], location);
		}
	}

	_sync_tab_strip();

	if (removed_active && !tabs.is_empty()) {
		set_active_tab(MIN(p_index, tabs.size() - 1));
		return;
	}

	_update_pane_state();
}

void WorkspacePane::move_tab(int p_from, int p_to) {
	ERR_FAIL_INDEX(p_from, tabs.size());
	ERR_FAIL_INDEX(p_to, tabs.size());
	if (p_from == p_to) {
		return;
	}

	const WorkspaceTab moving_tab = tabs[p_from];
	if (moving_tab.get_type_id() == StringName("scene") && editor_data) {
		const int scene_idx = SceneTabType::find_scene_index(*editor_data, moving_tab);
		const int target_scene_idx = editor_data->tile_tab_to_scene_index(leaf_id, p_to);
		if (scene_idx >= 0 && target_scene_idx >= 0) {
			editor_data->set_tile_current_scene(leaf_id, scene_idx);
			editor_data->set_focused_tile_id(leaf_id);
			editor_data->move_edited_scene_to_index(target_scene_idx);
			sync_scene_tabs_from_editor_data();
			return;
		}
	}

	tabs.remove_at(p_from);
	tabs.insert(p_to, moving_tab);
	if (active_tab_index == p_from) {
		active_tab_index = p_to;
	} else if (p_from < active_tab_index && p_to >= active_tab_index) {
		active_tab_index--;
	} else if (p_from > active_tab_index && p_to <= active_tab_index) {
		active_tab_index++;
	}
	_refresh_canonical_locations();
	_sync_tab_strip();
	_update_pane_state();
}

void WorkspacePane::set_active_tab(int p_index, bool p_activate) {
	suppress_tab_strip_callback = true;
	if (p_index < 0 || p_index >= tabs.size()) {
		if (active_tab_index >= 0) {
			_unmount_active_tab();
			active_tab_index = -1;
		}
		if (tab_strip && tab_strip->get_tab_count() > 0) {
			tab_strip->set_block_signals(true);
			tab_strip->set_current_tab(-1);
			tab_strip->set_block_signals(false);
		}
		_update_pane_state(p_activate);
		suppress_tab_strip_callback = false;
		return;
	}

	if (p_index == active_tab_index && mounted_tab_stable_id == tabs[p_index].get_stable_id()) {
		suppress_tab_strip_callback = false;
		return;
	}

	if (active_tab_index >= 0) {
		_unmount_active_tab();
	}

	active_tab_index = p_index;
	if (tab_strip && tab_strip->get_current_tab() != active_tab_index) {
		tab_strip->set_block_signals(true);
		tab_strip->set_current_tab(active_tab_index);
		tab_strip->set_block_signals(false);
	}
	_update_pane_state(p_activate);
	suppress_tab_strip_callback = false;
}

WorkspaceTab WorkspacePane::take_tab(int p_index) {
	ERR_FAIL_INDEX_V(p_index, tabs.size(), WorkspaceTab());

	// Unmount first so the tab type captures its current payload (caret/scroll/
	// fold for scripts) into the tab record before it leaves this pane.
	if (p_index == active_tab_index && mounted_tab_stable_id == tabs[p_index].get_stable_id()) {
		_unmount_active_tab();
	}
	WorkspaceTab taken = tabs[p_index];
	remove_tab(p_index);
	return taken;
}

bool WorkspacePane::has_non_scene_tabs() const {
	for (const WorkspaceTab &tab : tabs) {
		if (tab.get_type_id() != StringName("scene")) {
			return true;
		}
	}
	return false;
}

int WorkspacePane::find_scene_tab_index(int p_scene_idx) const {
	if (!editor_data || p_scene_idx < 0 || p_scene_idx >= editor_data->get_edited_scene_count()) {
		return -1;
	}
	const String key = SceneTabType::resource_key_for_scene(*editor_data, p_scene_idx);
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].get_type_id() == StringName("scene") && tabs[i].get_resource_key() == key) {
			return i;
		}
	}
	return -1;
}

WorkspaceTabCloseResult WorkspacePane::request_close_active_tab() {
	if (active_tab_index < 0) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	return request_close_tab(active_tab_index);
}

WorkspaceTabCloseResult WorkspacePane::request_close_tab(int p_index) {
	ERR_FAIL_INDEX_V(p_index, tabs.size(), WorkspaceTabCloseResult::CANCEL);
	WorkspaceTabType *type = tab_registry ? tab_registry->find_type(tabs[p_index].get_type_id()) : nullptr;
	ERR_FAIL_NULL_V(type, WorkspaceTabCloseResult::CANCEL);

	// Inactive tabs unmount their chrome, and a tab type that inspects its live
	// surface to decide the close policy (e.g. a script tab checking unsaved state)
	// cannot prompt once that surface is gone. Mount the target tab before asking
	// so closing an inactive dirty tab still routes through its save/discard flow.
	if (p_index != active_tab_index || mounted_tab_stable_id != tabs[p_index].get_stable_id()) {
		set_active_tab(p_index);
	}

	// Identify the tab by stable id so a deferred close (an async save/discard
	// prompt) drops the right tab even if the index shifted while the prompt was
	// open. On CANCEL nothing is removed; on DEFERRED the type drives its own flow
	// and only invokes the callback if it ultimately resolves to a close.
	const int stable_id = tabs[p_index].get_stable_id();
	const Callable on_deferred_close = callable_mp(this, &WorkspacePane::_on_deferred_tab_closed).bind(stable_id);
	const WorkspaceTabCloseResult result = type->request_close(tabs.write[p_index], on_deferred_close);
	if (result == WorkspaceTabCloseResult::CLOSE) {
		remove_tab(p_index);
		_collapse_self_if_empty();
	}
	return result;
}

void WorkspacePane::_on_deferred_tab_closed(int p_stable_id) {
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].get_stable_id() == p_stable_id) {
			remove_tab(i);
			_collapse_self_if_empty();
			return;
		}
	}
}

StringName WorkspacePane::get_content_type() const {
	return StringName("pane");
}

Control *WorkspacePane::get_root_control() const {
	return const_cast<WorkspacePane *>(this);
}

String WorkspacePane::get_tab_title() const {
	if (const WorkspaceTab *tab = _active_tab()) {
		if (WorkspaceTabType *type = _active_tab_type()) {
			return type->get_title(*tab);
		}
		return tab->get_title_cache();
	}
	if (script_leaf) {
		return script_leaf->get_tab_title();
	}
	if (scene_tile) {
		return scene_tile->get_tab_title();
	}
	return String();
}

Ref<Texture2D> WorkspacePane::get_tab_icon() const {
	if (const WorkspaceTab *tab = _active_tab()) {
		if (WorkspaceTabType *type = _active_tab_type()) {
			return type->get_icon(*tab);
		}
	}
	if (script_leaf) {
		return script_leaf->get_tab_icon();
	}
	if (scene_tile) {
		return scene_tile->get_tab_icon();
	}
	return Ref<Texture2D>();
}

EditorSceneContext *WorkspacePane::get_scene_context() const {
	sync_from_editor_data();
	if (const WorkspaceTab *tab = _active_tab()) {
		if (tab->get_type_id() == StringName("script")) {
			return nullptr;
		}
		if (tab->get_type_id() == StringName("scene") && scene_tile) {
			return scene_tile->get_scene_context();
		}
		return nullptr;
	}
	if (_has_legacy_scene_content() && scene_tile) {
		return scene_tile->get_scene_context();
	}
	return nullptr;
}

void WorkspacePane::on_focus_entered() {
	if (script_leaf && is_script_pane() && tabs.is_empty()) {
		script_leaf->on_focus_entered();
	} else if (scene_tile && is_scene_pane() && tabs.is_empty()) {
		scene_tile->on_focus_entered();
	} else if (WorkspaceTab *tab = _active_tab_mut()) {
		if (WorkspaceTabType *type = _active_tab_type()) {
			type->activate(*tab);
		}
	}
}

// Per-pane persistence schema, written under the pane's leaf layout section
// (EditorSceneWorkspace::leaf_layout_section(leaf_id)):
//
//   initial_content_type (String)  -- seed content kind for the empty pane
//   tab_count (int)                -- number of persisted tabs in this pane
//   active_tab (int)               -- index of the active tab, or -1
//   tab_<i>/...                    -- one WorkspaceTab record per tab, holding
//                                     stable_id, type_id, resource_key,
//                                     title_cache, icon_key_cache, and a
//                                     tab_<i>/payload sub-section owned by the
//                                     tab type (scene: path/unsaved-key only;
//                                     script: caret/scroll/fold view layout).
//
// Scene edit state is NOT duplicated here; it stays in EditorData and the
// per-scene edit-state files. The focused pane is the workspace-level
// focused_leaf_id (leaf_id == pane id), so it is not repeated per pane.
void WorkspacePane::save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const {
	ERR_FAIL_COND(p_config.is_null());
	if (!initial_content_type.is_empty()) {
		p_config->set_value(p_section, "initial_content_type", initial_content_type);
	}
	// The scene tile's dock arrangement is pane chrome and persists regardless of
	// tabs. The legacy script_leaf bridge only owns content in no-tab mode, so it
	// is persisted only then -- symmetric with load_layout, which restores the
	// bridge only when tab_count == 0. In tab mode the tab records are the source
	// of truth and the hidden bridge holds no state worth serializing.
	if (scene_tile) {
		scene_tile->save_layout(p_config, p_section);
	}
	if (script_leaf && tabs.is_empty()) {
		script_leaf->save_layout(p_config, p_section);
	}

	p_config->set_value(p_section, "tab_count", tabs.size());
	p_config->set_value(p_section, "active_tab", active_tab_index);
	for (int i = 0; i < tabs.size(); i++) {
		WorkspaceTabType *type = tab_registry ? tab_registry->find_type(tabs[i].get_type_id()) : nullptr;
		if (!type) {
			continue;
		}
		const String tab_section = p_section.path_join(vformat("tab_%d", i));
		tabs[i].save_to_config(p_config, tab_section, type);
	}
}

void WorkspacePane::load_layout(const Ref<ConfigFile> &p_config, const String &p_section) {
	ERR_FAIL_COND(p_config.is_null());
	const String stored_type = p_config->get_value(p_section, "initial_content_type", String());
	if (!stored_type.is_empty()) {
		initial_content_type = stored_type;
	}
	// The scene tile's dock arrangement is pane chrome that persists regardless of
	// which tabs the pane holds, so restore it unconditionally.
	if (scene_tile) {
		scene_tile->load_layout(p_config, p_section);
	}

	const int tab_count = int(p_config->get_value(p_section, "tab_count", 0));
	if (tab_count <= 0) {
		// Legacy single-content pane: the bridge surface owns the content.
		if (script_leaf) {
			script_leaf->load_layout(p_config, p_section);
		}
		_update_pane_state();
		return;
	}

	// Rebuild the per-tab records. Mounting the active tab (which may create a
	// live script surface) is deferred to NOTIFICATION_ENTER_TREE because restore
	// runs while the pane is still detached from the scene tree.
	const int stored_active = int(p_config->get_value(p_section, "active_tab", -1));
	Vector<WorkspaceTab> restored;
	int restored_active = -1;
	for (int i = 0; i < tab_count; i++) {
		const String tab_section = p_section.path_join(vformat("tab_%d", i));
		const StringName type_id = p_config->get_value(tab_section, "type_id", StringName());
		WorkspaceTabType *type = tab_registry ? tab_registry->find_type(type_id) : nullptr;
		if (!type) {
			WARN_PRINT(vformat("Workspace pane %d: skipping restored tab %d with unknown type '%s'.", leaf_id, i, String(type_id)));
			continue;
		}
		WorkspaceTab tab;
		tab.load_from_config(p_config, tab_section, type);
		// Reserve the restored id even for a dropped tab so a fresh allocation never
		// collides with a persisted stable id elsewhere in the tree.
		if (tab_registry) {
			tab_registry->reserve_stable_id(tab.get_stable_id());
		}
		if (!type->is_resource_available(tab)) {
			WARN_PRINT(vformat("Workspace pane %d: dropping restored tab '%s' (%s); its backing resource is missing.", leaf_id, tab.get_resource_key(), String(type_id)));
			continue;
		}
		if (i == stored_active) {
			restored_active = restored.size();
		}
		restored.push_back(tab);
	}

	tabs = restored;
	if (restored_active < 0 && !tabs.is_empty()) {
		restored_active = 0;
	}
	_refresh_canonical_locations();
	_sync_tab_strip();

	pending_active_tab_index = restored_active;
	has_pending_active_tab = true;
	if (is_inside_tree()) {
		callable_mp(this, &WorkspacePane::_apply_pending_active_tab).call_deferred();
	}
}

WorkspacePane::WorkspacePane() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);

	tab_registry = &get_shared_tab_registry();

	tab_strip = memnew(TabBar);
	tab_strip->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tab_strip->set_drag_to_rearrange_enabled(true);
	tab_strip->set_tab_close_display_policy(TabBar::CLOSE_BUTTON_SHOW_ACTIVE_ONLY);
	tab_strip->hide();
	add_child(tab_strip);
	_bind_tab_strip();

	chrome_host = memnew(Control);
	chrome_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chrome_host->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chrome_host->set_clip_contents(true);
	add_child(chrome_host);

	// One rosette drop overlay per pane, painted over the pane body while any
	// workspace tab is dragged. It stays MOUSE_FILTER_IGNORE until a drag begins.
	drop_overlay = memnew(EditorTileDropOverlay);
	drop_overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	drop_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	chrome_host->add_child(drop_overlay);

	empty_placeholder = memnew(Control);
	empty_placeholder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	empty_placeholder->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *placeholder_label = memnew(Label);
	placeholder_label->set_text(TTR("Open a scene or script to get started"));
	placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	placeholder_label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	empty_placeholder->add_child(placeholder_label);
	add_child(empty_placeholder);
}

WorkspacePane::~WorkspacePane() {
	scene_tile = nullptr;
	script_leaf = nullptr;
}
