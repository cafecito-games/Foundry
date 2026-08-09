/**************************************************************************/
/*  test_doctest_case_shard.h                                             */
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

#pragma once

#include "tests/test_case_shard.h"
#include "tests/test_macros.h"

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace TestDoctestCaseShard {

TEST_CASE("[TestCaseShard] A single shard of one is the whole registry") {
	const Vector<String> unsharded = FoundryTestCaseShard::select_case_identities(1, 1);
	REQUIRE_MESSAGE(unsharded.size() > 0, "The doctest registry should not be empty.");

	HashSet<String> unique;
	for (const String &identity : unsharded) {
		unique.insert(identity);
	}
	CHECK_MESSAGE(unique.size() == unsharded.size(), "Case identities should be unique.");
}

TEST_CASE("[TestCaseShard] Shards cover every case exactly once") {
	const Vector<String> unsharded = FoundryTestCaseShard::select_case_identities(1, 1);
	HashSet<String> expected;
	for (const String &identity : unsharded) {
		expected.insert(identity);
	}

	HashSet<String> run_everywhere;
	for (const String &identity : FoundryTestCaseShard::select_run_everywhere_case_identities()) {
		run_everywhere.insert(identity);
	}

	for (int shard_total = 2; shard_total <= 4; shard_total++) {
		HashMap<String, int> occurrences;
		for (int shard_index = 1; shard_index <= shard_total; shard_index++) {
			for (const String &identity : FoundryTestCaseShard::select_case_identities(shard_index, shard_total)) {
				occurrences[identity] = occurrences.has(identity) ? occurrences[identity] + 1 : 1;
			}
		}

		CHECK_MESSAGE(occurrences.size() == expected.size(),
				"Every registered case should appear on some shard of ", shard_total, ".");

		int omitted = 0;
		int overlapping = 0;
		for (const String &identity : expected) {
			const int count = occurrences.has(identity) ? occurrences[identity] : 0;
			// A fixture-partitioned case runs on all shards with its own 1/n slice; every
			// other case must run on exactly one.
			const int wanted = run_everywhere.has(identity) ? shard_total : 1;
			if (count == 0) {
				omitted++;
			} else if (count != wanted) {
				overlapping++;
			}
		}
		CHECK_MESSAGE(omitted == 0, "Shards of ", shard_total, " omitted ", omitted, " case(s).");
		CHECK_MESSAGE(overlapping == 0, "Shards of ", shard_total, " ran ", overlapping, " case(s) the wrong number of times.");
	}
}

TEST_CASE("[TestCaseShard] The fixture-looped suites are on the run-everywhere allowlist") {
	CHECK(FoundryTestCaseShard::runs_on_every_shard("[Modules][FoundryScript]", "Script compilation and runtime"));
	CHECK(FoundryTestCaseShard::runs_on_every_shard("[Modules][FoundryScript]", "Script compilation and runtime with compiled bytecode round-trip"));
	CHECK(FoundryTestCaseShard::runs_on_every_shard("[Modules][FoundryScript][Completion]", "[Editor] Check suggestion list"));
	CHECK_FALSE(FoundryTestCaseShard::runs_on_every_shard("[Modules][FoundryScript]", "Script compilation and runtime "));
	CHECK_FALSE(FoundryTestCaseShard::runs_on_every_shard("[Core]", "Script compilation and runtime"));
}

TEST_CASE("[TestCaseShard] count_selectable_cases is invariant under shard configuration pre-partition") {
	// `full_suite_case_count` must be identical on every shard. It is derived from
	// `count_selectable_cases()`, which counts non-skip-marked registered cases. `configure()`
	// only records the shard identity; it never touches skip marks (only `apply_to_registry()`
	// does). So, measured before the partition is applied, the count cannot depend on which
	// shard is configured. That is what makes every shard self-report the same value.
	const int baseline = FoundryTestCaseShard::count_selectable_cases();
	CHECK_MESSAGE(baseline > 0, "The doctest registry should not be empty.");

	const bool was_active = FoundryTestCaseShard::is_active();
	const int saved_index = FoundryTestCaseShard::get_shard_index();
	const int saved_total = FoundryTestCaseShard::get_shard_total();

	for (int shard_total = 2; shard_total <= 4; shard_total++) {
		for (int shard_index = 1; shard_index <= shard_total; shard_index++) {
			FoundryTestCaseShard::configure(shard_index, shard_total);
			CHECK_EQ(FoundryTestCaseShard::count_selectable_cases(), baseline);
		}
	}

	// Restore the configured shard so this case leaves global state untouched.
	if (was_active) {
		FoundryTestCaseShard::configure(saved_index, saved_total);
	} else {
		FoundryTestCaseShard::configure(-1, -1);
	}
}

} // namespace TestDoctestCaseShard
