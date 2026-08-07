/**************************************************************************/
/*  test_doctest_case_filter.h                                            */
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

#include "tests/test_case_filter.h"
#include "tests/test_macros.h"

#include <initializer_list>

namespace TestDoctestCaseFilter {

static Vector<String> patterns(std::initializer_list<const char *> p_patterns) {
	Vector<String> result;
	for (const char *pattern : p_patterns) {
		result.push_back(String::utf8(pattern));
	}
	return result;
}

TEST_CASE("[TestCaseFilter] A value without commas is one pattern") {
	const Vector<String> split = FoundryTestCaseFilter::split_patterns("*FoundryCLI*");
	REQUIRE_EQ(split.size(), 1);
	CHECK_EQ(split[0], "*FoundryCLI*");
}

TEST_CASE("[TestCaseFilter] A comma list splits into one pattern per element") {
	const Vector<String> split = FoundryTestCaseFilter::split_patterns("*A*,*B*,*C*");
	REQUIRE_EQ(split.size(), 3);
	CHECK_EQ(split[0], "*A*");
	CHECK_EQ(split[1], "*B*");
	CHECK_EQ(split[2], "*C*");
}

TEST_CASE("[TestCaseFilter] An escaped comma stays inside its pattern") {
	const Vector<String> split = FoundryTestCaseFilter::split_patterns("*Init\\, font loading*,*Second*");
	REQUIRE_EQ(split.size(), 2);
	CHECK_EQ(split[0], "*Init, font loading*");
	CHECK_EQ(split[1], "*Second*");
}

TEST_CASE("[TestCaseFilter] An escaped backslash decodes to one literal backslash") {
	const Vector<String> split = FoundryTestCaseFilter::split_patterns("*trailing\\\\*,*Second*");
	REQUIRE_EQ(split.size(), 2);
	CHECK_EQ(split[0], "*trailing\\*");
	CHECK_EQ(split[1], "*Second*");
}

TEST_CASE("[TestCaseFilter] Empty elements contribute no pattern") {
	CHECK(FoundryTestCaseFilter::split_patterns("").is_empty());
	CHECK_EQ(FoundryTestCaseFilter::split_patterns(",,*A*,,").size(), 1);
}

TEST_CASE("[TestCaseFilter] Wildcards match the way doctest matches them") {
	CHECK(FoundryTestCaseFilter::matches_pattern("[FoundryCLIParser] Test run", "*Test run*"));
	CHECK(FoundryTestCaseFilter::matches_pattern("[FoundryCLIParser] Test run", "*"));
	CHECK(FoundryTestCaseFilter::matches_pattern("abc", "a?c"));
	CHECK(FoundryTestCaseFilter::matches_pattern("abc", "abc"));
	CHECK_FALSE(FoundryTestCaseFilter::matches_pattern("abc", "ab"));
	CHECK_FALSE(FoundryTestCaseFilter::matches_pattern("abc", "*d*"));
	// A literal comma only survives tokenization when escaped, but by the time a pattern
	// reaches the matcher it is an ordinary character.
	CHECK(FoundryTestCaseFilter::matches_pattern("Init, font loading", "*Init, font*"));
}

TEST_CASE("[TestCaseFilter] Matching ignores case like doctest's default") {
	CHECK(FoundryTestCaseFilter::matches_pattern("[Modules][FoundryScript][Completion]", "*completion*"));
	CHECK(FoundryTestCaseFilter::matches_pattern("lower", "*LOWER*"));
}

TEST_CASE("[TestCaseFilter] No pattern at all selects every case") {
	CHECK(FoundryTestCaseFilter::selects(Vector<String>(), Vector<String>(), "[Suite]", "A case"));
}

TEST_CASE("[TestCaseFilter] A case pattern matches the case name, never the suite") {
	CHECK(FoundryTestCaseFilter::selects(patterns({ "*A case*" }), Vector<String>(), "[Suite]", "A case"));
	CHECK_FALSE(FoundryTestCaseFilter::selects(patterns({ "*Suite*" }), Vector<String>(), "[Suite]", "A case"));
}

TEST_CASE("[TestCaseFilter] A suite pattern matches the suite name, never the case") {
	CHECK(FoundryTestCaseFilter::selects(Vector<String>(), patterns({ "*Suite*" }), "[Suite]", "A case"));
	CHECK_FALSE(FoundryTestCaseFilter::selects(Vector<String>(), patterns({ "*A case*" }), "[Suite]", "A case"));
}

TEST_CASE("[TestCaseFilter] Case and suite patterns select their union, not their intersection") {
	// doctest intersects its own name and suite filters, so a case matched by only one of
	// the two fields is exactly what this union has to keep.
	const Vector<String> case_patterns = patterns({ "*A case*" });
	const Vector<String> suite_patterns = patterns({ "*Other suite*" });
	CHECK(FoundryTestCaseFilter::selects(case_patterns, suite_patterns, "[Suite]", "A case"));
	CHECK(FoundryTestCaseFilter::selects(case_patterns, suite_patterns, "[Other suite]", "Unrelated case"));
	CHECK_FALSE(FoundryTestCaseFilter::selects(case_patterns, suite_patterns, "[Suite]", "Unrelated case"));
}

TEST_CASE("[TestCaseFilter] Repeated patterns of one kind select their union") {
	const Vector<String> case_patterns = patterns({ "*first*", "*second*" });
	CHECK(FoundryTestCaseFilter::selects(case_patterns, Vector<String>(), "[Suite]", "the first case"));
	CHECK(FoundryTestCaseFilter::selects(case_patterns, Vector<String>(), "[Suite]", "the second case"));
	CHECK_FALSE(FoundryTestCaseFilter::selects(case_patterns, Vector<String>(), "[Suite]", "the third case"));
}

TEST_CASE("[TestCaseFilter] Every doctest spelling of no-skip is recognized") {
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--no-skip"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("-ns"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--ns"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--dt-no-skip"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("-dt-ns"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--no-skip=true"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--no-skip=1"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--ns=YES"));
	CHECK(FoundryTestCaseFilter::is_no_skip_argument("--dt-no-skip=on"));
}

TEST_CASE("[TestCaseFilter] Arguments that leave no-skip off are not recognized") {
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("--no-skip=false"));
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("--no-skip=0"));
	// A different option that merely starts with the same characters.
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("--no-skipped-summary"));
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("--nss"));
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("--no-colors"));
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument("no-skip"));
	CHECK_FALSE(FoundryTestCaseFilter::is_no_skip_argument(""));
}

} // namespace TestDoctestCaseFilter

// Two small suites whose only job is to give `--suite` a stable target that does not depend
// on which modules are compiled in. The end-to-end `--suite` coverage in
// `test_foundry_cli_project_test.h` filters on these suite names.
TEST_SUITE("[TestCaseFilterFixture]") {
	TEST_CASE("A first filter fixture case") {
		CHECK(true);
	}

	TEST_CASE("A second filter fixture case") {
		CHECK(true);
	}
} // TEST_SUITE

TEST_SUITE("[TestCaseFilterFixtureAlternate]") {
	TEST_CASE("An alternate filter fixture case") {
		CHECK(true);
	}
} // TEST_SUITE
