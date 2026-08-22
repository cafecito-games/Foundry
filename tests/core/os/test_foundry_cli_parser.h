/**************************************************************************/
/*  test_foundry_cli_parser.h                                             */
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

#include "main/cli_parser.h"

#include "tests/test_macros.h"

namespace TestFoundryCLIParser {

static PackedStringArray make_args(const std::initializer_list<String> &p_args) {
	PackedStringArray args;
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	return args;
}

static bool has_arg(const PackedStringArray &p_args, const String &p_arg) {
	for (int i = 0; i < p_args.size(); i++) {
		if (p_args[i] == p_arg) {
			return true;
		}
	}
	return false;
}

using Kind = FoundryCLIParser::CLIInvocation::Kind;

static void require_kind(
		const std::initializer_list<String> &p_input,
		Kind p_expected_kind) {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(p_input));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, p_expected_kind);
}

TEST_CASE("[FoundryCLIParser] Version query accepts JSON in either option order") {
	FoundryCLIParser::ParseResult before = FoundryCLIParser::parse(make_args({
			"foundry",
			"--version",
			"--json",
	}));
	REQUIRE_MESSAGE(before.ok, before.error);
	CHECK(before.version_requested);
	CHECK(before.json);
	CHECK(before.command_path.is_empty());

	FoundryCLIParser::ParseResult after = FoundryCLIParser::parse(make_args({
			"foundry",
			"--json",
			"--version",
	}));
	REQUIRE_MESSAGE(after.ok, after.error);
	CHECK(after.version_requested);
	CHECK(after.json);
}

TEST_CASE("[FoundryCLIParser] Test completeness run collects families and required inputs") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"completeness",
			"run",
			"--family",
			"union_destination_membership",
			"--family=second_family",
			"--catalog",
			"catalog",
			"--scratch",
			"scratch",
			"--report",
			"scratch/report.json",
			"--surface",
			"text",
			"--tier",
			"presubmit",
			"--timeout-seconds",
			"90",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_COMPLETENESS_RUN);
	CHECK_EQ(result.invocation.completeness_families,
			make_args({ "union_destination_membership", "second_family" }));
	CHECK_EQ(result.invocation.completeness_catalog, "catalog");
	CHECK_EQ(result.invocation.completeness_scratch, "scratch");
	CHECK_EQ(result.invocation.completeness_report, "scratch/report.json");
	CHECK_EQ(result.invocation.completeness_surface, "text");
	CHECK_EQ(result.invocation.completeness_tier, "presubmit");
	CHECK_EQ(result.invocation.completeness_timeout_seconds, 90);
	CHECK_EQ(result.command_path, make_args({ "test", "completeness run" }));
}

TEST_CASE("[FoundryCLIParser] Test completeness select parses its inputs") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"completeness",
			"select",
			"--changed-paths",
			"changed.txt",
			"--catalog=catalog",
			"--json",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_COMPLETENESS_SELECT);
	CHECK_EQ(result.invocation.completeness_changed_paths, "changed.txt");
	CHECK_EQ(result.invocation.completeness_catalog, "catalog");
	CHECK(result.invocation.completeness_json);
	CHECK_EQ(result.command_path, make_args({ "test", "completeness select" }));
}

// Every input of the selection is named. A selection that invented a catalog, read an unnamed change
// set, or published an encoding nobody asked for would be unusable as the gate's single source.
TEST_CASE("[FoundryCLIParser] Test completeness select requires every input") {
	const Vector<PackedStringArray> incomplete = {
		make_args({ "foundry", "test", "completeness", "select", "--catalog", "catalog", "--json" }),
		make_args({ "foundry", "test", "completeness", "select", "--changed-paths", "changed.txt", "--json" }),
		make_args({ "foundry", "test", "completeness", "select", "--changed-paths", "changed.txt",
				"--catalog", "catalog" }),
	};
	for (const PackedStringArray &arguments : incomplete) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(arguments);
		CHECK_FALSE(result.ok);
	}

	FoundryCLIParser::ParseResult unknown = FoundryCLIParser::parse(make_args({ "foundry", "test", "completeness",
			"select", "--changed-paths", "changed.txt", "--catalog", "catalog", "--json", "--surface", "text" }));
	CHECK_FALSE(unknown.ok);
}

TEST_CASE("[FoundryCLIParser] Test completeness run defaults the timeout to the tier budget") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"completeness",
			"run",
			"--family",
			"union_destination_membership",
			"--catalog",
			"catalog",
			"--scratch",
			"scratch",
			"--report",
			"scratch/report.json",
			"--tier",
			"scheduled",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.completeness_timeout_seconds, 0);
	CHECK(result.invocation.completeness_surface.is_empty());
}

TEST_CASE("[FoundryCLIParser] Test completeness run names every missing required input") {
	struct MissingCase {
		const char *omitted;
		const char *expected_error;
	};
	const MissingCase cases[] = {
		{ "--family", "requires at least one --family" },
		{ "--catalog", "requires --catalog" },
		{ "--scratch", "requires --scratch" },
		{ "--report", "requires --report" },
		{ "--tier", "requires --tier" },
	};
	for (const MissingCase &missing : cases) {
		CAPTURE(missing.omitted);
		PackedStringArray args = make_args({ "foundry", "test", "completeness", "run" });
		const String omitted = missing.omitted;
		if (omitted != "--family") {
			args.push_back("--family");
			args.push_back("union_destination_membership");
		}
		if (omitted != "--catalog") {
			args.push_back("--catalog");
			args.push_back("catalog");
		}
		if (omitted != "--scratch") {
			args.push_back("--scratch");
			args.push_back("scratch");
		}
		if (omitted != "--report") {
			args.push_back("--report");
			args.push_back("scratch/report.json");
		}
		if (omitted != "--tier") {
			args.push_back("--tier");
			args.push_back("presubmit");
		}
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(args);
		CHECK_FALSE(result.ok);
		CHECK(result.error.contains(missing.expected_error));
	}
}

