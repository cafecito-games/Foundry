/**************************************************************************/
/*  editor_board_strip.cpp                                                */
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

#include "editor/editor_board_strip.h"

#include "editor/editor_board.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"

#include "core/io/config_file.h"
#include "core/math/math_funcs.h"
#include "scene/gui/button.h"
#include "scene/scene_string_names.h"

namespace {
constexpr const char *BOARDS_SECTION = "Boards";
// Vertical gap, in pixels, between a board's bottom edge and its caption.
constexpr real_t CAPTION_MARGIN = 6.0;
// Horizontal travel, in pixels, that turns a held caption into a reorder instead of a click.
constexpr real_t CAPTION_DRAG_THRESHOLD = 6.0;
} // namespace

void EditorBoardStrip::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			const Size2 size = get_size();
			const Transform2D transform = board_view.get_transform();
			const real_t view_scale = board_view.get_scale();
			const real_t pitch = size.width + board_view.get_board_pitch_gutter();
			const Size2 scaled_size = size * view_scale;
			for (int i = 0; i < boards.size(); i++) {
				const Point2 position = transform.xform(Point2(real_t(i) * pitch, 0.0));
				// Every board is laid out at the full strip rect and shrunk by a canvas
				// scale, never by being resized into the smaller overview rect. A resize
				// would re-run the board's own layout at the reduced width, where the docks
				// hold their minimum widths and the scene viewport absorbs the entire loss --
				// at three boards on a typical window the viewport reaches zero and the
				// overview degenerates into dock stacks. Scaling shrinks the board whole, so
				// it keeps the focused board's aspect and stays recognizable.
				fit_child_in_rect(boards[i], Rect2(position, size));
				boards[i]->set_scale(Size2(view_scale, view_scale));
			}
			_layout_captions(transform, pitch, scaled_size);
		} break;

		case NOTIFICATION_PROCESS: {
			const real_t delta = real_t(get_process_delta_time());
			_advance_transition(delta);
			if (board_view.is_overview_active()) {
				_pump_overview_refresh(delta);
			}
		} break;
	}
}

