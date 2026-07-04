/**************************************************************************/
/*  editor_scene_workspace.h                                              */
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

#pragma once

#include "scene/gui/control.h"

class ConfigFile;
class EditorData;
class EditorSelection;
class ScenePaneTile;

// A recursive tree of editing tiles joined by SplitContainers. The workspace
// always keeps exactly one direct child: either a lone tile or the root split.
// Each tile owns a stable integer tile_id that is never changed or reused.
class EditorSceneWorkspace : public Control {
	FOUNDRY_CLASS(EditorSceneWorkspace, Control);

public:
	enum TileDropRegion {
		DROP_CENTER,
		DROP_LEFT,
		DROP_RIGHT,
		DROP_TOP,
		DROP_BOTTOM,
	};

private:
	static inline const char *WORKSPACE_CONFIG_SECTION = "Workspace";

	Vector<ScenePaneTile *> tiles; // All live leaves.
	int focused_tile_id = 0;
	int next_tile_id = 0;
	EditorSelection *editor_selection = nullptr; // Passed to each tile's docks.
	EditorData *editor_data = nullptr;

	ScenePaneTile *_create_tile(int p_tile_id);
	Control *_restore_node(const Ref<ConfigFile> &p_config, int p_node_index, int &r_max_tile_id);
	void _clear_tree();
	// The workspace is a plain Control, so its single direct child (a tile or the
	// root split) is anchored full-rect to follow the workspace's size.
	void _fit_root_child();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorSceneWorkspace *create_single_tile_workspace(EditorSelection *p_selection, EditorData *p_data);

	ScenePaneTile *split_tile(ScenePaneTile *p_target, bool p_vertical, bool p_insert_before);
	void collapse_tile(ScenePaneTile *p_tile); // No-op if it is the only tile.
	// Resolves a scene-tab drop onto p_target: center moves the scene into that
	// tile; an edge splits the tile in that direction and moves the scene into
	// the new tile. Collapses the source tile if the move empties it. Returns the
	// tile the scene ended up in.
	ScenePaneTile *handle_scene_drop(int p_scene_idx, ScenePaneTile *p_target, TileDropRegion p_region);
	ScenePaneTile *get_tile_by_id(int p_id) const;
	ScenePaneTile *get_focused_tile() const { return get_tile_by_id(focused_tile_id); }
	int get_focused_tile_id() const { return focused_tile_id; }
	void set_focused_tile(int p_id); // Sets focused_tile_id + update_focus_visuals.
	void request_tile_focus(int p_id); // Emits "tile_focus_requested" if changing.
	int get_tile_count() const { return tiles.size(); }
	Vector<ScenePaneTile *> get_tiles() const { return tiles; }
	void update_focus_visuals(); // set_focused_visual on each tile.

	// Persistence.
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config);
	// Every scene path recorded across all saved leaves, in leaf order. Used to
	// pre-load scenes before rebuilding the tree.
	static PackedStringArray get_saved_scene_paths(const Ref<ConfigFile> &p_config);
	void restore_from_config(const Ref<ConfigFile> &p_config); // Rebuilds the tree.

	EditorSceneWorkspace();
};
