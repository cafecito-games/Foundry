/**************************************************************************/
/*  test_name_mangler_keep_rules.h                                        */
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

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/fs_name_mangler_keep_rules.h"

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_compiler.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

static Ref<FoundryScript> compile_keep_rules_test_source(const String &p_source, const String &p_path) {
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}

	const bool previous_ignore_warnings = FSParser::is_ignoring_warnings();
	FSParser::set_ignoring_warnings(true);

	FSParser parser;
	String phase = "parse";
	Error error = parser.parse(p_source, p_path, false);
	if (error == OK) {
		phase = "analyze";
		FSAnalyzer analyzer(&parser);
		error = analyzer.analyze();
	}

	Ref<FoundryScript> script;
	if (error == OK) {
		phase = "compile";
		script.instantiate();
		script->set_path(p_path);
		FSCompiler compiler;
		error = compiler.compile(&parser, script.ptr(), false);
		INFO(compiler.get_error());
	}

	FSParser::set_ignoring_warnings(previous_ignore_warnings);
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		INFO(vformat("Fixture error at line %d: %s", parser_error.line, parser_error.message));
	}
	CAPTURE(phase);
	CHECK_EQ(error, OK);
	if (error != OK) {
		return Ref<FoundryScript>();
	}
	return script;
}

static bool keep_rules_has_reason(const FSNameManglerAnalysis::Result &p_result, const StringName &p_name) {
	const FSNameManglerAnalysis::Classification *classification = p_result.find(p_name);
	if (classification == nullptr) {
		return false;
	}
	for (const FSNameManglerAnalysis::KeepEvidence &evidence : classification->keep_evidence) {
		if (evidence.reason == FSNameManglerAnalysis::KEEP_RULE) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[FoundryScript][NameManglerKeepRules] Parses the supported subset transactionally") {
	const String source = "res://name-mangler-keep-rules.pro";
	const String text =
			"# Keep public gameplay entry points.\n"
			"-keep class game.Player\n"
			"-keep class game.Player # duplicate\n"
			"-keepclassmembers class game.** {\n"
			"\tDynamic*;\n"
			"\tState?;\n"
			"}\n";

	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
	CHECK_EQ(FSNameManglerKeepRules::parse(text, source, rules, diagnostics), OK);
	CHECK_EQ(rules.get_rule_count(), 2);
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].severity, FSNameManglerKeepRules::DIAGNOSTIC_WARNING);
		CHECK_EQ(diagnostics[0].source, source);
		CHECK_EQ(diagnostics[0].line, 3);
		CHECK_EQ(diagnostics[0].message,
				"Duplicate keep rule; first declared at res://name-mangler-keep-rules.pro:2.");
		CHECK_EQ(diagnostics[0].format(),
				"res://name-mangler-keep-rules.pro:3: warning: Duplicate keep rule; first declared at "
				"res://name-mangler-keep-rules.pro:2.");
	}

	struct MalformedCase {
		const char *text;
		const char *message_fragment;
	};
	const MalformedCase malformed_cases[] = {
		{ "-dontobfuscate class game.Player\n", "Unsupported keep-rule directive" },
		{ "-keep game.Player\n", "Expected `class`" },
		{ "-keep class game.Player { member; }\n", "Inline keep-rule blocks are not supported" },
		{ "-keep class game.Player {\n\tmember\n}\n", "must end with `;`" },
		{ "-keep class game.Player {\n}\n", "must contain at least one member pattern" },
		{ "-keepclassmembers class game.Player {\n\tmember;\n", "Missing closing `}`" },
	};

	for (const MalformedCase &test_case : malformed_cases) {
		CAPTURE(test_case.text);
		diagnostics.clear();
		CHECK_EQ(FSNameManglerKeepRules::parse(test_case.text, source, rules, diagnostics), ERR_PARSE_ERROR);
		CHECK_EQ(rules.get_rule_count(), 2);
		CHECK_FALSE(diagnostics.is_empty());
		if (!diagnostics.is_empty()) {
			CHECK_EQ(diagnostics[0].severity, FSNameManglerKeepRules::DIAGNOSTIC_ERROR);
			CHECK_EQ(diagnostics[0].source, source);
			CHECK_GT(diagnostics[0].line, 0);
			CHECK(diagnostics[0].message.contains(test_case.message_fragment));
			CHECK(diagnostics[0].format().begins_with(source + ":"));
		}
	}
}

TEST_CASE("[FoundryScript][NameManglerKeepRules] Loads rules and reports read failures") {
	const String path = TestUtils::get_temp_path("name_mangler_keep_rules.pro");
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		CHECK(file.is_valid());
		if (file.is_valid()) {
			file->store_string("-keep class game.Player\n");
		}
	}

	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
	CHECK_EQ(FSNameManglerKeepRules::load(path, rules, diagnostics), OK);
	CHECK_EQ(rules.get_rule_count(), 1);
	CHECK(diagnostics.is_empty());
	{
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		CHECK(file.is_valid());
		if (file.is_valid()) {
			file->store_string("-keep class game.Player {\n\tbroken\n}\n");
		}
	}
	CHECK_EQ(FSNameManglerKeepRules::load(path, rules, diagnostics), ERR_PARSE_ERROR);
	CHECK_EQ(rules.get_rule_count(), 1);
	CHECK_EQ(diagnostics.size(), 1);

	CHECK_EQ(DirAccess::remove_absolute(path), OK);

	diagnostics.clear();
	CHECK_EQ(FSNameManglerKeepRules::load(path, rules, diagnostics), ERR_FILE_NOT_FOUND);
	CHECK_EQ(rules.get_rule_count(), 1);
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].severity, FSNameManglerKeepRules::DIAGNOSTIC_ERROR);
		CHECK_EQ(diagnostics[0].source, path);
		CHECK_EQ(diagnostics[0].line, 1);
		CHECK(diagnostics[0].message.contains("Could not read keep-rules file"));
	}
}