TEST_CASE("[FoundryCLIParser] Test completeness run rejects unusable option values") {
	const String base[] = { "--catalog", "catalog", "--scratch", "scratch", "--report", "scratch/report.json" };
	struct RejectedCase {
		const char *option;
		const char *value;
		const char *expected_error;
	};
	const RejectedCase cases[] = {
		{ "--surface", "both", "--surface expects text or bytecode" },
		{ "--tier", "nightly", "--tier expects presubmit, strict, or scheduled" },
		{ "--timeout-seconds", "0", "--timeout-seconds expects a positive number" },
		{ "--timeout-seconds", "-5", "--timeout-seconds expects a positive number" },
		{ "--timeout-seconds", "soon", "--timeout-seconds expects a positive number" },
	};
	for (const RejectedCase &rejected : cases) {
		CAPTURE(rejected.option);
		CAPTURE(rejected.value);
		PackedStringArray args = make_args({ "foundry", "test", "completeness", "run", "--family",
				"union_destination_membership", "--tier", "presubmit" });
		for (const String &argument : base) {
			args.push_back(argument);
		}
		args.push_back(rejected.option);
		args.push_back(rejected.value);
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(args);
		CHECK_FALSE(result.ok);
		CHECK(result.error.contains(rejected.expected_error));
	}
}

TEST_CASE("[FoundryCLIParser] Unknown test completeness command is rejected") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"completeness",
			"compare",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("Unknown test completeness command"));
}

TEST_CASE("[FoundryCLIParser] Version query rejects command arguments") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"--version",
			"script",
			"format",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("Version"));
}

TEST_CASE("[FoundryCLIParser] Version-like user arguments stay behind separator") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"run",
			"--project",
			"demo",
			"--",
			"--version",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_FALSE(result.version_requested);
	CHECK_EQ(result.user_args, make_args({ "--version" }));
}

TEST_CASE("[FoundryCLIParser] Project run keeps user arguments behind separator") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"--json",
			"project",
			"run",
			"--project",
			"demo",
			"--scene",
			"res://main.tscn",
			"--",
			"--difficulty",
			"hard",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.json);
	CHECK_EQ(result.command_path, make_args({ "project", "run" }));
	CHECK_EQ(result.user_args, make_args({ "--difficulty", "hard" }));
	CHECK_EQ(result.invocation.kind, Kind::PROJECT_RUN);
	CHECK_EQ(result.invocation.project_path, "demo");
	CHECK_EQ(result.invocation.scene, "res://main.tscn");
	CHECK(has_arg(result.global_args, "--no-header"));
}

TEST_CASE("[FoundryCLIParser] Script format records formatter arguments") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"format",
			"--project",
			"demo",
			"--check",
			"modules/foundry_script/tests/scripts",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::SCRIPT_FORMAT);
	CHECK_EQ(result.invocation.project_path, "demo");
	CHECK_EQ(result.invocation.command_args, make_args({ "--check", "modules/foundry_script/tests/scripts" }));
	CHECK(has_arg(result.global_args, "--headless"));
}

TEST_CASE("[FoundryCLIParser] Script lint records lint arguments") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"lint",
			"--project",
			"demo",
			"--format=sarif",
			"--out",
			"lint.sarif",
			"scripts",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::SCRIPT_LINT);
	CHECK_EQ(result.invocation.command_args, make_args({ "--format=sarif", "--out", "lint.sarif", "scripts" }));
}

TEST_CASE("[FoundryCLIParser] Script tools parse when setup argv omits the executable") {
	require_kind({ "script", "format", "--project", "demo", "--write", "scripts" }, Kind::SCRIPT_FORMAT);
	require_kind({ "script", "lint", "--project", "demo", "--format=json", "scripts" }, Kind::SCRIPT_LINT);
}

TEST_CASE("[FoundryCLIParser] Script migrate uses explicit trust and strict options") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"migrate",
			"--trusted",
			"--project",
			"demo",
			"--apply",
			"--strict",
			"null,dynamic",
			"--activate-strict",
			"--confirm",
			"--follow-up",
			"follow_up.md",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.trusted);
	CHECK_EQ(result.invocation.kind, Kind::SCRIPT_MIGRATE);
	CHECK_EQ(result.invocation.project_path, "demo");
	CHECK(result.invocation.migrate_apply);
	CHECK(result.invocation.migrate_strict_null);
	CHECK(result.invocation.migrate_strict_dynamic);
	CHECK(result.invocation.migrate_activate_strict);
	CHECK(result.invocation.migrate_confirm);
	CHECK_EQ(result.invocation.migrate_follow_up, "follow_up.md");
	CHECK(has_arg(result.global_args, "--foundry-build-trusted"));
	CHECK(has_arg(result.global_args, "--headless"));
}

TEST_CASE("[FoundryCLIParser] Script eval captures inline source and project") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"eval",
			"--project",
			"demo",
			"print(\"ok\")",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::SCRIPT_EVAL);
	CHECK_EQ(result.command_path, make_args({ "script", "eval" }));
	CHECK_EQ(result.invocation.project_path, "demo");
	CHECK_EQ(result.invocation.eval_source, "print(\"ok\")");
	CHECK(has_arg(result.global_args, "--headless"));
}

TEST_CASE("[FoundryCLIParser] Script eval forwards user arguments after separator") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"eval",
			"print(args)",
			"--",
			"--some-user-arg",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::SCRIPT_EVAL);
	CHECK_EQ(result.invocation.eval_source, "print(args)");
	CHECK_EQ(result.user_args, make_args({ "--some-user-arg" }));
}

TEST_CASE("[FoundryCLIParser] Script eval requires a source argument") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"eval",
			"--project",
			"demo",
	}));

	CHECK_FALSE(result.ok);
}

TEST_CASE("[FoundryCLIParser] Script eval rejects a second positional source") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"script",
			"eval",
			"print(1)",
			"print(2)",
	}));

	CHECK_FALSE(result.ok);
}

