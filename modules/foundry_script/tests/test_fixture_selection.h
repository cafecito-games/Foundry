/**************************************************************************/
/*  test_fixture_selection.h                                              */
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

#include "fs_fixture_cli.h"
#include "fs_temporary_project_tree.h"
#include "fs_test_runner.h"

#include "core/io/file_access.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"

#include <cstdlib>

namespace FSTests {

#ifdef TOOLS_ENABLED

// The whole `.fs` corpus runs inside one doctest case, so `test run --case` cannot address a
// single fixture. `foundry test fixtures` is that address: it selects fixtures by
// corpus-relative path and reports each execution as data instead of as a console line.
TEST_SUITE("[Modules][FoundryScript][FixtureSelection]") {
	TEST_CASE("A pattern without a wildcard matches a path fragment") {
		CHECK(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "await_chain" }));
		CHECK(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "features" }));
		CHECK_FALSE(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "await_chains" }));
	}

	TEST_CASE("A pattern with a wildcard is a glob over the whole relative path") {
		CHECK(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "*await*" }));
		CHECK(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "runtime/*/await_chain.fs" }));
		// A bare glob is anchored, so it does not accidentally behave like a fragment.
		CHECK_FALSE(FSTestRunner::fixture_path_matches("runtime/features/await_chain.fs", { "await*" }));
	}

	TEST_CASE("Patterns are additive and an empty list selects everything") {
		CHECK(FSTestRunner::fixture_path_matches("parser/features/enums.fs", { "await", "enums" }));
		CHECK(FSTestRunner::fixture_path_matches("parser/features/enums.fs", Vector<String>()));
	}

	TEST_CASE("A named fixture runs alone and reports its outcome as data") {
		TemporaryProjectTree tree("fixture_selection_corpus");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Selection Corpus\"\n");
		tree.write_file("runtime/selected_fixture.fs", "func test():\n\tprint(\"selected\")\n");
		tree.write_file("runtime/other_fixture.fs", "func test():\n\tprint(\"other\")\n");

		{
			FSTestRunner generator(tree.root, true, false);
			REQUIRE_MESSAGE(generator.generate_outputs(), "The scratch corpus must produce expected output.");
		}

		FSFixtureCLI::Options options;
		options.corpus_dir = tree.root;
		options.patterns.push_back("selected_fixture");
		options.passes = FSFixtureCLI::PASS_TEXT;

		int failed = -1;
		const Dictionary report = FSFixtureCLI::run(options, failed);
		CHECK_EQ(failed, 0);
		CHECK_EQ(int(report["executed"]), 1);
		CHECK_EQ(int(report["passed"]), 1);

		const Array fixtures = report["fixtures"];
		REQUIRE_EQ(fixtures.size(), 1);
		const Dictionary entry = fixtures[0];
		CHECK_EQ(String(entry["path"]), "runtime/selected_fixture.fs");
		CHECK_EQ(String(entry["pass"]), "text");
		CHECK_EQ(String(entry["status"]), "ok");
		CHECK(bool(entry["passed"]));

		const Array failures = report["failures"];
		CHECK_EQ(failures.size(), 0);
	}

	TEST_CASE("A failing fixture reports the mismatch instead of only logging it") {
		TemporaryProjectTree tree("fixture_selection_failure");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Selection Failure\"\n");
		tree.write_file("runtime/drifted_fixture.fs", "func test():\n\tprint(\"actual\")\n");
		tree.write_file("runtime/drifted_fixture.out", "GDTEST_OK\nstale\n");

		FSFixtureCLI::Options options;
		options.corpus_dir = tree.root;
		options.patterns.push_back("drifted_fixture");
		options.passes = FSFixtureCLI::PASS_TEXT;

		int failed = -1;
		const Dictionary report = FSFixtureCLI::run(options, failed);
		CHECK_EQ(failed, 1);
		CHECK_EQ(int(report["executed"]), 1);

		const Array failures = report["failures"];
		REQUIRE_EQ(failures.size(), 1);
		const Dictionary failure = failures[0];
		CHECK_EQ(String(failure["path"]), "runtime/drifted_fixture.fs");
		CHECK_FALSE(bool(failure["passed"]));
		CHECK_MESSAGE(String(failure["output"]).contains("actual"),
				"The report must carry the produced output so a regression is diffable from data.");
		CHECK_MESSAGE(String(failure["expected"]).contains("stale"),
				"The report must carry the expected output so a regression is diffable from data.");
	}

	TEST_CASE("A once-per-process fixture runs in the pass that reaches it first") {
		TemporaryProjectTree tree("fixture_selection_once_per_process");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Selection Once Per Process\"\n");
		tree.write_file("runtime/once_fixture.fs", "#once-per-process\nfunc test():\n\tprint(\"once\")\n");

		{
			FSTestRunner generator(tree.root, true, false);
			REQUIRE_MESSAGE(generator.generate_outputs(), "The scratch corpus must produce expected output.");
		}

		FSFixtureCLI::Options options;
		options.corpus_dir = tree.root;
		options.patterns.push_back("once_fixture");

		SUBCASE("Both passes let the plain pass consume the once-only diagnostics") {
			int failed = -1;
			const Dictionary report = FSFixtureCLI::run(options, failed);
			CHECK_EQ(failed, 0);
			REQUIRE_EQ(int(report["executed"]), 1);
			const Array fixtures = report["fixtures"];
			const Dictionary entry = fixtures[0];
			CHECK_EQ(String(entry["pass"]), "text");
		}

		SUBCASE("A bytecode-only run still reaches the fixture") {
			options.passes = FSFixtureCLI::PASS_BYTECODE;
			int failed = -1;
			const Dictionary report = FSFixtureCLI::run(options, failed);
			CHECK_EQ(failed, 0);
			REQUIRE_MESSAGE(int(report["executed"]) == 1,
					"Nothing consumed the once-only diagnostics in this process, so the bytecode pass owns the fixture.");
			const Array fixtures = report["fixtures"];
			const Dictionary entry = fixtures[0];
			CHECK_EQ(String(entry["pass"]), "bytecode");
		}
	}

	TEST_CASE("An unwritable report path fails the run") {
		TemporaryProjectTree tree("fixture_selection_report_path");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Selection Report Path\"\n");
		tree.write_file("runtime/reported_fixture.fs", "func test():\n\tprint(\"reported\")\n");

		{
			FSTestRunner generator(tree.root, true, false);
			REQUIRE_MESSAGE(generator.generate_outputs(), "The scratch corpus must produce expected output.");
		}

		FSFixtureCLI::Options options;
		options.corpus_dir = tree.root;
		options.patterns.push_back("reported_fixture");
		options.passes = FSFixtureCLI::PASS_TEXT;
		options.output_path = tree.root.path_join("absent_directory/report.json");

		ERR_PRINT_OFF;
		const int status = FSFixtureCLI::run_cli(options);
		ERR_PRINT_ON;
		CHECK_MESSAGE(status != EXIT_SUCCESS,
				"A passing run that could not produce its requested report must not exit successfully.");
	}

	TEST_CASE("A pattern that matches nothing executes nothing") {
		TemporaryProjectTree tree("fixture_selection_unmatched");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Selection Unmatched\"\n");
		tree.write_file("runtime/present_fixture.fs", "func test():\n\tprint(\"present\")\n");

		{
			FSTestRunner generator(tree.root, true, false);
			REQUIRE_MESSAGE(generator.generate_outputs(), "The scratch corpus must produce expected output.");
		}

		FSFixtureCLI::Options options;
		options.corpus_dir = tree.root;
		options.patterns.push_back("absent_fixture");
		options.passes = FSFixtureCLI::PASS_TEXT;

		int failed = -1;
		const Dictionary report = FSFixtureCLI::run(options, failed);
		CHECK_EQ(failed, 0);
		CHECK_MESSAGE(int(report["executed"]) == 0,
				"An unmatched pattern must run nothing, which the CLI reports as a failure rather than an empty pass.");
	}
}

#endif // TOOLS_ENABLED

} // namespace FSTests
