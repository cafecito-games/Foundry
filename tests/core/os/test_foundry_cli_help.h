/**************************************************************************/
/*  test_foundry_cli_help.h                                               */
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

#include "core/io/json.h"
#include "main/cli_help.h"
#include "main/cli_parser.h"

#include "tests/test_macros.h"

namespace TestFoundryCLIHelp {

TEST_CASE("[FoundryCLIHelp] Top help lists nouns and omits legacy options") {
	const String text = FoundryCLIHelp::get_top_help_text("foundry");
	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	for (int i = 0; i < noun_count; i++) {
		if (FoundryCLIHelp::is_noun_in_build(nouns[i].name)) {
			CHECK_MESSAGE(text.contains(nouns[i].name), nouns[i].name);
		} else {
			CHECK_FALSE_MESSAGE(text.contains(nouns[i].name), nouns[i].name);
		}
	}
	CHECK(text.contains("--json"));
	CHECK(text.contains("Run 'foundry <command> --help'"));
	CHECK_FALSE(text.contains("--resolution"));
	CHECK_FALSE(text.contains("--fullscreen"));
	CHECK_FALSE(text.contains("--doctool"));
	CHECK_FALSE(text.contains("--export-release"));
}

#ifdef TOOLS_ENABLED
TEST_CASE("[FoundryCLIHelp] Editor help omits the removed project-manager command") {
	// The `editor` noun and its `open` command are editor-only, so this help text is
	// empty in release template builds; only assert on it when editor help exists.
	const String text = FoundryCLIHelp::get_noun_help_text("editor");
	CHECK(text.contains("open"));
	CHECK_FALSE(text.contains("project-manager"));
	CHECK_FALSE(text.contains("Project Manager"));
}
#endif

TEST_CASE("[FoundryCLIHelp] The removed lsp noun is absent from every help surface") {
	CHECK_FALSE(FoundryCLIHelp::has_noun("lsp"));
	CHECK_FALSE(FoundryCLIHelp::get_top_help_text("foundry").contains("lsp"));
	CHECK(FoundryCLIHelp::get_command_help_text("lsp", "serve").is_empty());

	// Scoped routing no longer resolves the retired noun, so nothing advertises it.
	PackedStringArray scope;
	scope.push_back("lsp");
	bool valid = true;
	(void)FoundryCLIHelp::get_scoped_help_text("foundry", scope, valid);
	CHECK_FALSE(valid);

	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	for (int i = 0; i < noun_count; i++) {
		CHECK_NE(String(nouns[i].name), "lsp");
	}

	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		CHECK_NE(String(commands[i].noun), "lsp");
	}

	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(PackedStringArray())), OK);
	const Dictionary root = json.get_data();
	const Array json_commands = root["commands"];
	for (int i = 0; i < json_commands.size(); i++) {
		const Dictionary command = json_commands[i];
		CHECK_NE(String(command["path"]), "lsp serve");
		CHECK_FALSE(String(command["path"]).begins_with("lsp "));
	}
}

TEST_CASE("[FoundryCLIHelp] Noun help lists its subcommands") {
	const String text = FoundryCLIHelp::get_noun_help_text("script");
	CHECK(text.contains("format"));
	CHECK(text.contains("lint"));
	CHECK(text.contains("eval"));
#ifdef TOOLS_ENABLED
	CHECK(text.contains("migrate"));
#endif
	CHECK(text.contains("Run 'foundry script <subcommand> --help'"));
}

TEST_CASE("[FoundryCLIHelp] Script eval command help documents its usage") {
	const String text = FoundryCLIHelp::get_command_help_text("script", "eval");
	CHECK(text.contains("source"));
	CHECK(text.contains("foundry --headless script eval 'print(\"ok\")'"));
}

TEST_CASE("[FoundryCLIHelp] Command help documents options and example") {
	const String text = FoundryCLIHelp::get_command_help_text("script", "format");
	CHECK(text.contains("--check"));
	CHECK(text.contains("--write"));
	CHECK(text.contains("--diff"));
	CHECK(text.contains("foundry script format --project . --check scripts"));
}

