/**************************************************************************/
/*  gdscript_batch_candidates.h                                           */
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

#include "gdscript_refactoring.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// All candidates collected from one file during a batch run. Reuses the
// per-file RefactorCandidate so the result feeds the same apply layer.
struct BatchFileCandidates {
	String path;
	bool ok = false; // false => this file could not be read or analyzed; see error_message.
	String error_message; // Populated only when !ok.
	Vector<RefactorCandidate> candidates; // Both enabled and disabled, for honest reporting.
};

// Result of collecting candidates across a batch of files in a single call.
struct BatchCandidatesResult {
	bool ok = false; // false only on a fatal precondition (e.g. an unsupported refactor kind).
	String error_message; // Populated only when !ok.
	Vector<BatchFileCandidates> files; // One entry per unique input path, in first-seen order.
	int total_candidates = 0; // Candidates summed across every successfully analyzed file.
	int enabled_candidates = 0; // Subset of total_candidates that are applicable.
};

// Read-only, multi-file counterpart to GDScriptRefactoring::find_candidates. This is the
// preview primitive a migration driver consults before applying any change: it returns
// every Add Type Annotation candidate (enabled and disabled, with edits and reasons)
// across a batch of files without modifying anything on disk.
class GDScriptBatchCandidates {
public:
	// Collects candidates for every path in p_paths. Each path is read from disk; duplicate
	// paths are collapsed while preserving first-seen order, so a file is never analyzed or
	// reported twice. A file that cannot be read or analyzed is recorded with ok=false in its
	// BatchFileCandidates entry and does not abort the batch. The call itself fails (ok=false,
	// empty files) only when p_kind has no headless collection (currently anything other than
	// ADD_TYPE_ANNOTATION).
	//
	// File discovery, ignore rules, and ordering are the caller's responsibility (issue #38);
	// this primitive operates on the explicit path list it is given.
	static BatchCandidatesResult collect(
			const Vector<String> &p_paths,
			RefactorKind p_kind = RefactorKind::ADD_TYPE_ANNOTATION);
};

#endif // TOOLS_ENABLED
