/**************************************************************************/
/*  editor_automation_commands.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/input/shortcut.h"
#include "core/variant/variant.h"

// Shared command discovery and execution for editor automation clients.
//
// Backed by EditorCommandPalette and EditorSettings shortcut registries.
class EditorAutomationCommands {
public:
	static Dictionary list_commands(const Dictionary &p_args = Dictionary());
	static Dictionary execute(const String &p_command);
	static Array suggest_commands(const String &p_query, int p_limit = 10);

private:
	static String _category_from_key(const String &p_key);
	static Dictionary _entry_from_palette(const String &p_key, const String &p_display_name, const String &p_shortcut_text, const Ref<Shortcut> &p_shortcut);
	static Dictionary _entry_from_shortcut(const String &p_key, const Ref<Shortcut> &p_shortcut);
	static float _score_match(const String &p_query, const String &p_key, const String &p_label);
	static bool _matches_filter(const Dictionary &p_entry, const String &p_query, const String &p_category, bool p_runnable_only);
};