void EditorBoardStrip::_bind_methods() {
	ADD_SIGNAL(MethodInfo("boards_about_to_restore"));
	ADD_SIGNAL(MethodInfo("boards_restored"));
	ADD_SIGNAL(MethodInfo("board_added", PropertyInfo(Variant::INT, "index")));
	// Emitted while the board still exists, so listeners can relocate editor-wide state
	// parented into it and drop the registrations keyed on its leaves.
	ADD_SIGNAL(MethodInfo("board_about_to_close", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("board_removed", PropertyInfo(Variant::INT, "index")));
	// Emitted once per completed reorder, with the indices the board came from and landed on.
	ADD_SIGNAL(MethodInfo("board_moved", PropertyInfo(Variant::INT, "from"), PropertyInfo(Variant::INT, "to")));
	ADD_SIGNAL(MethodInfo("active_board_changed", PropertyInfo(Variant::INT, "index")));
	// Emitted when the zoomed-out overview is entered or left, at the start of the motion
	// rather than once it settles, so chrome reflecting the mode never lags a transition.
	ADD_SIGNAL(MethodInfo("overview_changed", PropertyInfo(Variant::BOOL, "active")));
}

EditorBoardStrip *EditorBoardStrip::create(EditorSelection *p_editor_selection, EditorData *p_editor_data) {
	ERR_FAIL_NULL_V(p_editor_data, nullptr);

	EditorBoardStrip *strip = memnew(EditorBoardStrip);
	strip->editor_selection = p_editor_selection;
	strip->editor_data = p_editor_data;
	strip->add_board(String());
	return strip;
}

EditorBoard *EditorBoardStrip::add_board(const String &p_title) {
	const String title = p_title.is_empty() ? vformat(TTR("Board %d"), boards.size() + 1) : p_title;
	EditorBoard *board = _append_board(next_board_id, title);
	ERR_FAIL_NULL_V(board, nullptr);
	// Restores go through _append_board directly: boards_restored already rewires every
	// board at once, and emitting per board would wire the restored ones twice.
	emit_signal(SNAME("board_added"), boards.size() - 1);
	return board;
}

bool EditorBoardStrip::close_board(int p_index) {
	ERR_FAIL_INDEX_V(p_index, boards.size(), false);
	// A boardless editor has nowhere to put the docks, the scene-mode surface, or the
	// current scene, so the last board is not closable.
	if (boards.size() <= 1) {
		return false;
	}

	EditorBoard *board = boards[p_index];
	ERR_FAIL_NULL_V(board, false);

	const PackedInt32Array scene_indices = _collect_board_scene_indices(board);
	if (!scene_indices.is_empty()) {
		if (!board_scene_close_handler.is_valid()) {
			return false;
		}
		if (!bool(board_scene_close_handler.call(p_index, scene_indices))) {
			return false;
		}
		// The handler runs arbitrary editor code. Re-validate rather than trusting that
		// the board set survived it unchanged.
		ERR_FAIL_INDEX_V(p_index, boards.size(), false);
		ERR_FAIL_COND_V(boards[p_index] != board, false);
	}

	// Activate a neighbor before the outgoing board is torn down so the editor is
	// never left without a live board.
	if (p_index == active_index) {
		set_active_board(p_index > 0 ? p_index - 1 : p_index + 1);
	}

	emit_signal(SNAME("board_about_to_close"), p_index);

	boards.remove_at(p_index);
	remove_child(board);
	memdelete(board);

	if (active_index > p_index) {
		active_index--;
	}

	// A removal invalidates any slide in progress: board_view's target offset was computed
	// against a board list that no longer exists. The outgoing half of the pair may be the
	// board that was just freed above (either the one just displaced, or a stale one left
	// over from an earlier interrupted switch), but it may just as easily be a board that
	// survives this close untouched -- p_index need not be the active or outgoing board at
	// all. Resolving by instance id instead of dereferencing the raw pointer covers both:
	// a freed outgoing board resolves to null and is skipped, and a surviving one is put to
	// sleep here rather than being stranded awake and off-screen with nothing left to ever
	// settle it. Settling instantly on the current active board is a deliberate hard cut
	// rather than rebasing a partial animation against the new indices: this is a rare
	// interruption, and a clean landing beats a subtly wrong ease-out.
	// The same hard cut applies to a zoom out of the overview: the tiles' preview bounds and
	// the caption overlay are torn down here rather than left stranded in overview state
	// with no transition left to finish them.
	if (board_view.is_overview_active()) {
		_leave_overview_state();
		overview_exit_pending = true;
	}
	_settle_dormancy();
	board_view.switch_to_index(active_index, get_size());
	board_view.finish_transition();
	set_process(false);

	queue_sort();
	emit_signal(SNAME("board_removed"), p_index);
	return true;
}

void EditorBoardStrip::move_board(int p_from, int p_to) {
	ERR_FAIL_INDEX(p_from, boards.size());
	ERR_FAIL_INDEX(p_to, boards.size());
	if (p_from == p_to) {
		return;
	}

	EditorBoard *moved = boards[p_from];
	ERR_FAIL_NULL(moved);

	// Captured before the list changes and re-resolved after it, so the board on screen is
	// the same object either side of the reorder.
	EditorBoard *previous_active = get_active_board();
	const ObjectID active_board_id = previous_active ? previous_active->get_instance_id() : ObjectID();
	const int previous_active_index = active_index;

	boards.remove_at(p_from);
	boards.insert(p_to, moved);

	// Child order is board order: it decides draw order and which board a click lands on, so
	// it has to track the list. The caption overlay is pushed back to last afterwards because
	// it must keep drawing above every board.
	move_child(moved, p_to);
	if (caption_overlay) {
		move_child(caption_overlay, get_child_count() - 1);
		// Captions are built one per board in board order, so the same move keeps each caption
		// with its board. Reordering the existing buttons rather than rebuilding them matters
		// while a drag is live: a rebuild would free the very caption the input is coming from.
		if (caption_overlay->get_child_count() == boards.size()) {
			caption_overlay->move_child(caption_overlay->get_child(p_from), p_to);
		}
	}

	const int resolved_active = resolve_board_index(active_board_id);
	if (resolved_active >= 0) {
		active_index = resolved_active;
	}

	if (!board_view.is_overview_active() && active_index != previous_active_index) {
		// Boards are laid out by index, so moving the active board changes where it sits.
		// The reorder itself is instantaneous, so the strip scrolls onto its new position as
		// a hard cut rather than sliding: there is no motion here for an ease-out to express.
		// Settling first is what keeps a slide that was still in flight from leaving a board
		// awake with nothing left to sleep it.
		_settle_dormancy();
		board_view.switch_to_index(active_index, get_size());
		board_view.finish_transition();
		set_process(false);
	}

	queue_sort();
	emit_signal(SNAME("board_moved"), p_from, p_to);
}

PackedInt32Array EditorBoardStrip::_collect_board_scene_indices(const EditorBoard *p_board) const {
	PackedInt32Array indices;
	ERR_FAIL_NULL_V(p_board, indices);
	ERR_FAIL_NULL_V(editor_data, indices);

	EditorSceneWorkspace *workspace = p_board->get_workspace();
	ERR_FAIL_NULL_V(workspace, indices);
	for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
		for (const int scene_index : editor_data->get_tile_scene_indices(leaf->get_leaf_id())) {
			indices.push_back(scene_index);
		}
	}
	return indices;
}

