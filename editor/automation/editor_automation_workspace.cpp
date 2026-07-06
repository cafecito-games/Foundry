/**************************************************************************/
/*  editor_automation_workspace.cpp                                       */
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

#include "editor_automation_workspace.h"

#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/workspace/workspace_pane.h"
#include "core/object/object.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_bar.h"

namespace {

Dictionary _serialize_workspace_tree(Control *p_node) {
	Dictionary dict;
	if (p_node == nullptr) {
		return dict;
	}

	if (WorkspaceLeafNode *leaf = Object::cast_to<WorkspaceLeafNode>(p_node)) {
		dict["type"] = "leaf";
		dict["tile_id"] = leaf->get_leaf_id();
		dict["content_type"] = "pane";
		if (WorkspacePane *pane = leaf->get_workspace_pane()) {
			dict["initial_content_type"] = pane->get_initial_content_type();
		}
		return dict;
	}

	WorkspaceSplitNode *split_node = Object::cast_to<WorkspaceSplitNode>(p_node);
	ERR_FAIL_NULL_V(split_node, dict);
	SplitContainer *split_container = split_node->get_split_container();
	ERR_FAIL_NULL_V(split_container, dict);

	Array children;
	for (int i = 0; i < split_container->get_child_count(false); i++) {
		Control *child = Object::cast_to<Control>(split_container->get_child(i, false));
		if (child != nullptr) {
			children.push_back(_serialize_workspace_tree(child));
		}
	}
	dict["type"] = "split";
	dict["vertical"] = split_node->is_vertical();
	dict["offset"] = split_node->get_split_offset();
	dict["children"] = children;
	return dict;
}

Dictionary _tile_state_entry(EditorData *p_editor_data, int p_tile_id, int p_focused_tile_id) {
	Dictionary tile;
	tile["tile_id"] = p_tile_id;
	tile["focused"] = p_tile_id == p_focused_tile_id;
	const int current_scene_index = p_editor_data->get_tile_current_scene(p_tile_id);
	tile["current_scene"] = current_scene_index;
	tile["current_scene_path"] = current_scene_index >= 0 ? p_editor_data->get_scene_path(current_scene_index) : String();

	Array scenes;
	for (int scene_index : p_editor_data->get_tile_scene_indices(p_tile_id)) {
		Dictionary scene_entry;
		scene_entry["index"] = scene_index;
		scene_entry["path"] = p_editor_data->get_scene_path(scene_index);
		scenes.push_back(scene_entry);
	}
	tile["scenes"] = scenes;
	return tile;
}

bool _selector_has_key(const Dictionary &p_selector, const char *p_key) {
	if (!p_selector.has(p_key)) {
		return false;
	}
	return p_selector.get(p_key, Variant()).get_type() != Variant::NIL;
}

bool _read_optional_int(const Dictionary &p_selector, const char *p_key, int64_t &r_value) {
	if (!_selector_has_key(p_selector, p_key)) {
		return false;
	}
	const Variant value = p_selector.get(p_key, Variant());
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = value;
	return true;
}

bool _tile_scene_matches(const EditorAutomationElement &p_element, const String &p_scene_path) {
	if (p_scene_path.is_empty()) {
		return false;
	}
	const String current_path = p_element.metadata.get("current_scene_path", String());
	if (current_path == p_scene_path) {
		return true;
	}
	const Variant scenes_value = p_element.metadata.get("scenes", Variant());
	if (scenes_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array scenes = scenes_value;
	for (int i = 0; i < scenes.size(); i++) {
		const Dictionary scene_entry = scenes[i];
		if (String(scene_entry.get("path", String())) == p_scene_path) {
			return true;
		}
	}
	return false;
}

EditorAutomationSelectorResult _make_selector_result(EditorAutomationSelectorStatus p_status, const String &p_kind = String(), const String &p_message = String()) {
	EditorAutomationSelectorResult result;
	result.status = p_status;
	result.error_kind = p_kind;
	result.message = p_message;
	return result;
}

} // namespace

Dictionary EditorAutomationWorkspace::capture_workspace_state(EditorData *p_editor_data, EditorSceneWorkspace *p_workspace) {
	Dictionary workspace;
	workspace["supported"] = false;
	if (p_editor_data == nullptr || p_workspace == nullptr) {
		workspace["focused_tile_id"] = 0;
		workspace["tile_count"] = 0;
		workspace["tree"] = Dictionary();
		workspace["tiles"] = Array();
		return workspace;
	}

	workspace["supported"] = true;
	const int focused_tile_id = p_editor_data->get_focused_tile_id();
	workspace["focused_tile_id"] = focused_tile_id;
	workspace["tile_count"] = p_workspace->get_tile_count();

	Control *structural_root = p_workspace->get_child_count(false) > 0
			? Object::cast_to<Control>(p_workspace->get_child(0, false))
			: nullptr;
	workspace["tree"] = _serialize_workspace_tree(structural_root);

	Array tiles;
	for (ScenePaneTile *tile : p_workspace->get_tiles()) {
		tiles.push_back(_tile_state_entry(p_editor_data, tile->get_tile_id(), focused_tile_id));
	}
	workspace["tiles"] = tiles;
	return workspace;
}

bool EditorAutomationWorkspace::selector_is_tile_container(const Dictionary &p_selector) {
	return _selector_has_key(p_selector, "tile_id") || _selector_has_key(p_selector, "tile_scene") || _selector_has_key(p_selector, "tile");
}

EditorAutomationSelectorResult EditorAutomationWorkspace::resolve_tile_container(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	const EditorAutomationSnapshotData &data = p_snapshot.get_data();
	int64_t target_tile_id = -1;
	String tile_scene_path;
	bool resolved_tile_id = false;

	if (_read_optional_int(p_selector, "tile_id", target_tile_id)) {
		resolved_tile_id = true;
	} else if (_selector_has_key(p_selector, "tile")) {
		const String tile_value = String(p_selector.get("tile", Variant())).to_lower();
		if (tile_value == "focused") {
			target_tile_id = get_focused_tile_id();
			resolved_tile_id = true;
		} else if (tile_value.is_valid_int()) {
			target_tile_id = tile_value.to_int();
			resolved_tile_id = true;
		} else {
			return _make_selector_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_tile", "The `tile` selector must be \"focused\" or a numeric tile id.");
		}
	} else if (_selector_has_key(p_selector, "tile_scene")) {
		tile_scene_path = p_selector.get("tile_scene", Variant());
	} else {
		return _make_selector_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_tile_selector", "Tile container selectors require `tile_id`, `tile_scene`, or `tile`.");
	}

