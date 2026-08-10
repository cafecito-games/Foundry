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
	Callable board_scene_close_handler;

	EditorBoard *_append_board(int p_board_id, const String &p_title);
	void _clear_boards();
	PackedInt32Array _collect_board_scene_indices(const EditorBoard *p_board) const;
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

	// Appends a board whose workspace draws leaf ids from the strip-wide allocator and
	// emits board_added with its index. An empty title gets the next default one.
	EditorBoard *add_board(const String &p_title = String());

	// Frees the board at p_index and emits board_removed with the index it had.
	// Returns false when the close does not happen: the last board is never closable,
	// and a board that still owns edited scenes is handed to the scene-close handler
	// first. That handler drives the per-scene unsaved-changes prompts, which are
	// asynchronous in the GUI, so it returns false and calls close_board() again once
	// every scene is gone -- or never, if the user cancels a prompt. A strip with no
	// handler installed refuses to close a board that owns scenes rather than
	// discarding them silently.
	bool close_board(int p_index);

	// Invoked from close_board() as handler(board_index, scene_indices) with the scene
	// indices owned by the board's tiles. Returning true lets the close proceed
	// immediately; returning false leaves the board and every scene in it intact.
	void set_board_scene_close_handler(const Callable &p_handler) { board_scene_close_handler = p_handler; }

	int get_board_count() const { return boards.size(); }
	EditorBoard *get_board(int p_index) const;
	EditorBoard *get_active_board() const;
	int get_active_index() const { return active_index; }
	int get_board_index(const EditorBoard *p_board) const;
	EditorSceneWorkspace *get_active_workspace() const;

	// Makes p_index the visible board: the outgoing board remembers its focused leaf,
	// the incoming one wakes before the outgoing one sleeps so no frame is left without
	// a live board, and the incoming board's remembered leaf is requested as focused.
	// Emits active_board_changed exactly once, and nothing at all when p_index is
	// already active.
	void set_active_board(int p_index);

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

	// True when the leaf lives on the visible board, or on no board at all. Editor-wide
	// state -- focus, the edited scene, the shared scene-mode surface -- follows the
	// visible board, so call sites that would write it on behalf of a leaf must check
	// this first: a dormant board's pane can still ask, most notably when a restored
	// pane replays its persisted active tab after the restore bracket has closed.
	bool is_leaf_on_active_board(int p_leaf_id) const;

	EditorBoardStrip();
};
