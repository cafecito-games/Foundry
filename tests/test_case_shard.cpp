/**************************************************************************/
/*  test_case_shard.cpp                                                   */
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

#include "test_case_shard.h"

#include "thirdparty/doctest/doctest.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace doctest {
namespace detail {
// doctest declares its registry only inside its own implementation translation unit, but
// the definition has external linkage. Redeclaring it here lets the partition run before
// a doctest context without patching the vendored header.
std::set<TestCase> &getRegisteredTests();
} // namespace detail
} // namespace doctest

namespace {

// Full doctest names of the cases whose work is partitioned at the fixture level rather
// than here. Each entry is `{ test suite, test case name }`. A new suite that loops over a
// fixture corpus inside a single doctest case has to be added here as well; otherwise it
// is case-level partitioned and its whole corpus silently runs on one shard.
struct RunEverywhereCase {
	const char *test_suite;
	const char *name;
};

constexpr RunEverywhereCase RUN_EVERYWHERE_CASES[] = {
	{ "[Modules][FoundryScript]", "Script compilation and runtime" },
	{ "[Modules][FoundryScript]", "Script compilation and runtime with compiled bytecode round-trip" },
	{ "[Modules][FoundryScript][Completion]", "[Editor] Check suggestion list" },
};

int configured_shard_index = -1;
int configured_shard_total = -1;

bool case_order_less(const doctest::detail::TestCase *p_left, const doctest::detail::TestCase *p_right) {
	const int file_compare = std::strcmp(p_left->m_file.c_str(), p_right->m_file.c_str());
	if (file_compare != 0) {
		return file_compare < 0;
	}
	if (p_left->m_line != p_right->m_line) {
		return p_left->m_line < p_right->m_line;
	}
	return std::strcmp(p_left->m_name, p_right->m_name) < 0;
}

// Fills `r_ordered` with every registered case in the partition's stable order, and
// `r_selected` with the matching membership decision for shard `p_shard_index` of
// `p_shard_total`.
void partition_registry(int p_shard_index, int p_shard_total, Vector<const doctest::detail::TestCase *> &r_ordered, Vector<bool> &r_selected) {
	for (const doctest::detail::TestCase &test_case : doctest::detail::getRegisteredTests()) {
		r_ordered.push_back(&test_case);
	}
	if (!r_ordered.is_empty()) {
		std::sort(r_ordered.ptrw(), r_ordered.ptrw() + r_ordered.size(), case_order_less);
	}

	r_selected.resize(r_ordered.size());
	int partition_position = 0;
	for (int i = 0; i < r_ordered.size(); i++) {
		const doctest::detail::TestCase *test_case = r_ordered[i];
		if (FoundryTestCaseShard::runs_on_every_shard(test_case->m_test_suite, test_case->m_name)) {
			r_selected.write[i] = true;
			continue;
		}
		r_selected.write[i] = (partition_position % p_shard_total) == (p_shard_index - 1);
		partition_position++;
	}
}

String case_identity(const doctest::detail::TestCase *p_test_case) {
	return String(p_test_case->m_test_suite) + "|" + String(p_test_case->m_name) + "|" +
			String(p_test_case->m_file.c_str()) + ":" + itos(p_test_case->m_line);
}

} // namespace

void FoundryTestCaseShard::configure(int p_shard_index, int p_shard_total) {
	if (p_shard_total < 2 || p_shard_index < 1 || p_shard_index > p_shard_total) {
		configured_shard_index = -1;
		configured_shard_total = -1;
		return;
	}
	configured_shard_index = p_shard_index;
	configured_shard_total = p_shard_total;
}

bool FoundryTestCaseShard::is_active() {
	return configured_shard_total > 1;
}

int FoundryTestCaseShard::get_shard_index() {
	return configured_shard_index;
}

int FoundryTestCaseShard::get_shard_total() {
	return configured_shard_total;
}

bool FoundryTestCaseShard::runs_on_every_shard(const char *p_test_suite, const char *p_name) {
	const char *test_suite = p_test_suite != nullptr ? p_test_suite : "";
	const char *name = p_name != nullptr ? p_name : "";
	for (const RunEverywhereCase &entry : RUN_EVERYWHERE_CASES) {
		if (std::strcmp(entry.test_suite, test_suite) == 0 && std::strcmp(entry.name, name) == 0) {
			return true;
		}
	}
	return false;
}

Vector<String> FoundryTestCaseShard::select_case_identities(int p_shard_index, int p_shard_total) {
	Vector<String> identities;
	Vector<const doctest::detail::TestCase *> ordered;
	Vector<bool> selected;
	const int shard_total = MAX(p_shard_total, 1);
	const int shard_index = CLAMP(p_shard_index, 1, shard_total);
	partition_registry(shard_index, shard_total, ordered, selected);
	for (int i = 0; i < ordered.size(); i++) {
		if (selected[i]) {
			identities.push_back(case_identity(ordered[i]));
		}
	}
	return identities;
}

int FoundryTestCaseShard::count_selectable_cases() {
	// Counts every registered case that is not currently skip-marked. When measured after the
	// `--case`/`--suite` filter (which marks non-matches) and before the shard partition (which
	// marks out-of-shard cases), the result is identical on every shard regardless of `n`.
	int count = 0;
	for (const doctest::detail::TestCase &test_case : doctest::detail::getRegisteredTests()) {
		if (!test_case.m_skip) {
			count++;
		}
	}
	return count;
}

Vector<String> FoundryTestCaseShard::select_run_everywhere_case_identities() {
	Vector<String> identities;
	for (const doctest::detail::TestCase &test_case : doctest::detail::getRegisteredTests()) {
		if (runs_on_every_shard(test_case.m_test_suite, test_case.m_name)) {
			identities.push_back(case_identity(&test_case));
		}
	}
	return identities;
}

void FoundryTestCaseShard::apply_to_registry() {
	if (!is_active()) {
		return;
	}
	Vector<const doctest::detail::TestCase *> ordered;
	Vector<bool> selected;
	partition_registry(configured_shard_index, configured_shard_total, ordered, selected);
	for (int i = 0; i < ordered.size(); i++) {
		if (selected[i]) {
			continue;
		}
		// `m_skip` takes no part in the registry's ordering, so mutating it in place cannot
		// invalidate the set doctest iterates.
		const_cast<doctest::detail::TestCase *>(ordered[i])->m_skip = true;
	}
}