EditorBoard *EditorBoardStrip::_append_board(int p_board_id, const String &p_title) {
	ERR_FAIL_NULL_V(editor_data, nullptr);

	// The allocator is injected through create() rather than assigned afterwards:
	// the workspace's first leaf is allocated during construction, so a later
	// injection would leave that leaf holding an id from the workspace's own
	// fallback counter and collide with another board's first leaf.
	EditorBoard *board = EditorBoard::create(p_board_id, p_title, editor_selection, editor_data, this);
	ERR_FAIL_NULL_V(board, nullptr);
	// Lets the workspace resolve a cross-board tab drop through find_board_for_leaf()
	// instead of only ever looking at its own leaf list.
	board->get_workspace()->set_board_strip(this);
	// A split or a cross-board pane drop mints a new leaf -- and tile -- at any time, not
	// just through this method, so the overview's preview bounds are re-synced from the
	// signal rather than only from the call sites that happen to create boards.
	board->get_workspace()->connect(SNAME("leaf_added"), callable_mp(this, &EditorBoardStrip::_on_leaf_added));
	next_board_id = MAX(next_board_id, p_board_id + 1);

	board->set_dormant(!boards.is_empty());
	boards.push_back(board);
	add_child(board);
	// The captions draw on top of every board, which for a Control means being the last
	// child; a board appended after them would otherwise cover them.
	if (caption_overlay) {
		move_child(caption_overlay, get_child_count() - 1);
	}
	if (board_view.is_overview_active()) {
		// A board added while the filmstrip is up joins it live and bounded like the rest.
		board->set_dormant(false);
		_apply_overview_preview_bounds(true);
		_rebuild_captions();
		board_view.enter_overview(boards.size(), active_index, get_size());
	}
	queue_sort();
	return board;
}

void EditorBoardStrip::_clear_boards() {
	for (EditorBoard *board : boards) {
		remove_child(board);
		memdelete(board);
	}
	boards.clear();
	active_index = 0;
	next_board_id = 0;
	// Every board a slide could have been animating between is gone; the outgoing id would
	// resolve to null from here on regardless, but clearing it keeps the field's state
	// consistent with "no slide in progress" rather than pointing at a freed instance id.
	transition_outgoing_id = ObjectID();
	overview_exit_pending = false;
	caption_drag_board_id = ObjectID();
	caption_drag_active = false;
	set_process(false);
}

EditorBoard *EditorBoardStrip::get_board(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, boards.size(), nullptr);
	return boards[p_index];
}

EditorBoard *EditorBoardStrip::get_active_board() const {
	if (active_index < 0 || active_index >= boards.size()) {
		return nullptr;
	}
	return boards[active_index];
}

EditorSceneWorkspace *EditorBoardStrip::get_active_workspace() const {
	EditorBoard *board = get_active_board();
	return board ? board->get_workspace() : nullptr;
}

int EditorBoardStrip::get_board_index(const EditorBoard *p_board) const {
	for (int i = 0; i < boards.size(); i++) {
		if (boards[i] == p_board) {
			return i;
		}
	}
	return -1;
}

int EditorBoardStrip::resolve_board_index(ObjectID p_board_id) const {
	if (!p_board_id.is_valid()) {
		return -1;
	}
	EditorBoard *board = ObjectDB::get_instance<EditorBoard>(p_board_id);
	if (!board) {
		return -1;
	}
	return get_board_index(board);
}

void EditorBoardStrip::set_active_board(int p_index) {
	ERR_FAIL_INDEX(p_index, boards.size());
	if (board_view.is_overview_active()) {
		// Selecting a board is how the overview is left. Routing every selection through
		// one exit is what keeps the tiles' preview bounds and the caption overlay from
		// being stranded in their overview state by a caller that only knows about boards.
		_exit_overview_to(p_index);
		return;
	}
	_switch_to_board(p_index);
}

