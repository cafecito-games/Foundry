/**************************************************************************/
/*  test_case_filter.h                                                    */
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

// Selection behind `test run --case <pattern>` and `test run --suite <pattern>`.
//
// A registered doctest case is selected when its case name matches any `--case` pattern or
// its test suite matches any `--suite` pattern: the union of both sets. doctest's own
// `--test-case=`/`--test-suite=` filters cannot express that, because doctest intersects
// them, so the selection is decided here over the registry and no name filter is handed to
// doctest. Pattern syntax (`*`, `?`) and the comma tokenizer with backslash escapes are
// kept identical to doctest's, so a pattern behaves the same as it did when it was
// forwarded verbatim.
//
// Matching is case-insensitive, which is doctest's default; the doctest `--case-sensitive`
// passthrough option does not affect `--case`/`--suite`.
//
// Selection is applied by marking rejected cases as skipped, which is also how `--shard`
// expresses its partition. doctest's `--no-skip` makes its run loop ignore that mark, so
// the runner rejects that combination outright rather than quietly running every
// registered case under a scoped invocation.
class FoundryTestCaseFilter {
public:
	// Whether one passthrough argument turns doctest's `no_skip` on, in any spelling
	// doctest accepts for it (`--no-skip`, `-ns`, the `dt-` prefixed forms, and the
	// `=<bool>` value forms).
	static bool is_no_skip_argument(const String &p_argument);

	// Splits one CLI filter value on unescaped commas, mirroring doctest's tokenizer so a
	// single `--case "*A*,*B*"` value still means two patterns and `\,` stays literal.
	static Vector<String> split_patterns(const String &p_value);

	// doctest-compatible glob match over `*` and `?`, case-insensitive.
	static bool matches_pattern(const String &p_text, const String &p_pattern);

	// Whether a case belongs to the union the two pattern lists describe. Two empty lists
	// select everything, which is the unfiltered run.
	static bool selects(const Vector<String> &p_case_patterns, const Vector<String> &p_suite_patterns,
			const String &p_test_suite, const String &p_name);

	// Retains the patterns for the upcoming run. Each value is comma-tokenized, so repeated
	// options and comma lists compose into one flat pattern list per field.
	static void configure(const Vector<String> &p_case_values, const Vector<String> &p_suite_values);

	// Whether any pattern was configured. A run without patterns must never be reported as
	// matching nothing.
	static bool is_active();

	// Marks every registered case the filter rejects as skipped and returns how many
	// registered cases it selected. Must run before the doctest context executes.
	static int apply_to_registry();
};
