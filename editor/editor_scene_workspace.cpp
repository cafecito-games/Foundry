/**************************************************************************/
/*  editor_scene_workspace.cpp                                            */
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

#include "editor_scene_workspace.h"

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "scene/gui/split_container.h"

void EditorSceneWorkspace::_notification(int p_what) {
	// Reserved for future theme/layout hooks.
	switch (p_what) {
	}
}

void EditorSceneWorkspace::_bind_methods() {
	ADD_SIGNAL(MethodInfo("tile_focus_requested", PropertyInfo(Variant::INT, "tile_id")));
}

ScenePaneTile *EditorSceneWorkspace::_create_tile(int p_tile_id) {
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(p_tile_id, editor_selection, *editor_data, tiles.is_empty());
	tiles.push_back(tile);
	return tile;
}

void EditorSceneWorkspace::_fit_root_child() {
	if (get_child_count() == 0) {
		return;
	}
	Control *child = Object::cast_to<Control>(get_child(0));
	if (child) {
		child->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	}
}

EditorSceneWorkspace *EditorSceneWorkspace::create_single_tile_workspace(EditorSelection *p_selection, EditorData *p_data) {
	EditorSceneWorkspace *workspace = memnew(EditorSceneWorkspace);
	workspace->editor_selection = p_selection;
	workspace->editor_data = p_data;
	ScenePaneTile *tile = workspace->_create_tile(workspace->next_tile_id++);
	workspace->add_child(tile);
	workspace->_fit_root_child();
	workspace->set_focused_tile(tile->get_tile_id());
	return workspace;
}

ScenePaneTile *EditorSceneWorkspace::split_tile(ScenePaneTile *p_target, bool p_vertical, bool p_insert_before) {
	ERR_FAIL_NULL_V(p_target, nullptr);

	Node *parent = p_target->get_parent();
	ERR_FAIL_NULL_V(parent, nullptr);
	const int idx = p_target->get_index();

	SplitContainer *split = memnew(SplitContainer);
	split->set_vertical(p_vertical);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	parent->remove_child(p_target);
	parent->add_child(split);
	parent->move_child(split, idx);

	ScenePaneTile *new_tile = _create_tile(next_tile_id++);
	if (p_insert_before) {
		split->add_child(new_tile);
		split->add_child(p_target);
	} else {
		split->add_child(p_target);
		split->add_child(new_tile);
	}

	_fit_root_child();
	update_focus_visuals();
	return new_tile;
}

void EditorSceneWorkspace::collapse_tile(ScenePaneTile *p_tile) {
	ERR_FAIL_NULL(p_tile);
	if (tiles.size() <= 1) {
		return;
	}

	const int collapsed_tile_id = p_tile->get_tile_id();

	SplitContainer *split = Object::cast_to<SplitContainer>(p_tile->get_parent());
	ERR_FAIL_NULL(split);

	// The sibling is the split's other child.
	Node *sibling = nullptr;
	for (int i = 0; i < split->get_child_count(); i++) {
		if (split->get_child(i) != p_tile) {
			sibling = split->get_child(i);
			break;
		}
	}
	ERR_FAIL_NULL(sibling);

	Node *grand = split->get_parent();
	ERR_FAIL_NULL(grand);
	const int split_index = split->get_index();

	split->remove_child(sibling);
	split->remove_child(p_tile);
	grand->remove_child(split);
	memdelete(split);

	grand->add_child(sibling);
	grand->move_child(sibling, split_index);
	_fit_root_child();

	tiles.erase(p_tile);

	// If the focused tile is being collapsed, hand focus to a survivor BEFORE
	// freeing this tile. The focus handler (EditorNode) reparents the shared
	// main screen out of this tile first, so memdelete() below never frees
	// borrowed content that outlives the tile.
	if (focused_tile_id == collapsed_tile_id && !tiles.is_empty()) {
		set_focused_tile(tiles[0]->get_tile_id());
		emit_signal("tile_focus_requested", tiles[0]->get_tile_id());
	}

	memdelete(p_tile); // Frees its docks + tabs.

	update_focus_visuals();
}