void EditorBoardStrip::_switch_to_board(int p_index) {
	ERR_FAIL_INDEX(p_index, boards.size());
	if (p_index == active_index) {
		return;
	}

	// The board the strip was already showing (or already sliding towards, if this call
	// interrupts an in-flight switch) becomes the new outgoing board.
	EditorBoard *previous_active = get_active_board();
	EditorBoard *incoming = boards[p_index];
	if (previous_active) {
		previous_active->remember_focused_leaf_id(previous_active->get_workspace()->get_focused_leaf_id());
	}

	// An interrupted slide can leave a stale outgoing board still awake -- the one from
	// before this call -- that is neither the new outgoing nor the new incoming board.
	// It is no longer part of the live pair, so it sleeps immediately rather than riding
	// out a slide nobody is animating towards it for.
	EditorBoard *stale_outgoing = ObjectDB::get_instance<EditorBoard>(transition_outgoing_id);
	if (stale_outgoing && stale_outgoing != previous_active && stale_outgoing != incoming) {
		stale_outgoing->set_dormant(true);
	}

	// Wake before sleeping: a frame in which every board is hidden would tear down the
	// live scene viewports and re-create them on the next frame.
	incoming->set_dormant(false);
	transition_outgoing_id = (previous_active && previous_active != incoming) ? previous_active->get_instance_id() : ObjectID();

	// active_index is committed before the focus request, because the editor resolves
	// leaf_focus_requested through the active board's workspace. It is also committed at
	// the start of the slide, not once it settles, so is_leaf_on_active_board() and the
	// active_board_changed signal both reflect the target immediately.
	active_index = p_index;
	board_view.switch_to_index(p_index, get_size());
	set_process(true);
	queue_sort();

	EditorSceneWorkspace *workspace = incoming->get_workspace();
	const int remembered_leaf_id = incoming->get_remembered_focused_leaf_id();
	if (workspace && workspace->get_leaf_by_id(remembered_leaf_id)) {
		workspace->request_leaf_focus(remembered_leaf_id);
	}

	// After the focus request, so the incoming board's workspace has already settled on
	// the leaf it means to focus, and before the signal, so every listener observes an
	// editor-wide focused tile that already names a tile on the new active board.
	_sync_editor_focused_tile();

	emit_signal(SNAME("active_board_changed"), active_index);
}

void EditorBoardStrip::_sync_editor_focused_tile() {
	if (!editor_data) {
		return;
	}
	EditorSceneWorkspace *workspace = get_active_workspace();
	if (!workspace) {
		return;
	}
	const int tile_id = workspace->get_effective_focused_tile_id();
	if (tile_id < 0) {
		// A board of nothing but script leaves owns no scene tile to file scenes under;
		// leaving the previous id in place beats pointing it at a tile that is not there.
		return;
	}
	editor_data->set_focused_tile_id(tile_id);
}

void EditorBoardStrip::_advance_transition(real_t p_delta) {
	board_view.advance(p_delta);
	queue_sort();
	if (board_view.is_animating()) {
		return;
	}

	_settle_dormancy();
	// The overview keeps processing after its zoom settles: the throttled preview tick has
	// to keep running for as long as the filmstrip is on screen.
	if (!board_view.is_overview_active()) {
		set_process(false);
	}
}

void EditorBoardStrip::_settle_dormancy() {
	if (EditorBoard *outgoing_board = ObjectDB::get_instance<EditorBoard>(transition_outgoing_id)) {
		outgoing_board->set_dormant(true);
	}
	transition_outgoing_id = ObjectID();

	if (overview_exit_pending) {
		// Every board stayed awake for the zoom back in so none of them blanked mid-motion;
		// only the board landed on survives it.
		for (int i = 0; i < boards.size(); i++) {
			boards[i]->set_dormant(i != active_index);
		}
		overview_exit_pending = false;
	}
}

int EditorBoardStrip::overview_preview_shrink_for(int p_board_count, const Size2 &p_viewport) {
	const real_t board_scale = EditorBoardView::overview_scale_for(p_board_count, p_viewport);
	if (board_scale <= CMP_EPSILON) {
		return 1;
	}
	return MAX(1, int(Math::round(real_t(1.0) / board_scale)));
}

void EditorBoardStrip::set_overview(bool p_overview) {
	if (p_overview == board_view.is_overview_active()) {
		return;
	}
	if (p_overview) {
		_enter_overview();
	} else {
		_exit_overview_to(active_index);
	}
}

void EditorBoardStrip::refresh_overview_captions() {
	if (!board_view.is_overview_active()) {
		return;
	}
	_rebuild_captions();
}

void EditorBoardStrip::_enter_overview() {
	// Every board is on screen at once, so every board must be live. A slide still in
	// flight has a board queued to sleep the moment it settles; that pending sleep is
	// dropped here rather than allowed to blank one frame of the filmstrip. The zoom out
	// itself retargets from wherever the slide currently is, so the two motions join up
	// instead of snapping.
	transition_outgoing_id = ObjectID();
	overview_exit_pending = false;
	for (EditorBoard *board : boards) {
		board->set_dormant(false);
	}

	board_view.enter_overview(boards.size(), active_index, get_size());
	_apply_overview_preview_bounds(true);
	_rebuild_captions();
	if (caption_overlay) {
		caption_overlay->show();
	}

	overview_refresh_accumulator = 0.0;
	overview_refresh_count = 0;
	set_process(true);
	queue_sort();
	emit_signal(SNAME("overview_changed"), true);
}

