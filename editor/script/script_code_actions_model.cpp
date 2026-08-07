/**************************************************************************/
/*  script_code_actions_model.cpp                                         */
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

#include "editor/script/script_code_actions_model.h"

#ifdef TOOLS_ENABLED

Vector<ScriptCodeActionEntry> build_refactor_menu_entries(const Vector<RefactorAvailability> &p_available, int p_refactor_id_base) {
	Vector<ScriptCodeActionEntry> entries;
	entries.resize(p_available.size());
	ScriptCodeActionEntry *entries_write = entries.ptrw();
	for (int i = 0; i < p_available.size(); i++) {
		const RefactorAvailability &availability = p_available[i];
		ScriptCodeActionEntry &entry = entries_write[i];
		entry.id = p_refactor_id_base + (int)availability.kind;
		entry.title = availability.title;
		entry.enabled = availability.enabled;
		if (!availability.enabled) {
			entry.tooltip = availability.disabled_reason;
		}
	}
	return entries;
}

Vector<ScriptCodeActionEntry> build_code_action_menu_entries(
		const Vector<RefactorAvailability> &p_available,
		int p_refactor_id_base,
		bool p_is_foundry_script,
		int p_format_document_id,
		const String &p_format_document_shortcut_name,
		const String &p_format_document_title) {
	if (!p_is_foundry_script) {
		return Vector<ScriptCodeActionEntry>();
	}

	Vector<ScriptCodeActionEntry> entries = build_refactor_menu_entries(p_available, p_refactor_id_base);

	if (!entries.is_empty()) {
		ScriptCodeActionEntry separator;
		separator.is_separator = true;
		entries.push_back(separator);
	}

	ScriptCodeActionEntry format_entry;
	format_entry.id = p_format_document_id;
	format_entry.title = p_format_document_title;
	format_entry.shortcut_name = p_format_document_shortcut_name;
	entries.push_back(format_entry);

	return entries;
}

#endif // TOOLS_ENABLED
