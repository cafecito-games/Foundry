/**************************************************************************/
/*  editor_board_strip.h                                                  */
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

#include "editor/editor_scene_workspace.h"

#include "scene/gui/container.h"

class ConfigFile;
class EditorBoard;
class EditorData;
class EditorSelection;

/**
 * Ordered list of boards laid edge-to-edge horizontally, each sized to the full
 * strip rect. Owns the editor-wide leaf id allocator and is the only unit that
 * knows board geometry.
 */
class EditorBoardStrip : public Container, public WorkspaceLeafIdAllocator {
	FOUNDRY_CLASS(EditorBoardStrip, Container);

	Vector<EditorBoard *> boards;
	int active_index = 0;
	int next_board_id = 0;
	LocalWorkspaceLeafIdAllocator leaf_ids;

	EditorSelection *editor_selection = nullptr;
	EditorData *editor_data = nullptr;

	EditorBoard *_append_board(int p_board_id, const String &p_title);
	void _clear_boards();
	// Highest leaf id persisted anywhere in the config, plus one.
	static int _persisted_leaf_id_ceiling(const Ref<ConfigFile> &p_config, int p_board_count);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static EditorBoardStrip *create(EditorSelection *p_editor_selection, EditorData *p_editor_data);

	int allocate_leaf_id() override { return leaf_ids.allocate_leaf_id(); }
	void reserve_leaf_id(int p_leaf_id) override { leaf_ids.reserve_leaf_id(p_leaf_id); }
	int peek_next_leaf_id() const { return leaf_ids.peek_next_leaf_id(); }
	void set_next_leaf_id(int p_value) { leaf_ids.set_next_leaf_id(p_value); }

	// Appends a board whose workspace draws leaf ids from the strip-wide allocator.
	// The editor creates exactly one board today; the board switcher that lets users
	// add and close boards is built on top of this.
	EditorBoard *add_board(const String &p_title);

	int get_board_count() const { return boards.size(); }
	EditorBoard *get_board(int p_index) const;
	EditorBoard *get_active_board() const;
	int get_active_index() const { return active_index; }
	EditorSceneWorkspace *get_active_workspace() const;

	// Makes p_index the visible board: the outgoing board remembers its focused leaf
	// and goes dormant, the incoming one wakes. Focus restoration inside the incoming
	// workspace stays with the caller, which owns the editor-wide focus bookkeeping.
	void set_active_index(int p_index);
	void set_active_board(EditorBoard *p_board);

	// Config section holding the tiling tree of the board at p_index.
	static String board_section(int p_index);

	// Whole-strip session persistence. save_to_config() rewrites [Boards] and one
	// [Board_<i>] section per board; restore_from_config() rebuilds every board from
	// them. has_board_session() deliberately never inspects the pre-boards [Workspace]
	// section: a config written before boards existed restores as a fresh single board.
	static void save_to_config(const Ref<ConfigFile> &p_config, const EditorBoardStrip *p_strip);
	static bool has_board_session(const Ref<ConfigFile> &p_config);
	// Emits boards_about_to_restore before anything is freed and boards_restored once
	// every board has been rebuilt. Callers that own editor-wide state parented into a
	// board -- the shared scene-mode surface, the remote scene tree -- hang their detach
	// on the first signal and their reattach on the second, so that work happens exactly
	// once around the whole loop. Detaching per board would leave the surface parented
	// to an already-freed tile host.
	void restore_from_config(const Ref<ConfigFile> &p_config);

	// Every workspace in the editor, in board order. Call sites that mean "all
	// workspaces" (documentation refresh, feature-profile toggles, cross-board tile
	// resolution) use this instead of reaching through the active board.
	Vector<EditorSceneWorkspace *> get_workspaces() const;

	// Resolve against every board, not just the active one. Scene ownership in
	// EditorData is editor-wide, so a tile id may belong to any board.
	WorkspaceLeafNode *find_leaf_by_id(int p_leaf_id) const;
	EditorBoard *find_board_for_leaf(int p_leaf_id) const;
	ScenePaneTile *find_tile_by_id(int p_tile_id) const;

	EditorBoardStrip();
};
