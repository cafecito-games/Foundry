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

/**
 * A leaf of the recursive tiling tree. Holds exactly one self-contained
 * ScenePaneTile (tab strip + in-tile scene tree + viewport host + inspector).
 */
class WorkspaceLeafNode : public Container {
	FOUNDRY_CLASS(WorkspaceLeafNode, Container);

	int leaf_id = 0;
	String content_descriptor;
	ScenePaneTile *pane_tile = nullptr;

protected:
	void _notification(int p_what);

public:
	static WorkspaceLeafNode *create(int p_leaf_id, EditorSelection *p_editor_selection, EditorData *p_editor_data, const String &p_content_descriptor = String());

	int get_leaf_id() const { return leaf_id; }
	const String &get_content_descriptor() const { return content_descriptor; }
	void set_content_descriptor(const String &p_descriptor) { content_descriptor = p_descriptor; }
	ScenePaneTile *get_pane_tile() const { return pane_tile; }
	Control *get_content_host() const;

	WorkspaceLeafNode();
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

private:
	static inline const char *WORKSPACE_CONFIG_SECTION = "Workspace";

	Vector<WorkspaceLeafNode *> leaves;
	int focused_leaf_id = 0;
	int next_leaf_id = 0;
	EditorSelection *editor_selection = nullptr;
	EditorData *editor_data = nullptr;

	WorkspaceLeafNode *_create_leaf(int p_leaf_id, const String &p_content_descriptor = String());
	Control *_get_structural_root() const;
	Control *_restore_node_from_config(const Ref<ConfigFile> &p_config, int p_node, int p_node_count, HashSet<int> &r_visited);
	void _clear_tree();
	bool _is_leaf_node(Control *p_node) const;
	bool _is_split_node(Control *p_node) const;
	void _update_focus_visuals();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorSceneWorkspace *create_single_leaf_workspace(EditorSelection *p_editor_selection, EditorData *p_editor_data);

	// Tree ops.
	WorkspaceLeafNode *split(WorkspaceLeafNode *p_leaf, bool p_vertical, SplitSide p_side);
	void collapse(WorkspaceLeafNode *p_leaf);
	bool move_content(const String &p_content, WorkspaceLeafNode *p_from_leaf, WorkspaceLeafNode *p_to_leaf);

	WorkspaceLeafNode *get_leaf_by_id(int p_id) const;
	WorkspaceLeafNode *get_focused_leaf() const { return get_leaf_by_id(focused_leaf_id); }
	int get_focused_leaf_id() const { return focused_leaf_id; }
	void set_focused_leaf(int p_id);
	void request_leaf_focus(int p_leaf_id);
	int get_leaf_count() const { return leaves.size(); }
	Vector<WorkspaceLeafNode *> get_leaves() const { return leaves; }

	// Tile accessors (each leaf owns one ScenePaneTile; leaf id == tile id).
	ScenePaneTile *get_tile_by_id(int p_id) const;
	ScenePaneTile *get_focused_tile() const;
	Vector<ScenePaneTile *> get_tiles() const;
	int get_tile_count() const { return leaves.size(); }

	// Persistence (nested tree, flat index-addressed node list).
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorSceneWorkspace *p_workspace);
	static bool has_workspace_session(const Ref<ConfigFile> &p_config);
	void restore_from_config(const Ref<ConfigFile> &p_config);

	EditorSceneWorkspace();
};
