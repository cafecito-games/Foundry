/**************************************************************************/
/*  editor_scene_workspace.h                                              */
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

#pragma once

#include "scene/gui/container.h"

class ConfigFile;
class EditorData;
class EditorSelection;
class EditorTileDropOverlay;
class ScenePaneTile;
class SplitContainer;

/**
 * The recursive tiled scene workspace: a tree of ScenePaneTile leaves joined
 * by two-child SplitContainers. The workspace always has exactly one
 * structural child (a lone tile or the root split), plus a drop overlay that
 * handles drag-a-scene-tab-to-an-edge splitting.
 *
 * Tile ids are stable integers that are never reused once allocated; they key
 * EditorData's scene->tile ownership and per-tile current-scene bookkeeping.
 */
class EditorSceneWorkspace : public Container {
	FOUNDRY_CLASS(EditorSceneWorkspace, Container);

	static inline const char *WORKSPACE_CONFIG_SECTION = "Workspace";

	Vector<ScenePaneTile *> tiles; // All live leaves.
	int focused_tile_id = 0;
	int next_tile_id = 0;
	EditorSelection *editor_selection = nullptr; // Passed to each tile's docks.
	EditorData *editor_data = nullptr;
	EditorTileDropOverlay *drop_overlay = nullptr;

	ScenePaneTile *_create_tile(int p_tile_id);
	Control *_get_structural_root() const;
	Control *_restore_node_from_config(const Ref<ConfigFile> &p_config, int p_node);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	enum DropRegion {
		DROP_REGION_CENTER,
		DROP_REGION_LEFT,
		DROP_REGION_RIGHT,
		DROP_REGION_TOP,
		DROP_REGION_BOTTOM,
	};

	static EditorSceneWorkspace *create_single_tile_workspace(EditorSelection *p_selection, EditorData *p_data);

	ScenePaneTile *split_tile(ScenePaneTile *p_target, bool p_vertical, bool p_insert_before);
	void collapse_tile(ScenePaneTile *p_tile); // No-op if it is the only tile.
	ScenePaneTile *get_tile_by_id(int p_id) const;
	ScenePaneTile *get_focused_tile() const { return get_tile_by_id(focused_tile_id); }
	int get_focused_tile_id() const { return focused_tile_id; }
	void set_focused_tile(int p_id); // Sets focused_tile_id + update_focus_visuals.
	void request_tile_focus(int p_id); // Emits "tile_focus_requested" if changing.
	int get_tile_count() const { return tiles.size(); }
	Vector<ScenePaneTile *> get_tiles() const { return tiles; }
	void update_focus_visuals(); // set_focused_visual on each tile.

	EditorData *get_editor_data() const { return editor_data; }

	// Drop resolution for drag-a-scene-tab-to-a-tile: center moves the scene
	// into the target tile; an edge splits the target and moves the scene into
	// the new tile. An emptied source tile is collapsed. Emits
	// "tile_drop_completed" with the tile to focus. Returns false when the
	// drop resolves to nothing.
	bool perform_tab_drop(int p_src_tile_id, int p_src_tab, int p_target_tile_id, DropRegion p_region);

	// Persistence (nested tree, flat index-addressed node list).
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorData &p_data, const EditorSceneWorkspace *p_workspace);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config);
	void restore_from_config(const Ref<ConfigFile> &p_config); // Rebuilds the tree.

	EditorSceneWorkspace();
};