TEST_CASE("[FoundryCLIParser] Project export requires structured preset and output") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"export",
			"--project",
			"demo",
			"--preset",
			"Linux",
			"--output",
			"build/game.x86_64",
			"--mode",
			"release",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::PROJECT_EXPORT);
	CHECK_EQ(result.invocation.export_preset, "Linux");
	CHECK_EQ(result.invocation.export_output, "build/game.x86_64");
	CHECK_EQ(result.invocation.export_mode, "release");
}

TEST_CASE("[FoundryCLIParser] Test run records case filter") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--project",
			"demo",
			"--case",
			"*FoundryScript*",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_RUN);
	REQUIRE_EQ(result.invocation.test_cases.size(), 1);
	CHECK_EQ(result.invocation.test_cases[0], "*FoundryScript*");
	CHECK(has_arg(result.global_args, "--headless"));
}

TEST_CASE("[FoundryCLIParser] Test run has no case filters when option is absent") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--project",
			"demo",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.invocation.test_cases.is_empty());
}

TEST_CASE("[FoundryCLIParser] Test run retains every repeated case filter in order") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--case",
			"*Version query accepts JSON*",
			"--case",
			"*Test run records case filter*",
			"--case",
			"*A third disjoint pattern*",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	REQUIRE_EQ(result.invocation.test_cases.size(), 3);
	CHECK_EQ(result.invocation.test_cases[0], "*Version query accepts JSON*");
	CHECK_EQ(result.invocation.test_cases[1], "*Test run records case filter*");
	CHECK_EQ(result.invocation.test_cases[2], "*A third disjoint pattern*");
}

TEST_CASE("[FoundryCLIParser] Test run reports missing value for a repeated case filter") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--case",
			"*A*",
			"--case",
	}));

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--case"));
}

TEST_CASE("[FoundryCLIParser] Test run records suite filter") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--suite",
			"*Completion*",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_RUN);
	CHECK(result.invocation.test_cases.is_empty());
	REQUIRE_EQ(result.invocation.test_suites.size(), 1);
	CHECK_EQ(result.invocation.test_suites[0], "*Completion*");
}

TEST_CASE("[FoundryCLIParser] Test run has no suite filters when option is absent") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.invocation.test_suites.is_empty());
}

TEST_CASE("[FoundryCLIParser] Test run retains every repeated suite filter in order") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--suite",
			"*First suite*",
			"--suite",
			"*Second suite*",
			"--suite",
			"*Third suite*",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	REQUIRE_EQ(result.invocation.test_suites.size(), 3);
	CHECK_EQ(result.invocation.test_suites[0], "*First suite*");
	CHECK_EQ(result.invocation.test_suites[1], "*Second suite*");
	CHECK_EQ(result.invocation.test_suites[2], "*Third suite*");
}

TEST_CASE("[FoundryCLIParser] Test run keeps case and suite filters in separate lists") {
	// The two fields are unioned downstream, so they must stay distinguishable here: a
	// suite pattern folded into the case list would silently be matched against names.
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--case",
			"*a case*",
			"--suite",
			"*a suite*",
			"--case",
			"*another case*",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	REQUIRE_EQ(result.invocation.test_cases.size(), 2);
	CHECK_EQ(result.invocation.test_cases[0], "*a case*");
	CHECK_EQ(result.invocation.test_cases[1], "*another case*");
	REQUIRE_EQ(result.invocation.test_suites.size(), 1);
	CHECK_EQ(result.invocation.test_suites[0], "*a suite*");
}

TEST_CASE("[FoundryCLIParser] Test run reports missing value for a repeated suite filter") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--suite",
			"*A*",
			"--suite",
	}));

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--suite"));
}

TEST_CASE("[FoundryCLIParser] Test run records progress options") {
	FoundryCLIParser::ParseResult text = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--progress",
			"--case",
			"*FoundryCLIParser*",
	}));
	REQUIRE_MESSAGE(text.ok, text.error);
	CHECK(text.invocation.test_progress);
	CHECK(text.invocation.test_progress_format.is_empty());
	CHECK(text.invocation.test_progress_file.is_empty());
	CHECK_EQ(text.invocation.test_progress_heartbeat_seconds, -1);

	FoundryCLIParser::ParseResult jsonl_file = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--progress-format=jsonl",
			"--progress-file",
			"/tmp/progress.jsonl",
			"--progress-heartbeat-seconds",
			"0",
	}));
	REQUIRE_MESSAGE(jsonl_file.ok, jsonl_file.error);
	CHECK_FALSE(jsonl_file.invocation.test_progress);
	CHECK_EQ(jsonl_file.invocation.test_progress_format, "jsonl");
	CHECK_EQ(jsonl_file.invocation.test_progress_file, "/tmp/progress.jsonl");
	CHECK_EQ(jsonl_file.invocation.test_progress_heartbeat_seconds, 0);
}

TEST_CASE("[FoundryCLIParser] Test run records the shard selector") {
	FoundryCLIParser::ParseResult unsharded = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
	}));
	REQUIRE_MESSAGE(unsharded.ok, unsharded.error);
	CHECK_EQ(unsharded.invocation.test_shard_index, -1);
	CHECK_EQ(unsharded.invocation.test_shard_total, -1);

	FoundryCLIParser::ParseResult separate = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--shard",
			"2/4",
	}));
	REQUIRE_MESSAGE(separate.ok, separate.error);
	CHECK_EQ(separate.invocation.test_shard_index, 2);
	CHECK_EQ(separate.invocation.test_shard_total, 4);

	FoundryCLIParser::ParseResult inlined = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--shard=1/1",
			"--case",
			"*FoundryCLIParser*",
	}));
	REQUIRE_MESSAGE(inlined.ok, inlined.error);
	CHECK_EQ(inlined.invocation.test_shard_index, 1);
	CHECK_EQ(inlined.invocation.test_shard_total, 1);
	CHECK_EQ(inlined.invocation.test_cases, make_args({ "*FoundryCLIParser*" }));
}

