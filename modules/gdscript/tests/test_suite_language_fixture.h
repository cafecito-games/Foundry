/**************************************************************************/
/*  test_suite_language_fixture.h                                         */
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

#pragma once

#include "gdscript_test_runner.h"

#include "../gdscript_parser.h"

#include "core/string/ustring.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// Doctest listener that hoists the heavy GDScript language setup from once-per-
// `TEST_CASE` to once-per-`TEST_SUITE`.
//
// Suites whose cases call `GDScriptTests::initialize()` no longer tear the
// language down between cases. Instead `init_language()` is idempotent (it runs
// the expensive setup only the first time in a suite), and this listener:
//   * resets the per-case state (cache, global classes, annotations) after each
//     case so cases stay isolated, and
//   * finishes the language when control leaves the owning suite or the whole
//     run ends, so the next suite — including the `.gd` runner and completion
//     suites that call `init_language()` themselves — starts from a clean slate.
//
// It also isolates each GDScript case from leaked warning state. Bringing the
// language up (or running the `.gd` fixtures) enables project warnings globally,
// and some default to errors (e.g. INFERRED_DECLARATION), which makes an
// otherwise-clean analysis fail in a case that merely ran after such a suite. The
// fixture restores the warnings-suppressed baseline before every GDScript case so
// analyzer behavior is order-independent; cases that genuinely want warnings (the
// `.gd` runner) re-enable them for their own run.
//
// The listener only acts while the language is actually up, so suites that never
// initialize it (parser/tokenizer cases, non-GDScript suites) are untouched.
struct GDScriptLanguageSuiteFixture : public doctest::IReporter {
	// Name of the suite whose case last brought the language up, or empty when no
	// suite currently owns it.
	String owner_suite;
	// Suite of the case currently executing, captured at `test_case_start` because
	// `test_case_end` does not carry the test metadata.
	String current_suite;

	explicit GDScriptLanguageSuiteFixture(const doctest::ContextOptions &) {}

	void test_case_start(const doctest::TestCaseData &p_in) override {
		current_suite = String(p_in.m_test_suite);

		// A different suite is starting: tear down the language the previous suite
		// left up so this suite (or its own `init_language()` call) starts fresh.
		if (is_language_initialized() && !owner_suite.is_empty() && owner_suite != current_suite) {
			finish_language();
			owner_suite = String();
		}

#ifdef DEBUG_ENABLED
		// Restore the warnings-suppressed baseline before every GDScript case so a
		// case that analyzes a clean script is not tripped by warning-as-error state
		// a prior suite enabled globally. Cases that want warnings re-enable them.
		// (Warnings only exist in debug builds, so this is a no-op otherwise.)
		if (current_suite.contains("GDScript")) {
			GDScriptParser::set_ignoring_warnings(true);
		}
#endif // DEBUG_ENABLED
	}

	void test_case_end(const doctest::CurrentTestCaseStats &) override {
		if (!is_language_initialized()) {
			// The case either never initialized the language or tore it down itself
			// (e.g. the runner/completion suites); nothing to hoist or reset.
			return;
		}

		// First case in this suite to bring the language up claims ownership so the
		// teardown above can fire when control later leaves the suite.
		if (owner_suite.is_empty()) {
			owner_suite = current_suite;
		}

		reset_language_state();
	}

	void test_run_end(const doctest::TestRunStats &) override {
		// Tear down the language the final owning suite left up.
		if (is_language_initialized()) {
			finish_language();
			owner_suite = String();
		}
	}

	void report_query(const doctest::QueryData &) override {}
	void test_run_start() override {}
	void test_case_reenter(const doctest::TestCaseData &) override {}
	void test_case_exception(const doctest::TestCaseException &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void log_assert(const doctest::AssertData &) override {}
	void log_message(const doctest::MessageData &) override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}
};

} // namespace GDScriptTests

REGISTER_LISTENER("GDScriptLanguageSuiteFixture", 1, GDScriptTests::GDScriptLanguageSuiteFixture);
