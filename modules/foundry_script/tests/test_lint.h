/**************************************************************************/
/*  test_lint.h                                                           */
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

#include "../fs_lint.h"

#ifdef DEBUG_ENABLED
#include "../fs_parser.h"
#include "../fs_warning.h"
#endif

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

struct TemporaryLintTree {
	String root;

	explicit TemporaryLintTree(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir.is_valid());
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryLintTree() {
		remove_recursive(root);
	}

	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir.is_valid());
		REQUIRE_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", absolute_path));
		file->store_string(p_contents);
	}

	static void remove_recursive(const String &p_path) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		if (dir->list_dir_begin() != OK) {
			return;
		}
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(entry)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

class ScopedResourcePath {
	String previous_resource_path;

public:
	explicit ScopedResourcePath(const String &p_resource_path) {
		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		TestProjectSettingsInternalsAccessor::resource_path() = p_resource_path;
	}

	~ScopedResourcePath() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;
	}
};

static const FSLintCLI::Diagnostic *find_diagnostic(
		const FSLintCLI::Result &p_result,
		const String &p_rule_id,
		const String &p_message_part = String()) {
	for (int i = 0; i < p_result.diagnostics.size(); i++) {
		const FSLintCLI::Diagnostic &diagnostic = p_result.diagnostics[i];
		if (diagnostic.rule_id == p_rule_id && (p_message_part.is_empty() || diagnostic.message.contains(p_message_part))) {
			return &diagnostic;
		}
	}
	return nullptr;
}

static void check_diagnostic_basics(
		const FSLintCLI::Diagnostic &p_diagnostic,
		const String &p_path,
		FSLintCLI::Severity p_severity) {
	CHECK_EQ(p_diagnostic.path, p_path);
	CHECK_EQ(p_diagnostic.sarif_path, p_path);
	CHECK_EQ(p_diagnostic.severity, p_severity);
	CHECK_EQ(p_diagnostic.source, "foundry_script");
	CHECK_GE(p_diagnostic.range.start_line, 1);
	CHECK_GE(p_diagnostic.range.start_column, 1);
	CHECK_GE(p_diagnostic.range.end_line, p_diagnostic.range.start_line);
	CHECK_GE(p_diagnostic.range.end_column, 1);
	if (p_diagnostic.range.end_line == p_diagnostic.range.start_line) {
		CHECK_GE(p_diagnostic.range.end_column, p_diagnostic.range.start_column);
	}
}