void EditorBoardStrip::_leave_overview_state() {
	if (!board_view.is_overview_active()) {
		return;
	}
	// Clearing the view's own overview flag here, rather than leaving it to whatever
	// transition a caller kicks off afterwards, is what makes it structurally impossible
	// for is_overview_active() to still read true once this returns: every other piece of
	// overview-only state (preview bounds, captions) is torn down unconditionally in this
	// same function, so the flag cannot legally lag behind it, regardless of whether -- or
	// how long after -- a caller gets around to its own switch_to_index()/exit_overview()
	// call for the accompanying motion.
	board_view.leave_overview();
	// The captions are about to be hidden, so any caption drag they were feeding ends with
	// them rather than surviving as state pointing at a caption nobody can reach.
	caption_drag_board_id = ObjectID();
	caption_drag_active = false;
	_apply_overview_preview_bounds(false);
	if (caption_overlay) {
		caption_overlay->hide();
	}
	emit_signal(SNAME("overview_changed"), false);
}

void EditorBoardStrip::_exit_overview_to(int p_index) {
	ERR_FAIL_INDEX(p_index, boards.size());
	if (!board_view.is_overview_active()) {
		return;
	}

	_leave_overview_state();
	if (p_index != active_index) {
		// The switch supplies the zoom back in: switch_to_index() retargets from the
		// overview's current scale and origin, so this stays one continuous motion.
		_switch_to_board(p_index);
	} else {
		board_view.exit_overview(active_index, get_size());
		set_process(true);
		queue_sort();
	}
	overview_exit_pending = true;
}

void EditorBoardStrip::_apply_overview_preview_bounds(bool p_overview) {
	const int shrink = p_overview ? overview_preview_shrink_for(boards.size(), get_size()) : 1;
	for (EditorBoard *board : boards) {
		EditorSceneWorkspace *workspace = board->get_workspace();
		if (!workspace) {
			continue;
		}
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			if (ScenePaneTile *tile = leaf->get_pane_tile()) {
				tile->set_preview_render_shrink(shrink);
				tile->set_preview_refresh_throttled(p_overview);
			}
		}
	}
}

void EditorBoardStrip::_on_leaf_added(int p_leaf_id) {
	if (!board_view.is_overview_active()) {
		return;
	}
	// The board count has not changed, so this only needs to bring the new tile in line
	// with the rest of the filmstrip, not retarget the view. Re-applying to every tile
	// rather than resolving p_leaf_id's own tile keeps this on the same path
	// _enter_overview()/_append_board() already use, so there is exactly one place that
	// computes the shrink and cadence every tile in the overview must agree on.
	_apply_overview_preview_bounds(true);
}

void EditorBoardStrip::_pump_overview_refresh(real_t p_delta) {
	overview_refresh_accumulator += p_delta;
	if (overview_refresh_accumulator < OVERVIEW_REFRESH_INTERVAL) {
		return;
	}
	// A stall must not queue up a burst of catch-up frames: the filmstrip only ever needs
	// the newest one.
	overview_refresh_accumulator = Math::fmod(overview_refresh_accumulator, OVERVIEW_REFRESH_INTERVAL);
	overview_refresh_count++;

	for (EditorBoard *board : boards) {
		EditorSceneWorkspace *workspace = board->get_workspace();
		if (!workspace) {
			continue;
		}
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			if (ScenePaneTile *tile = leaf->get_pane_tile()) {
				tile->refresh_throttled_previews();
			}
		}
	}
}

bool EditorBoardStrip::route_overview_focus_request(const EditorSceneWorkspace *p_workspace) {
	if (!board_view.is_overview_active() || !p_workspace) {
		return false;
	}
	for (int i = 0; i < boards.size(); i++) {
		if (boards[i]->get_workspace() == p_workspace) {
			_exit_overview_to(i);
			return true;
		}
	}
	return false;
}

void EditorBoardStrip::_rebuild_captions() {
	ERR_FAIL_NULL(caption_overlay);
	while (caption_overlay->get_child_count() > 0) {
		Node *child = caption_overlay->get_child(0);
		caption_overlay->remove_child(child);
		child->queue_free();
	}

	for (EditorBoard *board : boards) {
		Button *caption = memnew(Button);
		caption->set_text(board->get_title());
		caption->set_tooltip_text(board->get_title());
		caption->set_accessibility_name(board->get_title());
		caption->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
		// Bound by instance id rather than index: a close shifts every index after it, and
		// these buttons outlive that shift.
		caption->connect(SceneStringName(pressed), callable_mp(this, &EditorBoardStrip::_on_caption_pressed).bind(board->get_instance_id()));
		// Taken before the button's own handling, so a horizontal drag reorders instead of
		// reading as a click on the board it started over.
		caption->connect(SceneStringName(gui_input), callable_mp(this, &EditorBoardStrip::_on_caption_gui_input).bind(board->get_instance_id()));
		caption_overlay->add_child(caption);
	}
}

