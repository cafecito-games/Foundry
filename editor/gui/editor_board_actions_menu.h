/**************************************************************************/
/*  editor_board_actions_menu.h                                           */
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

#include "core/object/object_id.h"
#include "core/templates/hash_map.h"

#include "scene/gui/popup_menu.h"

class EditorBoardStrip;

/**
 * Board menu for creation, overview, switching, rename, close, and reorder.
 *
 * The target board is captured by ObjectID when the menu opens so a close or
 * reorder that lands while the menu is still up cannot make an activation act
 * on the wrong board. The optional board list captures every target the same
 * way. Items and their disabled state are rebuilt from the strip on every popup.
 */
class EditorBoardActionsMenu : public PopupMenu {
	FOUNDRY_CLASS(EditorBoardActionsMenu, PopupMenu);

public:
	enum ItemID {
		ITEM_NEW_BOARD,
		ITEM_OVERVIEW,
		ITEM_RENAME,
		ITEM_CLOSE,
		ITEM_CLOSE_OTHERS,
		ITEM_MOVE_LEFT,
		ITEM_MOVE_RIGHT,
		ITEM_BOARD_BASE = 1000,
	};

private:
	EditorBoardStrip *strip = nullptr;
	ObjectID target_board_id;
	bool include_board_list = false;
	HashMap<int, ObjectID> board_item_targets;

	void _rebuild_items();
	void _on_id_pressed(int p_id);
	void _add_board_and_activate(ObjectID p_strip_id);
	void _enter_overview(ObjectID p_strip_id);
	void _activate_board(ObjectID p_strip_id, ObjectID p_board_id);
	// Deferred from id_pressed so collection mutations cannot run inside popup input dispatch.
	void _close_target_board(ObjectID p_strip_id, ObjectID p_board_id);
	void _close_other_boards(ObjectID p_strip_id, ObjectID p_target_board_id, const Array &p_board_ids);
	void _move_board(ObjectID p_strip_id, ObjectID p_board_id, int p_direction);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_strip(EditorBoardStrip *p_strip);
	EditorBoardStrip *get_strip() const { return strip; }

	// Rebuilds items for the board at p_index and pops the menu at p_screen_position.
	void popup_for_board(int p_index, const Point2 &p_screen_position, bool p_include_board_list = false);

	ObjectID get_target_board_id() const { return target_board_id; }

	EditorBoardActionsMenu();
};