TEST_CASE("[FoundryCLIHelp] Test run help documents the shard selector") {
	const String text = FoundryCLIHelp::get_command_help_text("test", "run");
	CHECK(text.contains("--shard"));
	CHECK(text.contains("i/n"));
	CHECK(text.contains("--case"));
}

TEST_CASE("[FoundryCLIHelp] Test run help documents the suite filter") {
	const String text = FoundryCLIHelp::get_command_help_text("test", "run");
	CHECK(text.contains("--suite"));
	CHECK(text.contains("Test suite name filter pattern"));
}

TEST_CASE("[FoundryCLIHelp] Test benchmark help documents the corpus directory and the profile pass") {
	const String noun_text = FoundryCLIHelp::get_noun_help_text("test");
	CHECK(noun_text.contains("benchmark"));
	CHECK(noun_text.contains("Run the Foundry Script benchmark corpus."));

	const String text = FoundryCLIHelp::get_command_help_text("test", "benchmark");
	CHECK(text.contains("[dir]"));
	CHECK(text.contains("--output"));
	CHECK(text.contains("--profile"));
	CHECK(text.contains("--profile-output"));
	CHECK(text.contains("implies --profile"));
	CHECK(text.contains("foundry --headless test benchmark modules/foundry_script/tests/benchmarks --output bench.json"));
}

TEST_CASE("[FoundryCLIHelp] Test fixtures help documents pattern scoping and the report") {
	const String text = FoundryCLIHelp::get_command_help_text("test", "fixtures");
	CHECK(text.contains("patterns"));
	CHECK(text.contains("--pass"));
	CHECK(text.contains("--output"));
	CHECK(text.contains("foundry --headless test fixtures trait_argument_binding"));
}

TEST_CASE("[FoundryCLIHelp] Test completeness run help documents its required inputs") {
	const String text = FoundryCLIHelp::get_command_help_text("test", "completeness run");
	CHECK(text.contains("--family"));
	CHECK(text.contains("--catalog"));
	CHECK(text.contains("--scratch"));
	CHECK(text.contains("--report"));
	CHECK(text.contains("--tier"));
	CHECK(text.contains("--surface"));
	CHECK(text.contains("--timeout-seconds"));
	CHECK(text.contains("(required)"));
	CHECK(text.contains("foundry --headless test completeness run"));
}

TEST_CASE("[FoundryCLIHelp] Test completeness select help documents its required inputs") {
	const String text = FoundryCLIHelp::get_command_help_text("test", "completeness select");
	CHECK(text.contains("--changed-paths"));
	CHECK(text.contains("--catalog"));
	CHECK(text.contains("--json"));
	CHECK(text.contains("(required)"));
	CHECK(text.contains("foundry --headless test completeness select"));
}

// The selection is an editor-build capability, so it is documented in every build's text help and
// listed in the machine-readable help only where the build can actually run it.
TEST_CASE("[FoundryCLIHelp] Test completeness select is listed exactly where the build includes it") {
	PackedStringArray scope;
	scope.push_back("test");
	scope.push_back("completeness");
	scope.push_back("select");
	bool valid = false;
	const String text = FoundryCLIHelp::get_scoped_help_text("foundry", scope, valid);
	CHECK(valid);
	CHECK(text.contains("--changed-paths"));

	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *registry = FoundryCLIHelp::get_commands(command_count);
	const FoundryCLIHelp::CommandSpec *spec = nullptr;
	for (int i = 0; i < command_count; i++) {
		if (String(registry[i].noun) == "test" && String(registry[i].verb) == "completeness select") {
			spec = &registry[i];
			break;
		}
	}
	REQUIRE_MESSAGE(spec != nullptr, "every configuration registers the selection command");

	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Dictionary root = json.get_data();
	const Array commands = root["commands"];
	if (FoundryCLIHelp::is_command_in_build(*spec)) {
		REQUIRE_EQ(commands.size(), 1);
		const Dictionary command = commands[0];
		const Array path = command["path"];
		REQUIRE_EQ(path.size(), 2);
		CHECK_EQ(String(path[0]), "test");
		CHECK_EQ(String(path[1]), "completeness select");
	} else {
		CHECK(commands.is_empty());
	}
}

