/**************************************************************************/
/*  fs_refactoring_edits.cpp                                              */
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

#include "fs_refactoring_edits.h"

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
	bool has_expected_text = false;
	String expected_text;
	String new_text;
};

struct ResolvedEditComparator {
	bool operator()(const ResolvedEdit &a, const ResolvedEdit &b) const {
		return a.start_offset < b.start_offset;
	}
};

int compare_line_column(int p_a_line, int p_a_column, int p_b_line, int p_b_column) {
	if (p_a_line != p_b_line) {
		return p_a_line < p_b_line ? -1 : 1;
	}
	if (p_a_column == p_b_column) {
		return 0;
	}
	return p_a_column < p_b_column ? -1 : 1;
}

int compare_edit_start(const RefactorTextEdit &p_a, const RefactorTextEdit &p_b) {
	const int start_order = compare_line_column(p_a.start_line, p_a.start_column, p_b.start_line, p_b.start_column);
	if (start_order != 0) {
		return start_order;
	}
	return compare_line_column(p_a.end_line, p_a.end_column, p_b.end_line, p_b.end_column);
}

struct EditStartComparator {
	bool operator()(const RefactorTextEdit &p_a, const RefactorTextEdit &p_b) const {
		return compare_edit_start(p_a, p_b) < 0;
	}
};

bool edit_contains_position(const RefactorTextEdit &p_edit, int p_line, int p_column) {
	return compare_line_column(p_line, p_column, p_edit.start_line, p_edit.start_column) >= 0 &&
			compare_line_column(p_line, p_column, p_edit.end_line, p_edit.end_column) < 0;
}

bool edit_ends_at_position(const RefactorTextEdit &p_edit, int p_line, int p_column) {
	return compare_line_column(p_line, p_column, p_edit.end_line, p_edit.end_column) == 0;
}

bool edit_overlaps_location(const RefactorTextEdit &p_edit, const RefactorLocation &p_location) {
	return compare_line_column(p_location.start_line, p_location.start_column, p_edit.end_line, p_edit.end_column) < 0 &&
			compare_line_column(p_location.end_line, p_location.end_column, p_edit.start_line, p_edit.start_column) > 0;
}
} // namespace

bool FSRefactorEdits::apply(const String &p_source, const Vector<RefactorTextEdit> &p_edits, String &r_result) {
	Vector<ResolvedEdit> resolved;
	for (const RefactorTextEdit &e : p_edits) {
		ResolvedEdit r;
		r.start_offset = to_offset(p_source, e.start_line, e.start_column);
		r.end_offset = to_offset(p_source, e.end_line, e.end_column);
		r.has_expected_text = e.has_expected_text;
		r.expected_text = e.expected_text;
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
	// This guards the exact edit span. It catches stale buffers when that span's
	// text changed, but it does not prove the surrounding token context is the
	// same as when the edit was resolved.
	for (const ResolvedEdit &r : resolved) {
		if (r.has_expected_text && p_source.substr(r.start_offset, r.end_offset - r.start_offset) != r.expected_text) {
			return false;
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

void FSRefactorEdits::sort_edits(Vector<RefactorTextEdit> &r_edits) {
	bool sorted = true;
	for (int i = 1; i < r_edits.size(); i++) {
		if (compare_edit_start(r_edits[i], r_edits[i - 1]) < 0) {
			sorted = false;
			break;
		}
	}
	if (sorted) {
		// Vector::sort_custom is not stable; skipping the sort keeps the relative
		// order of edits that share a start position (e.g. two insertions at the
		// same point).
		return;
	}
	r_edits.sort_custom<EditStartComparator>();
}

bool FSRefactorEdits::validate_structure(const Vector<RefactorTextEdit> &p_edits, String &r_error) {
	for (int i = 0; i < p_edits.size(); i++) {
		const RefactorTextEdit &edit = p_edits[i];
		if (edit.start_line < 0 || edit.start_column < 0 || edit.end_line < 0 || edit.end_column < 0) {
			r_error = vformat("Edit %d has a negative position (%d:%d)-(%d:%d).", i, edit.start_line, edit.start_column, edit.end_line, edit.end_column);
			return false;
		}
		if (compare_line_column(edit.start_line, edit.start_column, edit.end_line, edit.end_column) > 0) {
			r_error = vformat("Edit %d has a reversed range (%d:%d)-(%d:%d).", i, edit.start_line, edit.start_column, edit.end_line, edit.end_column);
			return false;
		}
		if (i == 0) {
			continue;
		}
		const RefactorTextEdit &previous = p_edits[i - 1];
		if (compare_edit_start(edit, previous) < 0) {
			r_error = vformat("Edit %d at (%d:%d) starts before edit %d at (%d:%d); edits must be sorted.", i, edit.start_line, edit.start_column, i - 1, previous.start_line, previous.start_column);
			return false;
		}
		if (compare_line_column(edit.start_line, edit.start_column, previous.end_line, previous.end_column) < 0) {
			r_error = vformat("Edit %d at (%d:%d) overlaps edit %d ending at (%d:%d).", i, edit.start_line, edit.start_column, i - 1, previous.end_line, previous.end_column);
			return false;
		}
	}
	r_error = String();
	return true;
}

bool FSRefactorEdits::validate(const String &p_source, const Vector<RefactorTextEdit> &p_edits, String &r_error) {
	if (!validate_structure(p_edits, r_error)) {
		return false;
	}
	for (int i = 0; i < p_edits.size(); i++) {
		const RefactorTextEdit &edit = p_edits[i];
		const int start_offset = to_offset(p_source, edit.start_line, edit.start_column);
		const int end_offset = to_offset(p_source, edit.end_line, edit.end_column);
		if (start_offset < 0 || end_offset < 0) {
			r_error = vformat("Edit %d range (%d:%d)-(%d:%d) is outside the source.", i, edit.start_line, edit.start_column, edit.end_line, edit.end_column);
			return false;
		}
		if (edit.has_expected_text && p_source.substr(start_offset, end_offset - start_offset) != edit.expected_text) {
			r_error = vformat("Edit %d expected text '%s' but the source range holds '%s'.", i, edit.expected_text, p_source.substr(start_offset, end_offset - start_offset));
			return false;
		}
	}
	r_error = String();
	return true;
}

int FSRefactorEdits::find_edit_at_location(const Vector<RefactorTextEdit> &p_edits, const RefactorLocation &p_location) {
	for (int i = 0; i < p_edits.size(); i++) {
		if (edit_contains_position(p_edits[i], p_location.start_line, p_location.start_column)) {
			return i;
		}
	}
	if (!p_location.has_selection()) {
		for (int i = 0; i < p_edits.size(); i++) {
			if (edit_ends_at_position(p_edits[i], p_location.start_line, p_location.start_column)) {
				return i;
			}
		}
		return -1;
	}
	for (int i = 0; i < p_edits.size(); i++) {
		if (edit_contains_position(p_edits[i], p_location.end_line, p_location.end_column)) {
			return i;
		}
	}
	for (int i = 0; i < p_edits.size(); i++) {
		if (edit_overlaps_location(p_edits[i], p_location)) {
			return i;
		}
	}
	return -1;
}

#endif // TOOLS_ENABLED
