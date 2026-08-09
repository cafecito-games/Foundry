/**************************************************************************/
/*  editor_board.cpp                                                      */
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


#include "editor/editor_board.h"

#include "editor/editor_scene_workspace.h"

void EditorBoard::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			if (workspace) {
				fit_child_in_rect(workspace, Rect2(Point2(), get_size()));
			}
		} break;
	}
}

void EditorBoard::_bind_methods() {
}

EditorBoard *EditorBoard::create(int p_board_id, const String &p_title, EditorSelection *p_editor_selection, EditorData *p_editor_data, WorkspaceLeafIdAllocator *p_allocator) {
	ERR_FAIL_NULL_V(p_editor_data, nullptr);

	EditorBoard *board = memnew(EditorBoard);
	board->board_id = p_board_id;
	board->title = p_title;
	board->set_name(vformat("EditorBoard_%d", p_board_id));

	board->workspace = EditorSceneWorkspace::create_single_leaf_workspace(p_editor_selection, p_editor_data, p_allocator);
	board->add_child(board->workspace);
	board->workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	board->focused_leaf_id = board->workspace->get_focused_leaf_id();
	return board;
}

void EditorBoard::set_title(const String &p_title) {
	title = p_title;
}

void EditorBoard::set_dormant(bool p_dormant) {
	if (dormant == p_dormant) {
		return;
	}
	dormant = p_dormant;
	set_visible(!dormant);
	set_process_mode(dormant ? PROCESS_MODE_DISABLED : PROCESS_MODE_INHERIT);
}

EditorBoard::EditorBoard() {
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}