	Vector<int> matches;
	for (int i = 0; i < data.elements.size(); i++) {
		const EditorAutomationElement &element = data.elements[i];
		if (element.role != "tile" && element.role != "pane") {
			continue;
		}
		if (resolved_tile_id) {
			const int element_tile_id = int(element.metadata.get("tile_id", -1));
			if (element_tile_id == target_tile_id) {
				matches.push_back(i);
			}
			continue;
		}
		if (_tile_scene_matches(element, tile_scene_path)) {
			matches.push_back(i);
		}
	}

	if (matches.is_empty()) {
		return _make_selector_result(EditorAutomationSelectorStatus::NO_MATCH, "no_match", "No tile container matched the selector.");
	}
	if (matches.size() > 1) {
		EditorAutomationSelectorResult result = _make_selector_result(EditorAutomationSelectorStatus::AMBIGUOUS, "ambiguous_selector", "Multiple tile containers matched the selector.");
		result.match_indices = matches;
		return result;
	}

	EditorAutomationSelectorResult result;
	result.status = EditorAutomationSelectorStatus::OK;
	result.match_indices.push_back(matches[0]);
	return result;
}

EditorSceneWorkspace::TileDropRegion EditorAutomationWorkspace::parse_drop_region(const String &p_region) {
	const String region = p_region.to_lower();
	if (region == "left") {
		return EditorSceneWorkspace::DROP_LEFT;
	}
	if (region == "right") {
		return EditorSceneWorkspace::DROP_RIGHT;
	}
	if (region == "top") {
		return EditorSceneWorkspace::DROP_TOP;
	}
	if (region == "bottom") {
		return EditorSceneWorkspace::DROP_BOTTOM;
	}
	return EditorSceneWorkspace::DROP_CENTER;
}

String EditorAutomationWorkspace::drop_region_name(EditorSceneWorkspace::TileDropRegion p_region) {
	switch (p_region) {
		case EditorSceneWorkspace::DROP_LEFT:
			return "left";
		case EditorSceneWorkspace::DROP_RIGHT:
			return "right";
		case EditorSceneWorkspace::DROP_TOP:
			return "top";
		case EditorSceneWorkspace::DROP_BOTTOM:
			return "bottom";
		case EditorSceneWorkspace::DROP_CENTER:
			return "center";
	}
	return "center";
}

Vector2 EditorAutomationWorkspace::global_drop_point(ScenePaneTile *p_tile, EditorSceneWorkspace::TileDropRegion p_region) {
	ERR_FAIL_NULL_V(p_tile, Vector2());
	const Rect2 global_rect = p_tile->get_global_rect();
	const Rect2 local_preview = EditorSceneWorkspace::drop_preview_rect(global_rect.size, p_region);
	return global_rect.position + local_preview.get_center();
}

