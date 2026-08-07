/**************************************************************************/
/*  script_code_actions_model.h                                           */
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

#include "modules/foundry_script/editor/fs_refactoring.h"

// One row of a code-action menu surface. The context menu's Refactor submenu and
// the caret-anchored code-actions popup are both built from these entries so the
// two surfaces cannot drift apart.
struct ScriptCodeActionEntry {
	bool is_separator = false;
	int id = -1;
	// Localized label, used when `shortcut_name` is empty.
	String title;
	// Editor shortcut path (`ED_GET_SHORTCUT`) when the entry should render its
	// accelerator; empty for plain items.
	String shortcut_name;
	bool enabled = true;
	// Shown as the item tooltip when the entry is disabled.
	String tooltip;
};

// Maps refactor availabilities onto menu entries. `p_refactor_id_base` is the
// menu option id that corresponds to `RefactorKind(0)`; each entry takes the id
// `p_refactor_id_base + (int)kind`, matching the dispatch in `_run_refactor`.
Vector<ScriptCodeActionEntry> build_refactor_menu_entries(const Vector<RefactorAvailability> &p_available, int p_refactor_id_base);

// Entries for the caret-anchored code-actions popup: the refactor entries plus a
// separator and Format Document. Returns an empty list for buffers that are not
// Foundry Script, since both refactoring and formatting are Foundry-Script-only.
Vector<ScriptCodeActionEntry> build_code_action_menu_entries(
		const Vector<RefactorAvailability> &p_available,
		int p_refactor_id_base,
		bool p_is_foundry_script,
		int p_format_document_id,
		const String &p_format_document_shortcut_name,
		const String &p_format_document_title);

#endif // TOOLS_ENABLED
