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
#include "editor/themes/editor_scale.h"

#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/container.h"
#include "scene/gui/line_edit.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box.h"
#include "scene/scene_string_names.h"

void EditorBoardSwitcher::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			resize_parent = Object::cast_to<Control>(get_parent());
			const Callable queue_update = callable_mp(this, &EditorBoardSwitcher::_queue_compact_mode_update);
			if (resize_parent && !resize_parent->is_connected(SceneStringName(resized), queue_update)) {
				resize_parent->connect(SceneStringName(resized), queue_update);
			}
			if (Container *container = Object::cast_to<Container>(resize_parent)) {
				if (!container->is_connected(SceneStringName(sort_children), queue_update)) {
					container->connect(SceneStringName(sort_children), queue_update);
				}
			}
			_queue_compact_mode_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			const Callable queue_update = callable_mp(this, &EditorBoardSwitcher::_queue_compact_mode_update);
			if (resize_parent && resize_parent->is_connected(SceneStringName(resized), queue_update)) {
				resize_parent->disconnect(SceneStringName(resized), queue_update);
			}
			if (Container *container = Object::cast_to<Container>(resize_parent)) {
				if (container->is_connected(SceneStringName(sort_children), queue_update)) {
					container->disconnect(SceneStringName(sort_children), queue_update);
				}
			}
			resize_parent = nullptr;
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			if (menu_button) {
				menu_button->set_button_icon(get_editor_theme_icon(SNAME("GuiTabMenuHl")));
			}
			if (strip) {
				_rebuild();
			}
		} break;
	}
}

void EditorBoardSwitcher::_bind_methods() {
	ADD_SIGNAL(MethodInfo("board_requested", PropertyInfo(Variant::INT, "board_index")));
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
	// Free rail children but keep the rail and owned actions menu across rebuilds. The menu
	// captures its target by ObjectID, so surviving a board_removed that frees that board
	// is what lets an activation after the close resolve to -1 and no-op.
	// renaming_button is cleared here: the button is about to be queue_freed, and leaving
	// the pointer set would let a later _cancel_rename() call show() on freed memory.
	Vector<Node *> to_free;
	for (int i = 0; i < rail_hbox->get_child_count(); i++) {
		to_free.push_back(rail_hbox->get_child(i));
	}
	for (Node *child : to_free) {
		rail_hbox->remove_child(child);
		child->queue_free();
	}
	menu_button = nullptr;
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
		button->set_theme_type_variation("BoardRailButton");
		button->set_text(board->get_title());
		button->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		button->set_clip_text(true);
		button->set_tooltip_text(board->get_title());
		button->set_accessibility_name(board->get_title());
		button->set_pressed_no_signal(i == strip->get_active_index());
		button->connect(SceneStringName(pressed), callable_mp(this, &EditorBoardSwitcher::_on_board_button_pressed).bind(i));
		button->connect(SceneStringName(gui_input), callable_mp(this, &EditorBoardSwitcher::_on_board_button_gui_input).bind(i));
		rail_hbox->add_child(button);
		const Ref<Font> segment_font = button->get_theme_font(SceneStringName(font));
		const int font_size = button->get_theme_font_size(SceneStringName(font_size));
		const int horizontal_padding = get_theme_constant("segment_horizontal_padding");
		const int maximum_width = get_theme_constant("segment_maximum_width");
		const int text_width = Math::ceil(segment_font->get_string_size(board->get_title(), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x);
		button->set_custom_minimum_size(Size2(MIN(text_width + horizontal_padding, maximum_width), 0));
		button->set_visible(!compact || i == strip->get_active_index());
		board_buttons.push_back(button);
	}

	menu_button = memnew(Button);
	menu_button->set_flat(true);
	menu_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	menu_button->set_theme_type_variation("BoardRailMenuButton");
	menu_button->set_accessibility_name(TTRC("Board Menu"));
	menu_button->set_tooltip_text(TTR("Board Menu"));
	menu_button->set_button_icon(get_editor_theme_icon(SNAME("GuiTabMenuHl")));
	menu_button->connect(SceneStringName(pressed), callable_mp(this, &EditorBoardSwitcher::_on_menu_pressed));
	rail_hbox->add_child(menu_button);

	_queue_compact_mode_update();
}

int EditorBoardSwitcher::_get_desired_full_width() const {
	if (!rail_hbox || !menu_button) {
		return 0;
	}

	int desired_width = menu_button->get_combined_minimum_size().x;
	for (Button *button : board_buttons) {
		if (button) {
			desired_width += button->get_combined_minimum_size().x;
		}
	}
	desired_width += rail_hbox->get_theme_constant(SNAME("separation")) * MAX(0, board_buttons.size());
	desired_width += get_theme_stylebox(SceneStringName(panel))->get_minimum_size().x;
	return desired_width;
}

void EditorBoardSwitcher::_set_compact(bool p_compact) {
	if (compact == p_compact) {
		return;
	}
	if (rename_edit) {
		_apply_pending_rename(rename_edit->get_text());
	}
	compact = p_compact;
	for (int i = 0; i < board_buttons.size(); i++) {
		if (board_buttons[i]) {
			board_buttons[i]->set_visible(!compact || (strip && i == strip->get_active_index()));
		}
	}
	update_minimum_size();
}

