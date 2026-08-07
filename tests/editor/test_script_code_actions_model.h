/**************************************************************************/
/*  test_script_code_actions_model.h                                      */
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

#ifdef TOOLS_ENABLED

#include "core/input/input_event.h"
#include "editor/script/script_code_actions_model.h"
#include "editor/script/script_text_editor.h"
#include "editor/settings/editor_settings.h"
#include "tests/test_macros.h"

namespace TestScriptCodeActionsModel {

// Mirrors the ScriptTextEditor option ids the menus dispatch through. The exact
// values do not matter; the mapping `base + (int)kind` does.
constexpr int REFACTOR_ID_BASE = 100;
constexpr int FORMAT_DOCUMENT_ID = 42;

Vector<RefactorAvailability> sample_availabilities() {
	Vector<RefactorAvailability> available;

	RefactorAvailability rename;
	rename.kind = RefactorKind::RENAME;
	rename.title = "Rename Symbol";
	rename.enabled = true;
	available.push_back(rename);

	RefactorAvailability extract;
	extract.kind = RefactorKind::EXTRACT_METHOD;
	extract.title = "Extract Method";
	extract.enabled = false;
	extract.disabled_reason = "Select statements to extract.";
	available.push_back(extract);

	return available;
}

TEST_CASE("[Editor][ScriptCodeActions] Refactor entries keep the RefactorKind id mapping") {
	const Vector<ScriptCodeActionEntry> entries = build_refactor_menu_entries(sample_availabilities(), REFACTOR_ID_BASE);

	REQUIRE_EQ(entries.size(), 2);

	CHECK_FALSE(entries[0].is_separator);
	CHECK_EQ(entries[0].id, REFACTOR_ID_BASE + (int)RefactorKind::RENAME);
	CHECK_EQ(entries[0].title, "Rename Symbol");
	CHECK(entries[0].enabled);
	CHECK(entries[0].tooltip.is_empty());

	CHECK_EQ(entries[1].id, REFACTOR_ID_BASE + (int)RefactorKind::EXTRACT_METHOD);
	CHECK_FALSE(entries[1].enabled);
	CHECK_EQ(entries[1].tooltip, "Select statements to extract.");
}

TEST_CASE("[Editor][ScriptCodeActions] Popup lists refactors and Format Document") {
	const Vector<ScriptCodeActionEntry> entries = build_code_action_menu_entries(
			sample_availabilities(),
			REFACTOR_ID_BASE,
			true,
			FORMAT_DOCUMENT_ID,
			"script_text_editor/format_document",
			"Format Document");

	REQUIRE_EQ(entries.size(), 4);

	CHECK_EQ(entries[0].id, REFACTOR_ID_BASE + (int)RefactorKind::RENAME);
	CHECK_EQ(entries[1].id, REFACTOR_ID_BASE + (int)RefactorKind::EXTRACT_METHOD);
	CHECK_FALSE(entries[1].enabled);
	CHECK_EQ(entries[1].tooltip, "Select statements to extract.");

	CHECK(entries[2].is_separator);

	CHECK_FALSE(entries[3].is_separator);
	CHECK_EQ(entries[3].id, FORMAT_DOCUMENT_ID);
	CHECK_EQ(entries[3].title, "Format Document");
	CHECK_EQ(entries[3].shortcut_name, "script_text_editor/format_document");
	CHECK(entries[3].enabled);
}

TEST_CASE("[Editor][ScriptCodeActions] Popup offers Format Document without any refactor") {
	const Vector<ScriptCodeActionEntry> entries = build_code_action_menu_entries(
			Vector<RefactorAvailability>(),
			REFACTOR_ID_BASE,
			true,
			FORMAT_DOCUMENT_ID,
			"script_text_editor/format_document",
			"Format Document");

	// No leading separator when there is nothing above it.
	REQUIRE_EQ(entries.size(), 1);
	CHECK_FALSE(entries[0].is_separator);
	CHECK_EQ(entries[0].id, FORMAT_DOCUMENT_ID);
}

TEST_CASE("[Editor][ScriptCodeActions] Popup is empty for non-Foundry-Script buffers") {
	const Vector<ScriptCodeActionEntry> entries = build_code_action_menu_entries(
			sample_availabilities(),
			REFACTOR_ID_BASE,
			false,
			FORMAT_DOCUMENT_ID,
			"script_text_editor/format_document",
			"Format Document");

	CHECK(entries.is_empty());
}

Ref<InputEventKey> enter_event(bool p_alt) {
	Ref<InputEventKey> key;
	key.instantiate();
	key->set_keycode(Key::ENTER);
	key->set_alt_pressed(p_alt);
	key->set_pressed(true);
	return key;
}

TEST_CASE("[Editor][ScriptCodeActions] Code Actions binds Alt+Enter without claiming plain Enter") {
	// The gui-input handler matches with ED_IS_SHORTCUT, so a plain Enter must not
	// match: it still has to reach the inline-rename commit path.
	ScriptTextEditor::register_editor();

	const Ref<Shortcut> shortcut = ED_GET_SHORTCUT("script_text_editor/show_code_actions");
	REQUIRE(shortcut.is_valid());

	CHECK(shortcut->matches_event(enter_event(true)));
	CHECK_FALSE(shortcut->matches_event(enter_event(false)));

	// The pre-existing Format Document binding must be untouched by the new shortcut.
	const Ref<Shortcut> format_shortcut = ED_GET_SHORTCUT("script_text_editor/format_document");
	REQUIRE(format_shortcut.is_valid());
	const Array format_events = format_shortcut->get_events();
	REQUIRE(format_events.size() >= 1);
	const Ref<InputEventKey> format_key = format_events[0];
	REQUIRE(format_key.is_valid());
	CHECK_EQ(format_key->get_keycode(), Key::L);
	CHECK(format_key->is_alt_pressed());
	CHECK(format_key->is_command_or_control_pressed());
}

} // namespace TestScriptCodeActionsModel

#endif // TOOLS_ENABLED
