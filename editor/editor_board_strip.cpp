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

#include "core/io/config_file.h"

namespace {
constexpr const char *BOARDS_SECTION = "Boards";
} // namespace

void EditorBoardStrip::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			const Size2 size = get_size();
			const Transform2D transform = board_view.get_transform();
			const real_t pitch = size.width + board_view.get_board_pitch_gutter();
			const Size2 scaled_size = size * board_view.get_scale();
			for (int i = 0; i < boards.size(); i++) {
				const Point2 position = transform.xform(Point2(real_t(i) * pitch, 0.0));
				fit_child_in_rect(boards[i], Rect2(position, scaled_size));
			}
		} break;

		case NOTIFICATION_PROCESS: {
			_advance_transition(real_t(get_process_delta_time()));
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
	ADD_SIGNAL(MethodInfo("active_board_changed", PropertyInfo(Variant::INT, "index")));
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

	// Activate a neighbour before the outgoing board is torn down so the editor is
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
	if (EditorBoard *outgoing_board = ObjectDB::get_instance<EditorBoard>(transition_outgoing_id)) {
		outgoing_board->set_dormant(true);
	}
	transition_outgoing_id = ObjectID();
	board_view.switch_to_index(active_index, get_size());
	board_view.finish_transition();
	set_process(false);

	queue_sort();
	emit_signal(SNAME("board_removed"), p_index);
	return true;
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
	next_board_id = MAX(next_board_id, p_board_id + 1);

	board->set_dormant(!boards.is_empty());
	boards.push_back(board);
	add_child(board);
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

	emit_signal(SNAME("active_board_changed"), active_index);
}

void EditorBoardStrip::_advance_transition(real_t p_delta) {
	board_view.advance(p_delta);
	queue_sort();
	if (board_view.is_animating()) {
		return;
	}

	if (EditorBoard *outgoing_board = ObjectDB::get_instance<EditorBoard>(transition_outgoing_id)) {
		outgoing_board->set_dormant(true);
	}
	transition_outgoing_id = ObjectID();
	set_process(false);
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
}
