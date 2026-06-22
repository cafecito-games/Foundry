/**************************************************************************/
/*  gdscript_refactoring.h                                                */
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

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include "../gdscript_parser.h"

// A single text edit within ONE file. 0-based, half-open [start, end) range.
struct RefactorTextEdit {
	int start_line = 0;
	int start_column = 0;
	int end_line = 0;
	int end_column = 0;
	String new_text;
};

enum class RefactorKind {
	RENAME,
	EXTRACT_VARIABLE,
	EXTRACT_METHOD,
	ADD_TYPE_ANNOTATION,
	INLINE_VARIABLE,
};

struct RefactorAvailability {
	RefactorKind kind = RefactorKind::RENAME;
	String title;
	bool enabled = false;
	String disabled_reason;
};

struct RefactorResult {
	bool ok = false;
	String error_message;
	// Non-fatal advisory shown alongside a successful result (e.g. an @export var
	// whose references may live outside this file and won't be updated).
	String warning;
	Vector<RefactorTextEdit> edits;
	int rename_anchor_line = -1;
	int rename_anchor_column = -1;
	String suggested_name;
};

// Caret position OR selection range. 0-based. No selection => end == start.
struct RefactorLocation {
	int start_line = 0;
	int start_column = 0;
	int end_line = 0;
	int end_column = 0;

	bool has_selection() const {
		return start_line != end_line || start_column != end_column;
	}
};

// Refactor-specific input gathered by the UI before `prepare`.
struct RefactorParams {
	String new_name;
};

// Everything a refactor needs about the target file.
struct RefactorContext {
	String path; // res:// path of the edited script.
	String source; // Current buffer contents.
};

class GDScriptRefactoring {
public:
	static Vector<RefactorAvailability> get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location);
	static RefactorResult prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params);
};

#endif // TOOLS_ENABLED
