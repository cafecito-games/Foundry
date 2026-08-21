/**************************************************************************/
/*  fs_type_completeness_cli.cpp                                          */
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

#include "fs_type_completeness_cli.h"

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_common.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"

namespace FSTests {

using namespace Completeness;

namespace {

// Worst-first, so an index over several families reports the strongest verdict any of them reached.
int outcome_rank(const String &p_outcome) {
	if (p_outcome == "structural_failure") {
		return 2;
	}
	if (p_outcome == "product_mismatch") {
		return 1;
	}
	return 0;
}

String worse_outcome(const String &p_left, const String &p_right) {
	return outcome_rank(p_left) >= outcome_rank(p_right) ? p_left : p_right;
}

int exit_code_for_outcome(const String &p_outcome) {
	if (p_outcome == "structural_failure") {
		return FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE;
	}
	return p_outcome == "product_mismatch" ? FSCompletenessCLI::EXIT_PRODUCT_MISMATCH
										   : FSCompletenessCLI::EXIT_PASSED;
}

void print_failure(const String &p_message) {
	print_error(vformat("[type-completeness] %s", p_message));
}

int refuse(const String &p_message) {
	print_failure(p_message);
	return FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE;
}

uint64_t default_clock() {
	return OS::get_singleton()->get_ticks_usec();
}

String family_report_path(const String &p_report_path, const String &p_family, bool p_single_family) {
	return p_single_family ? p_report_path : vformat("%s.%s.json", p_report_path, p_family);
}

} // namespace

int FSCompletenessCLI::run(const Options &p_options, PackedStringArray *r_published_paths) {
	if (r_published_paths != nullptr) {
		r_published_paths->clear();
	}
	if (p_options.families.is_empty()) {
		return refuse("at least one --family is required.");
	}
	if (p_options.catalog_root.is_empty() || p_options.scratch_root.is_empty() ||
			p_options.report_path.is_empty()) {
		return refuse("--catalog, --scratch, and --report are all required.");
	}
	if (!p_options.surface.is_empty() && p_options.surface != "text" && p_options.surface != "bytecode") {
		return refuse(vformat("unknown --surface '%s'; expected text or bytecode.", p_options.surface));
	}
	const String budgets_path =
			p_options.budgets_path.is_empty() ? FSCompletenessBudgets::tracked_path() : p_options.budgets_path;
	FSCompletenessBudgets budgets;
	Vector<String> budget_errors;
	if (FSCompletenessBudgets::load(budgets_path, budgets, budget_errors) != OK) {
		for (const String &error : budget_errors) {
			print_failure(error);
		}
		return EXIT_STRUCTURAL_FAILURE;
	}
	const int tier_timeout_seconds = budgets.hard_timeout_seconds_for_tier(p_options.tier);
	if (tier_timeout_seconds < 0) {
		return refuse(vformat("unknown --tier '%s'; expected presubmit, strict, or scheduled.", p_options.tier));
	}
	if (p_options.timeout_seconds < 0) {
		return refuse("--timeout-seconds must be a positive number of seconds.");
	}
	if (p_options.timeout_seconds > tier_timeout_seconds) {
		return refuse(vformat("--timeout-seconds %d exceeds the %s budget of %d seconds.",
				p_options.timeout_seconds, p_options.tier, tier_timeout_seconds));
	}
	const int timeout_seconds =
			p_options.timeout_seconds == 0 ? tier_timeout_seconds : p_options.timeout_seconds;

	// The runner only writes inside the configured test scratch space. Naming that here turns an
	// otherwise opaque per-family refusal into one actionable message about the invocation.
	const String owned_scratch_root = TemporaryProjectTree::get_test_scratch_root();
	if (owned_scratch_root.is_empty()) {
		return refuse("the test scratch space is unavailable; set FOUNDRY_TEST_SCRATCH to an absolute path.");
	}
	if (!TemporaryProjectTree::is_strict_descendant(owned_scratch_root, p_options.scratch_root.simplify_path())) {
		return refuse(vformat("--scratch %s must be an absolute path below the test scratch root %s.",
				p_options.scratch_root, owned_scratch_root));
	}
	if (!TemporaryProjectTree::is_strict_descendant(owned_scratch_root, p_options.report_path.simplify_path())) {
		return refuse(vformat("--report %s must be an absolute path below the test scratch root %s.",
				p_options.report_path, owned_scratch_root));
	}

	Vector<String> families;
	for (const String &family : p_options.families) {
		if (families.has(family)) {
			return refuse(vformat("--family %s was given more than once.", family));
		}
		const String rule_path = p_options.catalog_root.path_join("rules").path_join(family + ".json");
		if (!FileAccess::exists(rule_path)) {
			return refuse(vformat("family '%s' has no rule manifest at %s.", family, rule_path));
		}
		families.push_back(family);
	}

	const FSCompletenessClock clock = p_options.clock == nullptr ? default_clock : p_options.clock;
	const uint64_t deadline_usec = clock() + uint64_t(timeout_seconds) * 1000000ULL;

	const bool single_family = families.size() == 1;
	String worst = "passed";
	bool timed_out = false;
	bool unpublished = false;
	Array family_entries;
	for (const String &family : families) {
		FSCompletenessRunOptions options;
		options.catalog_root = p_options.catalog_root;
		options.family = family;
		options.scratch_root = p_options.scratch_root;
		options.report_path = family_report_path(p_options.report_path, family, single_family);
		options.published_surface = p_options.surface;
		options.deadline_usec = deadline_usec;
		options.observation_mutator = p_options.observation_mutator;
		options.persisted_write_hook = p_options.persisted_write_hook;
		options.clock = p_options.clock;
		FSCompletenessRunResult result;
		const Error error = FSCompletenessRunner::run(options, result);
		// Publication is a property of this run, never of the destination: a report left behind by an
		// earlier invocation must not be reported as evidence this one produced. The runner fills in
		// the result's report only once the document has been written.
		const bool published = !result.report.is_empty();
		timed_out = timed_out || error == ERR_TIMEOUT;
		unpublished = unpublished || !published;
		worst = worse_outcome(worst, result.outcome);
		if (published && r_published_paths != nullptr) {
			// Recorded here, before any later family runs and before the index is attempted, so a
			// failure after this point cannot make this report look unpublished.
			r_published_paths->push_back(options.report_path);
			if (!r_published_paths->has(p_options.scratch_root)) {
				// The scratch tree holds this report and the artifacts it references; naming the tree
				// keeps them all, whatever the invocation does afterwards.
				r_published_paths->push_back(p_options.scratch_root);
			}
		}
		if (!published) {
			print_failure(vformat("family '%s' aborted before its report could be published (error %d).",
					family, error));
		}
		Dictionary entry;
		entry["family"] = family;
		entry["report_path"] = options.report_path;
		entry["outcome"] = result.outcome;
		entry["published"] = published;
		family_entries.push_back(entry);
	}
	if (unpublished) {
		worst = "structural_failure";
	}

	if (!single_family) {
		Dictionary index;
		index["schema_version"] = 1.0;
		index["families"] = family_entries;
		index["outcome"] = worst;
		const Error write_error = FSCompletenessRunner::publish_owned_document(
				p_options.scratch_root, p_options.catalog_root, p_options.report_path, index);
		if (write_error != OK) {
			return refuse(vformat("could not write the report index at %s (error %d).",
					p_options.report_path, write_error));
		}
		if (r_published_paths != nullptr) {
			r_published_paths->push_back(p_options.report_path);
		}
	}

	// One last read, after the last write this invocation performs. Each family run decides its own
	// verdict after publishing its report, but the index is written afterwards and can carry the
	// invocation past its budget on its own.
	timed_out = timed_out || clock() >= deadline_usec;
	// Missing evidence outranks every other verdict: a family that never published cannot be judged at
	// all. Otherwise a timeout gets its own exit code, so a scheduler can tell an over-budget run from
	// a broken one even though the partial report calls it a structural failure.
	if (unpublished) {
		return EXIT_STRUCTURAL_FAILURE;
	}
	return timed_out ? int(EXIT_TIMEOUT) : exit_code_for_outcome(worst);
}

} // namespace FSTests