TEST_CASE("[FoundryCLIParser] A malformed shard selector is a hard error") {
	// There is no fallback to an unsharded run: a bad selector must stop the process before
	// any test executes, or a typo silently drops coverage.
	for (const String selector : { "0/4", "6/5", "3/2", "x/4", "2/0", "2", "", "-1/4", "1/2/3", "2 / 4" }) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
				"foundry",
				"test",
				"run",
				"--shard",
				selector,
		}));
		const String context = "Selector '" + selector + "'.";
		CHECK_MESSAGE(!result.ok, context);
		CHECK_MESSAGE(result.error.contains("--shard"), context);
	}

	FoundryCLIParser::ParseResult missing_value = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--shard",
	}));
	CHECK_FALSE(missing_value.ok);
}

TEST_CASE("[FoundryCLIParser] Test fixture generators record paths") {
	FoundryCLIParser::ParseResult fixtures = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"generate-fixtures",
			"modules/foundry_script/tests/scripts/parser",
	}));
	REQUIRE_MESSAGE(fixtures.ok, fixtures.error);
	CHECK_EQ(fixtures.invocation.kind, Kind::TEST_GENERATE_FIXTURES);
	CHECK_EQ(fixtures.invocation.command_args, make_args({ "modules/foundry_script/tests/scripts/parser" }));

	FoundryCLIParser::ParseResult print_names = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"generate-fixtures",
			"--print-filenames",
	}));
	REQUIRE_MESSAGE(print_names.ok, print_names.error);
	CHECK(print_names.invocation.print_filenames);
	CHECK_EQ(print_names.invocation.command_args, make_args({ "modules/foundry_script/tests/scripts" }));

	FoundryCLIParser::ParseResult format_fixtures = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"generate-format-fixtures",
			"modules/foundry_script/tests/scripts/format",
	}));
	REQUIRE_MESSAGE(format_fixtures.ok, format_fixtures.error);
	CHECK_EQ(format_fixtures.invocation.kind, Kind::TEST_GENERATE_FORMAT_FIXTURES);
	CHECK_EQ(format_fixtures.invocation.command_args, make_args({ "modules/foundry_script/tests/scripts/format" }));
}

TEST_CASE("[FoundryCLIParser] Test benchmark records the corpus directory and both output paths") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
			"modules/foundry_script/tests/benchmarks/_baseline",
			"--output",
			"bench.json",
			"--profile",
			"--profile-output",
			"profile.json",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_BENCHMARK);
	CHECK_EQ(result.invocation.command_args, make_args({ "modules/foundry_script/tests/benchmarks/_baseline" }));
	CHECK_EQ(result.invocation.benchmark_output, "bench.json");
	CHECK(result.invocation.benchmark_profile);
	CHECK_EQ(result.invocation.benchmark_profile_output, "profile.json");
}

TEST_CASE("[FoundryCLIParser] Test benchmark defaults the corpus directory") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_BENCHMARK);
	CHECK_EQ(result.invocation.command_args, make_args({ "modules/foundry_script/tests/benchmarks" }));
	CHECK(result.invocation.benchmark_output.is_empty());
	CHECK_FALSE(result.invocation.benchmark_profile);
	CHECK(result.invocation.benchmark_profile_output.is_empty());
}

TEST_CASE("[FoundryCLIParser] Test benchmark profile output implies the profile pass") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
			"--profile-output=profile.json",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.invocation.benchmark_profile);
	CHECK_EQ(result.invocation.benchmark_profile_output, "profile.json");
}

TEST_CASE("[FoundryCLIParser] Test benchmark keeps an equals sign inside an inline artifact path") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
			"--output=/tmp/run=1/bench.json",
			"--profile-output=/tmp/run=1/profile.json",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.benchmark_output, "/tmp/run=1/bench.json");
	CHECK_EQ(result.invocation.benchmark_profile_output, "/tmp/run=1/profile.json");
}

TEST_CASE("[FoundryCLIParser] Test fixtures records patterns, corpus, and report options") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"fixtures",
			"trait_argument_binding",
			"*await*",
			"--dir",
			"modules/foundry_script/tests/scripts/runtime",
			"--pass",
			"text",
			"--use-binary-tokens",
			"--output",
			"fixtures.json",
			"--print-filenames",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TEST_FIXTURES);
	CHECK_EQ(result.invocation.command_args, make_args({ "trait_argument_binding", "*await*" }));
	CHECK_EQ(result.invocation.fixtures_dir, "modules/foundry_script/tests/scripts/runtime");
	CHECK_EQ(result.invocation.fixtures_pass, "text");
	CHECK(result.invocation.fixtures_binary_tokens);
	CHECK_EQ(result.invocation.fixtures_output, "fixtures.json");
	CHECK(result.invocation.print_filenames);
}

TEST_CASE("[FoundryCLIParser] Test fixtures defaults the corpus directory and runs every pass") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"fixtures",
			"await_chain",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.fixtures_dir, "modules/foundry_script/tests/scripts");
	CHECK(result.invocation.fixtures_pass.is_empty());
	CHECK_FALSE(result.invocation.fixtures_binary_tokens);
	CHECK(result.invocation.fixtures_output.is_empty());
}

TEST_CASE("[FoundryCLIParser] Test fixtures rejects an unknown pass selector") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"fixtures",
			"--pass",
			"interpreted",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--pass"));
}

TEST_CASE("[FoundryCLIParser] Test fixtures keeps an equals sign inside an inline report path") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"fixtures",
			"--output=/tmp/run=1/fixtures.json",
			"--dir=/tmp/run=1/scripts",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.fixtures_output, "/tmp/run=1/fixtures.json");
	CHECK_EQ(result.invocation.fixtures_dir, "/tmp/run=1/scripts");
}

TEST_CASE("[FoundryCLIParser] Test run keeps an equals sign inside an inline progress file path") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--progress-file=/tmp/run=1/progress.jsonl",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.test_progress_file, "/tmp/run=1/progress.jsonl");
}