TEST_CASE("[FoundryCLIHelp] A multi-word verb resolves from a deeper scope") {
	PackedStringArray scope;
	scope.push_back("test");
	scope.push_back("completeness");
	scope.push_back("run");

	// The registry stores a multi-word verb as one label and the command line spells it one token per
	// word, in every configuration: the table is built the same way whatever the build can run, so
	// `foundry test completeness run --help` explains the command even where it is unavailable.
	bool valid = false;
	const String text = FoundryCLIHelp::get_scoped_help_text("foundry", scope, valid);
	CHECK(valid);
	CHECK(text.contains("--tier"));

	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *registry = FoundryCLIHelp::get_commands(command_count);
	const FoundryCLIHelp::CommandSpec *spec = nullptr;
	for (int i = 0; i < command_count; i++) {
		if (String(registry[i].noun) == "test" && String(registry[i].verb) == "completeness run") {
			spec = &registry[i];
			break;
		}
	}
	REQUIRE_MESSAGE(spec != nullptr, "every configuration registers the multi-word verb");

	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Dictionary root = json.get_data();
	const Array commands = root["commands"];
	// The machine-readable listing carries a command exactly when this build includes it, so an
	// editor-only command is documented in text and omitted from the listing of a template build.
	// Deciding that through the same predicate the filter uses keeps this test honest if the
	// command's availability ever changes.
	if (FoundryCLIHelp::is_command_in_build(*spec)) {
		REQUIRE_EQ(commands.size(), 1);
		const Dictionary command = commands[0];
		const Array path = command["path"];
		REQUIRE_EQ(path.size(), 2);
		CHECK_EQ(String(path[0]), "test");
		CHECK_EQ(String(path[1]), "completeness run");
	} else {
		CHECK(commands.is_empty());
	}
}

TEST_CASE("[FoundryCLIHelp] Scoped routing validates nouns and verbs") {
	bool valid = false;

	(void)FoundryCLIHelp::get_scoped_help_text("foundry", PackedStringArray(), valid);
	CHECK(valid);

	PackedStringArray noun_scope;
	noun_scope.push_back("script");
	(void)FoundryCLIHelp::get_scoped_help_text("foundry", noun_scope, valid);
	CHECK(valid);

	PackedStringArray verb_scope = noun_scope;
	verb_scope.push_back("format");
	(void)FoundryCLIHelp::get_scoped_help_text("foundry", verb_scope, valid);
	CHECK(valid);

	PackedStringArray bad_noun;
	bad_noun.push_back("not-a-noun");
	const String fallback_top = FoundryCLIHelp::get_scoped_help_text("foundry", bad_noun, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_top.contains("Global options"));

	PackedStringArray bad_verb = noun_scope;
	bad_verb.push_back("fmt");
	const String fallback_noun = FoundryCLIHelp::get_scoped_help_text("foundry", bad_verb, valid);
	CHECK_FALSE(valid);
	CHECK(fallback_noun.contains("Subcommands"));
}

TEST_CASE("[FoundryCLIHelp] Unknown command help is empty") {
	CHECK(FoundryCLIHelp::get_command_help_text("script", "fmt").is_empty());
	CHECK(FoundryCLIHelp::get_command_help_text("nope", "format").is_empty());
}

TEST_CASE("[FoundryCLIHelp] Scope deeper than a command falls back to noun help") {
	PackedStringArray deep_scope;
	deep_scope.push_back("script");
	deep_scope.push_back("format");
	deep_scope.push_back("extra");
	bool valid = true;
	const String fallback = FoundryCLIHelp::get_scoped_help_text("foundry", deep_scope, valid);
	CHECK_FALSE(valid);
	CHECK(fallback.contains("Subcommands"));
}

TEST_CASE("[FoundryCLIHelp] JSON help is valid and versioned") {
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(PackedStringArray())), OK);
	const Dictionary root = json.get_data();
	CHECK_EQ(int(root["foundry_cli_help_version"]), 1);
	const Array commands = root["commands"];
#ifdef TOOLS_ENABLED
	int command_count = 0;
	FoundryCLIHelp::get_commands(command_count);
	CHECK_EQ(commands.size(), command_count);
#else
	CHECK(commands.size() > 0);