ScenePaneTile *EditorSceneWorkspace::handle_scene_drop(int p_scene_idx, ScenePaneTile *p_target, TileDropRegion p_region) {
	ERR_FAIL_NULL_V(p_target, nullptr);
	ERR_FAIL_NULL_V(editor_data, nullptr);
	ERR_FAIL_INDEX_V(p_scene_idx, editor_data->get_edited_scene_count(), nullptr);

	const int source_tile_id = editor_data->get_scene_tile(p_scene_idx);
	ScenePaneTile *source_tile = get_tile_by_id(source_tile_id);

	ScenePaneTile *dest_tile = p_target;
	if (p_region != DROP_CENTER) {
		const bool vertical = p_region == DROP_TOP || p_region == DROP_BOTTOM;
		const bool insert_before = p_region == DROP_LEFT || p_region == DROP_TOP;
		dest_tile = split_tile(p_target, vertical, insert_before);
		ERR_FAIL_NULL_V(dest_tile, nullptr);
	}

	editor_data->set_scene_tile(p_scene_idx, dest_tile->get_tile_id());

	// Collapse the source tile if the move emptied it (and it is not the only tile).
	if (source_tile && source_tile != dest_tile && editor_data->get_tile_scene_indices(source_tile_id).is_empty() && tiles.size() > 1) {
		collapse_tile(source_tile);
	}
	return dest_tile;
}

ScenePaneTile *EditorSceneWorkspace::get_tile_by_id(int p_id) const {
	for (ScenePaneTile *tile : tiles) {
		if (tile->get_tile_id() == p_id) {
			return tile;
		}
	}
	return nullptr;
}

void EditorSceneWorkspace::set_focused_tile(int p_id) {
	focused_tile_id = p_id;
	update_focus_visuals();
}

void EditorSceneWorkspace::request_tile_focus(int p_id) {
	if (p_id == focused_tile_id) {
		return;
	}
	emit_signal("tile_focus_requested", p_id);
}

void EditorSceneWorkspace::update_focus_visuals() {
	for (ScenePaneTile *tile : tiles) {
		tile->set_focused_visual(tile->get_tile_id() == focused_tile_id);
	}
}

void EditorSceneWorkspace::_clear_tree() {
	// Freeing the single direct child recursively frees the whole tree (nested
	// SplitContainers and every tile with its docks/tabs). The tiles vector only
	// holds borrowed pointers, so clearing it must not free anything itself.
	tiles.clear();
	while (get_child_count() > 0) {
		Node *child = get_child(0);
		remove_child(child);
		memdelete(child);
	}
}

