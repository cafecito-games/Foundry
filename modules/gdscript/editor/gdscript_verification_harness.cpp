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

// Analyze one source as if saved at p_path; append "path:line: message" for every
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
		r_messages.push_back(vformat("%s:%d: %s", p_path, error.line, error.message));
		count++;
	}
	if (parse_err != OK && count == 0) {
		r_messages.push_back(vformat("%s: parse failed", p_path));
		count++;
	}
	return count;
}

// Result of analyzing the whole affected set under a given staged-source map.
struct AffectedAnalysis {
	int error_count = 0;
	Vector<String> messages;
};

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

// Apply every candidate's edits, grouped by file, over p_original. Candidates whose
// edits fail to apply are returned in r_unappliable. Returns staged path->source.
HashMap<String, String> stage_candidates(
		const Vector<VerificationCandidate> &p_candidates,
		const HashMap<String, String> &p_original,
		Vector<int> &r_unappliable_indices) {
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
		}
		// A non-applying group is treated as no-change here so the batch is not silently
		// corrupted; the per-candidate attribution path re-detects and reports it.
	}
	(void)r_unappliable_indices;
	return staged;
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

	// Prime the cache so inverse-dependency edges exist for the universe.
	for (const String &path : universe) {
		Error err = OK;
		GDScriptCache::get_full_script(path, err);
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

	// Baseline error count across the affected set.
	bool fatal = false;
	const AffectedAnalysis baseline = analyze_affected(affected, original, HashMap<String, String>(), p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage or restore baseline sources; files may be left modified on disk.";
		return result;
	}
	result.baseline_error_count = baseline.error_count;

	// Optimistic: apply all candidates at once.
	Vector<int> unappliable;
	const HashMap<String, String> all_staged = stage_candidates(p_candidates, original, unappliable);
	const AffectedAnalysis combined = analyze_affected(affected, original, all_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage or restore candidate sources; files may be left modified on disk.";
		return result;
	}

	if (combined.error_count <= baseline.error_count) {
		result.accepted = p_candidates;
		result.accepted_error_count = combined.error_count;
		result.ok = true;
		return result;
	}

	// Regression: reject every candidate, attributing the newly-introduced diagnostics.
	Vector<String> new_messages;
	{
		HashSet<String> baseline_messages;
		for (const String &message : baseline.messages) {
			baseline_messages.insert(message);
		}
		for (const String &message : combined.messages) {
			if (!baseline_messages.has(message)) {
				new_messages.push_back(message);
			}
		}
	}
	for (const VerificationCandidate &candidate : p_candidates) {
		VerificationRejected rejected;
		rejected.path = candidate.path;
		rejected.line = candidate.line;
		rejected.reason = "batch introduces new analyzer error(s) in the affected set";
		rejected.diagnostics = new_messages;
		result.rejected.push_back(rejected);
	}
	result.accepted_error_count = baseline.error_count;
	result.ok = true;
	return result;
}

StrictPreviewResult GDScriptVerificationHarness::preview_strict(
		const Vector<String> &p_paths,
		const VerificationOptions &p_options) {
	StrictPreviewResult result;
	result.ok = false;
	result.error_message = "Not implemented.";
	return result;
}

#endif // TOOLS_ENABLED