#endif
	const Dictionary first = commands[0];
	for (const char *key : { "path", "summary", "usage", "availability", "options", "positionals", "examples" }) {
		CHECK_MESSAGE(first.has(key), key);
	}
}

TEST_CASE("[FoundryCLIHelp] JSON help scopes to a single command") {
	PackedStringArray scope;
	scope.push_back("script");
	scope.push_back("format");
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Dictionary root = json.get_data();
	const Array commands = root["commands"];
	REQUIRE_EQ(commands.size(), 1);
	const Dictionary entry = commands[0];
	const Array path = entry["path"];
	REQUIRE_EQ(path.size(), 2);
	CHECK_EQ(String(path[0]), "script");
	CHECK_EQ(String(path[1]), "format");
	const Array options = entry["options"];
	CHECK(options.size() > 0);
}

TEST_CASE("[FoundryCLIHelp] JSON help contains no ANSI escapes") {
	const String json_text = FoundryCLIHelp::get_help_json(PackedStringArray());
	CHECK_FALSE(json_text.contains(String::chr(0x1b)));
}

TEST_CASE("[FoundryCLIHelp] JSON help keeps the version envelope for unknown scopes") {
	PackedStringArray scope;
	scope.push_back("not-a-noun");
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Dictionary root = json.get_data();
	CHECK_EQ(int(root["foundry_cli_help_version"]), 1);
	CHECK_EQ(Array(root["commands"]).size(), 0);
}

TEST_CASE("[FoundryCLIHelp] JSON help noun scope lists only that noun") {
	PackedStringArray scope;
	scope.push_back("script");
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Array commands = Dictionary(json.get_data())["commands"];
	CHECK(commands.size() > 1);
	for (int i = 0; i < commands.size(); i++) {
		const Array path = Dictionary(commands[i])["path"];
		CHECK_EQ(String(path[0]), "script");
	}
}

TEST_CASE("[FoundryCLIHelp] JSON help distinguishes option value styles") {
	PackedStringArray scope;
	scope.push_back("script");
	scope.push_back("lint");
	JSON json;
	REQUIRE_EQ(json.parse(FoundryCLIHelp::get_help_json(scope)), OK);
	const Array commands = Dictionary(json.get_data())["commands"];
	REQUIRE_EQ(commands.size(), 1);
	const Array options = Dictionary(commands[0])["options"];
	bool saw_equals = false;
	bool saw_space = false;
	for (int i = 0; i < options.size(); i++) {
		const Dictionary option = options[i];
		const Variant value = option["value"];
		const Variant style = option["style"];
		if (value.get_type() == Variant::NIL) {
			CHECK(style.get_type() == Variant::NIL);
		} else if (String(style) == "equals") {
			saw_equals = true;
		} else if (String(style) == "space") {
			saw_space = true;
		}
	}
	CHECK(saw_equals);
	CHECK(saw_space);
}

#ifndef TOOLS_ENABLED
TEST_CASE("[FoundryCLIHelp] Release builds hide editor-only commands from top help") {
	const String text = FoundryCLIHelp::get_top_help_text("foundry");
	CHECK_FALSE(text.contains("editor"));
	CHECK_FALSE(text.contains("tooling"));
	CHECK_FALSE(text.contains("docs"));
	CHECK_FALSE(text.contains("extension"));
	CHECK(text.contains("project"));
	CHECK(text.contains("script"));
	CHECK(text.contains("test"));
}
#endif

// The first "|" alternative of value_name doubles as a sample value the
// parser must accept (e.g. "release|debug|pack|patch" -> "release").
static String drift_option_value(const FoundryCLIHelp::CommandOption &p_option) {
	return String(p_option.value_name).get_slice("|", 0);
}

static void drift_append_option(PackedStringArray &r_args, const FoundryCLIHelp::CommandOption &p_option) {
	if (p_option.value_name && p_option.equals_form) {
		r_args.push_back(String(p_option.flag) + "=" + drift_option_value(p_option));
		if (String(p_option.flag) == "--automation-run-workflow") {
			r_args.push_back("--automation");
		}
		return;
	}
	r_args.push_back(p_option.flag);
	if (p_option.value_name) {
		r_args.push_back(drift_option_value(p_option));
	}
	if (String(p_option.flag) == "--automation-run-workflow") {
		r_args.push_back("--automation");
	}
}

