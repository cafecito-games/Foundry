/**************************************************************************/
/*  gdscript_fixpoint_inference.h                                         */
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

// Options controlling a fixpoint inference run.
struct FixpointInferenceOptions {
	// 0 => auto: (first-pass candidate count) + 1, clamped by a hard ceiling.
	int max_iterations = 0;
	// Strict modes are forwarded to per-pass verification (issue #34).
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};

// Before/after record for one file the run modified.
struct FixpointFileChange {
	String path;
	String before_source;
	String after_source;
	int annotations_applied = 0;
};

// A candidate batch that was found but not applied, with why.
struct FixpointSkipped {
	String path;
	int line = -1;
	String reason;
};

struct FixpointInferenceResult {
	// True only means the run completed without a fatal error; it does not imply any
	// annotations were applied. Consult changed_files and skipped for actual results.
	bool ok = false;
	String error_message; // Set only on a fatal, no-op failure (e.g. unreadable file).
	// Counts every pass, including the final confirming pass that applies nothing and
	// detects the fixpoint, so a chain of depth N typically reports N+1.
	int iterations = 0;
	int total_annotations_applied = 0;
	// True means a pass produced no new annotations, so the iteration reached a real
	// fixpoint. False means the iteration bound stopped the loop early before any such
	// empty pass, so further annotations might still have been inferable.
	bool converged = false;
	Vector<FixpointFileChange> changed_files;
	Vector<FixpointSkipped> skipped;
};

// Each pass routes its accepted rewrites through GDScriptVerificationHarness::verify,
// which re-analyzes the inverse-dependency closure of the edited files (bounded by the
// run's path set) and rolls back any rewrite that introduces a new error in a
// dependent. An edit that would break a caller is therefore rejected and reported as a
// skip rather than committed. Dependents outside p_paths are not re-analyzed, so the
// path set should cover the project being migrated.
class GDScriptFixpointInference {
public:
	// Drives Add Type Annotation to a fixpoint over p_paths, writing accepted
	// changes to disk. Returns a report of what changed and what was skipped.
	// Mutates files on disk and global GDScriptCache state, so it is NOT safe to
	// call concurrently with a live editing session or another run().
	static FixpointInferenceResult run(
			const Vector<String> &p_paths,
			const FixpointInferenceOptions &p_options = FixpointInferenceOptions());
};

#endif // TOOLS_ENABLED
