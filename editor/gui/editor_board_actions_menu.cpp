/**************************************************************************/
/*  editor_board_actions_menu.cpp                                         */
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

#include "editor_board_actions_menu.h"

#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"

#include "scene/scene_string_names.h"

void EditorBoardActionsMenu::_bind_methods() {
	ADD_SIGNAL(MethodInfo("rename_requested", PropertyInfo(Variant::INT, "board_index")));
}

void EditorBoardActionsMenu::set_strip(EditorBoardStrip *p_strip) {
	strip = p_strip;
}

void EditorBoardActionsMenu::_rebuild_items() {
	clear();
	reset_size();

	const int board_count = strip ? strip->get_board_count() : 0;
	const int index = strip ? strip->resolve_board_index(target_board_id) : -1;
	const bool can_close = board_count > 1;
	const bool can_move_left = index > 0;
	const bool can_move_right = index >= 0 && index < board_count - 1;

	add_item(TTRC("Rename Board"), ITEM_RENAME);
	add_item(TTRC("Close Board"), ITEM_CLOSE);
	set_item_disabled(get_item_index(ITEM_CLOSE), !can_close);
	add_item(TTRC("Close Other Boards"), ITEM_CLOSE_OTHERS);
	set_item_disabled(get_item_index(ITEM_CLOSE_OTHERS), !can_close);
	add_separator();
	add_item(TTRC("Move Left"), ITEM_MOVE_LEFT);
	set_item_disabled(get_item_index(ITEM_MOVE_LEFT), !can_move_left);
	add_item(TTRC("Move Right"), ITEM_MOVE_RIGHT);
	set_item_disabled(get_item_index(ITEM_MOVE_RIGHT), !can_move_right);
}

void EditorBoardActionsMenu::popup_for_board(int p_index, const Point2 &p_screen_position) {
	ERR_FAIL_NULL(strip);
	EditorBoard *board = strip->get_board(p_index);
	ERR_FAIL_NULL(board);

	target_board_id = board->get_instance_id();
	_rebuild_items();
	popup(Rect2i(p_screen_position, Size2i()));
}

void EditorBoardActionsMenu::_on_id_pressed(int p_id) {
	if (!strip) {
		return;
	}

	// Re-resolve at activation time: a close or reorder while the menu was open can
	// free the target or shift every index after it.
	const int index = strip->resolve_board_index(target_board_id);
	if (index < 0) {
		return;
	}

	switch (p_id) {
		case ITEM_RENAME: {
			emit_signal(SNAME("rename_requested"), index);
		} break;
		case ITEM_CLOSE: {
			strip->close_board(index);
		} break;
		case ITEM_CLOSE_OTHERS: {
			Vector<ObjectID> others;
			for (int i = 0; i < strip->get_board_count(); i++) {
				if (i == index) {
					continue;
				}
				EditorBoard *board = strip->get_board(i);
				if (board) {
					others.push_back(board->get_instance_id());
				}
			}
			strip->close_boards(others);
		} break;
		case ITEM_MOVE_LEFT: {
			if (index > 0) {
				strip->move_board(index, index - 1);
			}
		} break;
		case ITEM_MOVE_RIGHT: {
			if (index < strip->get_board_count() - 1) {
				strip->move_board(index, index + 1);
			}
		} break;
	}
}

EditorBoardActionsMenu::EditorBoardActionsMenu() {
	connect(SceneStringName(id_pressed), callable_mp(this, &EditorBoardActionsMenu::_on_id_pressed));
}
