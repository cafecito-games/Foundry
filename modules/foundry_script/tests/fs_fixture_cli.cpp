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

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/string/print_string.h"
#include "core/templates/hash_set.h"
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
		const HashSet<String> &p_fixtures_already_run, Vector<FSTestRunner::FixtureOutcome> &r_outcomes,
		bool &r_setup_ok) {
	FSTestRunner runner(p_options.corpus_dir, true, p_options.print_filenames, p_binary_tokens, p_compiled_bytecode);
	runner.set_fixture_filters(p_options.patterns);
	// A `#once-per-process` fixture reproduces its expected engine diagnostics only on its first
	// run in the process. Rather than assume a preceding pass covered the corpus, the bytecode
	// pass is told exactly which fixtures already ran, so one that no earlier pass reached (a
	// bytecode-only run, or a `.textonly.fs` fixture under `--use-binary-tokens`) still runs.
	runner.set_once_per_process_diagnostics_consumed(false);
	runner.set_fixtures_already_run(p_fixtures_already_run);

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

	HashSet<String> fixtures_already_run;
	if (p_options.passes != PASS_BYTECODE) {
		run_pass(p_options, p_options.binary_tokens, false, fixtures_already_run, outcomes, setup_ok);
		for (const FSTestRunner::FixtureOutcome &outcome : outcomes) {
			fixtures_already_run.insert(outcome.path);
		}
	}
	// The bytecode round-trip is a separate pass over the same corpus, exactly as the doctest
	// suite splits it into a second case: a fixture can pass one and fail the other.
	if (setup_ok && p_options.passes != PASS_TEXT) {
		run_pass(p_options, false, true, fixtures_already_run, outcomes, setup_ok);
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
			// A truncated report that still exits zero would let automation accept a run it
			// cannot actually read, so the write is checked, not just the open, and the closed
			// artifact is measured against what was handed to it. `close()` discards the
			// underlying flush result, so the size check is what catches a write that only
			// failed once the buffer reached the filesystem.
			const bool stored = file->store_string(json);
			file->flush();
			const Error write_error = file->get_error();
			file->close();

			const uint64_t expected_size = json.utf8().length();
			uint64_t written_size = 0;
			{
				Ref<FileAccess> written = FileAccess::open(p_options.output_path, FileAccess::READ);
				if (written.is_valid()) {
					written_size = written->get_length();
				}
			}

			if (!stored || write_error != OK || written_size != expected_size) {
				ERR_PRINT(vformat("Could not write fixture report output file %s (error %d, %d of %d bytes).",
						p_options.output_path, (int)write_error, (int)written_size, (int)expected_size));
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