#ifdef DEBUG_ENABLED
class LintWarningSettingsScope {
	Variant previous_enable;
	Variant previous_unused_variable;
	bool previous_ignore = false;

public:
	LintWarningSettingsScope() {
		const String unused_variable_setting = FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE);
		previous_enable = ProjectSettings::get_singleton()->get_setting("debug/foundry_script/warnings/enable", true);
		previous_unused_variable = ProjectSettings::get_singleton()->get_setting(
				unused_variable_setting,
				(int)FSWarning::WARN);
		previous_ignore = FSParser::is_ignoring_warnings();

		ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/enable", true);
		ProjectSettings::get_singleton()->set_setting(unused_variable_setting, (int)FSWarning::WARN);
		FSParser::update_project_settings();
	}

	~LintWarningSettingsScope() {
		const String unused_variable_setting = FSWarning::get_setting_path_from_code(FSWarning::UNUSED_VARIABLE);
		ProjectSettings::get_singleton()->set_setting("debug/foundry_script/warnings/enable", previous_enable);
		ProjectSettings::get_singleton()->set_setting(unused_variable_setting, previous_unused_variable);
		FSParser::update_project_settings();
		FSParser::set_ignoring_warnings(previous_ignore);
	}
};
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing uses CI defaults") {
	List<String> args;
	args.push_back("--headless");
	args.push_back("--foundry_script-lint");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_JSON);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_ERROR);
	CHECK(options.output_path.is_empty());
	CHECK(options.paths.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing accepts SARIF output file and warning threshold") {
	List<String> args;
	args.push_back("--foundry_script-lint");
	args.push_back("--format=sarif");
	args.push_back("--out");
	args.push_back("lint.sarif");
	args.push_back("--fail-on=warning");
	args.push_back("res://scripts");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_SARIF);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_WARNING);
	CHECK_EQ(options.output_path, "lint.sarif");
	REQUIRE_EQ(options.paths.size(), 1);
	CHECK_EQ(options.paths[0], "res://scripts");
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing rejects invalid choices") {
	List<String> bad_format;
	bad_format.push_back("--foundry_script-lint");
	bad_format.push_back("--format=xml");
	String format_error;
	FSLintCLI::parse_options(bad_format, format_error);
	CHECK(format_error.contains("Invalid --format value"));

	List<String> bad_fail_on;
	bad_fail_on.push_back("--foundry_script-lint");
	bad_fail_on.push_back("--fail-on=note");
	String fail_on_error;
	FSLintCLI::parse_options(bad_fail_on, fail_on_error);
	CHECK(fail_on_error.contains("Invalid --fail-on value"));

	List<String> missing_out;
	missing_out.push_back("--foundry_script-lint");
	missing_out.push_back("--out");
	String out_error;
	FSLintCLI::parse_options(missing_out, out_error);
	CHECK(out_error.contains("Missing file path after --out"));
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI result exit code honors fail-on threshold") {
	FSLintCLI::Diagnostic warning;
	warning.severity = FSLintCLI::SEVERITY_WARNING;

	FSLintCLI::Result warning_result;
	warning_result.diagnostics.push_back(warning);

	FSLintCLI::Options fail_on_error;
	fail_on_error.fail_on = FSLintCLI::FAIL_ON_ERROR;
	CHECK_EQ(warning_result.get_exit_code(fail_on_error), 0);

	FSLintCLI::Options fail_on_warning;
	fail_on_warning.fail_on = FSLintCLI::FAIL_ON_WARNING;
	CHECK_EQ(warning_result.get_exit_code(fail_on_warning), 1);

	FSLintCLI::Diagnostic error;
	error.severity = FSLintCLI::SEVERITY_ERROR;

	FSLintCLI::Result error_result;
	error_result.diagnostics.push_back(error);
	CHECK_EQ(error_result.get_exit_code(fail_on_error), 1);

	FSLintCLI::Result command_error_result;
	command_error_result.had_command_error = true;
	CHECK_EQ(command_error_result.get_exit_code(fail_on_error), 2);
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI writes JSON report to filesystem file") {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	const String report_path = dir->get_current_dir().path_join(
			"lint_report_" + itos(OS::get_singleton()->get_ticks_usec()) + ".json");

	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = "res://scripts/warning.fs";
	diagnostic.range.start_line = 2;
	diagnostic.range.start_column = 4;
	diagnostic.range.end_line = 2;
	diagnostic.range.end_column = 9;
	diagnostic.severity = FSLintCLI::SEVERITY_WARNING;
	diagnostic.rule_id = "unused-variable";
	diagnostic.message = "Unused local variable.";

	FSLintCLI::Result result;
	result.diagnostics.push_back(diagnostic);

	FSLintCLI::Options options;
	options.output_format = FSLintCLI::OUTPUT_JSON;
	options.output_path = report_path;

	DirAccess::remove_absolute(report_path);
	CHECK_EQ(FSLintCLI::write_report(options, result), OK);
	CHECK(FileAccess::exists(report_path));

	Error read_error = OK;
	const String report = FileAccess::get_file_as_string(report_path, &read_error);
	CHECK_EQ(read_error, OK);
	CHECK(report.contains("\"version\""));
	CHECK(report.contains("\"diagnostics\""));

	DirAccess::remove_absolute(report_path);
}

TEST_CASE("[Modules][FoundryScript][Lint] File collection recurses deterministically") {
	TemporaryLintTree tree("fs_lint_collection");
	tree.write_file("z_root.fs", "var z = 1\n");
	tree.write_file("nested/b_mid.fs", "var b = 1\n");
	tree.write_file("a_root.fs", "var a = 1\n");
	tree.write_file("nested/ignore.txt", "not a script\n");
	tree.write_file("nested/wrong.fs.txt", "not a script\n");
	tree.write_file(".godot/generated/cache.fs", "var hidden = 1\n");

	Vector<String> paths;
	paths.push_back(tree.root);

	bool had_error = false;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);

	CHECK_FALSE(had_error);
	REQUIRE_EQ(files.size(), 3);
	CHECK_EQ(files[0], tree.root.path_join("a_root.fs"));
	CHECK_EQ(files[1], tree.root.path_join("nested/b_mid.fs"));
	CHECK_EQ(files[2], tree.root.path_join("z_root.fs"));
}

