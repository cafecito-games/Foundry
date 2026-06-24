/**************************************************************************/
/*  gdscript_fixpoint_inference.cpp                                       */
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

#include "gdscript_fixpoint_inference.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring.h"
#include "gdscript_refactoring_edits.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "editor/script/script_refactor_apply.h"

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace {

String read_source(const String &p_path, bool &r_ok) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	r_ok = (err == OK);
	return source;
}

// Drop cached parse/script state so a later analysis re-reads p_path from disk.
void invalidate_cache(const String &p_path) {
	GDScriptCache::remove_parser(p_path);
	GDScriptCache::remove_script(p_path);
}

// Re-parse and analyze p_source as if saved at p_path. True when analysis
// succeeds, i.e. the applied annotations introduced no new errors.
bool verify_source(const String &p_path, const String &p_source) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return false;
	}
	GDScriptAnalyzer analyzer(&parser);
	return analyzer.analyze() == OK;
}

int count_enabled_edits(const RefactorContext &p_ctx, Vector<RefactorTextEdit> &r_edits) {
	RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(p_ctx, RefactorKind::ADD_TYPE_ANNOTATION);
	if (!candidates.ok) {
		return 0;
	}
	int collected = 0;
	for (const RefactorCandidate &candidate : candidates.candidates) {
		if (!candidate.enabled) {
			continue;
		}
		for (const RefactorTextEdit &edit : candidate.edits) {
			r_edits.push_back(edit);
		}
		collected++;
	}
	return collected;
}

} // namespace

FixpointInferenceResult GDScriptFixpointInference::run(const Vector<String> &p_paths, const FixpointInferenceOptions &p_options) {
	FixpointInferenceResult result;

	// Collapse duplicate inputs while preserving first-seen order so the same file is
	// never snapshotted, committed, or reported twice.
	Vector<String> paths;
	{
		HashSet<String> seen;
		for (const String &path : p_paths) {
			if (seen.has(path)) {
				continue;
			}
			seen.insert(path);
			paths.push_back(path);
		}
	}

	// Capture originals for before/after reporting and change detection.
	HashMap<String, String> original_source;
	for (const String &path : paths) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		original_source[path] = source;
	}

	// Determine the iteration ceiling.
	int bound = p_options.max_iterations;
	if (bound <= 0) {
		int first_pass_candidates = 0;
		for (const String &path : paths) {
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = original_source[path];
			RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
			first_pass_candidates += candidates.candidates.size();
		}
		bound = first_pass_candidates + 1;
	}
	const int hard_ceiling = 1000;
	if (bound > hard_ceiling) {
		bound = hard_ceiling;
	}

	HashMap<String, int> applied_per_file;

	int iteration = 0;
	while (iteration < bound) {
		iteration++;

		// Snapshot a consistent view of every file for this pass.
		HashMap<String, String> snapshot;
		for (const String &path : paths) {
			bool ok = false;
			const String source = read_source(path, ok);
			if (ok) {
				snapshot[path] = source;
			}
		}

		// Collect + verify accepted rewrites without committing yet.
		HashMap<String, String> pending_source;
		HashMap<String, int> pending_count;
		for (const String &path : paths) {
			if (!snapshot.has(path)) {
				continue;
			}
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = snapshot[path];

			Vector<RefactorTextEdit> edits;
			const int enabled = count_enabled_edits(ctx, edits);
			if (enabled == 0 || edits.is_empty()) {
				continue;
			}

			String new_source;
			if (!GDScriptRefactorEdits::apply(snapshot[path], edits, new_source)) {
				continue; // Overlapping/out-of-range edits; skip this file this pass.
			}
			if (!verify_source(path, new_source)) {
				continue; // Rejected this pass; the final-state scan reports what remains.
			}
			pending_source[path] = new_source;
			pending_count[path] = enabled;
		}

		// Whether this pass found anything worth applying, independent of whether the
		// writes themselves succeed, so a fixpoint is not falsely declared on write failure.
		const bool had_pending = !pending_source.is_empty();

		// Commit at pass end so each iteration advances exactly one dependency layer.
		for (const KeyValue<String, String> &entry : pending_source) {
			String write_error;
			if (!ScriptRefactorApply::write_file(entry.key, entry.value, write_error)) {
				continue; // Write failed; the final-state scan reports what remains.
			}
			invalidate_cache(entry.key);
			const int count = pending_count[entry.key];
			applied_per_file[entry.key] += count;
		}

		if (!had_pending) {
			result.converged = true;
			break; // Fixpoint reached: no file produced an accepted rewrite this pass.
		}
	}

	result.iterations = iteration;

	// Report what remains untyped from the final on-disk state only, so skips are an
	// honest, duplicate-free snapshot. A successfully applied annotation is no longer a
	// candidate, so it never reappears. Disabled candidates were never inferable;
	// still-enabled ones were inferable but never landed (verification rejected them or
	// the iteration bound was hit).
	for (const String &path : paths) {
		RefactorContext ctx;
		ctx.path = path;
		bool ok = false;
		ctx.source = read_source(path, ok);
		if (!ok) {
			continue;
		}
		RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		if (!candidates.ok) {
			continue;
		}
		for (const RefactorCandidate &candidate : candidates.candidates) {
			if (candidate.enabled) {
				// Inferable but never landed: verification rejected it or the bound was hit.
				result.skipped.push_back({ path, candidate.line, "unresolved after fixpoint (verification rejected or iteration bound reached)" });
				continue;
			}
			// A declaration that already carries an annotation (pre-existing or applied by
			// this run) is not a genuine skip, so it is excluded from the report.
			if (candidate.disabled_reason.contains("already")) {
				continue;
			}
			result.skipped.push_back({ path, candidate.line, candidate.disabled_reason });
		}
	}

	// Build the change report from captured originals vs final on-disk source.
	for (const String &path : paths) {
		bool ok = false;
		const String after = read_source(path, ok);
		if (!ok) {
			continue;
		}
		const String &before = original_source[path];
		if (before == after) {
			continue;
		}
		FixpointFileChange change;
		change.path = path;
		change.before_source = before;
		change.after_source = after;
		change.annotations_applied = applied_per_file.has(path) ? applied_per_file[path] : 0;
		result.total_annotations_applied += change.annotations_applied;
		result.changed_files.push_back(change);
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