bool EditorAutomationWorkspace::resolve_scene_tab_source(const EditorAutomationElement &p_element, int &r_tile_id, int &r_tab_index) {
	if (p_element.role != "tab") {
		return false;
	}
	if (p_element.metadata.has("tile_id") && p_element.metadata.has("tab_index")) {
		r_tile_id = int(p_element.metadata.get("tile_id", -1));
		r_tab_index = int(p_element.metadata.get("tab_index", -1));
		return r_tile_id >= 0 && r_tab_index >= 0;
	}

	String handle_kind;
	String handle_key;
	if (!EditorAutomationSnapshot::parse_durable_handle(p_element.handle, handle_kind, handle_key)) {
		return false;
	}
	const int colon_pos = handle_key.find_char(':');
	if (colon_pos < 0) {
		return false;
	}
	const uint64_t tab_bar_id = handle_key.substr(0, colon_pos).to_int();
	const int tab_index = handle_key.substr(colon_pos + 1).to_int();
	Node *tab_bar_node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(tab_bar_id)));
	if (tab_bar_node == nullptr) {
		return false;
	}
	for (Node *node = tab_bar_node; node != nullptr; node = node->get_parent()) {
		if (EditorSceneTabs *scene_tabs = Object::cast_to<EditorSceneTabs>(node)) {
			r_tile_id = scene_tabs->get_tile_id();
			r_tab_index = tab_index;
			return r_tile_id >= 0 && r_tab_index >= 0;
		}
	}
	return false;
}

int EditorAutomationWorkspace::resolve_target_tile_id(const Dictionary &p_options, const EditorAutomationSnapshot &p_snapshot) {
	if (p_options.has("target_tile_id")) {
		return int(p_options.get("target_tile_id", -1));
	}
	if (p_options.has("target_tile")) {
		const Dictionary target_tile = p_options.get("target_tile", Dictionary());
		const EditorAutomationSelectorResult tile_result = resolve_tile_container(p_snapshot, target_tile);
		if (tile_result.status == EditorAutomationSelectorStatus::OK && tile_result.match_indices.size() == 1) {
			return int(p_snapshot.get_element(tile_result.match_indices[0]).metadata.get("tile_id", -1));
		}
	}
	if (p_options.has("target")) {
		const Dictionary target = p_options.get("target", Dictionary());
		if (selector_is_tile_container(target)) {
			const EditorAutomationSelectorResult tile_result = resolve_tile_container(p_snapshot, target);
			if (tile_result.status == EditorAutomationSelectorStatus::OK && tile_result.match_indices.size() == 1) {
				return int(p_snapshot.get_element(tile_result.match_indices[0]).metadata.get("tile_id", -1));
			}
		}
	}
	return -1;
}

bool EditorAutomationWorkspace::dock_scene_tab(
		EditorData *p_editor_data,
		EditorSceneWorkspace *p_workspace,
		int p_source_tile_id,
		int p_source_tab,
		int p_target_tile_id,
		EditorSceneWorkspace::TileDropRegion p_region,
		EditorNode *p_editor_node) {
	ERR_FAIL_NULL_V(p_editor_data, false);
	ERR_FAIL_NULL_V(p_workspace, false);

	if (p_editor_node != nullptr) {
		p_editor_node->handle_tile_scene_drop(p_target_tile_id, (int)p_region, p_source_tile_id, p_source_tab);
		return true;
	}

	const int scene_idx = p_editor_data->tile_tab_to_scene_index(p_source_tile_id, p_source_tab);
	if (scene_idx < 0) {
		return false;
	}
	WorkspaceLeafNode *target_leaf = p_workspace->get_leaf_by_id(p_target_tile_id);
	ERR_FAIL_NULL_V(target_leaf, false);
	WorkspaceLeafNode *dest_leaf = p_workspace->handle_scene_drop(scene_idx, target_leaf, p_region);
	if (dest_leaf == nullptr) {
		return false;
	}
	const int dest_tile_id = dest_leaf->get_leaf_id();
	p_editor_data->set_tile_current_scene(dest_tile_id, scene_idx);
	p_workspace->set_focused_leaf(dest_tile_id);
	p_editor_data->set_focused_tile_id(dest_tile_id);
	p_workspace->sync_scene_tabs_from_editor_data();
	return true;
}

int EditorAutomationWorkspace::get_focused_tile_id() {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->is_editor_ready()) {
		return EditorNode::get_editor_data().get_focused_tile_id();
	}
	return 0;
}

int EditorAutomationWorkspace::get_tile_count(EditorSceneWorkspace *p_workspace) {
	if (p_workspace == nullptr) {
		return 0;
	}
	return p_workspace->get_tile_count();
}
