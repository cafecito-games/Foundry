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

#include "scene/gui/popup_menu.h"

class EditorBoardStrip;

/**
 * Context menu for a single board: rename, close, close others, and reorder.
 *
 * The target board is captured by ObjectID when the menu opens so a close or
 * reorder that lands while the menu is still up cannot make an activation act
 * on the wrong board. Items and their disabled state are rebuilt from the strip
 * on every popup.
 */
class EditorBoardActionsMenu : public PopupMenu {
	FOUNDRY_CLASS(EditorBoardActionsMenu, PopupMenu);

public:
	enum ItemID {
		ITEM_RENAME = 0,
		ITEM_CLOSE = 1,
		ITEM_CLOSE_OTHERS = 2,
		ITEM_MOVE_LEFT = 3,
		ITEM_MOVE_RIGHT = 4,
	};

private:
	EditorBoardStrip *strip = nullptr;
	ObjectID target_board_id;

	void _rebuild_items();
	void _on_id_pressed(int p_id);
	// Deferred from id_pressed so close_board cannot run inside popup input dispatch.
	void _close_target_board();
	void _close_other_boards();

protected:
	static void _bind_methods();

public:
	void set_strip(EditorBoardStrip *p_strip);
	EditorBoardStrip *get_strip() const { return strip; }

	// Rebuilds items for the board at p_index and pops the menu at p_screen_position.
	void popup_for_board(int p_index, const Point2 &p_screen_position);

	ObjectID get_target_board_id() const { return target_board_id; }

	EditorBoardActionsMenu();
};