TEST_CASE("[FoundryCLIParser] Test run validates the whole inline value of a constrained option") {
	// Truncating at the second separator would let these malformed values pass validation.
	FoundryCLIParser::ParseResult shard = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--shard=1/2=junk",
	}));
	CHECK_FALSE(shard.ok);
	CHECK(shard.error.contains("1/2=junk"));

	FoundryCLIParser::ParseResult format = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--progress-format=jsonl=junk",
	}));
	CHECK_FALSE(format.ok);
	CHECK(format.error.contains("jsonl=junk"));

	FoundryCLIParser::ParseResult heartbeat = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--progress-heartbeat-seconds=30=junk",
	}));
	CHECK_FALSE(heartbeat.ok);
	CHECK(heartbeat.error.contains("30=junk"));
}

TEST_CASE("[FoundryCLIParser] Editor open keeps an equals sign inside an inline workflow path") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--project",
			"/tmp/project",
			"--automation",
			"--automation-run-workflow=/tmp/run=1/workflow.json",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.automation_run_workflow, "/tmp/run=1/workflow.json");
}

TEST_CASE("[FoundryCLIParser] Editor open validates the whole inline automation transport value") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--automation-transport=mcp=junk",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("mcp=junk"));
}

TEST_CASE("[FoundryCLIParser] Test benchmark rejects a second corpus directory") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
			"first",
			"second",
	}));
	CHECK_FALSE(result.ok);
	CHECK_EQ(result.error, "test benchmark accepts at most one corpus directory.");
}

TEST_CASE("[FoundryCLIParser] Test benchmark rejects an unknown option") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"benchmark",
			"--bogus",
	}));
	CHECK_FALSE(result.ok);
	CHECK_EQ(result.error, "Unknown option for test benchmark: --bogus.");
}

TEST_CASE("[FoundryCLIParser] Removed benchmark flags report the test benchmark replacement") {
	auto expect_removed = [](const std::initializer_list<String> &p_input, const String &p_flag) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(p_input));
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.error, vformat("`%s` has been removed. Use `foundry test benchmark`.", p_flag));
		// The removal must not degrade into legacy engine argument parsing, which is
		// how the retired scanner used to receive these flags.
		CHECK(result.global_args.is_empty());
		CHECK_EQ(result.invocation.kind, Kind::NONE);
	};

	expect_removed({ "foundry", "--foundry_script-benchmark", "corpus" }, "--foundry_script-benchmark");
	expect_removed({ "foundry", "--foundry_script-benchmark-output", "bench.json" }, "--foundry_script-benchmark-output");
	expect_removed({ "foundry", "--foundry_script-benchmark-profile" }, "--foundry_script-benchmark-profile");
	expect_removed({ "foundry", "--foundry_script-benchmark-profile-output", "p.json" }, "--foundry_script-benchmark-profile-output");
}

TEST_CASE("[FoundryCLIParser] Removed benchmark flags are rejected in test run passthrough position") {
	// `test run` forwards unknown tokens to doctest, so without an explicit removal
	// clause the retired flags would keep reaching a raw argv scanner behind its back.
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--foundry_script-benchmark",
			"corpus",
	}));
	CHECK_FALSE(result.ok);
	CHECK_EQ(result.error, "`--foundry_script-benchmark` has been removed. Use `foundry test benchmark`.");
}

TEST_CASE("[FoundryCLIParser] Removed lsp serve reports the tooling serve replacement") {
	const String expected_error = "`foundry lsp serve` has been removed. Use `foundry tooling serve --project <dir> --lsp-port <port> --dap-port <port>`.";

	FoundryCLIParser::ParseResult legacy = FoundryCLIParser::parse(make_args({
			"foundry",
			"lsp",
			"serve",
			"--project",
			"demo",
			"--port",
			"6005",
	}));
	CHECK_FALSE(legacy.ok);
	CHECK_EQ(legacy.error, expected_error);
	// The removal must not degrade into legacy engine argument parsing.
	CHECK(legacy.global_args.is_empty());
	CHECK_EQ(legacy.invocation.kind, Kind::NONE);
	CHECK(legacy.invocation.project_path.is_empty());
	CHECK_FALSE(legacy.help_requested);

	// The bare noun, a help request on it, and an unknown verb all report the removal
	// rather than advertising a command that no longer exists.
	for (const std::initializer_list<String> &input : {
				 std::initializer_list<String>{ "foundry", "lsp" },
				 std::initializer_list<String>{ "foundry", "lsp", "--help" },
				 std::initializer_list<String>{ "foundry", "lsp", "bogus" },
				 std::initializer_list<String>{ "foundry", "--headless", "lsp", "serve", "--project", "demo" },
				 // The engine receives arguments without the executable, so the retired noun
				 // also has to be caught in the leading position.
				 std::initializer_list<String>{ "lsp", "serve", "--project", "demo", "--port", "6005" },
				 std::initializer_list<String>{ "lsp", "serve", "--project", "demo", "--lsp-port", "6005" },
		 }) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(input));
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.error, expected_error);
		CHECK_FALSE(result.help_requested);
	}
}

TEST_CASE("[FoundryCLIParser] Tooling serve records both ports") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--lsp-port",
			"6005",
			"--dap-port",
			"6006",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::TOOLING_SERVE);
	CHECK_EQ(result.command_path, make_args({ "tooling", "serve" }));
	CHECK_EQ(result.invocation.project_path, "demo");
	CHECK_EQ(result.invocation.lsp_port, "6005");
	CHECK_EQ(result.invocation.dap_port, "6006");
}

TEST_CASE("[FoundryCLIParser] Tooling serve defaults both ports to the host defaults") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.invocation.lsp_port.is_empty());
	CHECK(result.invocation.dap_port.is_empty());
}

TEST_CASE("[FoundryCLIParser] Tooling serve accepts ephemeral port requests") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--lsp-port",
			"0",
			"--dap-port",
			"0",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.lsp_port, "0");
	CHECK_EQ(result.invocation.dap_port, "0");
}

