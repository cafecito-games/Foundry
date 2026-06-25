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
	bool has_expected_text = false;
	String expected_text;
	String new_text;
};

struct RefactorFileEdit {
	String path;
	Vector<RefactorTextEdit> edits;
};

struct RefactorUnresolvedReference {
	String path;
	int line = -1;
	int column = -1;
	String message;
};

enum class RefactorKind {
	RENAME,
	EXTRACT_VARIABLE,
	EXTRACT_METHOD,
	ADD_TYPE_ANNOTATION,
	INLINE_VARIABLE,
	IMPLEMENT_ABSTRACT_METHODS,
	INSERT_EXPLICIT_CAST,
	WIDEN_TO_NULLABLE,
	SORT_MEMBERS_BY_STYLE_GUIDE,
};

struct RefactorAvailability {
	RefactorKind kind = RefactorKind::RENAME;
	String title;
	bool enabled = false;
	String disabled_reason;
};

// One independently-applicable refactor opportunity found without a caret.
struct RefactorCandidate {
	RefactorKind kind = RefactorKind::ADD_TYPE_ANNOTATION;
	bool enabled = false; // false => found but not applicable.
	String disabled_reason; // Populated when !enabled.
	// For ADD_TYPE_ANNOTATION, the kind of declaration this site annotates:
	// "variable", "constant", "parameter", or "return". Empty for refactors that
	// do not classify by declaration kind. Lets a batch caller (e.g. the migration
	// report) bucket inferable sites without re-parsing the reason text.
	String declaration_kind;
	int line = -1; // 0-based anchor line of the declaration.
	int column = -1; // 0-based anchor column.
	Vector<RefactorTextEdit> edits; // Active-file edits (reuses RefactorTextEdit).
};

struct RefactorCandidatesResult {
	bool ok = false;
	String error_message; // Set only when the file cannot be analyzed at all.
	Vector<RefactorCandidate> candidates;
};

struct RefactorResult {
	bool ok = false;
	String error_message;
	// Non-fatal advisory shown alongside a successful result (e.g. an @export var
	// whose references may live outside this file and won't be updated).
	String warning;
	// Edits in the active file, kept for existing single-file refactor callers.
	Vector<RefactorTextEdit> edits;
	Vector<RefactorFileEdit> file_edits;
	Vector<RefactorUnresolvedReference> unresolved_references;
	// Current-file rename ranges for inline editing. These carry expected_text
	// guards but intentionally clear new_text because the live editor provides
	// the tentative replacement text.
	Vector<RefactorTextEdit> rename_occurrences;
	int rename_anchor_line = -1;
	int rename_anchor_column = -1;
	String suggested_name;
	// Existing members in the Extract Method target class. The script editor uses
	// this to validate typed method names without rebuilding extract candidates on
	// every keystroke.
	Vector<String> extract_method_member_names;
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
	// Class-wide member-variable container element inference (e.g. upgrading a
	// private `var _items = []` to `Array[int]`) is only sound under the open-world
	// assumption when a verifier re-checks the dependency closure afterward, since
	// an external subclass could mutate the member with a different type. The
	// fixpoint migration path sets this so member elements are upgraded only there;
	// the interactive editor refactor leaves it false and keeps members at the
	// analyzer's bare container type, which is always safe to apply directly.
	bool allow_member_container_inference = false;
};

class GDScriptRefactoring {
public:
	static Vector<RefactorAvailability> get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location);
	static RefactorResult prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params);
	static RefactorCandidatesResult find_candidates(const RefactorContext &p_context, RefactorKind p_kind);
	static bool validate_extract_method_name(
			const Vector<String> &p_existing_member_names,
			const String &p_name,
			String &r_error_message);
};

#endif // TOOLS_ENABLED
