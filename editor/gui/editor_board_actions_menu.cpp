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

void EditorBoardActionsMenu::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_POSTINITIALIZE: {
			// id_pressed belongs to PopupMenu and is only visible to ClassDB after this
			// class finishes _post_initialize; connecting from the constructor always fails
			// for the first instance in a process (the one EditorNode actually uses).
			connect(SceneStringName(id_pressed), callable_mp(this, &EditorBoardActionsMenu::_on_id_pressed));
		} break;
	}
}

void EditorBoardActionsMenu::set_strip(EditorBoardStrip *p_strip) {
	strip = p_strip;
}

void EditorBoardActionsMenu::_rebuild_items() {
	clear();
	board_item_targets.clear();

	const int board_count = strip ? strip->get_board_count() : 0;
	const int index = strip ? strip->resolve_board_index(target_board_id) : -1;
	const bool can_close = board_count > 1;
	const bool can_move_left = index > 0;
	const bool can_move_right = index >= 0 && index < board_count - 1;

	if (include_board_list) {
		for (int i = 0; i < board_count; i++) {
			EditorBoard *board = strip->get_board(i);
			if (!board) {
				continue;
			}
			const int item_id = ITEM_BOARD_BASE + i;
			add_check_item(board->get_title(), item_id);
			const int item_index = get_item_index(item_id);
			set_item_auto_translate_mode(item_index, AUTO_TRANSLATE_MODE_DISABLED);
			set_item_checked(item_index, i == strip->get_active_index());
			board_item_targets.insert(item_id, board->get_instance_id());
		}
		add_separator();
	}

	add_item(TTRC("New Board"), ITEM_NEW_BOARD);
	add_item(TTRC("Board Overview"), ITEM_OVERVIEW);
	add_separator();
	add_item(TTRC("Rename Board"), ITEM_RENAME);
	add_item(TTRC("Move Left"), ITEM_MOVE_LEFT);
	set_item_disabled(get_item_index(ITEM_MOVE_LEFT), !can_move_left);
	add_item(TTRC("Move Right"), ITEM_MOVE_RIGHT);
	set_item_disabled(get_item_index(ITEM_MOVE_RIGHT), !can_move_right);
	add_separator();
	add_item(TTRC("Close Board"), ITEM_CLOSE);
	set_item_disabled(get_item_index(ITEM_CLOSE), !can_close);
	add_item(TTRC("Close Other Boards"), ITEM_CLOSE_OTHERS);
	set_item_disabled(get_item_index(ITEM_CLOSE_OTHERS), !can_close);
	reset_size();
}

void EditorBoardActionsMenu::popup_for_board(int p_index, const Point2 &p_screen_position, bool p_include_board_list) {
	ERR_FAIL_NULL(strip);
	EditorBoard *board = strip->get_board(p_index);
	ERR_FAIL_NULL(board);

	target_board_id = board->get_instance_id();
	include_board_list = p_include_board_list;
	_rebuild_items();
	// Size2i(1, 1) avoids Window::popup()'s empty-rect special case at position (0, 0).
	popup(Rect2i(p_screen_position, Size2i(1, 1)));
}

void EditorBoardActionsMenu::_add_board_and_activate(ObjectID p_strip_id) {
	EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id);
	if (!target_strip) {
		return;
	}
	EditorBoard *board = target_strip->add_board();
	if (board) {
		target_strip->set_active_board(target_strip->get_board_index(board));
	}
}

void EditorBoardActionsMenu::_enter_overview(ObjectID p_strip_id) {
	if (EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id)) {
		target_strip->set_overview(true);
	}
}

void EditorBoardActionsMenu::_activate_board(ObjectID p_strip_id, ObjectID p_board_id) {
	EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id);
	if (!target_strip) {
		return;
	}
	const int index = target_strip->resolve_board_index(p_board_id);
	if (index >= 0) {
		target_strip->set_active_board(index);
	}
}

void EditorBoardActionsMenu::_close_target_board(ObjectID p_strip_id, ObjectID p_board_id) {
	EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id);
	if (!target_strip) {
		return;
	}
	const int index = target_strip->resolve_board_index(p_board_id);
	if (index < 0) {
		return;
	}
	target_strip->close_board(index);
}

void EditorBoardActionsMenu::_close_other_boards(
		ObjectID p_strip_id, ObjectID p_target_board_id, const Array &p_board_ids) {
	EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id);
	if (!target_strip || target_strip->resolve_board_index(p_target_board_id) < 0) {
		return;
	}

	Vector<ObjectID> board_ids;
	for (int i = 0; i < p_board_ids.size(); i++) {
		board_ids.push_back(ObjectID(uint64_t(p_board_ids[i])));
	}
	target_strip->close_boards(board_ids);
}

void EditorBoardActionsMenu::_move_board(ObjectID p_strip_id, ObjectID p_board_id, int p_direction) {
	EditorBoardStrip *target_strip = ObjectDB::get_instance<EditorBoardStrip>(p_strip_id);
	if (!target_strip) {
		return;
	}
	const int index = target_strip->resolve_board_index(p_board_id);
	const int destination = index + p_direction;
	if (index < 0 || destination < 0 || destination >= target_strip->get_board_count()) {
		return;
	}
	target_strip->move_board(index, destination);
}

void EditorBoardActionsMenu::_on_id_pressed(int p_id) {
	if (!strip) {
		return;
	}
	const ObjectID strip_id = strip->get_instance_id();
	if (p_id == ITEM_NEW_BOARD) {
		callable_mp(this, &EditorBoardActionsMenu::_add_board_and_activate).bind(strip_id).call_deferred();
		return;
	}
	if (p_id == ITEM_OVERVIEW) {
		callable_mp(this, &EditorBoardActionsMenu::_enter_overview).bind(strip_id).call_deferred();
		return;
	}
	if (const ObjectID *board_id = board_item_targets.getptr(p_id)) {
		callable_mp(this, &EditorBoardActionsMenu::_activate_board).bind(strip_id, *board_id).call_deferred();
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
			// Never tear a board down from inside PopupMenu input dispatch: EditorNode's
			// board-close finish path is deferred for the same reason.
			callable_mp(this, &EditorBoardActionsMenu::_close_target_board).bind(strip_id, target_board_id).call_deferred();
		} break;
		case ITEM_CLOSE_OTHERS: {
			Array others;
			for (int i = 0; i < strip->get_board_count(); i++) {
				if (i == index) {
					continue;
				}
				EditorBoard *board = strip->get_board(i);
				if (board) {
					others.push_back(uint64_t(board->get_instance_id()));
				}
			}
			callable_mp(this, &EditorBoardActionsMenu::_close_other_boards)
					.bind(strip_id, target_board_id, others)
					.call_deferred();
		} break;
		case ITEM_MOVE_LEFT: {
			if (index > 0) {
				// Same deferral as close: move_board rebuilds switcher chrome and must not
				// run inside PopupMenu input dispatch.
				callable_mp(this, &EditorBoardActionsMenu::_move_board).bind(strip_id, target_board_id, -1).call_deferred();
			}
		} break;
		case ITEM_MOVE_RIGHT: {
			if (index < strip->get_board_count() - 1) {
				callable_mp(this, &EditorBoardActionsMenu::_move_board).bind(strip_id, target_board_id, 1).call_deferred();
			}
		} break;
	}
}

EditorBoardActionsMenu::EditorBoardActionsMenu() {
}
