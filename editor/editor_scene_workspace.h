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

#include "scene/gui/container.h"

class ConfigFile;
class EditorData;
class EditorSelection;
class ScenePaneTile;
class SplitContainer;
class WorkspaceLeafContent;

/**
 * A leaf of the recursive tiling tree. Holds exactly one WorkspaceLeafContent
 * (scene tile, script leaf, etc.).
 */
class WorkspaceLeafNode : public Container {
	FOUNDRY_CLASS(WorkspaceLeafNode, Container);

	int leaf_id = 0;
	WorkspaceLeafContent *leaf_content = nullptr;

protected:
	void _notification(int p_what);

public:
	static WorkspaceLeafNode *create(int p_leaf_id, EditorSelection *p_editor_selection, EditorData *p_editor_data, const StringName &p_content_type = StringName("scene"));

	int get_leaf_id() const { return leaf_id; }
	WorkspaceLeafContent *get_leaf_content() const { return leaf_content; }
	void set_leaf_content(WorkspaceLeafContent *p_content);
	WorkspaceLeafContent *take_leaf_content();

	ScenePaneTile *get_pane_tile() const;
	Control *get_content_host() const;

	WorkspaceLeafNode();
	~WorkspaceLeafNode();
};

/**
 * A split node of the recursive tiling tree. Wraps a two-child SplitContainer
 * (horizontal or vertical) with a persisted divider offset.
 */
class WorkspaceSplitNode : public Container {
	FOUNDRY_CLASS(WorkspaceSplitNode, Container);

	SplitContainer *split_container = nullptr;

protected:
	void _notification(int p_what);

public:
	static WorkspaceSplitNode *create(bool p_vertical);

	SplitContainer *get_split_container() const { return split_container; }
	bool is_vertical() const;
	void set_vertical(bool p_vertical);
	int get_split_offset() const;
	void set_split_offset(int p_offset);

	WorkspaceSplitNode();
};

/**
 * Recursive tiling workspace container: a binary tree of WorkspaceSplitNode /
 * WorkspaceLeafNode nodes. The workspace always has exactly one structural
 * child (a lone leaf or the root split).
 */
class EditorSceneWorkspace : public Container {
	FOUNDRY_CLASS(EditorSceneWorkspace, Container);

public:
	enum SplitSide {
		SPLIT_SIDE_FIRST,
		SPLIT_SIDE_SECOND,
	};

	// Drop target for drag-a-scene-tab-to-a-tile: center moves the scene into
	// the target tile; an edge splits the tile and places the scene in the new leaf.
	enum TileDropRegion {
		DROP_CENTER,
		DROP_LEFT,
		DROP_RIGHT,
		DROP_TOP,
		DROP_BOTTOM,
	};

private:
	static inline const char *WORKSPACE_CONFIG_SECTION = "Workspace";

	Vector<WorkspaceLeafNode *> leaves;
	int focused_leaf_id = 0;
	int next_leaf_id = 0;
	bool restoring_from_config = false;
	EditorSelection *editor_selection = nullptr;
	EditorData *editor_data = nullptr;

	WorkspaceLeafContent *_create_leaf_content(int p_leaf_id, const StringName &p_content_type);
	WorkspaceLeafNode *_create_leaf(int p_leaf_id, const StringName &p_content_type = StringName("scene"));
	void _mount_leaf_content(WorkspaceLeafNode *p_leaf, WorkspaceLeafContent *p_content);
	Control *_get_structural_root() const;
	Control *_restore_node_from_config(const Ref<ConfigFile> &p_config, int p_node, int p_node_count, HashSet<int> &r_visited);
	WorkspaceLeafNode *_find_first_leaf(Control *p_node) const;
	void _clear_tree();
	bool _is_leaf_node(Control *p_node) const;
	bool _is_split_node(Control *p_node) const;
	void _update_focus_visuals();
	void _collapse_if_empty(int p_tile_id);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorSceneWorkspace *create_single_leaf_workspace(EditorSelection *p_editor_selection, EditorData *p_editor_data);

	// Tree ops.
	WorkspaceLeafNode *split(WorkspaceLeafNode *p_leaf, bool p_vertical, SplitSide p_side);
	WorkspaceLeafNode *split_with_content(WorkspaceLeafNode *p_leaf, bool p_vertical, SplitSide p_side, const StringName &p_content_type);
	void collapse(WorkspaceLeafNode *p_leaf);
	bool move_content(WorkspaceLeafNode *p_from_leaf, WorkspaceLeafNode *p_to_leaf);

	// Script leaves (U15c): each leaf owns a dedicated ScriptEditorView.
	WorkspaceLeafNode *get_script_leaf() const;
	Vector<WorkspaceLeafNode *> get_script_leaves() const;
	WorkspaceLeafNode *get_focused_script_leaf() const;
	WorkspaceLeafNode *find_script_leaf_for_path(const String &p_script_path) const;
	// Reveal the script leaf pointed at p_script_path, splitting beside p_source_leaf
	// when no leaf already hosts that script. Returns the script leaf.
	WorkspaceLeafNode *open_script_leaf(WorkspaceLeafNode *p_source_leaf, const String &p_script_path, bool p_force_new_leaf = false);

	// Drag-a-tab drop resolution (center = move scene; edge = split + move).
	WorkspaceLeafNode *handle_scene_drop(int p_scene_idx, WorkspaceLeafNode *p_target_leaf, TileDropRegion p_region);

	// Hit-test and preview helpers (testable without GUI).
	static TileDropRegion drop_region_at(const Size2 &p_size, const Point2 &p_local);
	static Rect2 drop_preview_rect(const Size2 &p_size, TileDropRegion p_region);

	WorkspaceLeafNode *get_leaf_by_id(int p_id) const;
	WorkspaceLeafNode *get_focused_leaf() const { return get_leaf_by_id(focused_leaf_id); }
	int get_focused_leaf_id() const { return focused_leaf_id; }
	void set_focused_leaf(int p_id);
	void request_leaf_focus(int p_leaf_id);
	int get_leaf_count() const { return leaves.size(); }
	Vector<WorkspaceLeafNode *> get_leaves() const { return leaves; }

	// Tile accessors (scene leaves only; leaf id == tile id for scene content).
	ScenePaneTile *get_tile_by_id(int p_id) const;
	ScenePaneTile *get_focused_tile() const;
	Vector<ScenePaneTile *> get_tiles() const;
	int get_tile_count() const;

	// Persistence (nested tree, flat index-addressed node list).
	static String leaf_layout_section(int p_leaf_id);
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorSceneWorkspace *p_workspace);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config);
	void restore_from_config(const Ref<ConfigFile> &p_config);

	EditorSceneWorkspace();
};
