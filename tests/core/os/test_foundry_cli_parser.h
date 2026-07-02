/**************************************************************************/
/*  test_foundry_cli_parser.h                                             */
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

static void require_normalized(
		const std::initializer_list<String> &p_input,
		const std::initializer_list<String> &p_expected) {
	FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(make_args(p_input));
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.normalized_args, make_args(p_expected));
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
	CHECK_EQ(result.normalized_args, make_args({
											 "foundry",
											 "--no-header",
											 "--path",
											 "demo",
											 "--scene",
											 "res://main.tscn",
											 "--",
											 "--difficulty",
											 "hard",
									 }));
}

TEST_CASE("[FoundryCLIParser] Script format maps to the existing formatter command") {
	require_normalized({
							   "foundry",
							   "script",
							   "format",
							   "--project",
							   "demo",
							   "--check",
							   "modules/foundry_script/tests/scripts",
					   },
			{
					"foundry",
					"--headless",
					"--path",
					"demo",
					"--foundry_script-format",
					"--check",
					"modules/foundry_script/tests/scripts",
			});
}

TEST_CASE("[FoundryCLIParser] Script lint maps to the existing lint command") {
	require_normalized({
							   "foundry",
							   "script",
							   "lint",
							   "--project",
							   "demo",
							   "--format=sarif",
							   "--out",
							   "lint.sarif",
							   "scripts",
					   },
			{
					"foundry",
					"--headless",
					"--path",
					"demo",
					"--foundry_script-lint",
					"--format=sarif",
					"--out",
					"lint.sarif",
					"scripts",
			});
}

TEST_CASE("[FoundryCLIParser] Script tools map when setup argv omits the executable") {
	require_normalized({
							   "script",
							   "format",
							   "--project",
							   "demo",
							   "--write",
							   "scripts",
					   },
			{
					"--headless",
					"--path",
					"demo",
					"--foundry_script-format",
					"--write",
					"scripts",
			});

	require_normalized({
							   "script",
							   "lint",
							   "--project",
							   "demo",
							   "--format=json",
							   "scripts",
					   },
			{
					"--headless",
					"--path",
					"demo",
					"--foundry_script-lint",
					"--format=json",
					"scripts",
			});
}

TEST_CASE("[FoundryCLIParser] Script migrate uses explicit trust and strict options") {
	require_normalized({
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
					   },
			{
					"foundry",
					"--foundry-build-trusted",
					"--headless",
					"--foundry_script-migrate",
					"demo",
					"--foundry_script-migrate-apply",
					"--foundry_script-migrate-strict-null-checks",
					"--foundry_script-migrate-strict-dynamic-checks",
					"--foundry_script-migrate-activate-strict",
					"--foundry_script-migrate-confirm",
					"--foundry_script-migrate-follow-up",
					"follow_up.md",
			});
}

TEST_CASE("[FoundryCLIParser] Project export requires structured preset and output") {
	require_normalized({
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
					   },
			{
					"foundry",
					"--path",
					"demo",
					"--export-release",
					"Linux",
					"build/game.x86_64",
			});
}

TEST_CASE("[FoundryCLIParser] Test run maps agent-friendly options to doctest") {
	require_normalized({
							   "foundry",
							   "test",
							   "run",
							   "--project",
							   "demo",
							   "--case",
							   "*FoundryScript*",
					   },
			{
					"foundry",
					"--headless",
					"--path",
					"demo",
					"--test",
					"--test-case=*FoundryScript*",
			});
}

TEST_CASE("[FoundryCLIParser] LSP serve maps to editor language server startup") {
	require_normalized({
							   "foundry",
							   "lsp",
							   "serve",
							   "--project",
							   "demo",
							   "--port",
							   "6005",
					   },
			{
					"foundry",
					"--path",
					"demo",
					"--editor",
					"--lsp-port",
					"6005",
			});
}

TEST_CASE("[FoundryCLIParser] Docs and extension commands map to generator tools") {
	require_normalized({
							   "foundry",
							   "docs",
							   "generate-api",
							   "--include-docs",
					   },
			{
					"foundry",
					"--dump-extension-api-with-docs",
			});

	require_normalized({
							   "foundry",
							   "docs",
							   "generate-engine",
							   "--output",
							   "doc-out",
					   },
			{
					"foundry",
					"--doctool",
					"doc-out",
			});

	require_normalized({
							   "foundry",
							   "extension",
							   "dump-interface",
							   "--format",
							   "json",
					   },
			{
					"foundry",
					"--dump-foundryextension-interface-json",
			});
}

TEST_CASE("[FoundryCLIParser] Diagnostics commands map to render device probes") {
	require_normalized({
							   "foundry",
							   "diagnostics",
							   "render-device-support",
					   },
			{
					"foundry",
					"--test-rd-support",
			});
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
	// script migrate requires --project, but a help request must not fail on it.
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
	for (const String &noun : { String("editor"), String("lsp"), String("docs"), String("extension"), String("diagnostics") }) {
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

TEST_CASE("[FoundryCLIParser] Help flag after legacy token stays legacy") {
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

} // namespace TestFoundryCLIParser