void EditorBoardStrip::_layout_captions(const Transform2D &p_transform, real_t p_pitch, const Size2 &p_scaled_size) {
	if (!caption_overlay || !caption_overlay->is_visible()) {
		return;
	}

	const Size2 size = get_size();
	fit_child_in_rect(caption_overlay, Rect2(Point2(), size));

	const int count = MIN(caption_overlay->get_child_count(), boards.size());
	const int dragged_index = caption_drag_active ? resolve_board_index(caption_drag_board_id) : -1;
	for (int i = 0; i < count; i++) {
		Control *caption = Object::cast_to<Control>(caption_overlay->get_child(i));
		if (!caption) {
			continue;
		}
		if (i == dragged_index) {
			// The dragged caption tracks the pointer instead of its board slot, so it stays
			// under the cursor between crossings while the boards themselves snap into their
			// new order behind it.
			const Size2 dragged_size = caption->get_combined_minimum_size();
			const Point2 slot_position = p_transform.xform(Point2(real_t(i) * p_pitch, 0.0));
			const real_t dragged_x = CLAMP(caption_drag_pointer_x - dragged_size.width * 0.5, real_t(0.0), MAX(real_t(0.0), size.width - dragged_size.width));
			const real_t dragged_y = MIN(slot_position.y + p_scaled_size.height + CAPTION_MARGIN, size.height - dragged_size.height);
			caption->set_size(dragged_size);
			caption->set_position(Point2(dragged_x, dragged_y));
			continue;
		}
		// The caption sits outside the scaled board container, so it is laid out at its own
		// natural size and stays crisp at full font size while the board behind it shrinks.
		const Size2 caption_size = caption->get_combined_minimum_size();
		const Point2 board_position = p_transform.xform(Point2(real_t(i) * p_pitch, 0.0));
		const real_t x = board_position.x + (p_scaled_size.width - caption_size.width) * 0.5;
		const real_t y = MIN(board_position.y + p_scaled_size.height + CAPTION_MARGIN, size.height - caption_size.height);
		caption->set_size(caption_size);
		caption->set_position(Point2(x, y));
	}
}

void EditorBoardStrip::_on_caption_pressed(ObjectID p_board_id) {
	if (caption_drag_swallow_click) {
		caption_drag_swallow_click = false;
		return;
	}
	const int index = resolve_board_index(p_board_id);
	if (index < 0) {
		return;
	}
	_exit_overview_to(index);
}

int EditorBoardStrip::_overview_index_at_x(real_t p_x) const {
	// The boards are centered vertically in the overview, so the strip's own vertical center
	// is always inside the board band whatever the board count or scale.
	return board_view.index_at_point(Point2(p_x, get_size().height * 0.5), boards.size(), get_size());
}

void EditorBoardStrip::_end_caption_drag(bool p_release_inside_caption) {
	// Only arm the swallow when the caption's own BaseButton is about to emit "pressed" for
	// this same release (i.e. the pointer came back inside its bounds). A release outside the
	// caption never fires "pressed" at all, so leaving the flag armed for that case would go
	// uncleared until some unrelated activation happened to trip it, including a keyboard
	// activation on a different caption that never touched this drag.
	caption_drag_swallow_click = caption_drag_active && p_release_inside_caption;
	caption_drag_board_id = ObjectID();
	caption_drag_active = false;
	queue_sort();
}