TEST_CASE("[Modules][FoundryScript][Lint] File collection flags a missing path") {
	Vector<String> paths;
	const String missing_root = OS::get_singleton()->get_temp_path().path_join(
			"fs_lint_missing_path_" + itos(OS::get_singleton()->get_ticks_usec()));
	paths.push_back(missing_root.path_join("none.fs"));

	bool had_error = false;
	ERR_PRINT_OFF;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);
	ERR_PRINT_ON;

	CHECK(files.is_empty());
	CHECK_MESSAGE(had_error, "A missing path must mark the collection as failed.");
}

TEST_CASE("[Modules][FoundryScript][Lint] Parser errors become diagnostics") {
	const String source = "func run() -> void\n\tpass\n";
	TemporaryLintTree tree("fs_lint_parse_diagnostics");
	const String path = tree.root.path_join("parse_error.fs");
	tree.write_file("parse_error.fs", source);

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	const FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	CHECK_FALSE(result.had_command_error);
	const FSLintCLI::Diagnostic *diagnostic = find_diagnostic(result, "parse-error");
	REQUIRE(diagnostic != nullptr);
	check_diagnostic_basics(*diagnostic, path, FSLintCLI::SEVERITY_ERROR);
	const bool has_expected_message = diagnostic->message.contains("Expected") || diagnostic->message.contains("expected");
	CHECK_MESSAGE(has_expected_message, diagnostic->message);
}

TEST_CASE("[Modules][FoundryScript][Lint] Analyzer errors become diagnostics") {
	const String source = "func run() -> int:\n\treturn \"bad\"\n";
	TemporaryLintTree tree("fs_lint_analyzer_diagnostics");
	const String path = tree.root.path_join("analyzer_error.fs");
	tree.write_file("analyzer_error.fs", source);

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	const FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	CHECK_FALSE(result.had_command_error);
	const FSLintCLI::Diagnostic *diagnostic = find_diagnostic(result, "analyzer-error", "Cannot return");
	REQUIRE(diagnostic != nullptr);
	check_diagnostic_basics(*diagnostic, path, FSLintCLI::SEVERITY_ERROR);
	CHECK(diagnostic->message.contains("String"));
	CHECK(diagnostic->message.contains("int"));
}

TEST_CASE("[Modules][FoundryScript][Lint] Project files report resource and relative SARIF paths") {
	const String source = "func run() -> void\n\tpass\n";
	TemporaryLintTree tree("fs_lint_project_paths");
	tree.write_file("project.foundry", "");
	tree.write_file("scripts/bad.fs", source);
	const String raw_root = tree.root + "_raw_alias";

	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	REQUIRE_EQ(dir->change_dir(tree.root), OK);
	const String canonical_root = dir->get_current_dir();
	DirAccess::remove_absolute(raw_root);
	REQUIRE_EQ(dir->create_link(canonical_root, raw_root), OK);
	ScopedResourcePath resource_path_scope(canonical_root);

	Vector<String> paths;
	paths.push_back(raw_root.path_join("scripts"));
	FSLintCLI::Options options;
	const FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);
	DirAccess::remove_absolute(raw_root);

	CHECK_FALSE(result.had_command_error);
	const FSLintCLI::Diagnostic *diagnostic = find_diagnostic(result, "parse-error");
	REQUIRE(diagnostic != nullptr);
	CHECK_EQ(diagnostic->path, "res://scripts/bad.fs");
	CHECK_EQ(diagnostic->sarif_path, "scripts/bad.fs");
}

