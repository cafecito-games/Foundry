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

// Names of the per-run user-data roots a completeness command creates under p_scratch_root. Compared
// before and after a run, because the scratch space is shared with other runs and may already hold
// roots this test did not create.
static PackedStringArray completeness_cli_user_roots(const String &p_scratch_root) {
	PackedStringArray roots;
	Ref<DirAccess> directory = DirAccess::open(p_scratch_root);
	if (directory.is_null()) {
		return roots;
	}
	directory->set_include_hidden(true);
	directory->list_dir_begin();
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry != "." && entry != ".." && entry.begins_with("user-completeness-")) {
			roots.push_back(entry);
		}
	}
	directory->list_dir_end();
	roots.sort();
	return roots;
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

	// The command runs outside any doctest context, so every helper it reaches must work without the
	// assertion machinery. Only a real subprocess proves that; an in-process call always has a context.
	TEST_CASE("TypeCompleteness CLI runs as a command without a doctest context") {
		const String executable = OS::get_singleton()->get_executable_path();
		if (executable.is_empty() || !FileAccess::exists(executable)) {
			Completeness::fs_completeness_skip("the running executable is not available to re-invoke");
			return;
		}
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		if (scratch_root.is_empty()) {
			Completeness::fs_completeness_skip("the test scratch space is unavailable");
			return;
		}
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_subprocess_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		// The child resolves its own scratch root from the environment, so it is pointed at the one
		// this process owns; otherwise it would refuse a report path below a root it does not share.
		const bool had_scratch_environment = OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH");
		const String previous_scratch_environment =
				had_scratch_environment ? OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH") : String();
		OS::get_singleton()->set_environment("FOUNDRY_TEST_SCRATCH", scratch_root);

		const PackedStringArray user_roots_before = completeness_cli_user_roots(scratch_root);

		List<String> arguments;
		arguments.push_back("--headless");
		arguments.push_back("test");
		arguments.push_back("completeness");
		arguments.push_back("run");
		arguments.push_back("--family");
		arguments.push_back(completeness_cli_family);
		arguments.push_back("--catalog");
		arguments.push_back(tracked_catalog_root());
		arguments.push_back("--scratch");
		arguments.push_back(tree.root.path_join("scratch"));
		arguments.push_back("--report");
		arguments.push_back(tree.root.path_join("scratch/report.json"));
		arguments.push_back("--tier");
		arguments.push_back("presubmit");
		String output;
		int exit_code = -1;
		const Error execute_error = OS::get_singleton()->execute(executable, arguments, &output, &exit_code, true);

		if (had_scratch_environment) {
			OS::get_singleton()->set_environment("FOUNDRY_TEST_SCRATCH", previous_scratch_environment);
		} else {
			OS::get_singleton()->unset_environment("FOUNDRY_TEST_SCRATCH");
		}

		REQUIRE_MESSAGE(execute_error == OK, output);
		CHECK_MESSAGE(exit_code == FSCompletenessCLI::EXIT_PASSED, output);
		const String report_path = tree.root.path_join("scratch/report.json");
		REQUIRE_MESSAGE(FileAccess::exists(report_path), output);
		const Dictionary report = completeness_cli_read_json(report_path);
		CHECK_EQ(String(report.get("outcome", String())), "passed");
		CHECK(report.has("timings_ms"));

		// The command owns the user-data root it creates: evidence that lives outside it means the root
		// is removed, and the report it was asked for survives that removal.
		PackedStringArray created_user_roots;
		for (const String &entry : completeness_cli_user_roots(scratch_root)) {
			if (!user_roots_before.has(entry)) {
				created_user_roots.push_back(entry);
			}
		}
		CHECK_MESSAGE(created_user_roots.is_empty(), String(", ").join(created_user_roots));
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
		SUBCASE("a scratch root outside the test scratch space") {
			FSCompletenessCLI::Options options = valid;
			options.scratch_root = "relative/scratch";
			options.report_path = "relative/scratch/report.json";
			CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
			CHECK_FALSE(FileAccess::exists(options.report_path));
		}
		SUBCASE("a report path outside the test scratch space") {
			FSCompletenessCLI::Options options = valid;
			options.report_path = tree.root.get_base_dir().path_join("escaped.json");
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
	TEST_CASE("TypeCompleteness CLI refuses a timeout whose partial report cannot be published") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cli_unwritable_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "caller report sentinel\n");
		// The atomic write needs a free temporary name beside the report; occupying every candidate is
		// how an unpublishable report is produced without touching the filesystem's permissions.
		for (int attempt = 0; attempt < 128; attempt++) {
			tree.write_file(vformat("report.json.tmp.%d.%d", OS::get_singleton()->get_process_id(), attempt),
					"occupied temp\n");
		}

		FSCompletenessCLI::Options options = completeness_cli_options(tree);
		options.timeout_seconds = 1;
		options.clock = completeness_cli_advancing_clock;
		// A timed-out run that published nothing is missing evidence, not merely over budget.
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "caller report sentinel\n");
	}

	TEST_CASE("TypeCompleteness CLI refuses an index path that resolves through a link") {
		TemporaryProjectTree tree(vformat("type_completeness_cli_link_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		TemporaryProjectTree outside(
				vformat("type_completeness_cli_outside_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(outside.is_valid());
		outside.write_file("stolen.json", "outside sentinel\n");

		const String staged_root = tree.root.path_join("catalog");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE_EQ(filesystem->copy_dir(tracked_catalog_root(), staged_root), OK);
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(
				staged_root.path_join("rules").path_join(completeness_cli_family + ".json"), &read_error);
		REQUIRE_EQ(read_error, OK);
		tree.write_file("catalog/rules/cli_linked_index_family.json",
				source.replace(vformat("\"family\": \"%s\"", completeness_cli_family),
						"\"family\": \"cli_other_family\""));
		REQUIRE_EQ(filesystem->make_dir_recursive(tree.root.path_join("scratch")), OK);
		if (filesystem->create_link(outside.root.path_join("stolen.json"),
					tree.root.path_join("scratch/report.json")) != OK) {
			Completeness::fs_completeness_skip("this filesystem cannot create the symbolic link the test needs");
			return;
		}

		FSCompletenessCLI::Options options;
		options.families.push_back(completeness_cli_family);
		options.families.push_back("cli_linked_index_family");
		options.catalog_root = staged_root;
		options.scratch_root = tree.root.path_join("scratch");
		options.report_path = tree.root.path_join("scratch/report.json");
		options.tier = "strict";
		// Both families run, so the index write is attempted; it must be refused because the index path
		// resolves through a link, and the file the link points at must be untouched.
		CHECK_EQ(FSCompletenessCLI::run(options), FSCompletenessCLI::EXIT_STRUCTURAL_FAILURE);
		CHECK_EQ(FileAccess::get_file_as_string(outside.root.path_join("stolen.json")), "outside sentinel\n");
	}

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

		// A report left behind by an earlier invocation must never be counted as this run's evidence.
		REQUIRE_EQ(filesystem->make_dir_recursive(tree.root.path_join("scratch")), OK);
		tree.write_file("scratch/report.json.cli_broken_family.json", "{\"stale\": true}\n");

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
		// The stale file is still there; it is simply not evidence, and the index says so.
		CHECK_EQ(FileAccess::get_file_as_string(vformat("%s.cli_broken_family.json", options.report_path)),
				"{\"stale\": true}\n");

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