Control *EditorSceneWorkspace::_restore_node(const Ref<ConfigFile> &p_config, int p_node_index, int p_node_count, int &r_max_tile_id, Vector<RestoredLeaf> &r_leaves, HashSet<int> &r_visited) {
	// Guard against a corrupted/hand-edited layout: an out-of-range index or a
	// cycle (a split referencing an already-visited node) would otherwise recurse
	// forever. Fall back to a plain leaf so restore still yields a usable tree.
	if (p_node_index < 0 || p_node_index >= p_node_count || r_visited.has(p_node_index)) {
		const int fallback_tile_id = next_tile_id + r_leaves.size();
		r_max_tile_id = MAX(r_max_tile_id, fallback_tile_id);
		ScenePaneTile *fallback = _create_tile(fallback_tile_id);
		RestoredLeaf leaf;
		leaf.tile = fallback;
		r_leaves.push_back(leaf);
		return fallback;
	}
	r_visited.insert(p_node_index);

	const String type = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_type", p_node_index), "leaf");
	if (type == "split") {
		SplitContainer *split = memnew(SplitContainer);
		split->set_vertical(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_vertical", p_node_index), false));
		split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		split->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		const int child_a = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_a", p_node_index), -1);
		const int child_b = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_b", p_node_index), -1);
		split->add_child(_restore_node(p_config, child_a, p_node_count, r_max_tile_id, r_leaves, r_visited));
		split->add_child(_restore_node(p_config, child_b, p_node_count, r_max_tile_id, r_leaves, r_visited));
		split->set_split_offset(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_offset", p_node_index), 0));
		return split;
	}

	const int tile_id = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_tile_id", p_node_index), 0);
	r_max_tile_id = MAX(r_max_tile_id, tile_id);
	ScenePaneTile *tile = _create_tile(tile_id);

	// Record the scenes this leaf should host; the caller loads them once the
	// structure is fully in place (so scenes never load into a churning tree).
	RestoredLeaf leaf;
	leaf.tile = tile;
	leaf.scenes = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_scenes", p_node_index), PackedStringArray());
	leaf.current = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_current", p_node_index), String());
	r_leaves.push_back(leaf);

	return tile;
}

Vector<EditorSceneWorkspace::RestoredLeaf> EditorSceneWorkspace::restore_from_config(const Ref<ConfigFile> &p_config) {
	Vector<RestoredLeaf> leaves;
	if (!has_workspace_session(p_config)) {
		return leaves;
	}

	const int root_node = p_config->get_value(WORKSPACE_CONFIG_SECTION, "root_node", 0);
	const int node_count = p_config->get_value(WORKSPACE_CONFIG_SECTION, "node_count", 0);

	_clear_tree();

	int max_tile_id = 0;
	HashSet<int> visited;
	Control *root = _restore_node(p_config, root_node, node_count, max_tile_id, leaves, visited);
	add_child(root);
	_fit_root_child();
	next_tile_id = max_tile_id + 1;

	focused_tile_id = p_config->get_value(WORKSPACE_CONFIG_SECTION, "focused_tile_id", 0);
	if (!get_tile_by_id(focused_tile_id) && !tiles.is_empty()) {
		focused_tile_id = tiles[0]->get_tile_id();
	}
	update_focus_visuals();
	return leaves;
}

static int _save_workspace_node(const Ref<ConfigFile> &p_config, const char *p_section, const EditorData &p_data, Node *p_node, int &r_counter) {
	const int index = r_counter++;
	SplitContainer *split = Object::cast_to<SplitContainer>(p_node);
	if (split) {
		p_config->set_value(p_section, vformat("node_%d_type", index), "split");
		p_config->set_value(p_section, vformat("node_%d_vertical", index), split->is_vertical());
		p_config->set_value(p_section, vformat("node_%d_offset", index), split->get_split_offset());
		int child_a = -1;
		int child_b = -1;
		if (split->get_child_count() > 0) {
			child_a = _save_workspace_node(p_config, p_section, p_data, split->get_child(0), r_counter);
		}
		if (split->get_child_count() > 1) {
			child_b = _save_workspace_node(p_config, p_section, p_data, split->get_child(1), r_counter);
		}
		p_config->set_value(p_section, vformat("node_%d_child_a", index), child_a);
		p_config->set_value(p_section, vformat("node_%d_child_b", index), child_b);
		return index;
	}

	ScenePaneTile *tile = Object::cast_to<ScenePaneTile>(p_node);
	const int tile_id = tile ? tile->get_tile_id() : 0;
	p_config->set_value(p_section, vformat("node_%d_type", index), "leaf");
	p_config->set_value(p_section, vformat("node_%d_tile_id", index), tile_id);

	PackedStringArray scenes;
	const Vector<int> indices = p_data.get_tile_scene_indices(tile_id);
	for (int idx : indices) {
		const String path = p_data.get_scene_path(idx);
		if (!path.is_empty()) {
			scenes.push_back(path);
		}
	}
	p_config->set_value(p_section, vformat("node_%d_scenes", index), scenes);

	const int current = p_data.get_tile_current_scene(tile_id);
	const String current_path = current >= 0 ? p_data.get_scene_path(current) : String();
	p_config->set_value(p_section, vformat("node_%d_current", index), current_path);
	return index;
}

void EditorSceneWorkspace::save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_workspace);
	if (p_workspace->get_child_count() == 0) {
		return;
	}

	Node *root = p_workspace->get_child(0);
	int counter = 0;
	const int root_index = _save_workspace_node(p_config, WORKSPACE_CONFIG_SECTION, p_data, root, counter);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "node_count", counter);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "root_node", root_index);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "focused_tile_id", p_workspace->focused_tile_id);
}

bool EditorSceneWorkspace::has_workspace_session(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section(WORKSPACE_CONFIG_SECTION)) {
		return false;
	}
	if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "node_count")) {
		return false;
	}
	const int node_count = p_config->get_value(WORKSPACE_CONFIG_SECTION, "node_count", 0);
	if (node_count < 1) {
		return false;
	}
	for (int i = 0; i < node_count; i++) {
		const String type_key = vformat("node_%d_type", i);
		if (!p_config->has_section_key(WORKSPACE_CONFIG_SECTION, type_key)) {
			continue;
		}
		if (String(p_config->get_value(WORKSPACE_CONFIG_SECTION, type_key)) != "leaf") {
			continue;
		}
		const String scenes_key = vformat("node_%d_scenes", i);
		if (p_config->has_section_key(WORKSPACE_CONFIG_SECTION, scenes_key)) {
			const PackedStringArray scenes = p_config->get_value(WORKSPACE_CONFIG_SECTION, scenes_key);
			if (!scenes.is_empty()) {
				return true;
			}
		}
	}
	return false;
}

EditorSceneWorkspace::EditorSceneWorkspace() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}
