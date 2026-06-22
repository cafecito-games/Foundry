/**************************************************************************/
/*  gdscript_position.cpp                                                 */
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

#include "gdscript_position.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#endif

int GDScriptTextPosition::get_indent_size() {
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		return EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif
	return 4;
}

int GDScriptTextPosition::godot_column_to_text_column(const String &p_line, int p_column) {
	if (p_column <= 1) {
		return 0;
	}

	const int tab_size = get_indent_size();
	int text_column = 0;
	int godot_column = 1;
	while (text_column < p_line.length() && godot_column < p_column) {
		if (p_line[text_column] == '\t') {
			godot_column += tab_size;
		} else {
			godot_column++;
		}
		text_column++;
	}
	return text_column;
}

int GDScriptTextPosition::text_column_to_godot_column(const String &p_line, int p_text_column) {
	int godot_column = p_text_column + 1;
	const int tab_size = get_indent_size();
	for (int i = 0; i < p_text_column && i < p_line.length(); i++) {
		if (p_line[i] == '\t') {
			godot_column += tab_size - 1;
		}
	}
	return godot_column;
}
