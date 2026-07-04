/**************************************************************************/
/*  editor_scene_workspace.cpp                                            */
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

#include "editor_scene_workspace.h"

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_tile_drop_overlay.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/split_container.h"

void EditorSceneWorkspace::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			// One structural child (a lone tile or the root split) plus the
			// drop overlay, both filling the workspace rect.
			for (int i = 0; i < get_child_count(false); i++) {
				Control *child = Object::cast_to<Control>(get_child(i, false));
				if (child) {
					fit_child_in_rect(child, Rect2(Point2(), get_size()));
				}
			}
		} break;
	}
}

void EditorSceneWorkspace::_bind_methods() {
	ADD_SIGNAL(MethodInfo("tile_focus_requested", PropertyInfo(Variant::INT, "tile_id")));
	ADD_SIGNAL(MethodInfo("tile_added", PropertyInfo(Variant::INT, "tile_id")));
	ADD_SIGNAL(MethodInfo("tile_removing", PropertyInfo(Variant::INT, "tile_id")));
	ADD_SIGNAL(MethodInfo("tile_drop_completed", PropertyInfo(Variant::INT, "tile_id")));
}

ScenePaneTile *EditorSceneWorkspace::_create_tile(int p_tile_id) {
	ScenePaneTile *tile = memnew(ScenePaneTile);
	tile->setup(p_tile_id, editor_selection, *editor_data);
	tile->set_custom_minimum_size(Size2(120, 120) * EDSCALE);
	tiles.push_back(tile);
	emit_signal(SNAME("tile_added"), p_tile_id);
	return tile;
}

Control *EditorSceneWorkspace::_get_structural_root() const {
	for (int i = 0; i < get_child_count(false); i++) {
		Control *child = Object::cast_to<Control>(get_child(i, false));
		if (child && child != drop_overlay) {
			return child;
		}
	}
	return nullptr;
}

EditorSceneWorkspace *EditorSceneWorkspace::create_single_tile_workspace(EditorSelection *p_selection, EditorData *p_data) {
	ERR_FAIL_NULL_V(p_data, nullptr);
	EditorSceneWorkspace *workspace = memnew(EditorSceneWorkspace);
	workspace->editor_selection = p_selection;
	workspace->editor_data = p_data;

	ScenePaneTile *tile = workspace->_create_tile(workspace->next_tile_id++);
	workspace->add_child(tile);

	workspace->drop_overlay = memnew(EditorTileDropOverlay);
	workspace->drop_overlay->setup(workspace);
	workspace->add_child(workspace->drop_overlay);

	workspace->set_focused_tile(tile->get_tile_id());
	return workspace;
}

ScenePaneTile *EditorSceneWorkspace::split_tile(ScenePaneTile *p_target, bool p_vertical, bool p_insert_before) {
	ERR_FAIL_NULL_V(p_target, nullptr);
	ERR_FAIL_COND_V(!tiles.has(p_target), nullptr);

	Node *parent = p_target->get_parent();
	ERR_FAIL_NULL_V(parent, nullptr);
	const int idx = p_target->get_index(false);

	SplitContainer *sc = memnew(SplitContainer);
	sc->set_vertical(p_vertical);
	sc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sc->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	parent->remove_child(p_target);
	parent->add_child(sc);
	parent->move_child(sc, idx);

	ScenePaneTile *nt = _create_tile(next_tile_id++);
	if (p_insert_before) {
		sc->add_child(nt);
		sc->add_child(p_target);
	} else {
		sc->add_child(p_target);
		sc->add_child(nt);
	}

	update_focus_visuals();
	queue_sort();
	return nt;
}

void EditorSceneWorkspace::collapse_tile(ScenePaneTile *p_tile) {
	ERR_FAIL_NULL(p_tile);
	ERR_FAIL_COND(!tiles.has(p_tile));
	if (tiles.size() <= 1) {
		// The workspace always keeps at least one tile.
		return;
	}

	SplitContainer *sc = Object::cast_to<SplitContainer>(p_tile->get_parent());
	ERR_FAIL_NULL(sc);

	Control *sibling = nullptr;
	for (int i = 0; i < sc->get_child_count(false); i++) {
		Control *child = Object::cast_to<Control>(sc->get_child(i, false));
		if (child && child != p_tile) {
			sibling = child;
			break;
		}
	}
	ERR_FAIL_NULL(sibling);

	Node *grand = sc->get_parent();
	ERR_FAIL_NULL(grand);
	const int sidx = sc->get_index(false);

	const int collapsed_tile_id = p_tile->get_tile_id();

	sc->remove_child(sibling);
	sc->remove_child(p_tile);
	grand->remove_child(sc);
	memdelete(sc);

	grand->add_child(sibling);
	grand->move_child(sibling, sidx);

	tiles.erase(p_tile);
	// Let the owner detach anything it hosts inside the tile (e.g. the
	// reparented main screen) before the tile and its docks are freed.
	emit_signal(SNAME("tile_removing"), collapsed_tile_id);
	memdelete(p_tile); // Frees its docks and tab strip.

	if (focused_tile_id == collapsed_tile_id && !tiles.is_empty()) {
		// Focus a surviving tile and let the owner re-sync global state.
		set_focused_tile(tiles[0]->get_tile_id());
		emit_signal(SNAME("tile_focus_requested"), tiles[0]->get_tile_id());
	}

	update_focus_visuals();
	queue_sort();
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
	ERR_FAIL_NULL(get_tile_by_id(p_id));
	focused_tile_id = p_id;
	update_focus_visuals();
}