void EditorBoardStrip::_on_caption_gui_input(const Ref<InputEvent> &p_event, ObjectID p_board_id) {
	if (!board_view.is_overview_active()) {
		return;
	}

	// Event positions are viewport-relative; every board index the drag resolves is computed
	// in the strip's own space, the same space the boards are laid out in.
	const Transform2D to_strip = get_global_transform().affine_inverse();

	const Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid() && mouse_button->get_button_index() == MouseButton::LEFT) {
		if (mouse_button->is_pressed()) {
			caption_drag_board_id = p_board_id;
			caption_drag_press_x = to_strip.xform(mouse_button->get_global_position()).x;
			caption_drag_pointer_x = caption_drag_press_x;
			caption_drag_active = false;
			caption_drag_swallow_click = false;
		} else if (caption_drag_board_id.is_valid()) {
			// Approximates the bounds check BaseButton itself runs to decide whether it will
			// emit "pressed" for this release, in the strip's own space rather than trusting
			// the event's local position field. Horizontal-only, like the rest of the drag: the
			// caption overlay sits over the strip with an identity transform, so a caption's
			// laid-out x-range is already in that space.
			const int drag_index = resolve_board_index(caption_drag_board_id);
			bool release_inside = false;
			if (caption_overlay && drag_index >= 0 && drag_index < caption_overlay->get_child_count()) {
				if (Control *caption = Object::cast_to<Control>(caption_overlay->get_child(drag_index))) {
					const real_t release_x = to_strip.xform(mouse_button->get_global_position()).x;
					release_inside = release_x >= caption->get_position().x && release_x <= caption->get_position().x + caption->get_size().x;
				}
			}
			_end_caption_drag(release_inside);
		}
		return;
	}

	const Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_null() || !caption_drag_board_id.is_valid()) {
		return;
	}
	if (!motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
		// The button came up somewhere this caption never saw, so its BaseButton never
		// registered a press-inside release; drop the drag rather than letting a later hover
		// keep reordering boards, and without arming a swallow that would never be consumed.
		_end_caption_drag(false);
		return;
	}

	caption_drag_pointer_x = to_strip.xform(motion->get_global_position()).x;
	if (!caption_drag_active && Math::abs(caption_drag_pointer_x - caption_drag_press_x) < CAPTION_DRAG_THRESHOLD) {
		return;
	}
	caption_drag_active = true;

	// The reorder is committed as the pointer crosses into another board rather than held
	// back until release, so the boards visibly part around the one being dragged. Each
	// crossing is one reorder and emits board_moved once; a pointer in a gutter or past the
	// ends resolves to no board and leaves the order alone.
	const int from = resolve_board_index(caption_drag_board_id);
	const int to = _overview_index_at_x(caption_drag_pointer_x);
	if (from >= 0 && to >= 0 && from != to) {
		move_board(from, to);
	} else {
		// Between crossings the caption still has to follow the pointer.
		queue_sort();
	}
}

bool EditorBoardStrip::is_leaf_on_active_board(int p_leaf_id) const {
	EditorBoard *owner = find_board_for_leaf(p_leaf_id);
	return owner == nullptr || owner == get_active_board();
}

Vector<EditorSceneWorkspace *> EditorBoardStrip::get_workspaces() const {
	Vector<EditorSceneWorkspace *> workspaces;
	workspaces.resize(boards.size());
	for (int i = 0; i < boards.size(); i++) {
		workspaces.write[i] = boards[i]->get_workspace();
	}
	return workspaces;
}

WorkspaceLeafNode *EditorBoardStrip::find_leaf_by_id(int p_leaf_id) const {
	for (EditorBoard *board : boards) {
		if (WorkspaceLeafNode *leaf = board->get_workspace()->get_leaf_by_id(p_leaf_id)) {
			return leaf;
		}
	}
	return nullptr;
}

EditorBoard *EditorBoardStrip::find_board_for_leaf(int p_leaf_id) const {
	for (EditorBoard *board : boards) {
		if (board->get_workspace()->get_leaf_by_id(p_leaf_id)) {
			return board;
		}
	}
	return nullptr;
}

ScenePaneTile *EditorBoardStrip::find_tile_by_id(int p_tile_id) const {
	for (EditorBoard *board : boards) {
		if (ScenePaneTile *tile = board->get_workspace()->get_tile_by_id(p_tile_id)) {
			return tile;
		}
	}
	return nullptr;
}

String EditorBoardStrip::board_section(int p_index) {
	return vformat("Board_%d", p_index);
}

void EditorBoardStrip::save_to_config(const Ref<ConfigFile> &p_config, const EditorBoardStrip *p_strip) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(p_strip);

	if (p_config->has_section(BOARDS_SECTION)) {
		p_config->erase_section(BOARDS_SECTION);
	}

	const int board_count = p_strip->get_board_count();
	p_config->set_value(BOARDS_SECTION, "board_count", board_count);
	p_config->set_value(BOARDS_SECTION, "active_board", p_strip->get_active_index());
	// Persisting the allocator high-water mark keeps leaf ids -- which key scene-tile
	// ownership and per-pane dock layout -- stable across a restart even when the
	// restored trees no longer contain the highest id ever issued.
	p_config->set_value(BOARDS_SECTION, "next_leaf_id", p_strip->peek_next_leaf_id());

	for (int i = 0; i < board_count; i++) {
		EditorBoard *board = p_strip->get_board(i);
		ERR_CONTINUE(!board);
		EditorSceneWorkspace *workspace = board->get_workspace();
		ERR_CONTINUE(!workspace);

		// Only the active board's workspace holds live focus; a dormant board's focus
		// was captured when it was switched away from.
		int focused_leaf = i == p_strip->get_active_index() ? workspace->get_focused_leaf_id() : board->get_remembered_focused_leaf_id();
		if (!workspace->get_leaf_by_id(focused_leaf)) {
			focused_leaf = workspace->get_focused_leaf_id();
		}

		p_config->set_value(BOARDS_SECTION, vformat("board_%d_id", i), board->get_board_id());
		p_config->set_value(BOARDS_SECTION, vformat("board_%d_title", i), board->get_title());
		p_config->set_value(BOARDS_SECTION, vformat("board_%d_focused_leaf", i), focused_leaf);

		EditorSceneWorkspace::save_to_config(p_config, workspace, board_section(i));
	}

	// Drop tiling trees left behind by boards that no longer exist so a later restore
	// cannot resurrect them.
	for (int i = board_count; p_config->has_section(board_section(i)); i++) {
		p_config->erase_section(board_section(i));
	}
}

