/**************************************************************************/
/*  scene_tab.cpp                                                         */
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

#include "scene_tab.h"

#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/workspace/workspace_pane.h"
#include "scene/gui/control.h"

static constexpr const char *UNSAVED_SCENE_KEY_PREFIX = "unsaved:scene:";

WorkspacePane *SceneTabType::_get_mounted_pane(const WorkspaceTab &p_tab) const {
	const ObjectID *pane_id = mounted_panes.getptr(p_tab.get_stable_id());
	if (!pane_id) {
		return nullptr;
	}
	return ObjectDB::get_instance<WorkspacePane>(*pane_id);
}

WorkspaceTabCloseResult SceneTabType::request_editor_close(int p_scene_idx) {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return WorkspaceTabCloseResult::CLOSE;
	}
	editor->request_workspace_scene_tab_close(p_scene_idx);
	return WorkspaceTabCloseResult::DEFERRED;
}

String SceneTabType::resource_key_for_scene(const EditorData &p_editor_data, int p_scene_idx) {
	ERR_FAIL_INDEX_V(p_scene_idx, p_editor_data.get_edited_scene_count(), String());

	const String scene_path = p_editor_data.get_scene_path(p_scene_idx);
	if (!scene_path.is_empty()) {
		return scene_path;
	}
	return String(UNSAVED_SCENE_KEY_PREFIX) + itos(p_editor_data.get_scene_history_id(p_scene_idx));
}

int SceneTabType::find_scene_index(const EditorData &p_editor_data, const WorkspaceTab &p_tab) {
	const Dictionary &payload = p_tab.get_payload();
	if (payload.has("scene_history_id")) {
		const int history_id = int(payload["scene_history_id"]);
		for (int i = 0; i < p_editor_data.get_edited_scene_count(); i++) {
			if (p_editor_data.get_scene_history_id(i) == history_id) {
				return i;
			}
		}
	}

	const String key = p_tab.get_resource_key();
	if (key.begins_with(UNSAVED_SCENE_KEY_PREFIX)) {
		const int history_id = key.substr(String(UNSAVED_SCENE_KEY_PREFIX).length()).to_int();
		for (int i = 0; i < p_editor_data.get_edited_scene_count(); i++) {
			if (p_editor_data.get_scene_history_id(i) == history_id) {
				return i;
			}
		}
		return -1;
	}
	return p_editor_data.get_edited_scene_from_path(key);
}

WorkspaceTab SceneTabType::make_tab_for_scene(const EditorData &p_editor_data, int p_scene_idx, int p_stable_id) {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(StringName("scene"));
	tab.set_resource_key(resource_key_for_scene(p_editor_data, p_scene_idx));
	tab.set_title_cache(p_editor_data.get_scene_title(p_scene_idx));
	tab.set_icon_key_cache("EditorScene");

	Dictionary payload;
	payload["resource_key"] = tab.get_resource_key();
	payload["scene_history_id"] = p_editor_data.get_scene_history_id(p_scene_idx);
	const String scene_path = p_editor_data.get_scene_path(p_scene_idx);
	if (!scene_path.is_empty()) {
		payload["scene_path"] = scene_path;
	} else {
		payload["unsaved_key"] = tab.get_resource_key();
	}
	tab.set_payload(payload);
	return tab;
}

StringName SceneTabType::type_id() const {
	return StringName("scene");
}

bool SceneTabType::can_open(const String &p_resource) const {
	return !p_resource.is_empty();
}

WorkspaceTab SceneTabType::make_tab(const String &p_resource, int p_stable_id) const {
	WorkspaceTab tab;
	tab.set_stable_id(p_stable_id);
	tab.set_type_id(type_id());
	tab.set_resource_key(p_resource);
	tab.set_title_cache(p_resource.begins_with(UNSAVED_SCENE_KEY_PREFIX) ? String("[unsaved]") : p_resource.get_file().get_basename());
	tab.set_icon_key_cache("EditorScene");

	Dictionary payload;
	payload["resource_key"] = p_resource;
	if (p_resource.begins_with("res://")) {
		payload["scene_path"] = p_resource;
	} else {
		payload["unsaved_key"] = p_resource;
	}
	tab.set_payload(payload);
	return tab;
}