TEST_CASE("[FoundryCLIParser] Tooling serve rejects invalid ports") {
	auto expect_rejected = [](const String &p_value) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
				"foundry",
				"tooling",
				"serve",
				"--project",
				"demo",
				"--lsp-port",
				p_value,
		}));
		CHECK_FALSE_MESSAGE(result.ok, ("--lsp-port accepted " + p_value));
		CHECK(result.error.contains("--lsp-port"));
	};

	expect_rejected("abc");
	expect_rejected("-1");
	expect_rejected("65536");
	expect_rejected("123456");
	expect_rejected("60 05");
	expect_rejected("0x10");
	expect_rejected("6005.5");
}

TEST_CASE("[FoundryCLIParser] Tooling serve rejects identical explicit ports") {
	FoundryCLIParser::ParseResult clash = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--lsp-port",
			"7000",
			"--dap-port",
			"7000",
	}));
	CHECK_FALSE(clash.ok);
	CHECK(clash.error.contains("distinct"));

	// Two ephemeral requests are not a clash: the kernel hands out distinct ports.
	FoundryCLIParser::ParseResult ephemeral = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--lsp-port",
			"0",
			"--dap-port",
			"0",
	}));
	REQUIRE_MESSAGE(ephemeral.ok, ephemeral.error);
}

TEST_CASE("[FoundryCLIParser] Tooling serve rejects an override that hits the other default") {
	FoundryCLIParser::ParseResult lsp_on_dap_default = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--lsp-port",
			String::num_int64(FoundryCLIParser::DEFAULT_DAP_PORT),
	}));
	CHECK_FALSE(lsp_on_dap_default.ok);
	CHECK(lsp_on_dap_default.error.contains("distinct"));

	FoundryCLIParser::ParseResult dap_on_lsp_default = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--dap-port",
			String::num_int64(FoundryCLIParser::DEFAULT_LSP_PORT),
	}));
	CHECK_FALSE(dap_on_lsp_default.ok);
	CHECK(dap_on_lsp_default.error.contains("distinct"));
}

TEST_CASE("[FoundryCLIParser] Tooling serve rejects recovery mode") {
	// Recovery mode never starts the debug adapter, so the combined host could not
	// serve both advertised services.
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--recovery-mode",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--recovery-mode"));
}

TEST_CASE("[FoundryCLIParser] Tooling serve requires a project") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--project"));
}

TEST_CASE("[FoundryCLIParser] Tooling serve requests help without a project") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--help",
	}));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "tooling", "serve" }));
}

TEST_CASE("[FoundryCLIParser] Tooling serve rejects unknown options") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"tooling",
			"serve",
			"--project",
			"demo",
			"--port",
			"6005",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("Unknown option"));
}

TEST_CASE("[FoundryCLIParser] Docs and extension commands record generator options") {
	FoundryCLIParser::ParseResult api = FoundryCLIParser::parse(make_args({
			"foundry",
			"docs",
			"generate-api",
			"--include-docs",
	}));
	REQUIRE_MESSAGE(api.ok, api.error);
	CHECK_EQ(api.invocation.kind, Kind::DOCS_GENERATE_API);
	CHECK(api.invocation.docs_include_docs);

	FoundryCLIParser::ParseResult engine = FoundryCLIParser::parse(make_args({
			"foundry",
			"docs",
			"generate-engine",
			"--output",
			"doc-out",
	}));
	REQUIRE_MESSAGE(engine.ok, engine.error);
	CHECK_EQ(engine.invocation.kind, Kind::DOCS_GENERATE_ENGINE);
	CHECK_EQ(engine.invocation.docs_engine_output, "doc-out");

	FoundryCLIParser::ParseResult extension = FoundryCLIParser::parse(make_args({
			"foundry",
			"extension",
			"dump-interface",
			"--format",
			"json",
	}));
	REQUIRE_MESSAGE(extension.ok, extension.error);
	CHECK_EQ(extension.invocation.kind, Kind::EXTENSION_DUMP_INTERFACE);
	CHECK_EQ(extension.invocation.extension_interface_format, "json");
}

TEST_CASE("[FoundryCLIParser] Projectless CLI tools do not require a main scene") {
	const std::initializer_list<std::initializer_list<String>> commands = {
		{ "foundry", "script", "format", "--check", "scripts" },
		{ "foundry", "script", "lint", "scripts" },
		{ "foundry", "script", "migrate", "--trusted", "--project", "demo" },
		{ "foundry", "script", "eval", "print('ok')" },
		{ "foundry", "docs", "generate-api" },
		{ "foundry", "docs", "generate-api", "--include-docs" },
		{ "foundry", "docs", "generate-engine" },
		{ "foundry", "docs", "generate-script", "--source", "addons/library" },
		{ "foundry", "extension", "dump-interface" },
		{ "foundry", "extension", "dump-interface", "--format", "json" },
		{ "foundry", "extension", "validate-api", "--input", "extension_api.json" },
	};

	for (const std::initializer_list<String> &command : commands) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(command));
		REQUIRE_MESSAGE(result.ok, result.error);
		CHECK(FoundryCLIParser::can_run_without_main_scene(result.invocation));
	}
}

TEST_CASE("[FoundryCLIParser] Diagnostics commands map to render device probes") {
	require_kind({ "foundry", "diagnostics", "render-device-support" }, Kind::DIAGNOSTICS_RENDER_DEVICE_SUPPORT);
	require_kind({ "foundry", "diagnostics", "render-device-create" }, Kind::DIAGNOSTICS_RENDER_DEVICE_CREATE);
}

TEST_CASE("[FoundryCLIParser] Project test records runner and user args") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"--json",
			"project",
			"test",
			"--project",
			"demo",
			"--runner",
			"res://run.fs",
			"--",
			"--filter",
			"x",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.json);
	CHECK_EQ(result.command_path, make_args({ "project", "test" }));
	CHECK_EQ(result.user_args, make_args({ "--filter", "x" }));
	CHECK_EQ(result.invocation.kind, Kind::PROJECT_TEST);
	CHECK_EQ(result.invocation.runner, "res://run.fs");
}

TEST_CASE("[FoundryCLIParser] Project test requires runner") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"test",
			"--project",
			"demo",
	}));

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--runner"));
	CHECK_EQ(result.command_path, make_args({ "project", "test" }));
}

