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

void EditorBoardStrip::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			const Size2 size = get_size();
			for (int i = 0; i < boards.size(); i++) {
				fit_child_in_rect(boards[i], Rect2(Point2(i * size.width, 0), size));
			}
		} break;
	}
}

void EditorBoardStrip::_bind_methods() {
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
	ERR_FAIL_NULL_V(editor_data, nullptr);

	// The allocator is injected through create() rather than assigned afterwards:
	// the workspace's first leaf is allocated during construction, so a later
	// injection would leave that leaf holding an id from the workspace's own
	// fallback counter and collide with another board's first leaf.
	EditorBoard *board = EditorBoard::create(next_board_id++, p_title, editor_selection, editor_data, this);
	ERR_FAIL_NULL_V(board, nullptr);

	board->set_dormant(!boards.is_empty());
	boards.push_back(board);
	add_child(board);
	queue_sort();
	return board;
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

void EditorBoardStrip::set_active_index(int p_index) {
	ERR_FAIL_INDEX(p_index, boards.size());
	if (p_index == active_index) {
		return;
	}

	if (EditorBoard *outgoing = get_active_board()) {
		outgoing->remember_focused_leaf_id(outgoing->get_workspace()->get_focused_leaf_id());
		outgoing->set_dormant(true);
	}
	active_index = p_index;
	boards[active_index]->set_dormant(false);
	queue_sort();
}

void EditorBoardStrip::set_active_board(EditorBoard *p_board) {
	ERR_FAIL_NULL(p_board);
	const int index = boards.find(p_board);
	ERR_FAIL_COND(index < 0);
	set_active_index(index);
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

EditorBoardStrip::EditorBoardStrip() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}
