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

void WorkspacePane::_bind_tab_strip() {
	if (!tab_strip) {
		return;
	}
	if (!tab_strip->is_connected(SNAME("tab_changed"), callable_mp(this, &WorkspacePane::_on_tab_strip_changed))) {
		tab_strip->connect(SNAME("tab_changed"), callable_mp(this, &WorkspacePane::_on_tab_strip_changed));
	}
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

void WorkspacePane::_detach_ephemeral_chrome() {
	if (!chrome_host) {
		return;
	}
	mounted_tab_stable_id = -1;
	for (int i = chrome_host->get_child_count(false) - 1; i >= 0; i--) {
		Node *child = chrome_host->get_child(i, false);
		if (child == scene_tile || child == script_leaf) {
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
}

void WorkspacePane::_mount_script_bridge() {
	ERR_FAIL_NULL(chrome_host);
	ERR_FAIL_NULL(script_leaf);
	_detach_ephemeral_chrome();
	_set_bridge_visibility(false, true);
}

void WorkspacePane::_mount_active_tab() {
	ERR_FAIL_NULL(chrome_host);
	ERR_FAIL_NULL(tab_registry);
	ERR_FAIL_COND(active_tab_index < 0 || active_tab_index >= tabs.size());

	WorkspaceTab &tab = tabs.write[active_tab_index];
	WorkspaceTabType *type = tab_registry->find_type(tab.get_type_id());
	ERR_FAIL_NULL(type);

	if (mounted_tab_stable_id == tab.get_stable_id()) {
		type->activate(tab);
		return;
	}

	_unmount_active_tab();
	_detach_ephemeral_chrome();
	_set_bridge_visibility(false, false);

	type->mount(tab, chrome_host);
	mounted_tab_stable_id = tab.get_stable_id();

	// The scene tab type still bridges to the pane's scene tile; the concrete
	// ScriptResourceTabType owns and shows its own script surface, so no script
	// bridge is shown here.
	if (tab.get_type_id() == StringName("scene") && scene_tile) {
		scene_tile->show();
	}

	type->activate(tab);
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

void WorkspacePane::_update_pane_state() {
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
		_mount_active_tab();
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

	if (is_scene_pane()) {
		scene_tile = memnew(ScenePaneTile);
		scene_tile->setup(leaf_id, editor_selection, *editor_data);
		chrome_host->add_child(scene_tile);
	} else if (is_script_pane()) {
		script_leaf = memnew(ScriptLeaf);
		chrome_host->add_child(script_leaf);
	}

	_update_pane_state();
}

void WorkspacePane::set_tab_registry(WorkspaceTabRegistry *p_registry) {
	tab_registry = p_registry ? p_registry : &get_shared_tab_registry();
}

void WorkspacePane::sync_from_editor_data() const {
	const_cast<WorkspacePane *>(this)->_update_pane_state();
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

void WorkspacePane::set_active_tab(int p_index) {
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
		_update_pane_state();
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
	_update_pane_state();
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

WorkspaceTabCloseResult WorkspacePane::request_close_active_tab() {
	WorkspaceTab *tab = _active_tab_mut();
	if (!tab) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	WorkspaceTabType *type = _active_tab_type();
	if (!type) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	// The tab is identified by its stable id so a deferred close (an async
	// save/discard prompt) removes the right tab even if the index shifted.
	const int stable_id = tab->get_stable_id();
	const Callable on_deferred_close = callable_mp(this, &WorkspacePane::_on_deferred_tab_closed).bind(stable_id);
	const WorkspaceTabCloseResult result = type->request_close(*tab, on_deferred_close);
	if (result == WorkspaceTabCloseResult::CLOSE) {
		remove_tab(active_tab_index);
	}
	return result;
}

void WorkspacePane::_on_deferred_tab_closed(int p_stable_id) {
	for (int i = 0; i < tabs.size(); i++) {
		if (tabs[i].get_stable_id() == p_stable_id) {
			remove_tab(i);
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

void WorkspacePane::save_layout(const Ref<ConfigFile> &p_config, const String &p_section) const {
	ERR_FAIL_COND(p_config.is_null());
	if (!initial_content_type.is_empty()) {
		p_config->set_value(p_section, "initial_content_type", initial_content_type);
	}
	if (scene_tile) {
		scene_tile->save_layout(p_config, p_section);
	}
	if (script_leaf) {
		script_leaf->save_layout(p_config, p_section);
	}
}

void WorkspacePane::load_layout(const Ref<ConfigFile> &p_config, const String &p_section) {
	ERR_FAIL_COND(p_config.is_null());
	const String stored_type = p_config->get_value(p_section, "initial_content_type", String());
	if (!stored_type.is_empty()) {
		initial_content_type = stored_type;
	}
	if (scene_tile) {
		scene_tile->load_layout(p_config, p_section);
	}
	if (script_leaf) {
		script_leaf->load_layout(p_config, p_section);
	}
	_update_pane_state();
}

WorkspacePane::WorkspacePane() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);

	tab_registry = &get_shared_tab_registry();

	tab_strip = memnew(TabBar);
	tab_strip->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tab_strip->hide();
	add_child(tab_strip);
	_bind_tab_strip();

	chrome_host = memnew(Control);
	chrome_host->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chrome_host->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chrome_host->set_clip_contents(true);
	add_child(chrome_host);

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
