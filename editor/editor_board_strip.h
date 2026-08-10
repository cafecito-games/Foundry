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

#include "editor/editor_board_view.h"
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

	// Drives the horizontal slide. Idle (is_animating() == false) outside of a switch.
	EditorBoardView board_view;
	// The board that must go dormant once the current slide finishes. Valid exactly while
	// a slide has an outgoing board still awake; see set_active_board(). Held by instance
	// id rather than by pointer because a board close for an unrelated index can free the
	// board this was pointing at without going through set_active_board() or
	// _advance_transition() first -- resolving through ObjectDB rather than dereferencing
	// a raw pointer is what makes every settle site safe regardless of whether the board
	// is still alive.
	ObjectID transition_outgoing_id;
	// Set while a zoom back out of the overview is still running. On settle every board but
	// the active one goes dormant. The boards to sleep are read from the live list at
	// settle time rather than captured here, so a close during the zoom cannot strand a
	// freed board or a board that shifted index.
	bool overview_exit_pending = false;
	// Seconds of process time accumulated towards the next throttled preview refresh.
	real_t overview_refresh_accumulator = 0.0;
	// Throttled refreshes issued since the current overview was entered. Readable so the
	// cadence bound is observable rather than inferred from timing.
	uint32_t overview_refresh_count = 0;
	// Board titles drawn over, not inside, the scaled board container, so they stay at
	// full font size and remain clickable while the boards behind them shrink. Always the
	// strip's last child so it draws above every board.
	Control *caption_overlay = nullptr;
	// The board whose caption is currently held down in the overview, or an invalid id when
	// no caption is held. Held by instance id for the same reason the transition's outgoing
	// board is: the drag outlives individual input events, and a board can be closed from
	// elsewhere in between them.
	ObjectID caption_drag_board_id;
	// Strip-local pointer x at the moment the caption was pressed, and the latest one seen.
	real_t caption_drag_press_x = 0.0;
	real_t caption_drag_pointer_x = 0.0;
	// True once the pointer has travelled far enough for the press to read as a reorder
	// rather than as a click.
	bool caption_drag_active = false;
	// A caption that was dragged still reports a press when the button is released. That
	// press must not additionally read as "open this board", so it is swallowed once. Reset
	// on the next caption press so a drag released off the button cannot strand the flag and
	// eat an unrelated click later.
	bool caption_drag_swallow_click = false;

	EditorBoard *_append_board(int p_board_id, const String &p_title);
	void _clear_boards();
	PackedInt32Array _collect_board_scene_indices(const EditorBoard *p_board) const;
	// Highest leaf id persisted anywhere in the config, plus one.
	static int _persisted_leaf_id_ceiling(const Ref<ConfigFile> &p_config, int p_board_count);
	// Applies one frame of the slide and, once it settles, sleeps the outgoing board.
	void _advance_transition(real_t p_delta);
	// Puts every board that must no longer be live to sleep. Called from every site where a
	// transition lands, whether by animating to its end or by being cut short.
	void _settle_dormancy();
	// The body of set_active_board() with no overview handling, so the overview's own exit
	// can drive a switch without recursing back through set_active_board().
	void _switch_to_board(int p_index);
	// Wakes every board, bounds every tile's preview cost, and retargets the view at the
	// overview layout.
	void _enter_overview();
	// Restores every tile's preview cost, hides the captions, and clears the view's own
	// overview flag immediately (with no transition motion of its own). is_overview_active()
	// is guaranteed false the moment this returns; callers still follow it with whichever
	// transition (switch_to_index()/exit_overview()) lands the visual exit, but nothing
	// between this call and that one can observe the overview-only state and the flag
	// disagreeing.
	void _leave_overview_state();
	// The one way out of the overview: restores the tiles, animates onto p_index, and
	// arranges for every other board to sleep once the motion settles.
	void _exit_overview_to(int p_index);
	void _apply_overview_preview_bounds(bool p_overview);
	// Consumes p_delta and issues a preview refresh whenever a tick is due.
	void _pump_overview_refresh(real_t p_delta);
	void _rebuild_captions();
	void _layout_captions(const Transform2D &p_transform, real_t p_pitch, const Size2 &p_scaled_size);
	void _on_caption_pressed(ObjectID p_board_id);
	void _on_caption_gui_input(const Ref<InputEvent> &p_event, ObjectID p_board_id);
	// Clears the drag state and decides whether the press the caption is about to report has
	// to be swallowed.
	void _end_caption_drag(bool p_release_inside_caption);
	// The board under a strip-local pointer x while the overview is up, or -1 in a gutter or
	// past the ends. Captions hang below the boards, so a caption drag hit-tests at the
	// board band's own height rather than at the pointer's y: the horizontal math is
	// index_at_point()'s, unchanged, so a drop target can never disagree with the board a
	// click at the same x would select.
	int _overview_index_at_x(real_t p_x) const;
	// A split or a cross-board pane drop can create a new tile at any time, live and full
	// size by default. Wired to every board's workspace so that a tile created while the
	// overview is up is bounded the same as the rest of the filmstrip instead of rendering
	// at full resolution until the next overview toggle re-syncs it.
	void _on_leaf_added(int p_leaf_id);

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

	// Re-resolves a board captured by instance id back to its current index. Any board
	// close can shift every index after it, so a caller that holds on to a board across
	// an asynchronous gap -- an unsaved-changes prompt, a deferred call -- must re-resolve
	// through here instead of trusting a position captured before the gap. Returns -1 when
	// the board no longer exists.
	int resolve_board_index(ObjectID p_board_id) const;

	// Makes p_index the visible board over a short horizontal slide: the outgoing board
	// remembers its focused leaf, the incoming one wakes immediately so both are live for
	// the duration of the slide, and the incoming board's remembered leaf is requested as
	// focused. Emits active_board_changed exactly once, at the start of the switch, and
	// nothing at all when p_index is already the active (or already-targeted) board.
	// The outgoing board goes dormant only once the slide completes. Calling this again
	// before the slide finishes retargets the animation from its current position instead
	// of snapping, and immediately sleeps whichever board is no longer part of the new
	// outgoing/incoming pair.
	void set_active_board(int p_index);

	// Moves the board at p_from so that it ends up at p_to, shifting the boards in between.
	// Emits board_moved(from, to) exactly once, and nothing at all when the two indices name
	// the same position.
	//
	// A reorder is purely a change of board order: no workspace is rebuilt, no leaf id is
	// reissued, and no scene changes hands. The board that was on screen stays on screen,
	// which is why active_index is re-derived from the moved list by identity rather than
	// patched with index arithmetic -- the active board may be the one being moved, may be
	// displaced by it, or may sit outside the affected range entirely, and only identity
	// gets all three right.
	void move_board(int p_from, int p_to);

	// Zooms every board out into a live filmstrip, or back onto the active board.
	//
	// The overview is live rather than a wall of frozen thumbnails: every board stays
	// awake, keeps rendering, and remains an ordinary Control, so panes can still be
	// dropped across boards while it is up. What makes that affordable is two explicit
	// bounds, both applied here and both undone on exit. Resolution: every tile's preview
	// SubViewports are shrunk by overview_preview_shrink_for(), so a board drawn at 1/n of
	// its layout size renders roughly 1/n^2 the pixels and the whole filmstrip costs about
	// one board. Cadence: previews render one frame per OVERVIEW_REFRESH_INTERVAL tick
	// instead of one per frame, which is invisible on a miniature.
	//
	// Entering during an in-flight board switch retargets the slide into the zoom out from
	// wherever it currently is and cancels the pending sleep of the outgoing board, since
	// every board must be live in the overview.
	void set_overview(bool p_overview);
	bool is_overview_active() const { return board_view.is_overview_active(); }

	// True while a board switch or an overview zoom is still in flight, and false from the
	// frame its progress reaches the target onwards. Exposed because the strip's own process
	// flag is not a usable "motion finished" signal inside the overview: the overview keeps
	// processing after its zoom lands, to drive the throttled preview refresh. Observers that
	// need to know when board motion is over -- automation waits above all -- read this.
	bool is_transition_animating() const { return board_view.is_animating(); }

	// Re-draws the overview captions from each board's current title, or does nothing
	// outside the overview. The captions are snapshotted text, not bound to the board's
	// title property, so a rename that happens while the overview is up -- the title-bar
	// switcher supports renaming without leaving it -- has no other way to reach them.
	void refresh_overview_captions();

	// Seconds between overview preview refreshes.
	static constexpr real_t OVERVIEW_REFRESH_INTERVAL = real_t(1.0) / real_t(15.0);

	// The resolution divisor for p_board_count boards in p_viewport: the reciprocal of the
	// overview's on-screen scale, rounded to the nearest whole divisor and never below 1.
	static int overview_preview_shrink_for(int p_board_count, const Size2 &p_viewport);

	uint32_t get_overview_refresh_count() const { return overview_refresh_count; }
	// The unscaled caption layer over the overview. Exposed so callers can observe the
	// captions without reaching through the scaled board container.
	Control *get_caption_overlay() const { return caption_overlay; }

	// Converts a leaf focus request raised while the overview is up into a board
	// selection, and returns true when it did. In the overview every board is on screen at
	// once, so pointing at one -- clicking inside it, dropping a pane onto it -- means
	// "open this board", not "retarget the editor at that board's leaf". Returns false
	// when the overview is not up or the workspace belongs to no board here, in which case
	// the request proceeds normally.
	bool route_overview_focus_request(const EditorSceneWorkspace *p_workspace);

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