TEST_CASE("[Modules][FoundryScript][Lint] JSON serialization returns diagnostics report") {
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = "res://scripts/bad.fs";
	diagnostic.range.start_line = 3;
	diagnostic.range.start_column = 5;
	diagnostic.range.end_line = 3;
	diagnostic.range.end_column = 11;
	diagnostic.severity = FSLintCLI::SEVERITY_WARNING;
	diagnostic.rule_id = "strict-types";
	diagnostic.message = "Type mismatch.";

	Vector<FSLintCLI::Diagnostic> diagnostics;
	diagnostics.push_back(diagnostic);

	const String report = FSLintCLI::to_json(diagnostics);
	const Variant parsed = JSON::parse_string(report);

	REQUIRE_EQ(parsed.get_type(), Variant::DICTIONARY);
	const Dictionary root = parsed;
	CHECK_EQ(int(root["version"]), 1);
	REQUIRE_EQ(root["diagnostics"].get_type(), Variant::ARRAY);
	const Array parsed_diagnostics = root["diagnostics"];
	REQUIRE_EQ(parsed_diagnostics.size(), 1);
	REQUIRE_EQ(parsed_diagnostics[0].get_type(), Variant::DICTIONARY);
	const Dictionary parsed_diagnostic = parsed_diagnostics[0];
	CHECK_EQ(String(parsed_diagnostic["path"]), diagnostic.path);
	CHECK_EQ(String(parsed_diagnostic["severity"]), "warning");
	CHECK_EQ(String(parsed_diagnostic["source"]), "foundry_script");
	CHECK_EQ(String(parsed_diagnostic["ruleId"]), diagnostic.rule_id);
	CHECK_EQ(String(parsed_diagnostic["message"]), diagnostic.message);
	REQUIRE_EQ(parsed_diagnostic["range"].get_type(), Variant::DICTIONARY);
	const Dictionary range = parsed_diagnostic["range"];
	CHECK_EQ(int(range["startLine"]), 3);
	CHECK_EQ(int(range["startColumn"]), 5);
	CHECK_EQ(int(range["endLine"]), 3);
	CHECK_EQ(int(range["endColumn"]), 11);
	CHECK(report.ends_with("\n"));
}

