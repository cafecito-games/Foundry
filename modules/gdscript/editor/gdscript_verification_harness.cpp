/**************************************************************************/
/*  gdscript_verification_harness.cpp                                     */
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

#include "gdscript_verification_harness.h"

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
#include "core/templates/list.h"

namespace {

String read_source(const String &p_path, bool &r_ok) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	r_ok = (err == OK);
	return source;
}

void invalidate_cache(const String &p_path) {
	GDScriptCache::remove_parser(p_path);
	GDScriptCache::remove_script(p_path);
}

// Analyze one source as if saved at p_path; append "path:line:column:message" for every
// parser/analyzer error to r_messages. Returns the count contributed by this file.
// A parse failure contributes at least one error so a malformed edit is never "clean".
int analyze_one(const String &p_path, const String &p_source, const VerificationOptions &p_options, Vector<String> &r_messages) {
	GDScriptParser parser;
	const Error parse_err = parser.parse(p_source, p_path, false);
	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_options.strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_options.strict_dynamic_checks);
	analyzer.analyze();
	int count = 0;
	for (const GDScriptParser::ParserError &error : parser.get_errors()) {
		r_messages.push_back(vformat("%s:%d:%d:%s", p_path, error.line, error.column, error.message));
		count++;
	}
	if (parse_err != OK && count == 0) {
		r_messages.push_back(vformat("%s: parse failed", p_path));
		count++;
	}
	return count;
}

// Result of analyzing the whole affected set under a given staged-source map.
// Each message key encodes path:line:column:message so position and text are both
// part of the identity; two diagnostics at different locations are always distinct.
struct AffectedAnalysis {
	int error_count = 0;
	Vector<String> messages; // Each entry is a "path:line:column:message" key.
};

// Returns true when p_candidate introduces at least one diagnostic key that appears
// more times in p_candidate than in p_baseline (multiset difference is non-empty).
// A net-zero swap — one error removed, a different one added — correctly returns true.
bool regresses(const AffectedAnalysis &p_baseline, const AffectedAnalysis &p_candidate) {
	HashMap<String, int> baseline_counts;
	for (const String &key : p_baseline.messages) {
		baseline_counts[key] += 1;
	}
	for (const String &key : p_candidate.messages) {
		HashMap<String, int>::Iterator it = baseline_counts.find(key);
		if (it && it->value > 0) {
			it->value -= 1;
		} else {
			return true; // New diagnostic not covered by baseline.
		}
	}
	return false;
}

// Stages p_staged (path -> source) to disk for the edited files, invalidates affected
// caches, analyzes every affected file, then restores originals. Always restores.
// r_fatal is set true (and the run should abort) on a staged-write failure or if any
// original could not be restored, since the latter may leave edited source on disk.
AffectedAnalysis analyze_affected(
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original, // current on-disk source for every affected file
		const HashMap<String, String> &p_staged, // overrides for edited files only
		const VerificationOptions &p_options,
		bool &r_fatal) {
	r_fatal = false;
	AffectedAnalysis analysis;

	// Stage: write only files whose staged source differs from the original.
	Vector<String> written;
	for (const KeyValue<String, String> &entry : p_staged) {
		const String &path = entry.key;
		if (!p_original.has(path) || p_original[path] == entry.value) {
			continue;
		}
		String write_error;
		if (!ScriptRefactorApply::write_file(path, entry.value, write_error)) {
			r_fatal = true;
			break;
		}
		written.push_back(path);
	}

	if (!r_fatal) {
		for (const String &path : written) {
			invalidate_cache(path);
		}
		// Analyze the full affected set against the staged disk state.
		for (const String &path : p_affected) {
			const String source = p_staged.has(path) ? p_staged[path] : (p_original.has(path) ? p_original[path] : String());
			analysis.error_count += analyze_one(path, source, p_options, analysis.messages);
		}
	}

	// Restore: rewrite originals for every file we wrote, then invalidate again. A
	// failed restore leaves edited source on disk, defeating the harness's guarantee of
	// leaving the tree as found, so surface it and abort. Restore the rest regardless so
	// a single failure does not strand additional files.
	bool restore_failed = false;
	for (const String &path : written) {
		String write_error;
		if (!ScriptRefactorApply::write_file(path, p_original[path], write_error)) {
			ERR_PRINT(vformat("Verification harness failed to restore '%s'; it may be left modified on disk: %s", path, write_error));
			restore_failed = true;
		}
		invalidate_cache(path);
	}
	if (restore_failed) {
		r_fatal = true;
	}

	return analysis;
}

