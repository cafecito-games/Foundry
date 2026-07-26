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

#include "fs_test_runner.h"

#include "../fs_parser.h"

#include "core/string/ustring.h"

#include "tests/test_macros.h"

namespace FSTests {

// Doctest listener that hoists the heavy FoundryScript language setup from once-per-
// `TEST_CASE` to once-per-`TEST_SUITE`.
//
// Suites whose cases call `FSTests::initialize()` no longer tear the
// language down between cases. Instead `init_language()` is idempotent (it runs
// the expensive setup only the first time in a suite), and this listener:
//   * resets the per-case state (cache, global classes, annotations) after each
//     case so cases stay isolated, and
//   * finishes the language when control leaves the owning suite or the whole
//     run ends, so the next suite — including the `.fs` runner and completion
//     suites that call `init_language()` themselves — starts from a clean slate.
//
// It also isolates each FoundryScript case from leaked warning state. Bringing the
// language up (or running the `.fs` fixtures) enables project warnings globally,
// and some default to errors (e.g. INFERRED_DECLARATION), which makes an
// otherwise-clean analysis fail in a case that merely ran after such a suite. The
// fixture restores the warnings-suppressed baseline before every FoundryScript case so
// analyzer behavior is order-independent; cases that genuinely want warnings (the
// `.fs` runner) re-enable them for their own run.
//
// The listener only acts while the language is actually up (including suites that
// called `FSLanguage::init()` directly), so suites that never initialize it
// (parser/tokenizer cases, non-FoundryScript suites) are untouched.
struct FSLanguageSuiteFixture : public doctest::IReporter {
	// Name of the suite whose case last brought the language up, or empty when no
	// named suite owns it. Direct TEST_CASE declarations legitimately use an empty
	// suite name, so ownership is tracked separately below.
	String owner_suite;
	bool has_owner_suite = false;
	// Suite of the case currently executing, captured at `test_case_start` because
	// `test_case_end` does not carry the test metadata.
	String current_suite;

	explicit FSLanguageSuiteFixture(const doctest::ContextOptions &) {}

	void test_case_start(const doctest::TestCaseData &p_in) override {
		current_suite = String(p_in.m_test_suite);

		// A different suite is starting: tear down the language the previous suite
		// left up so this suite (or its own `init_language()` call) starts fresh.
		if (is_fs_language_active() && has_owner_suite && owner_suite != current_suite) {
			finish_language();
			owner_suite = String();
			has_owner_suite = false;
		}

#ifdef DEBUG_ENABLED
		// Restore the warnings-suppressed baseline before every FoundryScript case so a
		// case that analyzes a clean script is not tripped by warning-as-error state
		// a prior suite enabled globally. Cases that want warnings re-enable them.
		// (Warnings only exist in debug builds, so this is a no-op otherwise.)
		if (current_suite.contains("FoundryScript")) {
			FSParser::set_ignoring_warnings(true);
		}
#endif // DEBUG_ENABLED
	}

	void test_case_end(const doctest::CurrentTestCaseStats &) override {
		if (!is_fs_language_active() && !is_language_initialized()) {
			// The case either never initialized the language or tore it down itself
			// (e.g. the runner/completion suites); nothing to hoist or reset.
			return;
		}

		// First case in this suite to bring the language up claims ownership so the
		// teardown above can fire when control later leaves the suite.
		if (!has_owner_suite) {
			owner_suite = current_suite;
			has_owner_suite = true;
		}

		if (is_language_initialized()) {
			reset_language_state();
		}
	}

	void test_run_end(const doctest::TestRunStats &) override {
		// Tear down any language state a suite left up, including suites that called
		// `FSLanguage::init()` directly instead of `init_language()`.
		if (is_fs_language_active()) {
			finish_language();
		}
		owner_suite = String();
		has_owner_suite = false;
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

} // namespace FSTests

REGISTER_LISTENER("FSLanguageSuiteFixture", 1, FSTests::FSLanguageSuiteFixture);