String SceneTabType::get_title(const WorkspaceTab &p_tab) const {
	WorkspacePane *pane = _get_mounted_pane(p_tab);
	EditorData *editor_data = pane ? pane->get_editor_data() : nullptr;
	if (editor_data) {
		const int scene_idx = find_scene_index(*editor_data, p_tab);
		if (scene_idx >= 0) {
			return editor_data->get_scene_title(scene_idx);
		}
	}
	return p_tab.get_title_cache();
}

Ref<Texture2D> SceneTabType::get_icon(const WorkspaceTab &p_tab) const {
	WorkspacePane *pane = _get_mounted_pane(p_tab);
	ScenePaneTile *tile = pane ? pane->get_scene_tile() : nullptr;
	if (tile) {
		return tile->get_tab_icon();
	}
	return Ref<Texture2D>();
}

void SceneTabType::mount(WorkspaceTab &p_tab, Control *p_chrome_host) {
	ERR_FAIL_NULL(p_chrome_host);
	WorkspacePane *pane = Object::cast_to<WorkspacePane>(p_chrome_host->get_parent());
	ERR_FAIL_NULL(pane);

	mounted_panes[p_tab.get_stable_id()] = pane->get_instance_id();

	if (ScenePaneTile *tile = pane->get_scene_tile()) {
		tile->show();
		if (tile->get_scene_tabs()) {
			tile->get_scene_tabs()->hide();
		}
	}
}

void SceneTabType::unmount(WorkspaceTab &p_tab) {
	mounted_panes.erase(p_tab.get_stable_id());
}

void SceneTabType::activate(WorkspaceTab &p_tab) {
	if (activating_tabs.has(p_tab.get_stable_id())) {
		return;
	}

	WorkspacePane *pane = _get_mounted_pane(p_tab);
	EditorData *editor_data = pane ? pane->get_editor_data() : nullptr;
	ERR_FAIL_NULL(editor_data);

	const int scene_idx = find_scene_index(*editor_data, p_tab);
	if (scene_idx < 0) {
		return;
	}

	activating_tabs.insert(p_tab.get_stable_id());

	const int tile_id = pane->get_leaf_id();
	if (editor_data->get_scene_tile(scene_idx) != tile_id) {
		editor_data->set_scene_tile(scene_idx, tile_id);
	}
	editor_data->set_tile_current_scene(tile_id, scene_idx);
	editor_data->set_focused_tile_id(tile_id);

	if (EditorSceneWorkspace *workspace = pane->get_workspace()) {
		workspace->set_focused_leaf(tile_id);
	}

	if (EditorNode *editor = EditorNode::get_singleton()) {
		editor->activate_workspace_scene_tab(scene_idx, tile_id);
	}

	activating_tabs.erase(p_tab.get_stable_id());
}

WorkspaceTabCloseResult SceneTabType::request_close(WorkspaceTab &p_tab, const Callable &p_on_deferred_close) {
	(void)p_on_deferred_close;
	WorkspacePane *pane = _get_mounted_pane(p_tab);
	EditorData *editor_data = pane ? pane->get_editor_data() : nullptr;
	ERR_FAIL_NULL_V(editor_data, WorkspaceTabCloseResult::CANCEL);

	const int scene_idx = find_scene_index(*editor_data, p_tab);
	if (scene_idx < 0) {
		return WorkspaceTabCloseResult::CANCEL;
	}
	return request_editor_close(scene_idx);
}

Dictionary SceneTabType::save_payload(const WorkspaceTab &p_tab) const {
	Dictionary payload;
	payload["resource_key"] = p_tab.get_resource_key();
	const Dictionary &source = p_tab.get_payload();
	if (source.has("scene_path")) {
		payload["scene_path"] = source["scene_path"];
	} else if (source.has("unsaved_key")) {
		payload["unsaved_key"] = source["unsaved_key"];
	} else if (p_tab.get_resource_key().begins_with("res://")) {
		payload["scene_path"] = p_tab.get_resource_key();
	} else {
		payload["unsaved_key"] = p_tab.get_resource_key();
	}
	return payload;
}

void SceneTabType::restore_payload(WorkspaceTab &p_tab, const Dictionary &p_payload) const {
	Dictionary payload = p_payload;
	if (!payload.has("resource_key")) {
		payload["resource_key"] = p_tab.get_resource_key();
	}
	p_tab.set_payload(payload);
}