void EditorSceneWorkspace::request_tile_focus(int p_id) {
	if (p_id == focused_tile_id) {
		return;
	}
	ERR_FAIL_NULL(get_tile_by_id(p_id));
	emit_signal(SNAME("tile_focus_requested"), p_id);
}

void EditorSceneWorkspace::update_focus_visuals() {
	for (ScenePaneTile *tile : tiles) {
		tile->set_focused_visual(tile->get_tile_id() == focused_tile_id && tiles.size() > 1);
	}
}

bool EditorSceneWorkspace::perform_tab_drop(int p_src_tile_id, int p_src_tab, int p_target_tile_id, DropRegion p_region) {
	ERR_FAIL_NULL_V(editor_data, false);
	ScenePaneTile *target = get_tile_by_id(p_target_tile_id);
	ERR_FAIL_NULL_V(target, false);

	const int scene_idx = editor_data->tile_tab_to_scene_index(p_src_tile_id, p_src_tab);
	if (scene_idx < 0) {
		return false;
	}

	int focus_id;
	if (p_region == DROP_REGION_CENTER) {
		if (p_src_tile_id == p_target_tile_id) {
			// The scene already lives here.
			return false;
		}
		editor_data->set_scene_tile(scene_idx, p_target_tile_id);
		focus_id = p_target_tile_id;
	} else {
		const bool vertical = p_region == DROP_REGION_TOP || p_region == DROP_REGION_BOTTOM;
		const bool insert_before = p_region == DROP_REGION_LEFT || p_region == DROP_REGION_TOP;
		ScenePaneTile *nt = split_tile(target, vertical, insert_before);
		ERR_FAIL_NULL_V(nt, false);
		editor_data->set_scene_tile(scene_idx, nt->get_tile_id());
		focus_id = nt->get_tile_id();
	}

	// Collapse the source tile when the move emptied it.
	ScenePaneTile *src = get_tile_by_id(p_src_tile_id);
	if (src && p_src_tile_id != focus_id && editor_data->get_tile_scene_indices(p_src_tile_id).is_empty()) {
		collapse_tile(src);
	}

	set_focused_tile(focus_id);
	emit_signal(SNAME("tile_drop_completed"), focus_id);
	return true;
}

// Persistence.
//
// The tree is serialized as a flat, index-addressed node list:
//   node_count (int), root_node (int), focused_tile_id (int)
//   node_<i>_type = "split" | "leaf"
//   split: node_<i>_vertical (bool), node_<i>_offset (int),
//          node_<i>_child_a (int), node_<i>_child_b (int)
//   leaf:  node_<i>_tile_id (int), node_<i>_scenes (PackedStringArray),
//          node_<i>_current (String)

struct WorkspaceSaveWalker {
	Ref<ConfigFile> config;
	const EditorData *data = nullptr;
	int next_index = 0;

	int walk(Control *p_node) {
		const int index = next_index++;
		ScenePaneTile *tile = Object::cast_to<ScenePaneTile>(p_node);
		if (tile) {
			config->set_value("Workspace", vformat("node_%d_type", index), "leaf");
			config->set_value("Workspace", vformat("node_%d_tile_id", index), tile->get_tile_id());

			PackedStringArray scenes;
			for (int scene_idx : data->get_tile_scene_indices(tile->get_tile_id())) {
				const String path = data->get_scene_path(scene_idx);
				if (!path.is_empty()) {
					scenes.push_back(path);
				}
			}
			config->set_value("Workspace", vformat("node_%d_scenes", index), scenes);

			const int current = data->get_tile_current_scene(tile->get_tile_id());
			config->set_value("Workspace", vformat("node_%d_current", index), current >= 0 ? data->get_scene_path(current) : String());
			return index;
		}

		SplitContainer *sc = Object::cast_to<SplitContainer>(p_node);
		ERR_FAIL_NULL_V(sc, index);
		Vector<Control *> children;
		for (int i = 0; i < sc->get_child_count(false); i++) {
			Control *child = Object::cast_to<Control>(sc->get_child(i, false));
			if (child) {
				children.push_back(child);
			}
		}
		ERR_FAIL_COND_V(children.size() != 2, index);

		config->set_value("Workspace", vformat("node_%d_type", index), "split");
		config->set_value("Workspace", vformat("node_%d_vertical", index), sc->is_vertical());
		config->set_value("Workspace", vformat("node_%d_offset", index), sc->get_split_offset());
		const int child_a = walk(children[0]);
		const int child_b = walk(children[1]);
		config->set_value("Workspace", vformat("node_%d_child_a", index), child_a);
		config->set_value("Workspace", vformat("node_%d_child_b", index), child_b);
		return index;
	}
};