TEST_CASE("[Modules][FoundryScript][Lint] SARIF serialization returns run with rules and results") {
	FSLintCLI::Diagnostic warning;
	warning.path = "res://scripts/warning.fs";
	warning.sarif_path = "scripts/warning.fs";
	warning.range.start_line = 2;
	warning.range.start_column = 4;
	warning.range.end_line = 2;
	warning.range.end_column = 9;
	warning.severity = FSLintCLI::SEVERITY_WARNING;
	warning.rule_id = "z-warning";
	warning.message = "Warning message.";

	FSLintCLI::Diagnostic error;
	error.path = "res://scripts/error.fs";
	error.range.start_line = 7;
	error.range.start_column = 1;
	error.range.end_line = 8;
	error.range.end_column = 6;
	error.severity = FSLintCLI::SEVERITY_ERROR;
	error.rule_id = "a-error";
	error.message = "Error message.";

	Vector<FSLintCLI::Diagnostic> diagnostics;
	diagnostics.push_back(warning);
	diagnostics.push_back(error);

	const String report = FSLintCLI::to_sarif(diagnostics);
	const Variant parsed = JSON::parse_string(report);

	REQUIRE_EQ(parsed.get_type(), Variant::DICTIONARY);
	const Dictionary root = parsed;
	CHECK_EQ(String(root["version"]), "2.1.0");
	CHECK_EQ(String(root["$schema"]), "https://json.schemastore.org/sarif-2.1.0.json");
	REQUIRE_EQ(root["runs"].get_type(), Variant::ARRAY);
	const Array runs = root["runs"];
	REQUIRE_EQ(runs.size(), 1);
	REQUIRE_EQ(runs[0].get_type(), Variant::DICTIONARY);
	const Dictionary run = runs[0];
	REQUIRE_EQ(run["tool"].get_type(), Variant::DICTIONARY);
	const Dictionary tool = run["tool"];
	REQUIRE_EQ(tool["driver"].get_type(), Variant::DICTIONARY);
	const Dictionary driver = tool["driver"];
	CHECK_EQ(String(driver["name"]), "Foundry Script Lint");
	REQUIRE_EQ(driver["rules"].get_type(), Variant::ARRAY);
	const Array rules = driver["rules"];
	REQUIRE_EQ(rules.size(), 2);
	REQUIRE_EQ(rules[0].get_type(), Variant::DICTIONARY);
	REQUIRE_EQ(rules[1].get_type(), Variant::DICTIONARY);
	CHECK_EQ(String(Dictionary(rules[0])["id"]), "a-error");
	CHECK_EQ(String(Dictionary(rules[1])["id"]), "z-warning");

	REQUIRE_EQ(run["results"].get_type(), Variant::ARRAY);
	const Array results = run["results"];
	REQUIRE_EQ(results.size(), 2);

	REQUIRE_EQ(results[0].get_type(), Variant::DICTIONARY);
	const Dictionary warning_result = results[0];
	CHECK_EQ(String(warning_result["ruleId"]), warning.rule_id);
	CHECK_EQ(String(warning_result["level"]), "warning");
	REQUIRE_EQ(warning_result["message"].get_type(), Variant::DICTIONARY);
	CHECK_EQ(String(Dictionary(warning_result["message"])["text"]), warning.message);
	REQUIRE_EQ(warning_result["locations"].get_type(), Variant::ARRAY);
	const Array warning_locations = warning_result["locations"];
	REQUIRE_EQ(warning_locations.size(), 1);
	REQUIRE_EQ(warning_locations[0].get_type(), Variant::DICTIONARY);
	const Dictionary warning_location = warning_locations[0];
	REQUIRE_EQ(warning_location["physicalLocation"].get_type(), Variant::DICTIONARY);
	const Dictionary warning_physical_location = warning_location["physicalLocation"];
	REQUIRE_EQ(warning_physical_location["artifactLocation"].get_type(), Variant::DICTIONARY);
	CHECK_EQ(String(Dictionary(warning_physical_location["artifactLocation"])["uri"]), warning.sarif_path);
	REQUIRE_EQ(warning_physical_location["region"].get_type(), Variant::DICTIONARY);
	const Dictionary warning_region = warning_physical_location["region"];
	CHECK_EQ(int(warning_region["startLine"]), 2);
	CHECK_EQ(int(warning_region["startColumn"]), 4);
	CHECK_EQ(int(warning_region["endLine"]), 2);
	CHECK_EQ(int(warning_region["endColumn"]), 9);

	REQUIRE_EQ(results[1].get_type(), Variant::DICTIONARY);
	const Dictionary error_result = results[1];
	CHECK_EQ(String(error_result["ruleId"]), error.rule_id);
	CHECK_EQ(String(error_result["level"]), "error");
	REQUIRE_EQ(error_result["message"].get_type(), Variant::DICTIONARY);
	CHECK_EQ(String(Dictionary(error_result["message"])["text"]), error.message);
	REQUIRE_EQ(error_result["locations"].get_type(), Variant::ARRAY);
	const Array error_locations = error_result["locations"];
	REQUIRE_EQ(error_locations.size(), 1);
	REQUIRE_EQ(error_locations[0].get_type(), Variant::DICTIONARY);
	const Dictionary error_location = error_locations[0];
	REQUIRE_EQ(error_location["physicalLocation"].get_type(), Variant::DICTIONARY);
	const Dictionary error_physical_location = error_location["physicalLocation"];
	REQUIRE_EQ(error_physical_location["artifactLocation"].get_type(), Variant::DICTIONARY);
	CHECK_EQ(String(Dictionary(error_physical_location["artifactLocation"])["uri"]), error.path);
	REQUIRE_EQ(error_physical_location["region"].get_type(), Variant::DICTIONARY);
	const Dictionary error_region = error_physical_location["region"];
	CHECK_EQ(int(error_region["startLine"]), 7);
	CHECK_EQ(int(error_region["startColumn"]), 1);
	CHECK_EQ(int(error_region["endLine"]), 8);
	CHECK_EQ(int(error_region["endColumn"]), 6);
	CHECK(report.ends_with("\n"));
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][FoundryScript][Lint] Warnings become diagnostics") {
	LintWarningSettingsScope warning_settings;

	const String source = "func run() -> void:\n\tvar unused := 1\n";
	TemporaryLintTree tree("fs_lint_warning_diagnostics");
	const String path = tree.root.path_join("warning.fs");
	tree.write_file("warning.fs", source);

	Vector<String> paths;
	paths.push_back(path);
	FSLintCLI::Options options;
	const FSLintCLI::Result result = FSLintCLI::lint_paths(paths, options);

	CHECK_FALSE(result.had_command_error);
	const FSLintCLI::Diagnostic *diagnostic = find_diagnostic(result, "UNUSED_VARIABLE");
	REQUIRE(diagnostic != nullptr);
	check_diagnostic_basics(*diagnostic, path, FSLintCLI::SEVERITY_WARNING);
	CHECK_EQ(diagnostic->rule_id.to_lower(), "unused_variable");
	CHECK(diagnostic->message.contains("unused"));
}
#endif // DEBUG_ENABLED

} // namespace FSTests
