/**************************************************************************/
/*  gdscript_refactoring_edits.cpp                                        */
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

#include "gdscript_refactoring_edits.h"

#ifdef TOOLS_ENABLED

namespace {
// Maps a 0-based (line, column) to an absolute character offset in p_source.
// Returns -1 if the position is out of range.
int to_offset(const String &p_source, int p_line, int p_column) {
	int line = 0;
	int offset = 0;
	const int len = p_source.length();
	while (line < p_line) {
		if (offset >= len) {
			return -1;
		}
		if (p_source[offset] == '\n') {
			line++;
		}
		offset++;
	}
	int column = 0;
	while (column < p_column) {
		if (offset >= len || p_source[offset] == '\n') {
			return -1;
		}
		offset++;
		column++;
	}
	return offset;
}

struct ResolvedEdit {
	int start_offset = 0;
	int end_offset = 0;
	String new_text;
};

struct ResolvedEditComparator {
	bool operator()(const ResolvedEdit &a, const ResolvedEdit &b) const {
		return a.start_offset < b.start_offset;
	}
};
} // namespace

bool GDScriptRefactorEdits::apply(const String &p_source, const Vector<RefactorTextEdit> &p_edits, String &r_result) {
	Vector<ResolvedEdit> resolved;
	for (const RefactorTextEdit &e : p_edits) {
		ResolvedEdit r;
		r.start_offset = to_offset(p_source, e.start_line, e.start_column);
		r.end_offset = to_offset(p_source, e.end_line, e.end_column);
		r.new_text = e.new_text;
		if (r.start_offset < 0 || r.end_offset < 0 || r.end_offset < r.start_offset) {
			return false;
		}
		resolved.push_back(r);
	}

	resolved.sort_custom<ResolvedEditComparator>();
	for (int i = 1; i < resolved.size(); i++) {
		if (resolved[i].start_offset < resolved[i - 1].end_offset) {
			return false; // Overlap.
		}
	}

	String out = p_source;
	for (int i = resolved.size() - 1; i >= 0; i--) {
		const ResolvedEdit &r = resolved[i];
		out = out.substr(0, r.start_offset) + r.new_text + out.substr(r.end_offset, out.length() - r.end_offset);
	}
	r_result = out;
	return true;
}

#endif // TOOLS_ENABLED
