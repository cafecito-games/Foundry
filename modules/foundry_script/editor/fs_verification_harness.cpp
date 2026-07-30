/**************************************************************************/
/*  fs_verification_harness.cpp                                           */
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

#include "fs_verification_harness.h"

#ifdef TOOLS_ENABLED

#include "fs_refactoring.h"
#include "fs_refactoring_edits.h"

#include "../fs_analyzer.h"
#include "../fs_cache.h"
#include "../fs_parser.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
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

// Cross-call inverse-dependency index for verify(). The dependency edges of a file are a
// pure function of its source, so caching them keyed by source hash lets repeated verify()
// calls within a run reprime only the files whose content actually changed (plus the
// dependents the cache invalidation cascades through), instead of reparsing the entire
// universe on every call. This is a performance cache only: it never changes which files
// land in the affected set versus rebuilding from scratch, because any path whose source
// differs from its recorded hash (or that is unknown) is always reprimed.
//
// The index is process-static and self-correcting: a stale entry left by a previous run or
// test is only reused when both the path and its exact source match, so an entry that no
// longer reflects disk is reprimed before it can affect a result.
struct VerifyDependencyGraphCache {
	// Order-independent signature of the universe the cached edges were primed against. A path's
	// recorded forward edges are only complete relative to the universe present when it was
	// primed (edges to files outside that universe are not captured), so an entry is reusable
	// only while the universe is unchanged. When the universe differs the whole index is dropped
	// and rebuilt, which keeps results identical to a from-scratch run; within a fixpoint run the
	// universe is fixed, so the signature is constant and the optimization stays fully active.
	uint64_t universe_signature = 0;
	bool universe_signature_set = false;
	// Global dependency-resolution state the edges were primed under. Dependency edges can change
	// without any universe source change: a class_name registration (tracked by the ScriptServer
	// global-class cache version) or an autoload/project setting (tracked by the ProjectSettings
	// version) can make a consumer resolve a name to a different provider. A bump in either must
	// rebuild the index so a newly resolvable dependent is never missed.
	uint64_t global_class_version = 0;
	uint32_t project_settings_version = 0;

	// Source hash recorded the last time this path's edges were primed.
	HashMap<String, uint64_t> source_hashes;
	// Forward dependencies (within the universe) recorded for this path, used to derive the
	// inverse index. Kept so a reprime can drop a path's old edges before recording new ones.
	HashMap<String, HashSet<String>> forward_dependencies;
	// Inverse dependencies: for each path, the set of universe files that directly depend on
	// it. Maintained incrementally so it survives the cache's staged-file invalidation.
	HashMap<String, HashSet<String>> inverse_dependencies;

	void forget(const String &p_path) {
		if (HashMap<String, HashSet<String>>::Iterator forward = forward_dependencies.find(p_path)) {
			for (const String &dependency : forward->value) {
				if (HashMap<String, HashSet<String>>::Iterator inverse = inverse_dependencies.find(dependency)) {
					inverse->value.erase(p_path);
					if (inverse->value.is_empty()) {
						inverse_dependencies.erase(dependency);
					}
				}
			}
			forward_dependencies.erase(p_path);
		}
		source_hashes.erase(p_path);
	}

	void record(const String &p_path, uint64_t p_source_hash, const HashSet<String> &p_dependencies) {
		forget(p_path);
		source_hashes[p_path] = p_source_hash;
		forward_dependencies[p_path] = p_dependencies;
		for (const String &dependency : p_dependencies) {
			inverse_dependencies[dependency].insert(p_path);
		}
	}

	HashSet<String> get_inverse(const String &p_path) const {
		if (HashMap<String, HashSet<String>>::ConstIterator it = inverse_dependencies.find(p_path)) {
			return it->value;
		}
		return HashSet<String>();
	}

	void clear() {
		source_hashes.clear();
		forward_dependencies.clear();
		inverse_dependencies.clear();
	}
};

