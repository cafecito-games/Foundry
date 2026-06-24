/**************************************************************************/
/*  gdscript_verification_harness.h                                       */
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

#include "gdscript_refactoring.h" // RefactorTextEdit

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// One candidate annotation edit to verify, addressed by file + declaration anchor.
// `edits` are the RefactorTextEdits of a single RefactorCandidate.
struct VerificationCandidate {
	String path;
	int line = -1; // 0-based declaration anchor, for reporting.
	Vector<RefactorTextEdit> edits;
};

struct VerificationOptions {
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};

// A candidate that was dropped, with the reason and the diagnostics attributed to it.
struct VerificationRejected {
	String path;
	int line = -1;
	String reason;
	Vector<String> diagnostics; // Analyzer messages newly introduced by this candidate.
};

struct VerificationResult {
	bool ok = false; // false only on a fatal precondition (e.g. unreadable file).
	String error_message; // Populated only when !ok.
	// Candidates proven to introduce no new diagnostics vs the baseline (multiset
	// difference is empty). Candidates whose edits fail to apply are always in rejected,
	// never here. Callers re-apply accepted candidates' edits themselves.
	Vector<VerificationCandidate> accepted;
	Vector<VerificationRejected> rejected; // Dropped, with attributed diagnostics.
	int baseline_error_count = 0; // Total analyzer errors across the affected set before any edit.
	int accepted_error_count = 0; // Total after applying all accepted edits.
};

struct StrictViolation {
	String path;
	int line = -1;
	int column = -1;
	String message;
};

struct StrictPreviewResult {
	bool ok = false;
	String error_message;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
	Vector<StrictViolation> violations; // Sites failing only under strict mode.
};

// Headless, caret-independent post-edit verification. Verification stages candidate
// sources to disk and restores originals, so it is NOT safe to call concurrently with
// a live editing session or another run.
class GDScriptVerificationHarness {
public:
	// Verifies p_candidates against the dependency closure of the files they touch.
	// p_universe bounds inverse-dependent discovery (typically the migration's full
	// path set); files outside it are not re-analyzed. Does NOT commit accepted edits.
	static VerificationResult verify(
			const Vector<VerificationCandidate> &p_candidates,
			const Vector<String> &p_universe,
			const VerificationOptions &p_options = VerificationOptions());

	// Read-only. Reports diagnostics present only under the requested strict mode.
	// Analyzes each file twice (a non-strict baseline and a strict pass) and returns the
	// strict-only difference; writes nothing to disk.
	static StrictPreviewResult preview_strict(
			const Vector<String> &p_paths,
			const VerificationOptions &p_options);
};

#endif // TOOLS_ENABLED
