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

#include "scene/gui/box_container.h"

class Button;
class EditorBoardActionsMenu;
class EditorBoardStrip;
class LineEdit;

/**
 * Title-bar chrome for switching between boards: one button per board, an "add
 * board" button, and a toggle for the zoomed-out board overview.
 *
 * The switcher holds no board list of its own. It mirrors EditorBoardStrip by
 * rebuilding its buttons from scratch on every structural signal the strip
 * emits, which is cheaper and far less bug-prone than incrementally patching
 * button state for a handful of boards.
 *
 * The active board button carries a dropdown caret and opens a board actions
 * menu (rename / close / reorder). Inactive buttons still switch on left-click;
 * any board button opens the same menu on right-click.
 */
class EditorBoardSwitcher : public HBoxContainer {
	FOUNDRY_CLASS(EditorBoardSwitcher, HBoxContainer);

	EditorBoardStrip *strip = nullptr;
	EditorBoardActionsMenu *actions_menu = nullptr;
	Button *add_button = nullptr;
	Button *overview_button = nullptr;

	// Identifies the board button currently swapped for a LineEdit. Stored as an
	// instance id rather than a cached board pointer, so a board closing mid-rename is
	// simply "the id no longer resolves" rather than a dangling pointer.
	ObjectID renaming_board_id;
	Button *renaming_button = nullptr;
	LineEdit *rename_edit = nullptr;
	// Set when a double-click begins a rename so the subsequent pressed signal on the
	// active board does not also open the actions menu.
	bool skip_actions_menu = false;

	void _rebuild();
	void _on_board_button_pressed(int p_index);
	void _on_board_button_gui_input(const Ref<InputEvent> &p_event, int p_index);
	void _on_add_pressed();
	void _on_overview_toggled(bool p_pressed);
	// Keeps the toggle in step with the strip when the overview is entered or left from
	// somewhere else -- a caption click, a drop onto another board, a board shortcut.
	void _on_overview_changed(bool p_active);
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

	// Opens the board actions menu for p_board_index at p_screen_position. Used by the
	// strip's caption context-menu handler and by the switcher's own button interactions.
	void popup_board_actions(int p_board_index, const Point2 &p_screen_position);

	// True while an inline rename LineEdit is showing. Exposed for tests, which
	// cannot observe the swapped-in LineEdit through the strip.
	bool is_renaming() const { return rename_edit != nullptr; }

	EditorBoardSwitcher();
};