// Apply every candidate's edits, grouped by file, over p_original.
// Files whose combined edit group fails to apply (overlap, out-of-range) are recorded
// in r_unapplicable_paths so callers can explicitly reject candidates on those files.
// Returns staged path->source for successfully-applied files only.
HashMap<String, String> stage_candidates(
		const Vector<VerificationCandidate> &p_candidates,
		const HashMap<String, String> &p_original,
		HashSet<String> *r_unapplicable_paths = nullptr) {
	HashMap<String, Vector<RefactorTextEdit>> edits_by_path;
	for (int i = 0; i < p_candidates.size(); i++) {
		const VerificationCandidate &candidate = p_candidates[i];
		for (const RefactorTextEdit &edit : candidate.edits) {
			edits_by_path[candidate.path].push_back(edit);
		}
	}
	HashMap<String, String> staged;
	for (const KeyValue<String, Vector<RefactorTextEdit>> &entry : edits_by_path) {
		String applied;
		if (p_original.has(entry.key) && GDScriptRefactorEdits::apply(p_original[entry.key], entry.value, applied)) {
			staged[entry.key] = applied;
		} else if (r_unapplicable_paths != nullptr) {
			r_unapplicable_paths->insert(entry.key);
		}
	}
	return staged;
}

// Stage only the given subset of candidates over originals and return the affected
// analysis. Used as the ddmin test oracle.
AffectedAnalysis analyze_subset(
		const Vector<VerificationCandidate> &p_subset,
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original,
		const VerificationOptions &p_options,
		bool &r_fatal) {
	const HashMap<String, String> staged = stage_candidates(p_subset, p_original);
	return analyze_affected(p_affected, p_original, staged, p_options, r_fatal);
}

// Returns the candidates from p_pool indexed by p_indices.
Vector<VerificationCandidate> subset_of(const Vector<VerificationCandidate> &p_pool, const Vector<int> &p_indices) {
	Vector<VerificationCandidate> out;
	for (int index : p_indices) {
		out.push_back(p_pool[index]);
	}
	return out;
}

// Classic ddmin: find a 1-minimal subset of p_indices whose application still
// regresses (introduces new diagnostics vs p_baseline_analysis). p_pool is the full
// candidate list. Returns the minimal offending index list. Assumes the full
// p_indices regresses.
Vector<int> ddmin_offending(
		const Vector<int> &p_indices,
		const Vector<VerificationCandidate> &p_pool,
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original,
		const VerificationOptions &p_options,
		const AffectedAnalysis &p_baseline_analysis,
		bool &r_fatal) {
	Vector<int> current = p_indices;
	int granularity = 2;
	while (current.size() >= 2) {
		const int subset_size = current.size() / granularity;
		bool reduced = false;
		for (int start = 0; start < current.size(); start += subset_size) {
			// Complement = current minus [start, start+subset_size).
			Vector<int> complement;
			for (int i = 0; i < current.size(); i++) {
				if (i < start || i >= start + subset_size) {
					complement.push_back(current[i]);
				}
			}
			if (complement.is_empty()) {
				continue;
			}
			const AffectedAnalysis analysis = analyze_subset(subset_of(p_pool, complement), p_affected, p_original, p_options, r_fatal);
			if (r_fatal) {
				return current;
			}
			if (regresses(p_baseline_analysis, analysis)) {
				current = complement;
				granularity = MAX(granularity - 1, 2);
				reduced = true;
				break;
			}
		}
		if (!reduced) {
			if (granularity >= current.size()) {
				break;
			}
			granularity = MIN(granularity * 2, current.size());
		}
	}
	return current;
}