void EditorSceneWorkspace::save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_workspace);

	// Drop stale keys from a previously larger tree.
	if (p_config->has_section(WORKSPACE_CONFIG_SECTION)) {
		p_config->erase_section(WORKSPACE_CONFIG_SECTION);
	}

	Control *root = p_workspace->_get_structural_root();
	ERR_FAIL_NULL(root);

	WorkspaceSaveWalker walker;
	walker.config = p_config;
	walker.data = &p_data;
	const int root_index = walker.walk(root);

	p_config->set_value(WORKSPACE_CONFIG_SECTION, "node_count", walker.next_index);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "root_node", root_index);
	p_config->set_value(WORKSPACE_CONFIG_SECTION, "focused_tile_id", p_workspace->get_focused_tile_id());
}

bool EditorSceneWorkspace::has_workspace_session(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section(WORKSPACE_CONFIG_SECTION) || !p_config->has_section_key(WORKSPACE_CONFIG_SECTION, "node_count")) {
		return false;
	}
	const int node_count = p_config->get_value(WORKSPACE_CONFIG_SECTION, "node_count");
	if (node_count < 1) {
		return false;
	}
	for (int i = 0; i < node_count; i++) {
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

Control *EditorSceneWorkspace::_restore_node_from_config(const Ref<ConfigFile> &p_config, int p_node) {
	const String type = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_type", p_node), "leaf");
	if (type == "split") {
		SplitContainer *sc = memnew(SplitContainer);
		sc->set_vertical(bool(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_vertical", p_node), false)));
		sc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		sc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		const int child_a = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_a", p_node), -1);
		const int child_b = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_child_b", p_node), -1);
		Control *a = child_a >= 0 ? _restore_node_from_config(p_config, child_a) : nullptr;
		Control *b = child_b >= 0 ? _restore_node_from_config(p_config, child_b) : nullptr;
		if (a) {
			sc->add_child(a);
		}
		if (b) {
			sc->add_child(b);
		}
		sc->set_split_offset(int(p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_offset", p_node), 0)));
		return sc;
	}

	const int tile_id = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_tile_id", p_node), 0);
	ScenePaneTile *tile = _create_tile(tile_id);
	next_tile_id = MAX(next_tile_id, tile_id + 1);

	if (editor_data) {
		// Re-tag already-loaded scenes into this tile and restore its current.
		const PackedStringArray scenes = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_scenes", p_node), PackedStringArray());
		for (const String &path : scenes) {
			const int scene_idx = editor_data->get_edited_scene_from_path(path);
			if (scene_idx >= 0) {
				editor_data->set_scene_tile(scene_idx, tile_id);
			}
		}
		const String current_path = p_config->get_value(WORKSPACE_CONFIG_SECTION, vformat("node_%d_current", p_node), String());
		if (!current_path.is_empty()) {
			const int current_idx = editor_data->get_edited_scene_from_path(current_path);
			if (current_idx >= 0 && editor_data->get_scene_tile(current_idx) == tile_id) {
				editor_data->set_tile_current_scene(tile_id, current_idx);
			}
		}
	}
	return tile;
}

void EditorSceneWorkspace::restore_from_config(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND(p_config.is_null());
	if (!has_workspace_session(p_config)) {
		return;
	}
	const int root_node = p_config->get_value(WORKSPACE_CONFIG_SECTION, "root_node", 0);

	// Tear down the current tree (the default single tile on startup). The
	// owner gets a chance to detach anything it hosts inside each tile first.
	for (ScenePaneTile *tile : tiles) {
		emit_signal(SNAME("tile_removing"), tile->get_tile_id());
	}
	Control *old_root = _get_structural_root();
	if (old_root) {
		remove_child(old_root);
		memdelete(old_root); // Frees the tiles under it.
	}
	tiles.clear();

	Control *new_root = _restore_node_from_config(p_config, root_node);
	ERR_FAIL_NULL(new_root);
	add_child(new_root);
	if (drop_overlay) {
		// Keep the overlay drawn on top of the tree.
		move_child(drop_overlay, -1);
	}

	ERR_FAIL_COND(tiles.is_empty());
	int saved_focus = p_config->get_value(WORKSPACE_CONFIG_SECTION, "focused_tile_id", tiles[0]->get_tile_id());
	if (!get_tile_by_id(saved_focus)) {
		saved_focus = tiles[0]->get_tile_id();
	}
	set_focused_tile(saved_focus);
	if (editor_data) {
		editor_data->set_focused_tile(saved_focus);
	}
	queue_sort();
}

EditorSceneWorkspace::EditorSceneWorkspace() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_mouse_filter(Control::MOUSE_FILTER_PASS);
}
