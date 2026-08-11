/**************************************************************************/
/*  editor_board_switcher.h                                               */
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

#include "scene/gui/panel_container.h"

class Button;
class Control;
class EditorBoardActionsMenu;
class EditorBoardStrip;
class HBoxContainer;
class LineEdit;

/**
 * Title-bar chrome for switching between boards: one button per board and a
 * dedicated menu for board creation, overview, switching, and board actions.
 *
 * The switcher holds no board list of its own. It mirrors EditorBoardStrip by
 * rebuilding its buttons from scratch on every structural signal the strip
 * emits, which is cheaper and far less bug-prone than incrementally patching
 * button state for a handful of boards.
 *
 * Inactive buttons switch on left-click. The active button stays selected and
 * leaves menu opening to the dedicated button, preserving double-click rename.
 * Any board button opens the actions menu for that board on right-click.
 */
class EditorBoardSwitcher : public PanelContainer {
	FOUNDRY_CLASS(EditorBoardSwitcher, PanelContainer);

	EditorBoardStrip *strip = nullptr;
	HBoxContainer *rail_hbox = nullptr;
	EditorBoardActionsMenu *actions_menu = nullptr;
	Button *menu_button = nullptr;
	Control *resize_parent = nullptr;
	bool compact = false;
	bool compact_update_queued = false;
	// Identity-keyed rather than child-index-keyed: rebuilds free and recreate buttons,
	// and the owned actions menu is a sibling that must not participate in board indexing.
	Vector<Button *> board_buttons;

	// Identifies the board button currently swapped for a LineEdit. Stored as an
	// instance id rather than a cached board pointer, so a board closing mid-rename is
	// simply "the id no longer resolves" rather than a dangling pointer.
	ObjectID renaming_board_id;
	Button *renaming_button = nullptr;
	LineEdit *rename_edit = nullptr;

	void _rebuild();
	void _on_board_button_pressed(int p_index);
	void _on_board_button_gui_input(const Ref<InputEvent> &p_event, int p_index);
	void _on_menu_pressed();
	void _set_compact(bool p_compact);
	void _queue_compact_mode_update();
	void _update_compact_mode();
	int _get_desired_full_width() const;
	void _begin_rename(int p_index);
	void _apply_pending_rename(const String &p_text);
	void _commit_rename(const String &p_text);
	void _commit_rename_from_focus_loss();
	void _cancel_rename();
	void _popup_actions_menu(int p_index, const Point2 &p_screen_position);
	Button *_board_button_at(int p_index) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Connects to the strip's board_added, board_removed, boards_restored, and
	// active_board_changed signals and performs the first rebuild. Safe to call
	// again to retarget the switcher at a different strip.
	void setup(EditorBoardStrip *p_strip);

	EditorBoardStrip *get_strip() const { return strip; }
	EditorBoardActionsMenu *get_actions_menu() const { return actions_menu; }
	Button *get_menu_button() const { return menu_button; }

	// Opens the board actions menu for p_board_index at p_screen_position. Used by the
	// strip's caption context-menu handler and by the switcher's own button interactions.
	void popup_board_actions(int p_board_index, const Point2 &p_screen_position);
	void popup_active_board_menu(const Point2 &p_screen_position, bool p_include_board_list = false);

	// True while an inline rename LineEdit is showing. Exposed for tests, which
	// cannot observe the swapped-in LineEdit through the strip.
	bool is_renaming() const { return rename_edit != nullptr; }

	// Board button at strip index p_index, or null. Identity-keyed across rebuilds;
	// preferred over reading HBoxContainer children by position (the actions menu is a
	// sibling that must not participate in board indexing).
	Button *get_board_button(int p_index) const { return _board_button_at(p_index); }

	EditorBoardSwitcher();
};
