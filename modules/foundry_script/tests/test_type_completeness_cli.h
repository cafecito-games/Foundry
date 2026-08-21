/**************************************************************************/
/*  test_type_completeness_cli.h                                          */
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

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_cli.h"
#include "fs_type_completeness_common.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String completeness_cli_family = "union_destination_membership";

// Advances an hour per reading, so any deadline computed from a positive timeout is already behind
// the clock at the run's first check. The timeout is proven by the contract, not by a real stopwatch.
static uint64_t completeness_cli_advancing_clock() {
	static SafeNumeric<uint64_t> ticks;
	return ticks.add(3600ULL * 1000000ULL);
}

static void completeness_cli_inject_diagnostic(FSCompletenessObservation &r_observation) {
	r_observation.diagnostics.push_back("injected completeness CLI diagnostic");
}

static Dictionary completeness_cli_read_json(const String &p_path) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	REQUIRE_EQ(read_error, OK);
	JSON json;
	REQUIRE_EQ(json.parse(source), OK);
	REQUIRE_EQ(json.get_data().get_type(), Variant::DICTIONARY);
	return json.get_data();
}

static FSCompletenessCLI::Options completeness_cli_options(const TemporaryProjectTree &p_tree) {
	FSCompletenessCLI::Options options;
	options.families.push_back(completeness_cli_family);
	options.catalog_root = tracked_catalog_root();
	options.scratch_root = p_tree.root;
	options.report_path = p_tree.root.path_join("report.json");
	options.tier = "presubmit";
	return options;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][CLI]") {
	TEST_CASE("TypeCompleteness CLI publishes a passing family and exits zero") {
		TemporaryProjectTree tree(vformat("type_completeness_cli_pass_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const FSCompletenessCLI::Options options = completeness_cli_options(tree);
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_PASSED);

		REQUIRE(FileAccess::exists(options.report_path));
		const Dictionary report = completeness_cli_read_json(options.report_path);
		CHECK_EQ(int(report.get("schema_version", 0)), 1);
		CHECK_EQ(String(report.get("family", String())), completeness_cli_family);
		CHECK_EQ(String(report.get("outcome", String())), "passed");
		CHECK(report.has("timings_ms"));
		CHECK_EQ(String(report.get("published_surface", "missing")), "");
	}

	TEST_CASE("TypeCompleteness CLI publishes only the selected surface") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_surface_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessCLI::Options options = completeness_cli_options(tree);
		options.surface = "bytecode";
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_PASSED);

		const Dictionary report = completeness_cli_read_json(options.report_path);
		CHECK_EQ(String(report.get("published_surface", String())), "bytecode");
		const Array cases = report.get("cases", Array());
		CHECK(cases.size() > 0);
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_report = cases[index];
			const Dictionary coordinates = case_report.get("coordinates", Dictionary());
			CHECK_EQ(String(coordinates.get("surface", String())), "bytecode");
		}
		// Both surfaces still executed: parity evidence is only meaningful when both were observed.
		const Dictionary executed_by_surface = report.get("executed_by_surface", Dictionary());
		CHECK(int(executed_by_surface.get("text", 0)) > 0);
		CHECK_EQ(int(executed_by_surface.get("text", 0)), int(executed_by_surface.get("bytecode", 0)));
	}

	TEST_CASE("TypeCompleteness CLI exits one on a product mismatch") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_mismatch_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessCLI::Options options = completeness_cli_options(tree);
		options.observation_mutator = completeness_cli_inject_diagnostic;
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_PRODUCT_MISMATCH);

		const Dictionary report = completeness_cli_read_json(options.report_path);
		CHECK_EQ(String(report.get("outcome", String())), "product_mismatch");
		CHECK_EQ(bool(report.get("success", true)), false);
		CHECK_FALSE(Array(report.get("findings", Array())).is_empty());
	}

	TEST_CASE("TypeCompleteness CLI exits two on every refused invocation") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_refused_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const FSCompletenessCLI::Options valid = completeness_cli_options(tree);

		SUBCASE("a family with no rule manifest") {
			FSCompletenessCLI::Options options = valid;
			options.families.clear();
			options.families.push_back("absent_family");
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
			CHECK_FALSE(FileAccess::exists(options.report_path));
		}
		SUBCASE("a repeated family") {
			FSCompletenessCLI::Options options = valid;
			options.families.push_back(completeness_cli_family);
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
		SUBCASE("no family at all") {
			FSCompletenessCLI::Options options = valid;
			options.families.clear();
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
		SUBCASE("a missing required path") {
			FSCompletenessCLI::Options options = valid;
			options.scratch_root = String();
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
		SUBCASE("an unknown tier") {
			FSCompletenessCLI::Options options = valid;
			options.tier = "nightly";
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
		SUBCASE("an unknown surface") {
			FSCompletenessCLI::Options options = valid;
			options.surface = "both";
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
		SUBCASE("a timeout larger than the tier budget") {
			FSCompletenessCLI::Options options = valid;
			options.timeout_seconds = 181;
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
			CHECK_FALSE(FileAccess::exists(options.report_path));
		}
		SUBCASE("a malformed budgets document") {
			FSCompletenessCLI::Options options = valid;
			tree.write_file("budgets/broken.json", "{ \"schema_version\": 1 }\n");
			options.budgets_path = tree.root.path_join("budgets/broken.json");
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
			CHECK_FALSE(FileAccess::exists(options.report_path));
		}
		SUBCASE("an absent budgets document") {
			FSCompletenessCLI::Options options = valid;
			options.budgets_path = tree.root.path_join("budgets/absent.json");
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		}
	}

	TEST_CASE("TypeCompleteness CLI exits three and publishes a partial report on timeout") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_timeout_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessCLI::Options options = completeness_cli_options(tree);
		options.timeout_seconds = 1;
		options.clock = completeness_cli_advancing_clock;
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_TIMEOUT);

		REQUIRE(FileAccess::exists(options.report_path));
		const Dictionary report = completeness_cli_read_json(options.report_path);
		CHECK_EQ(String(report.get("outcome", String())), "structural_failure");
		CHECK_EQ(bool(report.get("success", true)), false);
		CHECK(Array(report.get("cases", Array())).is_empty());
		const Array structural_failures = report.get("structural_failures", Array());
		REQUIRE_EQ(structural_failures.size(), 1);
		const Dictionary failure = structural_failures[0];
		CHECK_EQ(String(failure.get("stage", String())), "run_timeout");
		CHECK(report.has("timings_ms"));
	}

	// Only the pilot family has an adapter today, so a multi-family invocation is exercised with one
	// runnable family and one that cannot run. That is also the fail-closed case that matters: an
	// index must never report a family it never published as if it had a verdict.
	TEST_CASE("TypeCompleteness CLI reports an unpublished family in its index") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_unpublished_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = tree.root.path_join("catalog");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE_EQ(filesystem->copy_dir(tracked_catalog_root(), staged_root), OK);
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(
				staged_root.path_join("rules").path_join(completeness_cli_family + ".json"), &read_error);
		REQUIRE_EQ(read_error, OK);
		// A rule manifest whose family does not match its filename stem resolves to nothing, so the
		// second family aborts before it can publish anything.
		tree.write_file("catalog/rules/cli_broken_family.json",
				source.replace(vformat("\"family\": \"%s\"", completeness_cli_family),
						"\"family\": \"cli_other_family\""));

		FSCompletenessCLI::Options options;
		options.families.push_back(completeness_cli_family);
		options.families.push_back("cli_broken_family");
		options.catalog_root = staged_root;
		options.scratch_root = tree.root.path_join("scratch");
		options.report_path = tree.root.path_join("scratch/report.json");
		options.tier = "strict";
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);

		// Each family publishes to its own report beside the index, never to the index path itself.
		const String published_report_path =
				vformat("%s.%s.json", options.report_path, completeness_cli_family);
		REQUIRE(FileAccess::exists(published_report_path));
		CHECK_EQ(String(completeness_cli_read_json(published_report_path).get("family", String())),
				completeness_cli_family);
		CHECK_FALSE(FileAccess::exists(vformat("%s.cli_broken_family.json", options.report_path)));

		const Dictionary index = completeness_cli_read_json(options.report_path);
		CHECK_EQ(int(index.get("schema_version", 0)), 1);
		CHECK_EQ(String(index.get("outcome", String())), "structural_failure");
		const Array families = index.get("families", Array());
		REQUIRE_EQ(families.size(), 2);
		bool saw_published = false;
		bool saw_unpublished = false;
		for (int position = 0; position < families.size(); position++) {
			const Dictionary entry = families[position];
			CAPTURE(String(entry.get("family", String())));
			if (String(entry.get("family", String())) == completeness_cli_family) {
				saw_published = true;
				CHECK(bool(entry.get("published", false)));
				CHECK_EQ(String(entry.get("outcome", String())), "passed");
				CHECK_EQ(String(entry.get("report_path", String())), published_report_path);
				continue;
			}
			saw_unpublished = true;
			CHECK_FALSE(bool(entry.get("published", true)));
			CHECK_EQ(String(entry.get("outcome", String())), "structural_failure");
		}
		CHECK(saw_published);
		CHECK(saw_unpublished);
	}
}

} // namespace FSTests