TEST_CASE("[FoundryCLIParser] Missing required command arguments are rejected") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"export",
			"--project",
			"demo",
			"--preset",
			"Linux",
	}));

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--output"));
}

TEST_CASE("[FoundryCLIParser] Help flag at top level is detected") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK(result.command_path.is_empty());
}

TEST_CASE("[FoundryCLIParser] Help flag scoped to a noun") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Help flag scoped to a command") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Help flag skips required option validation") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "migrate", "--apply", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "migrate" }));
}

TEST_CASE("[FoundryCLIParser] Short and DOS help flags are recognized") {
	for (const String &flag : { String("-h"), String("/?") }) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "project", flag }));
		REQUIRE_MESSAGE(result.ok, result.error);
		CHECK(result.help_requested);
		CHECK_EQ(result.command_path, make_args({ "project" }));
	}
}

TEST_CASE("[FoundryCLIParser] Help alias routes scope") {
	FoundryCLIParser::ParseResult top = FoundryCLIParser::parse(make_args({ "foundry", "help" }));
	REQUIRE_MESSAGE(top.ok, top.error);
	CHECK(top.help_requested);
	CHECK(top.command_path.is_empty());

	FoundryCLIParser::ParseResult noun = FoundryCLIParser::parse(make_args({ "foundry", "help", "script" }));
	REQUIRE_MESSAGE(noun.ok, noun.error);
	CHECK(noun.help_requested);
	CHECK_EQ(noun.command_path, make_args({ "script" }));

	FoundryCLIParser::ParseResult verb = FoundryCLIParser::parse(make_args({ "foundry", "help", "script", "format" }));
	REQUIRE_MESSAGE(verb.ok, verb.error);
	CHECK(verb.help_requested);
	CHECK_EQ(verb.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] JSON global option combines with help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--json", "script", "format", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.json);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Bare noun error records noun scope") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script" }));
	CHECK_FALSE(result.ok);
	CHECK_FALSE(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Unknown verb error keeps noun scope") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "fmt" }));
	CHECK_FALSE(result.ok);
	CHECK_EQ(result.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Help flag as an option value requests help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "project", "export", "--preset", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK_EQ(result.command_path, make_args({ "project", "export" }));

	FoundryCLIParser::ParseResult global_value = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--project", "--help" }));
	REQUIRE_MESSAGE(global_value.ok, global_value.error);
	CHECK(global_value.help_requested);
	CHECK_EQ(global_value.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Unknown verb scope is the noun for every dispatcher") {
	for (const String &noun : { String("editor"), String("tooling"), String("docs"), String("extension"), String("diagnostics") }) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", noun, "bogus" }));
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.command_path, make_args({ noun }));
	}
}

TEST_CASE("[FoundryCLIParser] Help flag after user argument separator stays a user argument") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "project", "run", "--project", "demo", "--", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_FALSE(result.help_requested);
	CHECK_EQ(result.user_args, make_args({ "--help" }));
}

TEST_CASE("[FoundryCLIParser] Help flag after global option stays non-help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--fullscreen", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_FALSE(result.help_requested);
	CHECK_FALSE(result.used_new_cli);
}

TEST_CASE("[FoundryCLIParser] No-header global option is reported for help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "--no-header", "script", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK(result.no_header);
}

TEST_CASE("[FoundryCLIParser] No-header after the verb is reported for help") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--no-header", "--help" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK(result.no_header);
}

TEST_CASE("[FoundryCLIParser] Help alias accepts trailing global flags") {
	FoundryCLIParser::ParseResult top = FoundryCLIParser::parse(make_args({ "foundry", "help", "--json" }));
	REQUIRE_MESSAGE(top.ok, top.error);
	CHECK(top.help_requested);
	CHECK(top.json);
	CHECK(top.command_path.is_empty());

	FoundryCLIParser::ParseResult scoped = FoundryCLIParser::parse(make_args({ "foundry", "help", "script", "--json" }));
	REQUIRE_MESSAGE(scoped.ok, scoped.error);
	CHECK(scoped.help_requested);
	CHECK(scoped.json);
	CHECK_EQ(scoped.command_path, make_args({ "script" }));
}

TEST_CASE("[FoundryCLIParser] Help flag accepts trailing global flags") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--help", "--json" }));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.help_requested);
	CHECK(result.json);
	CHECK_EQ(result.command_path, make_args({ "script", "format" }));

	FoundryCLIParser::ParseResult no_header = FoundryCLIParser::parse(make_args({ "foundry", "script", "format", "--help", "--no-header" }));
	REQUIRE_MESSAGE(no_header.ok, no_header.error);
	CHECK(no_header.help_requested);
	CHECK(no_header.no_header);
	CHECK_EQ(no_header.command_path, make_args({ "script", "format" }));
}

TEST_CASE("[FoundryCLIParser] Project run forwards passthrough runtime flags") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"run",
			"--project",
			"demo",
			"--scene",
			"res://main.tscn",
			"--remote-debug",
			"tcp://127.0.0.1:6007",
			"--editor-pid",
			"42",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.scene, "res://main.tscn");
	CHECK_EQ(result.invocation.passthrough_args, make_args({ "--remote-debug", "tcp://127.0.0.1:6007", "--editor-pid", "42" }));
}

TEST_CASE("[FoundryCLIParser] Project test accepts the debug transport options") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"test",
			"--project",
			"demo",
			"--runner",
			"res://addons/example/run.fs",
			"--remote-debug",
			"tcp://127.0.0.1:6007",
			"--editor-pid",
			"42",
			"--",
			"adapter",
			"run",
			"--protocol-version",
			"1",
			"--select",
			"suite::case",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, FoundryCLIParser::CLIInvocation::PROJECT_TEST);
	CHECK_EQ(result.invocation.runner, "res://addons/example/run.fs");
	CHECK_EQ(result.invocation.passthrough_args, make_args({ "--remote-debug", "tcp://127.0.0.1:6007", "--editor-pid", "42" }));
	CHECK_EQ(result.user_args, make_args({ "adapter", "run", "--protocol-version", "1", "--select", "suite::case" }));
}