// Count the non-strict diagnostics of p_source keyed by line:column:message. The message
// is part of the key so a null-mode and a dynamic-mode diagnostic at the same position
// stay distinct. Counts (rather than set membership) so a strict pass that emits a shared
// diagnostic more times than the baseline surfaces only the extra occurrences.
HashMap<String, int> baseline_diagnostic_counts(const String &p_path, const String &p_source) {
	HashMap<String, int> counts;
	GDScriptParser parser;
	parser.parse(p_source, p_path, false);
	GDScriptAnalyzer analyzer(&parser);
	analyzer.analyze();
	for (const GDScriptParser::ParserError &error : parser.get_errors()) {
		counts[vformat("%d:%d:%s", error.line, error.column, error.message)] += 1;
	}
	return counts;
}

// Analyze p_source under a single strict flag and append the diagnostics it introduces
// over the non-strict baseline to r_violations, tagged with p_category. Each strict check
// site in the analyzer is gated by exactly one of the two strict flags, so isolating one
// flag per pass attributes every strict-only diagnostic to its fix category without
// depending on the wording of the analyzer's error messages.
void collect_strict_violations_for_flag(const String &p_path, const String &p_source, bool p_strict_null, bool p_strict_dynamic, StrictViolationCategory p_category, Vector<StrictViolation> &r_violations) {
	HashMap<String, int> baseline_counts = baseline_diagnostic_counts(p_path, p_source);

	GDScriptParser parser;
	parser.parse(p_source, p_path, false);
	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_strict_null);
	analyzer.set_strict_dynamic_checks(p_strict_dynamic);
	analyzer.analyze();
	for (const GDScriptParser::ParserError &error : parser.get_errors()) {
		const String key = vformat("%d:%d:%s", error.line, error.column, error.message);
		HashMap<String, int>::Iterator baseline = baseline_counts.find(key);
		if (baseline && baseline->value > 0) {
			baseline->value -= 1;
			continue;
		}
		StrictViolation violation;
		violation.path = p_path;
		violation.line = error.line;
		violation.column = error.column;
		violation.message = error.message;
		violation.category = p_category;
		r_violations.push_back(violation);
	}
}

// Append the strict-only violations of p_source to r_violations, each tagged with the fix
// category of the strict flag that produced it. Runs one isolated pass per requested flag
// so a combined null+dynamic request still attributes every violation to its category.
void collect_strict_violations(const String &p_path, const String &p_source, const VerificationOptions &p_options, Vector<StrictViolation> &r_violations) {
	if (p_options.strict_null_checks) {
		collect_strict_violations_for_flag(p_path, p_source, true, false, StrictViolationCategory::NULLABLE, r_violations);
	}
	if (p_options.strict_dynamic_checks) {
		collect_strict_violations_for_flag(p_path, p_source, false, true, StrictViolationCategory::VARIANT_BOUNDARY, r_violations);
	}
}

} // namespace