// Order-independent signature of the path set the cached edges are scoped to: the sum of each
// path's 64-bit hash. The commutative combine is insensitive to ordering, and the entries are a
// set, so two calls over the same set of paths produce the same signature. The scope is the
// universe plus any touched path that lies outside it: an in-universe consumer's edge to such an
// out-of-universe provider is only captured while that provider is in scope, so a change in the
// out-of-universe touched set must rebuild the index. In normal use every touched path is in the
// universe, so this set is just the universe and the signature is stable across a fixpoint run.
uint64_t edge_scope_signature_of(const HashSet<String> &p_scope) {
	uint64_t signature = 0;
	for (const String &path : p_scope) {
		signature += path.hash64();
	}
	return signature;
}

VerifyDependencyGraphCache &verify_dependency_graph_cache() {
	static VerifyDependencyGraphCache cache;
	return cache;
}

void invalidate_cache(const String &p_path) {
	FSCache::remove_parser(p_path);
	FSCache::remove_script(p_path);
}

// Analyze one source as if saved at p_path; append "path:line:column:message" for every
// parser/analyzer error to r_messages. Returns the count contributed by this file.
// A parse failure contributes at least one error so a malformed edit is never "clean".
int analyze_one(const String &p_path, const String &p_source, const VerificationOptions &p_options, Vector<String> &r_messages) {
	FSParser parser;
	const Error parse_err = parser.parse(p_source, p_path, false);
	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_options.strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_options.strict_dynamic_checks);
	analyzer.analyze();
	int count = 0;
	for (const FSParser::ParserError &error : parser.get_errors()) {
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

// Primes p_staged (path -> source) as in-memory source overrides for the edited files,
// invalidates affected caches so cross-file resolution re-reads the overridden buffers,
// analyzes every affected file, then clears the overrides and invalidates again so later
// runs see disk content. No disk writes occur; rollback is just clearing the overrides.
// r_fatal is retained for signature compatibility with the ddmin oracle and is never set,
// because there is no staged-write or restore step that can fail.
AffectedAnalysis analyze_affected(
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original, // current on-disk source for every affected file
		const HashMap<String, String> &p_staged, // overrides for edited files only
		const VerificationOptions &p_options,
		bool &r_fatal) {
	r_fatal = false;
	AffectedAnalysis analysis;

	// Override only files whose staged source differs from the original. Differing-only
	// keeps the invalidation set minimal so unrelated cached parsers survive.
	HashMap<String, String> overrides;
	for (const KeyValue<String, String> &entry : p_staged) {
		const String &path = entry.key;
		if (!p_original.has(path) || p_original[path] == entry.value) {
			continue;
		}
		overrides[path] = entry.value;
	}

	{
		// Install overrides; the guard clears exactly these paths when this scope exits,
		// which is the rollback. Invalidate first so dependents re-resolve against them.
		FSCacheSourceOverrideGuard override_guard(overrides);
		for (const KeyValue<String, String> &entry : overrides) {
			invalidate_cache(entry.key);
		}

		// Analyze the full affected set against the overridden in-memory state.
		for (const String &path : p_affected) {
			const String source = p_staged.has(path) ? p_staged[path] : (p_original.has(path) ? p_original[path] : String());
			analysis.error_count += analyze_one(path, source, p_options, analysis.messages);
		}
	}

	// Overrides are now cleared; invalidate again so the next analysis re-reads disk content
	// rather than reusing a stale parser/script that was built from the override.
	for (const KeyValue<String, String> &entry : overrides) {
		invalidate_cache(entry.key);
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
		if (p_original.has(entry.key) && FSRefactorEdits::apply(p_original[entry.key], entry.value, applied)) {
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

// Isolate the offending candidates within p_chunk via delta debugging and partition the
// chunk into accepted and rejected. Each rejected candidate is attributed the new diagnostic
// keys it alone introduces over the chunk's accepted base. p_chunk must be small enough to
// bound ddmin's cost (callers split larger batches into ceiling-sized chunks first). The
// returned VerificationRejected entries are not yet attributed against the global accepted
// base; callers that recombine multiple chunks re-confirm the union afterward.
void attribute_chunk(
		const Vector<VerificationCandidate> &p_chunk,
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original,
		const VerificationOptions &p_options,
		const AffectedAnalysis &p_baseline,
		Vector<VerificationCandidate> &r_accepted,
		Vector<VerificationRejected> &r_rejected,
		bool &r_fatal) {
	r_fatal = false;

	Vector<int> remaining;
	for (int i = 0; i < p_chunk.size(); i++) {
		remaining.push_back(i);
	}
	HashSet<int> rejected_indices;

	// Repeatedly carve out a minimal offending subset until the remainder is clean.
	while (true) {
		const AffectedAnalysis analysis = analyze_subset(subset_of(p_chunk, remaining), p_affected, p_original, p_options, r_fatal);
		if (r_fatal) {
			return;
		}
		if (!regresses(p_baseline, analysis)) {
			break;
		}
		const Vector<int> offending = ddmin_offending(remaining, p_chunk, p_affected, p_original, p_options, p_baseline, r_fatal);
		if (r_fatal) {
			return;
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

	// The chunk's accepted base, used both as the result and as the attribution baseline.
	Vector<VerificationCandidate> accepted_candidates;
	for (int i = 0; i < p_chunk.size(); i++) {
		if (!rejected_indices.has(i)) {
			accepted_candidates.push_back(p_chunk[i]);
		}
	}
	const AffectedAnalysis accepted_analysis = analyze_subset(accepted_candidates, p_affected, p_original, p_options, r_fatal);
	if (r_fatal) {
		return;
	}
	r_accepted.append_array(accepted_candidates);

	// Attribute diagnostics to each rejected candidate: the new diagnostic keys it alone
	// introduces over the chunk's accepted base.
	for (int i = 0; i < p_chunk.size(); i++) {
		if (!rejected_indices.has(i)) {
			continue;
		}
		Vector<VerificationCandidate> probe = accepted_candidates;
		probe.push_back(p_chunk[i]);
		const AffectedAnalysis probe_analysis = analyze_subset(probe, p_affected, p_original, p_options, r_fatal);
		if (r_fatal) {
			return;
		}
		VerificationRejected rejected;
		rejected.path = p_chunk[i].path;
		rejected.line = p_chunk[i].line;
		rejected.reason = "introduces new analyzer error(s) in the affected set";
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
		r_rejected.push_back(rejected);
	}
}

// Partition p_candidates into chunks no larger than p_ceiling, never splitting a single file's
// candidates across chunks. Candidates within a file interact (one can mask another's
// diagnostic), so per-chunk attribution is only coherent when each file is wholly inside one
// chunk. File groups are packed greedily up to the ceiling; a single file with more candidates
// than the ceiling forms one oversized chunk (ddmin still terminates, only its cost is higher).
Vector<Vector<VerificationCandidate>> partition_into_chunks(const Vector<VerificationCandidate> &p_candidates, int p_ceiling) {
	Vector<Vector<VerificationCandidate>> file_groups;
	HashMap<String, int> group_index;
	for (const VerificationCandidate &candidate : p_candidates) {
		HashMap<String, int>::ConstIterator it = group_index.find(candidate.path);
		int index;
		if (it) {
			index = it->value;
		} else {
			index = file_groups.size();
			group_index[candidate.path] = index;
			file_groups.push_back(Vector<VerificationCandidate>());
		}
		file_groups.write[index].push_back(candidate);
	}

	Vector<Vector<VerificationCandidate>> chunks;
	Vector<VerificationCandidate> current;
	for (const Vector<VerificationCandidate> &group : file_groups) {
		if (!current.is_empty() && current.size() + group.size() > p_ceiling) {
			chunks.push_back(current);
			current = Vector<VerificationCandidate>();
		}
		current.append_array(group);
	}
	if (!current.is_empty()) {
		chunks.push_back(current);
	}
	return chunks;
}

// Count the non-strict diagnostics of p_source keyed by line:column:message. The message
// is part of the key so a null-mode and a dynamic-mode diagnostic at the same position
// stay distinct. Counts (rather than set membership) so a strict pass that emits a shared
// diagnostic more times than the baseline surfaces only the extra occurrences.
HashMap<String, int> baseline_diagnostic_counts(const String &p_path, const String &p_source) {
	HashMap<String, int> counts;
	FSParser parser;
	parser.parse(p_source, p_path, false);
	FSAnalyzer analyzer(&parser);
	// Pin the baseline to non-strict regardless of the live project settings: the analyzer reads
	// the two strict flags from ProjectSettings on construction, so if a flag is already enabled
	// the baseline would itself be strict and the strict-only diff would collapse to nothing. The
	// preview is a flag-independent simulation, so its baseline must always be the non-strict one.
	analyzer.set_strict_null_checks(false);
	analyzer.set_strict_dynamic_checks(false);
	analyzer.analyze();
	for (const FSParser::ParserError &error : parser.get_errors()) {
		counts[vformat("%d:%d:%s", error.line, error.column, error.message)] += 1;
	}
	return counts;
}

// Analyze p_source under a single strict flag and append the diagnostics it introduces
// over the non-strict baseline to r_violations, tagged with p_category. Each strict check
// site in the analyzer is gated by exactly one of the two strict flags, so isolating one
// flag per pass attributes every strict-only diagnostic to its fix category without
// depending on the wording of the analyzer's error messages.
void collect_strict_violations_for_flag(const String &p_path, const String &p_source, bool p_strict_null, bool p_strict_dynamic, StrictViolationCategory p_category, const HashMap<String, int> &p_baseline_counts, Vector<StrictViolation> &r_violations) {
	HashMap<String, int> baseline_counts = p_baseline_counts; // Local copy: the diff consumes counts.

	FSParser parser;
	parser.parse(p_source, p_path, false);
	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_strict_null);
	analyzer.set_strict_dynamic_checks(p_strict_dynamic);
	analyzer.analyze();
	for (const FSParser::ParserError &error : parser.get_errors()) {
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
	if (!p_options.strict_null_checks && !p_options.strict_dynamic_checks) {
		return;
	}
	// One shared non-strict baseline drives every per-flag diff.
	const HashMap<String, int> baseline_counts = baseline_diagnostic_counts(p_path, p_source);
	if (p_options.strict_null_checks) {
		collect_strict_violations_for_flag(p_path, p_source, true, false, StrictViolationCategory::NULLABLE, baseline_counts, r_violations);
	}
	if (p_options.strict_dynamic_checks) {
		collect_strict_violations_for_flag(p_path, p_source, false, true, StrictViolationCategory::VARIANT_BOUNDARY, baseline_counts, r_violations);
	}
}

} // namespace

VerificationResult FSVerificationHarness::verify(
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

	HashSet<String> universe_set;
	for (const String &path : universe) {
		universe_set.insert(path);
	}

	// The scope the cached edges are valid for: the universe plus any touched provider outside it
	// (whose edges to in-universe consumers are captured only while it is in scope). Used both to
	// gate cache reuse and as the provider scan when recording edges, so the two always agree.
	HashSet<String> edge_scope = universe_set;
	for (const String &path : touched) {
		edge_scope.insert(path);
	}

	// Read every universe path's current on-disk source once. Reading files is cheap relative
	// to a full parse+analyze, and the source is needed both to validate the cached dependency
	// edges (by hash) and to prime the files whose content changed.
	VerifyDependencyGraphCache &graph_cache = verify_dependency_graph_cache();

	// A cached entry's recorded edges are only complete relative to the edge scope and the global
	// dependency-resolution state they were primed against, so drop the whole index when either
	// changes. The edge scope guards against a changed path set; the ScriptServer global-class
	// cache version guards against a class_name registration, and the ProjectSettings version
	// guards against an autoload or other setting changing how a consumer resolves a name, all
	// of which can shift dependency edges without any universe source change. This rebuilds every
	// path on the first call under a new key (identical to the from-scratch behavior) while
	// leaving the optimization fully active across the repeated, fixed-key calls of a fixpoint
	// run, where none of these inputs change.
	const uint64_t signature = edge_scope_signature_of(edge_scope);
	const uint64_t global_class_version = ScriptServer::get_global_class_cache_version();
	const uint32_t project_settings_version = ProjectSettings::get_singleton() ? ProjectSettings::get_singleton()->get_version() : 0;
	if (!graph_cache.universe_signature_set ||
			graph_cache.universe_signature != signature ||
			graph_cache.global_class_version != global_class_version ||
			graph_cache.project_settings_version != project_settings_version) {
		graph_cache.clear();
		graph_cache.universe_signature = signature;
		graph_cache.global_class_version = global_class_version;
		graph_cache.project_settings_version = project_settings_version;
		graph_cache.universe_signature_set = true;
	}

	HashMap<String, String> universe_source;
	HashMap<String, uint64_t> universe_hash;
	for (const String &path : universe) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			// Unreadable now: forget any stale edges so the cache cannot reuse them, and treat
			// the path as having no recorded edges (matching the from-scratch behavior where a
			// failed load simply contributes no edges).
			graph_cache.forget(path);
			continue;
		}
		universe_source[path] = source;
		universe_hash[path] = source.hash64();
	}

	// A universe path must be reprimed when its source differs from the recorded hash (or is
	// unknown). Repriming a path through FSCache invalidates its parser, which cascades
	// to every dependent and erases their cache-level inverse edges, so the dependent closure
	// of the changed set must be reprimed too. The harness-owned inverse index drives that
	// closure so the work scales with the changed set rather than the whole universe.
	HashSet<String> reprime_set;
	List<String> reprime_frontier;
	for (const String &path : universe) {
		if (!universe_source.has(path)) {
			continue;
		}
		HashMap<String, uint64_t>::ConstIterator recorded = graph_cache.source_hashes.find(path);
		if (!recorded || recorded->value != universe_hash[path]) {
			if (!reprime_set.has(path)) {
				reprime_set.insert(path);
				reprime_frontier.push_back(path);
			}
		}
	}
	while (!reprime_frontier.is_empty()) {
		const String path = reprime_frontier.front()->get();
		reprime_frontier.pop_front();
		for (const String &dependent : graph_cache.get_inverse(path)) {
			if (!universe_set.has(dependent) || reprime_set.has(dependent) || !universe_source.has(dependent)) {
				continue;
			}
			reprime_set.insert(dependent);
			reprime_frontier.push_back(dependent);
		}
	}

	// Reprime the changed set and its dependent closure: flush each first so get_full_script
	// re-resolves dependencies and repopulates the cache's inverse edges from disk content.
	for (const String &path : reprime_set) {
		FSCache::remove_parser(path);
		FSCache::remove_script(path);
	}
	for (const String &path : reprime_set) {
		Error err = OK;
		FSCache::get_full_script(path, err, String(), true);
		// A load error on one path means its edges are absent for this call; the from-scratch
		// behavior (the BFS simply misses dependents of this path) is preserved because the
		// forward-dependency capture below finds no edges for it.
	}

	// Capture each reprimed path's forward dependencies. Repriming a path records cache-level
	// inverse edges from every provider it touches to it, including unchanged providers that were
	// not themselves reprimed. The provider scan spans the universe plus the touched paths: a
	// candidate may touch a provider that is not itself in the universe, yet an in-universe
	// consumer can depend on it, and that consumer must still be discovered as affected (the
	// universe bounds which dependents are re-analyzed, not which providers can be edited). The
	// affected-set BFS below filters dependents by universe membership, so recording an edge keyed
	// on an out-of-universe provider is safe.
	{
		HashMap<String, HashSet<String>> forward_for_reprimed;
		for (const String &provider : edge_scope) {
			for (const String &dependent : FSCache::get_inverse_dependencies(provider)) {
				if (reprime_set.has(dependent)) {
					forward_for_reprimed[dependent].insert(provider);
				}
			}
		}
		for (const String &path : reprime_set) {
			HashMap<String, HashSet<String>>::ConstIterator edges = forward_for_reprimed.find(path);
			graph_cache.record(path, universe_hash[path], edges ? edges->value : HashSet<String>());
		}
	}

	// Affected set = touched ∪ transitive inverse-dependents(touched) ∩ universe, discovered
	// against the harness-owned inverse index, which now reflects every changed file's edges.
	HashSet<String> affected_set = touched;
	List<String> frontier;
	for (const String &path : touched) {
		frontier.push_back(path);
	}
	while (!frontier.is_empty()) {
		const String path = frontier.front()->get();
		frontier.pop_front();
		for (const String &dependent : graph_cache.get_inverse(path)) {
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

	// Regression. Attribution via delta debugging is bounded per chunk: each oracle/probe call
	// stages sources and re-analyzes the affected set, and ddmin needs roughly O(k log k) oracle
	// calls plus one confirmation probe per rejected candidate, where k is the chunk's candidate
	// count. To bound that cost in the common case the batch is partitioned into chunks no larger
	// than the ceiling and each chunk is attributed independently, so a large regressing batch
	// drops only its genuine offenders instead of falling back to all-or-nothing rejection.
	const int max_bisection_candidates = 64;

	Vector<VerificationCandidate> accepted_candidates;
	for (const Vector<VerificationCandidate> &chunk : partition_into_chunks(applicable_candidates, max_bisection_candidates)) {
		attribute_chunk(chunk, affected, original, p_options, baseline, accepted_candidates, result.rejected, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted during attribution.";
			return result;
		}
	}

	HashMap<String, String> accepted_staged = stage_candidates(accepted_candidates, original);
	AffectedAnalysis accepted_analysis = analyze_affected(affected, original, accepted_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted confirming accepted set.";
		return result;
	}

	// Chunks never share a file, but candidates from different files can still interact through a
	// common dependent: two provider edits each clean in isolation can together break a shared
	// consumer. File-grouped chunking can never co-locate such a pair, so re-chunking the union
	// would make no progress. When the accepted union regresses, fall back to one delta-debugging
	// pass over the whole union, which is guaranteed to isolate the cross-chunk offenders and leave
	// a clean accepted set. This raises the worst-case cost to a single O(k log k) ddmin over the
	// accepted set (k = accepted candidate count) for the rare cross-chunk case; the common case
	// stays within the per-chunk bound. This never accepts a regressing set.
	if (regresses(baseline, accepted_analysis)) {
		Vector<VerificationCandidate> reconciled;
		attribute_chunk(accepted_candidates, affected, original, p_options, baseline, reconciled, result.rejected, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted reconciling cross-chunk regressions.";
			return result;
		}
		accepted_candidates = reconciled;
		accepted_staged = stage_candidates(accepted_candidates, original);
		accepted_analysis = analyze_affected(affected, original, accepted_staged, p_options, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted confirming reconciled accepted set.";
			return result;
		}
	}

	result.accepted = accepted_candidates;
	result.accepted_error_count = accepted_analysis.error_count;

	result.ok = true;
	return result;
}

StrictPreviewResult FSVerificationHarness::preview_strict(
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
