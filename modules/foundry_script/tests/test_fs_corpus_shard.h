/**************************************************************************/
/*  test_fs_corpus_shard.h                                                */
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

#ifdef TOOLS_ENABLED

#include "fs_test_runner.h"
#include "test_completion.h"

#include "core/templates/hash_set.h"
#include "tests/test_macros.h"

namespace FSTests {

namespace {

Vector<String> collect_corpus_shard_keys(int p_shard_index, int p_shard_total) {
	// The language is not needed to enumerate fixtures, and leaving it down keeps this
	// coverage cheap next to the corpus runs it describes.
	FSTestRunner runner("modules/foundry_script/tests/scripts", false);
	runner.set_shard(p_shard_index, p_shard_total);
	return runner.collect_fixture_keys();
}

void check_partition_is_a_permutation(const Vector<String> &p_unsharded, const Vector<Vector<String>> &p_shards, const String &p_what) {
	HashSet<String> seen;
	int duplicates = 0;
	int total = 0;
	for (const Vector<String> &shard : p_shards) {
		total += shard.size();
		for (const String &key : shard) {
			if (seen.has(key)) {
				duplicates++;
				continue;
			}
			seen.insert(key);
		}
	}
	CHECK_MESSAGE(duplicates == 0, p_what, ": shards overlapped on ", duplicates, " unit(s).");
	CHECK_MESSAGE(total == p_unsharded.size(), p_what, ": shards ran ", total, " unit(s), expected ", p_unsharded.size(), ".");

	int missing = 0;
	for (const String &key : p_unsharded) {
		if (!seen.has(key)) {
			missing++;
		}
	}
	CHECK_MESSAGE(missing == 0, p_what, ": shards omitted ", missing, " unit(s).");
}

} // namespace

TEST_SUITE("[Modules][FoundryScript][Shard]") {
	TEST_CASE("Corpus shards partition the fixture list") {
		const Vector<String> unsharded = collect_corpus_shard_keys(-1, -1);
		REQUIRE_MESSAGE(unsharded.size() > 0, "The fixture corpus should not be empty.");

		// A single shard is the unsharded corpus, in the same order.
		const bool single_shard_matches = collect_corpus_shard_keys(1, 1) == unsharded;
		CHECK_MESSAGE(single_shard_matches, "Shard 1/1 should equal the unsharded corpus.");

		for (int shard_total = 2; shard_total <= 4; shard_total++) {
			Vector<Vector<String>> shards;
			for (int shard_index = 1; shard_index <= shard_total; shard_index++) {
				const Vector<String> shard = collect_corpus_shard_keys(shard_index, shard_total);
				// Every shard has to be a strict subset, otherwise nothing was partitioned.
				CHECK_MESSAGE(shard.size() < unsharded.size(),
						"Shard ", shard_index, "/", shard_total, " should be a strict subset of the corpus.");
				shards.push_back(shard);
			}
			check_partition_is_a_permutation(unsharded, shards, vformat("corpus with %d shards", shard_total));
		}
	}

	TEST_CASE("Completion shards partition the fixture list") {
		const String completion_dir = "modules/foundry_script/tests/scripts/completion";
		const Vector<String> unsharded = select_completion_fixtures(completion_dir, -1, -1);
		REQUIRE_MESSAGE(unsharded.size() > 0, "The completion corpus should not be empty.");

		const bool single_shard_matches = select_completion_fixtures(completion_dir, 1, 1) == unsharded;
		CHECK_MESSAGE(single_shard_matches, "Shard 1/1 should equal the unsharded completion corpus.");

		for (int shard_total = 2; shard_total <= 4; shard_total++) {
			Vector<Vector<String>> shards;
			for (int shard_index = 1; shard_index <= shard_total; shard_index++) {
				const Vector<String> shard = select_completion_fixtures(completion_dir, shard_index, shard_total);
				CHECK_MESSAGE(shard.size() < unsharded.size(),
						"Completion shard ", shard_index, "/", shard_total, " should be a strict subset of the corpus.");
				shards.push_back(shard);
			}
			check_partition_is_a_permutation(unsharded, shards, vformat("completion corpus with %d shards", shard_total));
		}
	}

	TEST_CASE("The modulo partition is deterministic and complete") {
		for (int shard_total = 1; shard_total <= 5; shard_total++) {
			for (int sorted_index = 0; sorted_index < 25; sorted_index++) {
				int selecting_shards = 0;
				for (int shard_index = 1; shard_index <= shard_total; shard_index++) {
					if (fs_test_shard_selects(sorted_index, shard_index, shard_total)) {
						selecting_shards++;
					}
				}
				CHECK_MESSAGE(selecting_shards == 1, "Unit ", sorted_index, " should belong to exactly one shard of ", shard_total, ".");
			}
		}
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
