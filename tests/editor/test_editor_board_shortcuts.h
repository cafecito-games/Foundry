/**************************************************************************/
/*  test_editor_board_shortcuts.h                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "core/input/input_event.h"
#include "core/input/shortcut.h"
#include "core/os/keyboard.h"
#include "editor/settings/editor_settings.h"

#include "tests/test_macros.h"

namespace TestEditorBoardShortcuts {

// EditorNode registers the real "editor/previous_board" and "editor/next_board"
// shortcuts from deep inside its (very heavy) constructor, and other editor
// components register their own shortcuts from their own constructors. None of
// that is reachable from a doctest without booting a full editor, so this test
// exercises the same public registration entry points (ED_SHORTCUT and
// ED_SHORTCUT_OVERRIDE) that production code calls, under test-only shortcut
// paths that mirror the exact chords involved in the previous_board/next_board
// vs. script_editor/history_previous/history_next macOS collision, and then
// checks the real Shortcut::matches_event() dispatch logic used by
// SceneTree's shortcut_input pass.
//
// A KeyModifierMask::CMD_OR_CTRL modifier resolves at compile time to META on
// macOS builds and CTRL everywhere else (core/os/keyboard.h), so this test is
// meaningful on every platform it is compiled for, not just macOS.

TEST_CASE("[EditorBoardShortcuts] Board switcher chords do not collide with script editor history chords") {
	ERR_FAIL_NULL(EditorSettings::get_singleton());

	Ref<Shortcut> previous_board = ED_SHORTCUT("test/previous_board", "Previous Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEUP);
	Ref<Shortcut> next_board = ED_SHORTCUT("test/next_board", "Next Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEDOWN);

	Ref<Shortcut> history_previous = ED_SHORTCUT("test/history_previous", "History Previous", KeyModifierMask::ALT | Key::LEFT);
	ED_SHORTCUT_OVERRIDE("test/history_previous", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::LEFT);

	Ref<Shortcut> history_next = ED_SHORTCUT("test/history_next", "History Next", KeyModifierMask::ALT | Key::RIGHT);
	ED_SHORTCUT_OVERRIDE("test/history_next", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::RIGHT);

	Ref<InputEventKey> previous_board_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::PAGEUP);
	Ref<InputEventKey> next_board_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::PAGEDOWN);

	CHECK(previous_board->matches_event(previous_board_event));
	CHECK(next_board->matches_event(next_board_event));

	CHECK_FALSE(history_previous->matches_event(previous_board_event));
	CHECK_FALSE(history_next->matches_event(next_board_event));
	CHECK_FALSE(history_previous->matches_event(next_board_event));
	CHECK_FALSE(history_next->matches_event(previous_board_event));

	Ref<InputEventKey> history_previous_event = InputEventKey::create_reference(KeyModifierMask::ALT | Key::LEFT);
	Ref<InputEventKey> history_next_event = InputEventKey::create_reference(KeyModifierMask::ALT | Key::RIGHT);

	CHECK_FALSE(previous_board->matches_event(history_previous_event));
	CHECK_FALSE(next_board->matches_event(history_next_event));
}

TEST_CASE("[EditorBoardShortcuts] Board switcher chords do not collide with the animation editor's bracket shortcuts") {
	ERR_FAIL_NULL(EditorSettings::get_singleton());

	Ref<Shortcut> previous_board = ED_SHORTCUT("test/previous_board_2", "Previous Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEUP);
	Ref<Shortcut> next_board = ED_SHORTCUT("test/next_board_2", "Next Board", KeyModifierMask::CMD_OR_CTRL | Key::PAGEDOWN);

	// editor/animation_editor uses CMD_OR_CTRL + Bracket[Left/Right] to nudge
	// audio track offsets. That chord is textually different from the board
	// switcher's PageUp/PageDown chord, but the collision this test guards
	// against is exactly the case where two textually different chords still
	// resolve to the same InputEventKey once modifiers are expanded, so check
	// the real events instead of the source text.
	Ref<Shortcut> set_start_offset = ED_SHORTCUT("test/set_start_offset", "Set Start Offset", KeyModifierMask::CMD_OR_CTRL | Key::BRACKETLEFT);
	Ref<Shortcut> set_end_offset = ED_SHORTCUT("test/set_end_offset", "Set End Offset", KeyModifierMask::CMD_OR_CTRL | Key::BRACKETRIGHT);

	Ref<InputEventKey> previous_board_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::PAGEUP);
	Ref<InputEventKey> next_board_event = InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::PAGEDOWN);

	CHECK_FALSE(set_start_offset->matches_event(previous_board_event));
	CHECK_FALSE(set_end_offset->matches_event(next_board_event));
	CHECK_FALSE(previous_board->matches_event(InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::BRACKETLEFT)));
	CHECK_FALSE(next_board->matches_event(InputEventKey::create_reference(KeyModifierMask::CMD_OR_CTRL | Key::BRACKETRIGHT)));
}

} // namespace TestEditorBoardShortcuts
