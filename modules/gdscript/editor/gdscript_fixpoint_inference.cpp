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
#include "gdscript_verification_harness.h"

#include "../gdscript_cache.h"

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

// Add one committed annotation of the given declaration kind to the running tally. An
// unrecognized kind still advances `total`, so the per-kind breakdown can never exceed it.
void tally_applied_kind(const String &p_kind, FixpointInferableCounts &r_counts) {
	r_counts.total++;
	if (p_kind == "variable") {
		r_counts.variable++;
	} else if (p_kind == "constant") {
		r_counts.constant++;
	} else if (p_kind == "parameter") {
		r_counts.parameter++;
	} else if (p_kind == "return") {
		r_counts.return_type++;
	}
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
			ctx.allow_member_container_inference = true; // Verified migration path: the harness re-checks dependents.
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
	// Per-kind tally of every annotation actually committed, accumulated only on a successful
	// write so a failed commit never inflates the projection's coverage.
	FixpointInferableCounts applied_kinds;

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

		// Collect every enabled candidate across the snapshot as a verification candidate.
		Vector<VerificationCandidate> candidates;
		for (const String &path : paths) {
			if (!snapshot.has(path)) {
				continue;
			}
			RefactorContext ctx;
			ctx.path = path;
			ctx.allow_member_container_inference = true; // Verified migration path: the harness re-checks dependents.
			ctx.source = snapshot[path];
			RefactorCandidatesResult found = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
			if (!found.ok) {
				continue;
			}
			for (const RefactorCandidate &candidate : found.candidates) {
				if (!candidate.enabled || candidate.edits.is_empty()) {
					continue;
				}
				VerificationCandidate vc;
				vc.path = path;
				vc.line = candidate.line;
				vc.declaration_kind = candidate.declaration_kind;
				vc.edits = candidate.edits;
				candidates.push_back(vc);
			}
		}

		HashMap<String, String> pending_source;
		HashMap<String, int> pending_count;
		// Declaration kinds of the accepted candidates per file, parallel to pending_count, so
		// the per-kind tally only advances for files whose write actually lands.
		HashMap<String, Vector<String>> pending_kinds;
		if (!candidates.is_empty()) {
			VerificationOptions verify_options;
			verify_options.strict_null_checks = p_options.strict_null_checks;
			verify_options.strict_dynamic_checks = p_options.strict_dynamic_checks;
			VerificationResult verified = GDScriptVerificationHarness::verify(candidates, paths, verify_options);
			if (!verified.ok) {
				// A fatal harness failure (unreadable file, failed disk restore) must not be
				// silently treated as convergence. Surface it immediately so the caller knows
				// the result is unreliable rather than a false ok/converged=true.
				result.ok = false;
				result.error_message = verified.error_message;
				return result;
			}
			// Group accepted candidates' edits per file and apply over the snapshot.
			HashMap<String, Vector<RefactorTextEdit>> accepted_edits;
			for (const VerificationCandidate &candidate : verified.accepted) {
				for (const RefactorTextEdit &edit : candidate.edits) {
					accepted_edits[candidate.path].push_back(edit);
				}
				pending_count[candidate.path] += 1;
				pending_kinds[candidate.path].push_back(candidate.declaration_kind);
			}
			for (const KeyValue<String, Vector<RefactorTextEdit>> &entry : accepted_edits) {
				String new_source;
				if (GDScriptRefactorEdits::apply(snapshot[entry.key], entry.value, new_source)) {
					pending_source[entry.key] = new_source;
				} else {
					pending_count.erase(entry.key); // Could not apply this file's accepted set.
					pending_kinds.erase(entry.key);
				}
			}
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
			if (pending_kinds.has(entry.key)) {
				for (const String &kind : pending_kinds[entry.key]) {
					tally_applied_kind(kind, applied_kinds);
				}
			}
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
		ctx.allow_member_container_inference = true; // Verified migration path: the harness re-checks dependents.
		bool ok = false;
		ctx.source = read_source(path, ok);
		if (!ok) {
			// The file became unreadable since the run started (it was readable at snapshot
			// time, or this is the final pass). Surface it rather than dropping it silently.
			result.unanalyzed_files.push_back({ path, vformat("Cannot read '%s'.", path) });
			continue;
		}
		RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		if (!candidates.ok) {
			// The file could not be analyzed (parse/analyze error, unresolved dependency), so
			// none of its declarations are visible. Report it explicitly so an unanalyzable
			// file is not mistaken for a clean, fully-typed one (epic #29: honest reporting).
			const String reason = candidates.error_message.is_empty() ? String("Cannot analyze this script.") : candidates.error_message;
			result.unanalyzed_files.push_back({ path, reason });
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

	result.inferable = applied_kinds;

	// Projection mode: the fixpoint and verification ran against real on-disk state (so the
	// reports above reflect the exact edit set a real run would commit), but the tree must be
	// left untouched. Restore every changed file to its captured original and drop the now-stale
	// cache so a subsequent analysis re-reads the unmodified source.
	if (p_options.dry_run) {
		for (const FixpointFileChange &change : result.changed_files) {
			String restore_error;
			if (!ScriptRefactorApply::write_file(change.path, change.before_source, restore_error)) {
				// A failed restore means the projection left the tree modified, which violates the
				// dry-run contract. Surface it rather than report a clean, no-op projection.
				result.ok = false;
				result.error_message = vformat("Cannot restore '%s' after dry-run projection: %s", change.path, restore_error);
				return result;
			}
			invalidate_cache(change.path);
		}
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