bool EditorBoardStrip::has_board_session(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), false);
	if (!p_config->has_section(BOARDS_SECTION) || !p_config->has_section_key(BOARDS_SECTION, "board_count")) {
		return false;
	}
	return int(p_config->get_value(BOARDS_SECTION, "board_count")) >= 1;
}

int EditorBoardStrip::_persisted_leaf_id_ceiling(const Ref<ConfigFile> &p_config, int p_board_count) {
	int ceiling = int(p_config->get_value(BOARDS_SECTION, "next_leaf_id", 0));
	for (int board = 0; board < p_board_count; board++) {
		const String section = board_section(board);
		if (!p_config->has_section(section)) {
			continue;
		}
		const int node_count = int(p_config->get_value(section, "node_count", 0));
		for (int node = 0; node < node_count; node++) {
			const String key = vformat("node_%d_leaf_id", node);
			if (!p_config->has_section_key(section, key)) {
				continue;
			}
			ceiling = MAX(ceiling, int(p_config->get_value(section, key)) + 1);
		}
	}
	return ceiling;
}

void EditorBoardStrip::restore_from_config(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND(p_config.is_null());
	ERR_FAIL_NULL(editor_data);
	if (!has_board_session(p_config)) {
		return;
	}

	const int board_count = int(p_config->get_value(BOARDS_SECTION, "board_count", 1));

	emit_signal(SNAME("boards_about_to_restore"));

	// The tiles the overview bounded are about to be freed, and the restored strip lands on
	// a single active board, so the overview does not survive a restore.
	_leave_overview_state();

	// Seed the allocator past every id persisted anywhere in the config before a single
	// leaf exists. Reserving each id as its leaf is restored is not enough: a malformed
	// node falls back to a freshly allocated id, which would then collide with a
	// persisted id restored later in the same pass. Since EditorData keys scene-tile
	// ownership on leaf id, that collision is silent ownership corruption.
	set_next_leaf_id(_persisted_leaf_id_ceiling(p_config, board_count));

	_clear_boards();

	for (int i = 0; i < board_count; i++) {
		const int board_id = int(p_config->get_value(BOARDS_SECTION, vformat("board_%d_id", i), i));
		const String title = p_config->get_value(BOARDS_SECTION, vformat("board_%d_title", i), String());
		EditorBoard *board = _append_board(board_id, title);
		ERR_CONTINUE(!board);

		EditorSceneWorkspace *workspace = board->get_workspace();
		ERR_CONTINUE(!workspace);
		workspace->restore_from_config(p_config, board_section(i));

		int focused_leaf = int(p_config->get_value(BOARDS_SECTION, vformat("board_%d_focused_leaf", i), workspace->get_focused_leaf_id()));
		if (!workspace->get_leaf_by_id(focused_leaf)) {
			focused_leaf = workspace->get_focused_leaf_id();
		}
		if (focused_leaf != workspace->get_focused_leaf_id()) {
			workspace->set_focused_leaf(focused_leaf);
		}
		board->remember_focused_leaf_id(focused_leaf);
	}

	active_index = boards.is_empty() ? 0 : CLAMP(int(p_config->get_value(BOARDS_SECTION, "active_board", 0)), 0, boards.size() - 1);
	for (int i = 0; i < boards.size(); i++) {
		boards[i]->set_dormant(i != active_index);
	}
	// A restored active board is not the result of a switch, so it appears already settled
	// at its resting position rather than sliding in from board 0.
	board_view.switch_to_index(active_index, get_size());
	board_view.finish_transition();
	queue_sort();

	emit_signal(SNAME("boards_restored"));
}

EditorBoardStrip::EditorBoardStrip() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);

	// A plain Control rather than a Container: its children are positioned from board
	// geometry every sort, and it must pass every event it does not sit under straight
	// through to the boards below so cross-board drag keeps working in the overview.
	caption_overlay = memnew(Control);
	caption_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	caption_overlay->hide();
	add_child(caption_overlay);
}