TEST_CASE("[FoundryScript][NameManglerKeepRules] Matches canonical class identities and atomic members") {
	const Ref<FoundryScript> resource_script = compile_keep_rules_test_source(
			"var root_single: int\n"
			"var root_recursive: int\n"
			"var shared_escape: int\n"
			"var resource_private: int\n",
			"res://actors/player.fs");
	const Ref<FoundryScript> global_script = compile_keep_rules_test_source(
			"namespace game.actors\n"
			"class_name Player\n"
			"\n"
			"var global_member: int\n"
			"var should_not: int\n"
			"class Inventory:\n"
			"\tvar nested_member: int\n"
			"\tvar shared_escape: int\n",
			"res://global_player.fs");
	CHECK(resource_script.is_valid());
	CHECK(global_script.is_valid());
	if (resource_script.is_null() || global_script.is_null()) {
		return;
	}

	const String source = "res://keep-names.pro";
	const String text =
			"-keepclassmembers class res://actors/*.fs {\n"
			"\troot_single;\n"
			"}\n"
			"-keepclassmembers class res://** {\n"
			"\troot_recursive;\n"
			"}\n"
			"-keep class game.actors.Player\n"
			"-keepclassmembers class game.actors.Player {\n"
			"\tglobal_*;\n"
			"}\n"
			"-keepclassmembers class game.* {\n"
			"\tshould_not;\n"
			"}\n"
			"-keepclassmembers class res:* {\n"
			"\tresource_private;\n"
			"}\n"
			"-keepclassmembers class res://* {\n"
			"\tresource_private;\n"
			"}\n"
			"-keepclassmembers class game.actors.Player:* {\n"
			"\tshared_escape;\n"
			"}\n"
			"-keepclassmembers class game.**::Inventor? {\n"
			"\tnested_member;\n"
			"\tshared_*;\n"
			"}\n";

	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
	CHECK_EQ(FSNameManglerKeepRules::parse(text, source, rules, diagnostics), OK);
	CHECK(diagnostics.is_empty());

	FSNameManglerAnalysis::Input input;
	input.scripts.push_back(resource_script);
	input.scripts.push_back(global_script);
	CHECK_EQ(rules.apply_to_input(input, diagnostics), OK);
	CHECK_EQ(diagnostics.size(), 4);
	if (diagnostics.size() == 4) {
		CHECK_EQ(diagnostics[0].severity, FSNameManglerKeepRules::DIAGNOSTIC_WARNING);
		CHECK_EQ(diagnostics[0].line, 11);
		CHECK(diagnostics[0].message.contains("matched no declarations"));
		CHECK_EQ(diagnostics[1].line, 14);
		CHECK_EQ(diagnostics[2].line, 17);
		CHECK_EQ(diagnostics[3].line, 20);
	}

	const FSNameManglerAnalysis::Result result = FSNameManglerAnalysis::analyze(input);
	CHECK_EQ(result.error, OK);
	CHECK(keep_rules_has_reason(result, SNAME("root_single")));
	CHECK(keep_rules_has_reason(result, SNAME("root_recursive")));
	CHECK(keep_rules_has_reason(result, SNAME("Player")));
	CHECK(result.find(SNAME("game.actors.Player")) == nullptr);
	CHECK(keep_rules_has_reason(result, SNAME("global_member")));
	CHECK(keep_rules_has_reason(result, SNAME("nested_member")));
	CHECK(keep_rules_has_reason(result, SNAME("shared_escape")));
	CHECK_FALSE(result.rename_map.has(SNAME("root_single")));
	CHECK_FALSE(result.rename_map.has(SNAME("root_recursive")));
	CHECK_FALSE(result.rename_map.has(SNAME("Player")));
	CHECK_FALSE(result.rename_map.has(SNAME("global_member")));
	CHECK_FALSE(result.rename_map.has(SNAME("nested_member")));
	CHECK_FALSE(result.rename_map.has(SNAME("shared_escape")));
	CHECK(result.rename_map.has(SNAME("resource_private")));
	CHECK(result.rename_map.has(SNAME("should_not")));
	CHECK(result.rename_map.has(SNAME("Inventory")));
}

TEST_CASE("[FoundryScript][NameManglerKeepRules] Unmatched warnings require a complete project graph") {
	FSNameManglerKeepRules rules;
	Vector<FSNameManglerKeepRules::Diagnostic> diagnostics;
	CHECK_EQ(FSNameManglerKeepRules::parse(
					 "-keep class missing.First\n"
					 "-keepclassmembers class missing.Second {\n"
					 "\tmember;\n"
					 "}\n",
					 "res://unmatched.pro", rules, diagnostics),
			OK);
	CHECK(diagnostics.is_empty());

	FSNameManglerAnalysis::Input incomplete;
	incomplete.complete_project_graph = false;
	CHECK_EQ(rules.apply_to_input(incomplete, diagnostics), OK);
	CHECK(diagnostics.is_empty());

	FSNameManglerAnalysis::Input complete;
	complete.complete_project_graph = true;
	CHECK_EQ(rules.apply_to_input(complete, diagnostics), OK);
	CHECK_EQ(diagnostics.size(), 2);
	if (diagnostics.size() == 2) {
		CHECK_EQ(diagnostics[0].line, 1);
		CHECK_EQ(diagnostics[1].line, 2);
		CHECK(diagnostics[0].message.contains("missing.First"));
		CHECK(diagnostics[1].message.contains("missing.Second"));
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
