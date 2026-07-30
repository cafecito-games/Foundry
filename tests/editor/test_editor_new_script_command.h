/**************************************************************************/
/*  test_editor_new_script_command.h                                      */
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

#include "tests/test_macros.h"

#ifdef TOOLS_ENABLED
#include "core/input/input_event.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#endif

namespace TestEditorNewScriptCommand {

#ifdef TOOLS_ENABLED

TEST_CASE("[Editor][NewScript] default shortcut binding is Cmd/Ctrl+Alt+N") {
	// Palette presence is not asserted: doctest does not boot EditorNode.
	// Binding mirrors EditorNode's ED_SHORTCUT_AND_COMMAND registration.
	ED_SHORTCUT_AND_COMMAND("editor/new_script", TTRC("New Script..."), KeyModifierMask::CMD_OR_CTRL + KeyModifierMask::ALT + Key::N);

	Ref<Shortcut> shortcut = ED_GET_SHORTCUT("editor/new_script");
	REQUIRE(shortcut.is_valid());

	const Array events = shortcut->get_events();
	REQUIRE(events.size() >= 1);

	Ref<InputEventKey> key = events[0];
	REQUIRE(key.is_valid());
	CHECK(key->get_keycode() == Key::N);
	CHECK(key->is_alt_pressed());
	CHECK(key->is_command_or_control_pressed());
}

TEST_CASE("[Editor][NewScript] SCENE_NEW_SCRIPT menu id is distinct from Quick Open Script") {
	CHECK(EditorNode::SCENE_NEW_SCRIPT != EditorNode::SCENE_QUICK_OPEN_SCRIPT);
	CHECK((int)EditorNode::SCENE_NEW_SCRIPT == (int)EditorNode::SCENE_QUICK_OPEN_SCRIPT + 1);
}

#endif // TOOLS_ENABLED

} // namespace TestEditorNewScriptCommand
