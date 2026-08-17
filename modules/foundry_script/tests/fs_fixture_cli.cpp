/**************************************************************************/
/*  fs_fixture_cli.cpp                                                    */
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

#include "fs_fixture_cli.h"

#include "fs_test_runner.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"
#include "core/variant/array.h"

#include <cstdlib>

namespace FSTests {

#ifdef TOOLS_ENABLED

static Dictionary outcome_to_summary(const FSTestRunner::FixtureOutcome &p_outcome) {
	Dictionary entry;
	entry["path"] = p_outcome.path;
	entry["pass"] = p_outcome.pass;
	entry["status"] = p_outcome.status;
	entry["passed"] = p_outcome.passed;
	return entry;
}

static Dictionary outcome_to_failure(const FSTestRunner::FixtureOutcome &p_outcome) {
	Dictionary entry = outcome_to_summary(p_outcome);
	// A failure has to be diffable without rerunning the corpus, which is the whole point of
	// reporting fixtures as data instead of as a console line.
	entry["output"] = p_outcome.output;
	entry["expected"] = p_outcome.expected;
	return entry;
}

// Runs one corpus pass. The runner owns the language for the pass, so each pass starts from
// the same state the matching doctest case starts from.
static void run_pass(const FSFixtureCLI::Options &p_options, bool p_binary_tokens, bool p_compiled_bytecode,
		bool p_preceded_by_another_pass, Vector<FSTestRunner::FixtureOutcome> &r_outcomes, bool &r_setup_ok) {
	FSTestRunner runner(p_options.corpus_dir, true, p_options.print_filenames, p_binary_tokens, p_compiled_bytecode);
	runner.set_fixture_filters(p_options.patterns);
	// A `#once-per-process` fixture reproduces its expected engine diagnostics only on its
	// first run in the process. When the bytecode pass runs alone, nothing consumed them yet,
	// so it must run those fixtures instead of skipping them as it does behind a plain pass.
	runner.set_once_per_process_diagnostics_consumed(p_preceded_by_another_pass);

	bool pass_setup_ok = false;
	const Vector<FSTestRunner::FixtureOutcome> outcomes = runner.run_tests_collecting(pass_setup_ok);
	if (!pass_setup_ok) {
		r_setup_ok = false;
		return;
	}
	r_outcomes.append_array(outcomes);
}

Dictionary FSFixtureCLI::run(const Options &p_options, int &r_failed_count) {
	Vector<FSTestRunner::FixtureOutcome> outcomes;
	bool setup_ok = true;

	const bool runs_text_pass = p_options.passes != PASS_BYTECODE;
	if (runs_text_pass) {
		run_pass(p_options, p_options.binary_tokens, false, false, outcomes, setup_ok);
	}
	// The bytecode round-trip is a separate pass over the same corpus, exactly as the doctest
	// suite splits it into a second case: a fixture can pass one and fail the other.
	if (setup_ok && p_options.passes != PASS_TEXT) {
		run_pass(p_options, false, true, runs_text_pass, outcomes, setup_ok);
	}

	Dictionary report;
	report["version"] = 1;
	report["command"] = "test fixtures";
	report["corpus"] = p_options.corpus_dir;

	Array patterns;
	for (const String &pattern : p_options.patterns) {
		patterns.push_back(pattern);
	}
	report["patterns"] = patterns;

	Array passes;
	if (p_options.passes != PASS_BYTECODE) {
		passes.push_back(p_options.binary_tokens ? "binary-tokens" : "text");
	}
	if (p_options.passes != PASS_TEXT) {
		passes.push_back("bytecode");
	}
	report["passes"] = passes;

	if (!setup_ok) {
		r_failed_count = -1;
		report["executed"] = 0;
		report["passed"] = 0;
		report["failed"] = 0;
		report["fixtures"] = Array();
		report["failures"] = Array();
		report["error"] = "Could not collect the fixture corpus.";
		return report;
	}

	Array fixtures;
	Array failures;
	int failed = 0;
	for (const FSTestRunner::FixtureOutcome &outcome : outcomes) {
		fixtures.push_back(outcome_to_summary(outcome));
		if (!outcome.passed) {
			failures.push_back(outcome_to_failure(outcome));
			failed++;
		}
	}

	report["executed"] = outcomes.size();
	report["passed"] = outcomes.size() - failed;
	report["failed"] = failed;
	report["fixtures"] = fixtures;
	report["failures"] = failures;

	r_failed_count = failed;
	return report;
}

bool FSFixtureCLI::report_path_is_inside_root(const String &p_report_path, const String &p_user_data_root) {
	if (p_report_path.is_empty() || p_user_data_root.is_empty()) {
		return false;
	}
	const String root = p_user_data_root.simplify_path().trim_suffix("/");
	const String report = ProjectSettings::get_singleton()->globalize_path(p_report_path).simplify_path();
	// Compared with the separator attached so a sibling directory whose name merely starts with
	// the root's name (`/scratch/user-fixtures-9` next to `/scratch/user-fixtures-91`) does not
	// look contained.
	return report == root || report.begins_with(root + "/");
}

int FSFixtureCLI::run_cli(const Options &p_options) {
	int failed = 0;
	const Dictionary report = run(p_options, failed);
	const String json = JSON::stringify(report, "\t", false, true);

	// The parseable channel is the `--output` file: it holds nothing but the report. Stdout
	// already carries the engine's startup header by the time this runs, so a stdout dump is
	// a human-readable convenience rather than an artifact to parse.
	bool wrote_output = true;
	if (p_options.output_path.is_empty()) {
		print_line(json);
	} else {
		Ref<FileAccess> file = FileAccess::open(p_options.output_path, FileAccess::WRITE);
		if (file.is_null()) {
			ERR_PRINT("Could not open fixture report output file: " + p_options.output_path);
			wrote_output = false;
		} else {
			file->store_string(json);
			// A truncated report that still exits zero would let automation accept a run it
			// cannot actually read, so the write itself is checked, not just the open.
			const Error write_error = file->get_error();
			file->close();
			if (write_error != OK) {
				ERR_PRINT(vformat("Could not write fixture report output file %s (error %d).",
						p_options.output_path, (int)write_error));
				wrote_output = false;
			}
		}
	}

	if (failed < 0) {
		ERR_PRINT("foundry test fixtures: could not collect the fixture corpus at " + p_options.corpus_dir + ".");
		return EXIT_FAILURE;
	}

	const int executed = report["executed"];
	if (executed == 0) {
		// Reporting success for a selection that matched nothing is how a broken fixture can
		// look verified, so this mirrors `test run`'s unmatched-filter failure.
		ERR_PRINT("foundry test fixtures: the pattern matched no fixtures; nothing was run.");
		return EXIT_FAILURE;
	}

	print_line(vformat("foundry test fixtures: %d executed, %d passed, %d failed.",
			executed, executed - failed, failed));

	return (failed == 0 && wrote_output) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif // TOOLS_ENABLED

} // namespace FSTests