void EditorBoardSwitcher::_queue_compact_mode_update() {
	if (compact_update_queued) {
		return;
	}
	compact_update_queued = true;
	callable_mp(this, &EditorBoardSwitcher::_update_compact_mode).call_deferred();
}

void EditorBoardSwitcher::_update_compact_mode() {
	compact_update_queued = false;
	if (!is_inside_tree()) {
		return;
	}
	Control *parent_control = resize_parent ? resize_parent : Object::cast_to<Control>(get_parent());
	if (!parent_control || parent_control != get_parent()) {
		return;
	}

	int before_width = 0;
	int after_width = 0;
	bool found_switcher = false;
	for (int i = 0; i < parent_control->get_child_count(); i++) {
		Control *sibling = Object::cast_to<Control>(parent_control->get_child(i));
		if (!sibling || !sibling->is_visible()) {
			continue;
		}
		if (sibling == this) {
			found_switcher = true;
			continue;
		}
		if (found_switcher) {
			after_width += sibling->get_combined_minimum_size().x;
		} else {
			before_width += sibling->get_combined_minimum_size().x;
		}
	}

	const int center_budget = MAX(0, int(parent_control->get_size().x) - 2 * MAX(before_width, after_width));
	_set_compact(_get_desired_full_width() > center_budget);
}

Button *EditorBoardSwitcher::_board_button_at(int p_index) const {
	if (p_index < 0 || p_index >= board_buttons.size()) {
		return nullptr;
	}
	return board_buttons[p_index];
}

void EditorBoardSwitcher::_popup_actions_menu(int p_index, const Point2 &p_screen_position) {
	if (!actions_menu) {
		return;
	}
	actions_menu->popup_for_board(p_index, p_screen_position);
}

void EditorBoardSwitcher::popup_board_actions(int p_board_index, const Point2 &p_screen_position) {
	_popup_actions_menu(p_board_index, p_screen_position);
}

void EditorBoardSwitcher::popup_active_board_menu(const Point2 &p_screen_position, bool p_include_board_list) {
	if (!actions_menu || !strip) {
		return;
	}
	actions_menu->popup_for_board(strip->get_active_index(), p_screen_position, p_include_board_list);
}

void EditorBoardSwitcher::_on_menu_pressed() {
	if (!menu_button) {
		return;
	}
	const Rect2 screen_rect = menu_button->get_screen_rect();
	popup_active_board_menu(Point2(screen_rect.position.x, screen_rect.position.y + screen_rect.size.y), compact);
}

void EditorBoardSwitcher::_on_board_button_pressed(int p_index) {
	if (!strip) {
		return;
	}
	if (rename_edit) {
		_apply_pending_rename(rename_edit->get_text());
		_rebuild();
	}
	if (p_index < 0 || p_index >= strip->get_board_count()) {
		return;
	}

	if (strip->is_overview_active()) {
		const bool requested_active_board = p_index == strip->get_active_index();
		strip->set_active_board(p_index);
		if (requested_active_board) {
			if (Button *button = _board_button_at(p_index)) {
				button->set_pressed_no_signal(true);
			}
		}
	} else if (p_index == strip->get_active_index()) {
		// Toggle mode flips the button off on press; the active segment remains selected.
		if (Button *button = _board_button_at(p_index)) {
			button->set_pressed_no_signal(true);
		}
	} else {
		strip->set_active_board(p_index);
	}
	emit_signal(SNAME("board_requested"), p_index);
}

void EditorBoardSwitcher::_on_board_button_gui_input(const Ref<InputEvent> &p_event, int p_index) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || !mb->is_pressed()) {
		return;
	}

	if (mb->get_button_index() == MouseButton::RIGHT) {
		// InputEventMouse::global_position is viewport-relative; Popup::popup expects screen
		// coordinates for non-embedded editor subwindows.
		_popup_actions_menu(p_index, get_screen_transform().xform(get_local_mouse_position()));
		return;
	}

	if (mb->get_button_index() == MouseButton::LEFT && mb->is_double_click()) {
		_begin_rename(p_index);
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
	const Size2 segment_minimum = button->get_combined_minimum_size();
	rename_edit->set_custom_minimum_size(Size2(segment_minimum.x, MAX(segment_minimum.y, button->get_size().y)));
	rename_edit->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	rail_hbox->add_child(rename_edit);
	// Place the editor where the (hidden) board button sits, without assuming board
	// buttons occupy child indices 0..n-1 relative to the actions menu sibling.
	rail_hbox->move_child(rename_edit, button->get_index());

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
	set_theme_type_variation("BoardRail");

	rail_hbox = memnew(HBoxContainer);
	rail_hbox->add_theme_constant_override(SNAME("separation"), MAX(1, Math::round(EDSCALE)));
	add_child(rail_hbox);

	actions_menu = memnew(EditorBoardActionsMenu);
	add_child(actions_menu);
	actions_menu->connect(SNAME("rename_requested"), callable_mp(this, &EditorBoardSwitcher::_begin_rename));
}