TEST_CASE("[FoundryCLIParser] Project test rejects malformed debug transport values") {
	FoundryCLIParser::ParseResult bad_uri = FoundryCLIParser::parse(make_args({ "foundry", "project", "test", "--runner", "res://run.fs", "--remote-debug", "127.0.0.1:6007" }));
	CHECK_FALSE(bad_uri.ok);
	CHECK(bad_uri.error.contains("--remote-debug"));

	FoundryCLIParser::ParseResult bad_pid = FoundryCLIParser::parse(make_args({ "foundry", "project", "test", "--runner", "res://run.fs", "--editor-pid", "not-a-pid" }));
	CHECK_FALSE(bad_pid.ok);
	CHECK(bad_pid.error.contains("--editor-pid"));

	FoundryCLIParser::ParseResult missing_value = FoundryCLIParser::parse(make_args({ "foundry", "project", "test", "--runner", "res://run.fs", "--remote-debug" }));
	CHECK_FALSE(missing_value.ok);
}

TEST_CASE("[FoundryCLIParser] Project test still rejects unrelated unknown options") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({ "foundry", "project", "test", "--runner", "res://run.fs", "--not-an-option" }));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--not-an-option"));
}

TEST_CASE("[FoundryCLIParser] Editor open accepts a scene path to reopen") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--project",
			"demo",
			"res://main.tscn",
	}));

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, Kind::EDITOR_OPEN);
	CHECK_EQ(result.invocation.passthrough_args, make_args({ "res://main.tscn" }));
}

TEST_CASE("[FoundryCLIParser] Editor project-manager is an unknown command") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"project-manager",
	}));

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("Unknown editor command"));
	CHECK(result.error.contains("project-manager"));
	// The removed command must not softly redirect to any project-manager surface.
	CHECK_FALSE(result.error.contains("has been removed"));
}

TEST_CASE("[FoundryCLIParser] Editor open accepts automation options") {
	FoundryCLIParser::ParseResult with_transport = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--project",
			"/tmp/project",
			"--automation",
			"--automation-transport=mcp",
	}));
	REQUIRE_MESSAGE(with_transport.ok, with_transport.error);
	CHECK_EQ(with_transport.invocation.kind, Kind::EDITOR_OPEN);
	CHECK(with_transport.invocation.automation);
	CHECK_EQ(with_transport.invocation.automation_transport, "mcp");

	FoundryCLIParser::ParseResult with_port = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--project",
			"/tmp/project",
			"--automation",
			"--automation-port",
			"0",
	}));
	REQUIRE_MESSAGE(with_port.ok, with_port.error);
	CHECK(with_port.invocation.automation);
	CHECK_EQ(with_port.invocation.automation_port, 0);
	CHECK_EQ(with_port.invocation.automation_transport, "mcp");
}

TEST_CASE("[FoundryCLIParser] Editor automation transport rejects unsupported values") {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args({
			"foundry",
			"editor",
			"open",
			"--automation-transport=http",
	}));
	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("--automation-transport"));
}

TEST_CASE("[FoundryCLIParser] Automation options are rejected outside editor open") {
	FoundryCLIParser::ParseResult test_run = FoundryCLIParser::parse(make_args({
			"foundry",
			"test",
			"run",
			"--automation",
	}));
	CHECK_FALSE(test_run.ok);
	CHECK(test_run.error.contains("Unknown option"));
	CHECK(test_run.error.contains("--automation"));

	FoundryCLIParser::ParseResult project_import = FoundryCLIParser::parse(make_args({
			"foundry",
			"project",
			"import",
			"--automation",
	}));
	CHECK_FALSE(project_import.ok);
	CHECK(project_import.error.contains("Unknown option"));
	CHECK(project_import.error.contains("--automation"));
}

TEST_CASE("[FoundryCLIParser] Removed legacy workflow flags are rejected") {
	auto expect_removed = [](const std::initializer_list<String> &p_input, const String &p_flag) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(p_input));
		CHECK_FALSE(result.ok);
		CHECK(result.error.contains(p_flag));
		CHECK(result.error.contains("has been removed"));
	};

	expect_removed({ "foundry", "--editor" }, "--editor");
	expect_removed({ "foundry", "-e" }, "-e");
	expect_removed({ "foundry", "--path", "." }, "--path");
	expect_removed({ "foundry", "--import" }, "--import");
	expect_removed({ "foundry", "--test" }, "--test");
	expect_removed({ "foundry", "--export-release", "Linux", "out" }, "--export-release");
	expect_removed({ "foundry", "--foundry_script-format" }, "--foundry_script-format");
	expect_removed({ "foundry", "--foundry_script-lint" }, "--foundry_script-lint");
	expect_removed({ "foundry", "--foundry_script-generate-tests" }, "--foundry_script-generate-tests");
	expect_removed({ "foundry", "--doctool" }, "--doctool");
	// The tooling listeners are configured through `tooling serve`; the global
	// pre-command spellings carry no startup behavior at all.
	expect_removed({ "foundry", "--lsp-port", "6005" }, "--lsp-port");
	expect_removed({ "foundry", "--dap-port", "6006" }, "--dap-port");
}

TEST_CASE("[FoundryCLIParser] Removed project-manager flags are unknown options") {
	// The Project Manager startup mode was removed with a clean break: the legacy
	// `-p` / `--project-manager` flags parse as unknown options with no deprecation
	// or redirect message (there is no project-manager surface to redirect to).
	auto expect_unknown_option = [](const std::initializer_list<String> &p_input, const String &p_flag) {
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(p_input));
		CHECK_FALSE(result.ok);
		CHECK(result.error.contains("Unknown option"));
		CHECK(result.error.contains(p_flag));
		CHECK_FALSE(result.error.contains("has been removed"));
		CHECK_FALSE(result.error.contains("Use "));
	};

	expect_unknown_option({ "foundry", "--project-manager" }, "--project-manager");
	expect_unknown_option({ "foundry", "-p" }, "-p");
}

} // namespace TestFoundryCLIParser