VerificationResult GDScriptVerificationHarness::verify(
		const Vector<VerificationCandidate> &p_candidates,
		const Vector<String> &p_universe,
		const VerificationOptions &p_options) {
	VerificationResult result;

	if (p_candidates.is_empty()) {
		result.ok = true;
		return result;
	}

	// De-duplicate the universe, preserving first-seen order.
	Vector<String> universe;
	{
		HashSet<String> seen;
		for (const String &path : p_universe) {
			if (!seen.has(path)) {
				seen.insert(path);
				universe.push_back(path);
			}
		}
	}

	// The set of files the candidates touch.
	HashSet<String> touched;
	for (const VerificationCandidate &candidate : p_candidates) {
		touched.insert(candidate.path);
	}

	// Rebuild the dependency graph from disk for every universe path so inverse-dependency
	// edges are always accurate on this call, regardless of what a previous call may have
	// invalidated. Flush each path first so get_full_script re-resolves dependencies and
	// repopulates parser_inverse_dependencies rather than returning a cached script.
	for (const String &path : universe) {
		GDScriptCache::remove_parser(path);
		GDScriptCache::remove_script(path);
	}
	for (const String &path : universe) {
		Error err = OK;
		GDScriptCache::get_full_script(path, err, String(), true);
		// A load error on one path means its edges are absent, but other paths are
		// still primed correctly — the BFS will simply miss dependents of this path.
	}

	// Affected set = touched ∪ transitive inverse-dependents(touched) ∩ universe.
	HashSet<String> universe_set;
	for (const String &path : universe) {
		universe_set.insert(path);
	}
	HashSet<String> affected_set = touched;
	List<String> frontier;
	for (const String &path : touched) {
		frontier.push_back(path);
	}
	while (!frontier.is_empty()) {
		const String path = frontier.front()->get();
		frontier.pop_front();
		for (const String &dependent : GDScriptCache::get_inverse_dependencies(path)) {
			if (!universe_set.has(dependent) || affected_set.has(dependent)) {
				continue;
			}
			affected_set.insert(dependent);
			frontier.push_back(dependent);
		}
	}
	Vector<String> affected;
	for (const String &path : affected_set) {
		affected.push_back(path);
	}

	// Capture current on-disk source for every affected file.
	HashMap<String, String> original;
	for (const String &path : affected) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		original[path] = source;
	}

	// Baseline diagnostics across the affected set.
	bool fatal = false;
	const AffectedAnalysis baseline = analyze_affected(affected, original, HashMap<String, String>(), p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage or restore baseline sources; files may be left modified on disk.";
		return result;
	}
	result.baseline_error_count = baseline.error_count;

	// Identify candidates whose edits cannot be applied to the current source (overlap or
	// out-of-range). These are immediately rejected so they never silently land in accepted.
	HashSet<String> unapplicable_paths;
	stage_candidates(p_candidates, original, &unapplicable_paths);

	// Separate candidates into those that can be applied and those that cannot.
	Vector<VerificationCandidate> applicable_candidates;
	for (int i = 0; i < p_candidates.size(); i++) {
		const VerificationCandidate &candidate = p_candidates[i];
		if (unapplicable_paths.has(candidate.path)) {
			VerificationRejected early_rejected;
			early_rejected.path = candidate.path;
			early_rejected.line = candidate.line;
			early_rejected.reason = "edit could not be applied";
			result.rejected.push_back(early_rejected);
		} else {
			applicable_candidates.push_back(candidate);
		}
	}

	if (applicable_candidates.is_empty()) {
		result.accepted_error_count = baseline.error_count;
		result.ok = true;
		return result;
	}

	// Optimistic: apply all applicable candidates at once.
	const HashMap<String, String> all_staged = stage_candidates(applicable_candidates, original);
	const AffectedAnalysis combined = analyze_affected(affected, original, all_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage or restore candidate sources; files may be left modified on disk.";
		return result;
	}

	// Accept all applicable candidates when none introduces a new diagnostic key.
	// A count-only check would accept a net-zero diagnostic swap (one error removed,
	// a different one added at a different location), so the multiset difference is used.
	if (!regresses(baseline, combined)) {
		result.accepted = applicable_candidates;
		result.accepted_error_count = combined.error_count;
		result.ok = true;
		return result;
	}

	// Bound the cost of attribution. Each oracle/probe call stages sources to disk and
	// re-analyzes the entire affected set, so it is far from free. ddmin needs roughly
	// O(k log k) oracle calls to isolate offending clusters, plus one confirmation probe
	// per rejected candidate, where k is the candidate count. Above this ceiling we skip
	// delta debugging and reject the whole batch to keep verification cost bounded; the
	// common case (no regression, or small batches) keeps precise per-candidate ddmin.
	const int max_bisection_candidates = 64;
	if (applicable_candidates.size() > max_bisection_candidates) {
		ERR_PRINT(vformat(
				"Verification: %d candidates exceed the bisection ceiling of %d; rejecting the batch as a whole to bound cost.",
				applicable_candidates.size(), max_bisection_candidates));
		// New diagnostic keys = combined multiset minus baseline multiset.
		Vector<String> new_messages;
		{
			HashMap<String, int> baseline_counts;
			for (const String &message : baseline.messages) {
				baseline_counts[message] += 1;
			}
			for (const String &message : combined.messages) {
				HashMap<String, int>::Iterator it = baseline_counts.find(message);
				if (it && it->value > 0) {
					it->value -= 1;
				} else {
					new_messages.push_back(message);
				}
			}
		}
		for (const VerificationCandidate &candidate : applicable_candidates) {
			VerificationRejected rejected;
			rejected.path = candidate.path;
			rejected.line = candidate.line;
			rejected.reason = "batch exceeds bisection ceiling; rejected as a whole to bound verification cost";
			rejected.diagnostics = new_messages;
			result.rejected.push_back(rejected);
		}
		result.accepted_error_count = baseline.error_count;
		result.ok = true;
		return result;
	}

	// Regression: isolate offending candidates via delta debugging, keep the rest.
	Vector<int> remaining;
	for (int i = 0; i < applicable_candidates.size(); i++) {
		remaining.push_back(i);
	}
	HashSet<int> rejected_indices;

	// Repeatedly carve out a minimal offending subset until the remainder is clean.
	while (true) {
		const AffectedAnalysis analysis = analyze_subset(subset_of(applicable_candidates, remaining), affected, original, p_options, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted during attribution.";
			return result;
		}
		if (!regresses(baseline, analysis)) {
			break;
		}
		const Vector<int> offending = ddmin_offending(remaining, applicable_candidates, affected, original, p_options, baseline, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted during attribution.";
			return result;
		}
		HashSet<int> offending_set;
		for (int index : offending) {
			offending_set.insert(index);
			rejected_indices.insert(index);
		}
		Vector<int> next;
		for (int index : remaining) {
			if (!offending_set.has(index)) {
				next.push_back(index);
			}
		}
		// Safety: if ddmin failed to shrink (shouldn't happen with a monotonic predicate),
		// drop the whole remainder to guarantee termination.
		if (next.size() == remaining.size()) {
			for (int index : remaining) {
				rejected_indices.insert(index);
			}
			remaining.clear();
			break;
		}
		remaining = next;
	}

	// Build the accepted list and re-verify it as a final confirmation.
	Vector<VerificationCandidate> accepted_candidates;
	for (int i = 0; i < applicable_candidates.size(); i++) {
		if (!rejected_indices.has(i)) {
			accepted_candidates.push_back(applicable_candidates[i]);
		}
	}
	const HashMap<String, String> accepted_staged = stage_candidates(accepted_candidates, original);
	const AffectedAnalysis accepted_analysis = analyze_affected(affected, original, accepted_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted confirming accepted set.";
		return result;
	}
	result.accepted = accepted_candidates;
	result.accepted_error_count = accepted_analysis.error_count;

	// Attribute diagnostics to each rejected applicable candidate: new diagnostic keys
	// it alone introduces over the accepted base.
	for (int i = 0; i < applicable_candidates.size(); i++) {
		if (!rejected_indices.has(i)) {
			continue;
		}
		// Re-analyze the accepted set plus this one candidate to find the diagnostics
		// it uniquely introduces.
		Vector<VerificationCandidate> probe = accepted_candidates;
		probe.push_back(applicable_candidates[i]);
		const AffectedAnalysis probe_analysis = analyze_subset(probe, affected, original, p_options, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted attributing diagnostics; files may be left modified on disk.";
			return result;
		}
		VerificationRejected rejected;
		rejected.path = applicable_candidates[i].path;
		rejected.line = applicable_candidates[i].line;
		rejected.reason = "introduces new analyzer error(s) in the affected set";
		// Attribute only the new diagnostic keys (multiset difference).
		HashMap<String, int> accepted_counts;
		for (const String &message : accepted_analysis.messages) {
			accepted_counts[message] += 1;
		}
		for (const String &message : probe_analysis.messages) {
			HashMap<String, int>::Iterator it = accepted_counts.find(message);
			if (it && it->value > 0) {
				it->value -= 1;
			} else {
				rejected.diagnostics.push_back(message);
			}
		}
		result.rejected.push_back(rejected);
	}

	result.ok = true;
	return result;
}

StrictPreviewResult GDScriptVerificationHarness::preview_strict(
		const Vector<String> &p_paths,
		const VerificationOptions &p_options) {
	StrictPreviewResult result;
	result.strict_null_checks = p_options.strict_null_checks;
	result.strict_dynamic_checks = p_options.strict_dynamic_checks;

	HashSet<String> seen;
	for (const String &path : p_paths) {
		if (seen.has(path)) {
			continue;
		}
		seen.insert(path);
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		collect_strict_violations(path, source, p_options, result.violations);
	}
	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
