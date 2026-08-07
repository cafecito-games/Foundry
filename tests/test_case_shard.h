/**************************************************************************/
/*  test_case_shard.h                                                     */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Case-level half of the `test run --shard i/n` partition.
//
// Two layers cooperate under the single `--shard` flag. Ordinary doctest cases are
// partitioned here, so each one executes on exactly one shard. Cases that loop over a
// fixture corpus inside a single doctest case are indivisible at this level; they
// partition their own work at the fixture level instead and therefore have to execute on
// every shard. Those cases are named in the run-everywhere allowlist in
// `test_case_shard.cpp`, which is the only place to edit when a new fixture-looped suite
// is added.
//
// The partition is deterministic: the registry is sorted by `(file, line, name)` and the
// case at sorted position `k` among the non-allowlisted cases goes to shard
// `(k mod n) + 1`. Modulo rather than contiguous slicing keeps the heavy-tailed suites
// spread across shards.
//
// `--case` composes by intersection. The partition is computed over the whole registry
// and doctest applies its own name filters on top, so for any filter the union across all
// `n` shards is exactly the unsharded filtered run, with no case executed twice.
class FoundryTestCaseShard {
public:
	// Restricts this process to shard `p_shard_index` of `p_shard_total`, both 1-based.
	// A total below 2 clears the configuration and leaves the run unpartitioned.
	static void configure(int p_shard_index, int p_shard_total);
	static bool is_active();
	static int get_shard_index();
	static int get_shard_total();

	// Whether the case identified by its doctest suite and name partitions its own work at
	// the fixture level and therefore runs on every shard.
	static bool runs_on_every_shard(const char *p_test_suite, const char *p_name);

	// Stable identities ("<suite>|<name>|<file>:<line>") of the registered cases that shard
	// `p_shard_index` of `p_shard_total` executes, in partition order. Backs the
	// coverage that proves the partition covers every case and never repeats one.
	static Vector<String> select_case_identities(int p_shard_index, int p_shard_total);

	// Identities of the registered cases on the run-everywhere allowlist. These are the only
	// cases a shard partition is allowed to hand to more than one shard.
	static Vector<String> select_run_everywhere_case_identities();

	// Marks every registered case outside the configured shard as skipped, so doctest walks
	// past it exactly as it would a `doctest::skip()` case. No-op when no shard is
	// configured. Must run before the doctest context executes.
	static void apply_to_registry();
};