static PackedStringArray drift_base_args(const FoundryCLIHelp::CommandSpec &p_spec) {
	PackedStringArray args;
	args.push_back("foundry");
	args.push_back(p_spec.noun);
	// A verb may be several words (`test completeness run`); the registry stores it as one label and
	// the command line takes one token per word.
	for (const String &word : String(p_spec.verb).split(" ", false)) {
		args.push_back(word);
	}
	for (int i = 0; i < p_spec.option_count; i++) {
		const FoundryCLIHelp::CommandOption &option = p_spec.options[i];
		if (!option.required) {
			continue;
		}
		drift_append_option(args, option);
	}
	for (int i = 0; i < p_spec.positional_count; i++) {
		const FoundryCLIHelp::Positional &positional = p_spec.positionals[i];
		if (positional.optional) {
			continue;
		}
		// A sample value for each required positional so a command whose only mandatory
		// input is positional (e.g. `script eval <source>`) still parses in the drift check.
		args.push_back(String("sample_") + positional.name);
	}
	return args;
}

TEST_CASE("[FoundryCLIHelp] Registry nouns match the parser") {
	int noun_count = 0;
	const FoundryCLIHelp::NounSpec *nouns = FoundryCLIHelp::get_nouns(noun_count);
	// Catches registry-side noun removal; additions must update this pin deliberately.
	CHECK_EQ(noun_count, 8);
	for (int i = 0; i < noun_count; i++) {
		CHECK_MESSAGE(FoundryCLIParser::is_new_cli_command(nouns[i].name), nouns[i].name);
	}
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		CHECK_MESSAGE(FoundryCLIHelp::has_noun(commands[i].noun), commands[i].noun);
	}
}

TEST_CASE("[FoundryCLIHelp] Every registry command is accepted by the parser") {
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		const FoundryCLIHelp::CommandSpec &spec = commands[i];
		const String label = String(spec.noun) + " " + spec.verb;
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(drift_base_args(spec));
		REQUIRE_MESSAGE(result.ok, (label + ": " + result.error));
		PackedStringArray expected_path;
		expected_path.push_back(spec.noun);
		expected_path.push_back(spec.verb);
		CHECK_MESSAGE(result.command_path == expected_path, label);
	}
}

// Vacuous for passthrough commands (see drift_command_is_passthrough); their
// real option handling lives downstream.
TEST_CASE("[FoundryCLIHelp] Every documented option is accepted by its parser") {
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		const FoundryCLIHelp::CommandSpec &spec = commands[i];
		for (int option_index = 0; option_index < spec.option_count; option_index++) {
			const FoundryCLIHelp::CommandOption &option = spec.options[option_index];
			PackedStringArray args = drift_base_args(spec);
			if (!option.required) {
				drift_append_option(args, option);
			}
			FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(args);
			REQUIRE_MESSAGE(result.ok, (String(spec.noun) + " " + spec.verb + " " + option.flag + ": " + result.error));
		}
	}
}

static bool drift_command_is_passthrough(const FoundryCLIHelp::CommandSpec &p_spec) {
	// These commands forward unrecognized tokens to a downstream CLI, so the
	// option-acceptance test above is vacuous for them and unknown options
	// cannot be rejected at this layer.
	const String label = String(p_spec.noun) + " " + p_spec.verb;
	return label == "script format" || label == "script lint" || label == "test run" || label == "project run";
}

TEST_CASE("[FoundryCLIHelp] Strict commands reject unknown options") {
	int command_count = 0;
	const FoundryCLIHelp::CommandSpec *commands = FoundryCLIHelp::get_commands(command_count);
	for (int i = 0; i < command_count; i++) {
		const FoundryCLIHelp::CommandSpec &spec = commands[i];
		if (drift_command_is_passthrough(spec)) {
			continue;
		}
		PackedStringArray args = drift_base_args(spec);
		args.push_back("--drift-unknown-option");
		FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(args);
		CHECK_MESSAGE(!result.ok, (String(spec.noun) + " " + spec.verb + " accepted an unknown option"));
	}
}

} // namespace TestFoundryCLIHelp
