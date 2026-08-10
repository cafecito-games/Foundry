/**************************************************************************/
/*  editor_board_switcher.cpp                                             */
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

#include "editor_board_switcher.h"

#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/gui/editor_board_actions_menu.h"

#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/scene_string_names.h"

// Slightly under a typical OS double-click window so a second press can cancel the
// pending menu open and claim the gesture for inline rename before the popup appears
// (a FLAG_POPUP menu would otherwise swallow the second press).
static constexpr double BOARD_ACTIONS_MENU_OPEN_DELAY_SEC = 0.2;

void EditorBoardSwitcher::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (add_button) {
				add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			}
			if (overview_button) {
				overview_button->set_button_icon(get_editor_theme_icon(SNAME("GridLayout")));
			}
			if (strip) {
				const int active = strip->get_active_index();
				if (Button *active_button = _board_button_at(active)) {
					active_button->set_button_icon(get_editor_theme_icon(SNAME("GuiDropdown")));
					active_button->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
				}
			}
		} break;
	}
}

void EditorBoardSwitcher::_bind_methods() {
}

void EditorBoardSwitcher::setup(EditorBoardStrip *p_strip) {
	if (strip == p_strip) {
		return;
	}
	if (strip) {
		strip->disconnect(SNAME("board_added"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		strip->disconnect(SNAME("board_removed"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		strip->disconnect(SNAME("board_moved"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(2));
		strip->disconnect(SNAME("boards_restored"), callable_mp(this, &EditorBoardSwitcher::_rebuild));
		strip->disconnect(SNAME("active_board_changed"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		strip->disconnect(SNAME("overview_changed"), callable_mp(this, &EditorBoardSwitcher::_on_overview_changed));
	}

	strip = p_strip;
	if (actions_menu) {
		actions_menu->set_strip(strip);
	}

	if (strip) {
		// The switcher rebuilds wholesale rather than patching individual buttons: with a
		// handful of boards this is cheaper than incremental updates and cannot drift from
		// the strip, since there is nothing incremental to keep in sync. A restore replaces
		// every EditorBoard instance without emitting board_added (boards_restored already
		// covers "every board changed at once"), so the switcher listens for both. Entries
		// are selected by position, so a reorder (board_moved) must rebuild too, or the
		// switcher's stale order silently activates the wrong board.
		strip->connect(SNAME("board_added"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		strip->connect(SNAME("board_removed"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		strip->connect(SNAME("board_moved"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(2));
		strip->connect(SNAME("boards_restored"), callable_mp(this, &EditorBoardSwitcher::_rebuild));
		strip->connect(SNAME("active_board_changed"), callable_mp(this, &EditorBoardSwitcher::_rebuild).unbind(1));
		// The overview toggle is the only piece of chrome the mode affects, so it is patched
		// in place rather than triggering a full rebuild the way structural changes do.
		strip->connect(SNAME("overview_changed"), callable_mp(this, &EditorBoardSwitcher::_on_overview_changed));
	}

	_rebuild();
}

void EditorBoardSwitcher::_rebuild() {
	// A pending rename must be committed, not discarded: clicking another board button
	// does not move keyboard focus off the LineEdit (the board buttons use
	// FOCUS_ACCESSIBILITY, so focus_exited never fires outside a screen reader), and
	// board_removed/boards_restored can just as easily land mid-rename. Committing here
	// covers every _rebuild() trigger uniformly instead of relying on focus behavior.
	if (rename_edit) {
		_apply_pending_rename(rename_edit->get_text());
	}
	_cancel_pending_menu();

	// Free chrome children but keep the owned actions menu across rebuilds. The menu
	// captures its target by ObjectID, so surviving a board_removed that frees that board
	// is what lets an activation after the close resolve to -1 and no-op.
	// renaming_button is cleared here: the button is about to be queue_freed, and leaving
	// the pointer set would let a later _cancel_rename() call show() on freed memory.
	Vector<Node *> to_free;
	for (int i = 0; i < get_child_count(); i++) {
		Node *child = get_child(i);
		if (child == actions_menu) {
			continue;
		}
		to_free.push_back(child);
	}
	for (Node *child : to_free) {
		remove_child(child);
		child->queue_free();
	}
	add_button = nullptr;
	overview_button = nullptr;
	renaming_button = nullptr;
	board_buttons.clear();

	if (!strip) {
		return;
	}

	for (int i = 0; i < strip->get_board_count(); i++) {
		EditorBoard *board = strip->get_board(i);
		if (!board) {
			board_buttons.push_back(nullptr);
			continue;
		}

		Button *button = memnew(Button);
		button->set_toggle_mode(true);
		button->set_focus_mode(FOCUS_ACCESSIBILITY);
		button->set_text(board->get_title());
		button->set_tooltip_text(board->get_title());
		button->set_accessibility_name(board->get_title());
		button->set_pressed_no_signal(i == strip->get_active_index());
		if (i == strip->get_active_index()) {
			// Trailing caret marks the active board as the menu trigger. Pressing it is
			// otherwise inert today, so treating the whole button as the menu open costs
			// no prior behavior and avoids hit-testing the icon half by hand.
			button->set_button_icon(get_editor_theme_icon(SNAME("GuiDropdown")));
			button->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
		}
		button->connect(SceneStringName(pressed), callable_mp(this, &EditorBoardSwitcher::_on_board_button_pressed).bind(i));
		button->connect(SceneStringName(gui_input), callable_mp(this, &EditorBoardSwitcher::_on_board_button_gui_input).bind(i));
		add_child(button);
		board_buttons.push_back(button);
	}

	add_button = memnew(Button);
	add_button->set_flat(true);
	add_button->set_text("+");
	add_button->set_tooltip_text(TTR("Add Board"));
	add_button->set_accessibility_name(TTRC("Add Board"));
	add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &EditorBoardSwitcher::_on_add_pressed));
	add_child(add_button);

	overview_button = memnew(Button);
	overview_button->set_flat(true);
	overview_button->set_toggle_mode(true);
	overview_button->set_tooltip_text(TTR("Board Overview"));
	overview_button->set_accessibility_name(TTRC("Board Overview"));
	overview_button->set_button_icon(get_editor_theme_icon(SNAME("GridLayout")));
	overview_button->set_pressed_no_signal(strip->is_overview_active());
	overview_button->connect(SceneStringName(toggled), callable_mp(this, &EditorBoardSwitcher::_on_overview_toggled));
	add_child(overview_button);
}

Button *EditorBoardSwitcher::_board_button_at(int p_index) const {
	if (p_index < 0 || p_index >= board_buttons.size()) {
		return nullptr;
	}
	return board_buttons[p_index];
}

void EditorBoardSwitcher::_on_overview_toggled(bool p_pressed) {
	if (!strip) {
		return;
	}
	strip->set_overview(p_pressed);
}

void EditorBoardSwitcher::_on_overview_changed(bool p_active) {
	if (overview_button) {
		overview_button->set_pressed_no_signal(p_active);
	}
}

void EditorBoardSwitcher::_popup_actions_menu(int p_index, const Point2 &p_screen_position) {
	if (!actions_menu) {
		return;
	}
	actions_menu->popup_for_board(p_index, p_screen_position);
}

void EditorBoardSwitcher::popup_board_actions(int p_board_index, const Point2 &p_screen_position) {
	_cancel_pending_menu();
	_popup_actions_menu(p_board_index, p_screen_position);
}

void EditorBoardSwitcher::_cancel_pending_menu() {
	pending_menu_board_id = ObjectID();
	if (pending_menu_timer.is_valid()) {
		// SceneTree keeps its own Ref; unref alone leaves the timeout armed. Drop the
		// connection so a cancelled delay cannot open the menu for a later press.
		pending_menu_timer->release_connections();
		pending_menu_timer.unref();
	}
}

void EditorBoardSwitcher::_open_pending_actions_menu() {
	pending_menu_timer.unref();
	if (!pending_menu_board_id.is_valid() || !strip || is_renaming()) {
		pending_menu_board_id = ObjectID();
		return;
	}

	const int index = strip->resolve_board_index(pending_menu_board_id);
	pending_menu_board_id = ObjectID();
	if (index < 0) {
		return;
	}

	if (Button *button = _board_button_at(index)) {
		button->set_pressed_no_signal(true);
		const Rect2 screen_rect = button->get_screen_rect();
		_popup_actions_menu(index, Point2(screen_rect.position.x, screen_rect.position.y + screen_rect.size.y));
	} else {
		_popup_actions_menu(index, get_screen_transform().xform(get_local_mouse_position()));
	}
}

void EditorBoardSwitcher::_on_board_button_pressed(int p_index) {
	if (!strip) {
		return;
	}
	if (p_index < 0 || p_index >= strip->get_board_count()) {
		return;
	}

	if (p_index == strip->get_active_index()) {
		// Toggle mode flips the button off on press; restore the active visual before
		// opening the menu so the button never reads as deselected.
		if (Button *button = _board_button_at(p_index)) {
			button->set_pressed_no_signal(true);
		}
		// A double-click starts an inline rename from gui_input, which runs before the
		// button's own release handling; that same release must not also open the menu.
		if (is_renaming()) {
			return;
		}

		EditorBoard *board = strip->get_board(p_index);
		if (!board || !get_tree()) {
			return;
		}
		// Delay the popup so a follow-up double-click can cancel it and rename instead.
		// Opening immediately would raise a FLAG_POPUP menu that swallows the second press.
		pending_menu_board_id = board->get_instance_id();
		pending_menu_timer = get_tree()->create_timer(BOARD_ACTIONS_MENU_OPEN_DELAY_SEC);
		pending_menu_timer->connect(SNAME("timeout"), callable_mp(this, &EditorBoardSwitcher::_open_pending_actions_menu), Object::CONNECT_ONE_SHOT);
		return;
	}

	_cancel_pending_menu();
	strip->set_active_board(p_index);
}

void EditorBoardSwitcher::_on_board_button_gui_input(const Ref<InputEvent> &p_event, int p_index) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || !mb->is_pressed()) {
		return;
	}

	if (mb->get_button_index() == MouseButton::RIGHT) {
		_cancel_pending_menu();
		// InputEventMouse::global_position is viewport-relative; Popup::popup expects screen
		// coordinates for non-embedded editor subwindows.
		_popup_actions_menu(p_index, get_screen_transform().xform(get_local_mouse_position()));
		return;
	}

	if (mb->get_button_index() == MouseButton::LEFT && mb->is_double_click()) {
		_cancel_pending_menu();
		_begin_rename(p_index);
	}
}

void EditorBoardSwitcher::_on_add_pressed() {
	if (!strip) {
		return;
	}
	EditorBoard *board = strip->add_board();
	if (board) {
		strip->set_active_board(strip->get_board_index(board));
	}
}

void EditorBoardSwitcher::_begin_rename(int p_index) {
	if (!strip) {
		return;
	}
	EditorBoard *board = strip->get_board(p_index);
	if (!board) {
		return;
	}

	_cancel_rename();

	Button *button = _board_button_at(p_index);
	if (!button) {
		return;
	}

	renaming_board_id = board->get_instance_id();
	renaming_button = button;
	button->hide();

	rename_edit = memnew(LineEdit);
	rename_edit->set_text(board->get_title());
	rename_edit->set_custom_minimum_size(button->get_size());
	rename_edit->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	add_child(rename_edit);
	// Place the editor where the (hidden) board button sits, without assuming board
	// buttons occupy child indices 0..n-1 relative to the actions menu sibling.
	move_child(rename_edit, button->get_index());

	rename_edit->connect(SceneStringName(text_submitted), callable_mp(this, &EditorBoardSwitcher::_commit_rename));
	rename_edit->connect(SceneStringName(focus_exited), callable_mp(this, &EditorBoardSwitcher::_commit_rename_from_focus_loss));

	rename_edit->grab_focus();
	rename_edit->select_all();
}

void EditorBoardSwitcher::_apply_pending_rename(const String &p_text) {
	if (!rename_edit) {
		return;
	}

	const ObjectID board_id = renaming_board_id;
	const String new_title = p_text.strip_edges();
	_cancel_rename();

	if (new_title.is_empty() || !strip) {
		return;
	}
	EditorBoard *board = ObjectDB::get_instance<EditorBoard>(board_id);
	if (!board) {
		return;
	}
	board->set_title(new_title);
	// The switcher's own button relabels on the _rebuild() that follows a text_submitted
	// signal, but the overview's captions are a separate snapshot the rename otherwise has
	// no way to reach: the switcher's rename field stays usable while the overview is up,
	// since renaming is exactly how a caption a user is looking at gets its name fixed.
	strip->refresh_overview_captions();
}

void EditorBoardSwitcher::_commit_rename(const String &p_text) {
	if (!rename_edit) {
		return;
	}
	_apply_pending_rename(p_text);
	_rebuild();
}

void EditorBoardSwitcher::_commit_rename_from_focus_loss() {
	if (!rename_edit) {
		return;
	}
	_commit_rename(rename_edit->get_text());
}

void EditorBoardSwitcher::_cancel_rename() {
	if (rename_edit) {
		rename_edit->queue_free();
		rename_edit = nullptr;
	}
	if (renaming_button) {
		renaming_button->show();
		renaming_button = nullptr;
	}
	renaming_board_id = ObjectID();
}

EditorBoardSwitcher::EditorBoardSwitcher() {
	set_mouse_filter(Control::MOUSE_FILTER_STOP);

	actions_menu = memnew(EditorBoardActionsMenu);
	add_child(actions_menu);
	actions_menu->connect(SNAME("rename_requested"), callable_mp(this, &EditorBoardSwitcher::_begin_rename));
}
