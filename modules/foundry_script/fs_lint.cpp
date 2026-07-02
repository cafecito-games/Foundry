/**************************************************************************/
/*  fs_lint.cpp                                                           */
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

#include "fs_lint.h"

#include "core/config/project_settings.h"
#include "core/core_globals.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/array.h"
#include "fs_analyzer.h"
#include "fs_parser.h"

#ifdef DEBUG_ENABLED
#include "fs_warning.h"
#endif

#include <stdio.h>
#include <stdlib.h>

namespace {

FSLintCLI::Range make_range(const Vector<String> &p_lines, int p_line, int p_column) {
	FSLintCLI::Range range;
	const int line_count = MAX(1, p_lines.size());
	const int line = CLAMP(p_line, 1, line_count);
	const String line_text = line <= p_lines.size() ? p_lines[line - 1] : String();
	const int end_column = MAX(1, line_text.strip_edges(false, true).length() + 1);
	const int start_column = CLAMP(p_column, 1, end_column);

	range.start_line = line;
	range.start_column = start_column;
	range.end_line = line;
	range.end_column = end_column;
	return range;
}

String get_report_path(const String &p_file) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings == nullptr) {
		return p_file;
	}
	return project_settings->localize_path(p_file);
}

String get_sarif_path(const String &p_report_path) {
	if (p_report_path.begins_with("res://")) {
		return p_report_path.trim_prefix("res://");
	}
	return p_report_path;
}

void add_diagnostic(
		Vector<FSLintCLI::Diagnostic> &r_diagnostics,
		const String &p_path,
		const String &p_sarif_path,
		const FSLintCLI::Range &p_range,
		FSLintCLI::Severity p_severity,
		const String &p_rule_id,
		const String &p_message) {
	FSLintCLI::Diagnostic diagnostic;
	diagnostic.path = p_path;
	diagnostic.sarif_path = p_sarif_path;
	diagnostic.range = p_range;
	diagnostic.severity = p_severity;
	diagnostic.rule_id = p_rule_id;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
}

void add_parser_errors(
		Vector<FSLintCLI::Diagnostic> &r_diagnostics,
		const String &p_path,
		const String &p_sarif_path,
		const Vector<String> &p_lines,
		const List<FSParser::ParserError> &p_errors,
		const String &p_rule_id) {
	for (const FSParser::ParserError &error : p_errors) {
		add_diagnostic(
				r_diagnostics,
				p_path,
				p_sarif_path,
				make_range(p_lines, error.line, error.column),
				FSLintCLI::SEVERITY_ERROR,
				p_rule_id,
				error.message);
	}
}

} // namespace

FSLintCLI::Options FSLintCLI::parse_options(const List<String> &p_cmdline_args, String &r_error) {
	Options options;
	bool reached_command = false;
	for (const List<String>::Element *element = p_cmdline_args.front(); element; element = element->next()) {
		const String &argument = element->get();
		if (!reached_command) {
			if (argument == "--foundry_script-lint") {
				reached_command = true;
			}
			continue;
		}

		if (argument == "--format=json") {
			options.output_format = OUTPUT_JSON;
		} else if (argument == "--format=sarif") {
			options.output_format = OUTPUT_SARIF;
		} else if (argument.begins_with("--format=")) {
			r_error = "Invalid --format value. Expected json or sarif.";
			return options;
		} else if (argument == "--fail-on=error") {
			options.fail_on = FAIL_ON_ERROR;
		} else if (argument == "--fail-on=warning") {
			options.fail_on = FAIL_ON_WARNING;
		} else if (argument.begins_with("--fail-on=")) {
			r_error = "Invalid --fail-on value. Expected error or warning.";
			return options;
		} else if (argument == "--out") {
			const List<String>::Element *next = element->next();
			if (next == nullptr || next->get().begins_with("-")) {
				r_error = "Missing file path after --out.";
				return options;
			}
			options.output_path = next->get();
			element = next;
		} else {
			options.paths.push_back(argument);
		}
	}

	return options;
}

void FSLintCLI::collect_files_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error) {
	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &open_error);
	if (dir.is_null() || open_error != OK) {
		if (CoreGlobals::print_error_enabled) {
			fprintf(stderr, "%s: could not open directory\n", p_dir.utf8().get_data());
		}
		r_had_error = true;
		return;
	}

	dir->set_include_hidden(false);
	if (dir->list_dir_begin() != OK) {
		if (CoreGlobals::print_error_enabled) {
			fprintf(stderr, "%s: could not list directory\n", p_dir.utf8().get_data());
		}
		r_had_error = true;
		return;
	}

	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == ".." || dir->current_is_hidden()) {
			continue;
		}
		const String full_path = p_dir.path_join(entry);
		if (dir->current_is_dir()) {
			if (dir->is_link(entry)) {
				continue;
			}
			collect_files_recursive(full_path, r_files, r_had_error);
		} else if (entry.get_extension() == "fs") {
			r_files.push_back(full_path);
		}
	}
	dir->list_dir_end();
}

Vector<String> FSLintCLI::collect_files(const Vector<String> &p_paths, bool &r_had_error) {
	Vector<String> files;
	for (const String &path : p_paths) {
		if (DirAccess::exists(path)) {
			collect_files_recursive(path, files, r_had_error);
		} else if (FileAccess::exists(path)) {
			if (path.get_extension() == "fs") {
				files.push_back(path);
			}
		} else {
			if (CoreGlobals::print_error_enabled) {
				fprintf(stderr, "%s: no such file or directory\n", path.utf8().get_data());
			}
			r_had_error = true;
		}
	}
	files.sort();
	return files;
}

FSLintCLI::Result FSLintCLI::lint_paths(const Vector<String> &p_paths, const Options &p_options) {
	(void)p_options;

	Result result;
	bool had_collection_error = false;
	const Vector<String> files = collect_files(p_paths, had_collection_error);
	if (had_collection_error) {
		result.had_command_error = true;
		result.command_error = "Could not collect all input files.";
	}

	for (const String &file : files) {
		const String report_path = get_report_path(file);
		const String sarif_path = get_sarif_path(report_path);
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(file, &read_error);
		if (read_error != OK) {
			const String message = vformat("%s: could not read file", file);
			result.had_command_error = true;
			if (result.command_error.is_empty()) {
				result.command_error = message;
			}
			if (CoreGlobals::print_error_enabled) {
				fprintf(stderr, "%s\n", message.utf8().get_data());
			}
			continue;
		}

		const Vector<String> lines = source.split("\n");
		FSParser parser;
		const Error parse_error = parser.parse(source, report_path, false);
		if (parse_error != OK || !parser.get_errors().is_empty()) {
			add_parser_errors(result.diagnostics, report_path, sarif_path, lines, parser.get_errors(), "parse-error");
			continue;
		}

		FSAnalyzer analyzer(&parser);
		analyzer.analyze();
		add_parser_errors(result.diagnostics, report_path, sarif_path, lines, parser.get_errors(), "analyzer-error");

#ifdef DEBUG_ENABLED
		for (const FSWarning &warning : parser.get_warnings()) {
			add_diagnostic(
					result.diagnostics,
					report_path,
					sarif_path,
					make_range(lines, warning.start_line, 1),
					SEVERITY_WARNING,
					warning.get_name(),
					warning.get_message());
		}
#endif
	}

	return result;
}

String FSLintCLI::severity_to_string(Severity p_severity) {
	switch (p_severity) {
		case SEVERITY_ERROR:
			return "error";
		case SEVERITY_WARNING:
			return "warning";
		case SEVERITY_NOTE:
			return "note";
	}
	return "error";
}

String FSLintCLI::sarif_level_for_severity(Severity p_severity) {
	return severity_to_string(p_severity);
}

Dictionary FSLintCLI::diagnostic_to_dictionary(const Diagnostic &p_diagnostic) {
	Dictionary range;
	range["startLine"] = p_diagnostic.range.start_line;
	range["startColumn"] = p_diagnostic.range.start_column;
	range["endLine"] = p_diagnostic.range.end_line;
	range["endColumn"] = p_diagnostic.range.end_column;

	Dictionary diagnostic;
	diagnostic["path"] = p_diagnostic.path;
	diagnostic["range"] = range;
	diagnostic["severity"] = severity_to_string(p_diagnostic.severity);
	diagnostic["source"] = p_diagnostic.source;
	diagnostic["ruleId"] = p_diagnostic.rule_id;
	diagnostic["message"] = p_diagnostic.message;
	return diagnostic;
}

String FSLintCLI::to_json(const Vector<Diagnostic> &p_diagnostics) {
	Array diagnostics;
	for (const Diagnostic &diagnostic : p_diagnostics) {
		diagnostics.push_back(diagnostic_to_dictionary(diagnostic));
	}

	Dictionary root;
	root["version"] = 1;
	root["diagnostics"] = diagnostics;
	return JSON::stringify(root, "\t", true, true) + "\n";
}

String FSLintCLI::to_sarif(const Vector<Diagnostic> &p_diagnostics) {
	Dictionary rules_by_id;
	Array results;

	for (const Diagnostic &diagnostic : p_diagnostics) {
		if (!rules_by_id.has(diagnostic.rule_id)) {
			Dictionary rule;
			rule["id"] = diagnostic.rule_id;
			rules_by_id[diagnostic.rule_id] = rule;
		}

		Dictionary message;
		message["text"] = diagnostic.message;

		Dictionary artifact_location;
		artifact_location["uri"] = diagnostic.sarif_path.is_empty() ? diagnostic.path : diagnostic.sarif_path;

		Dictionary region;
		region["startLine"] = diagnostic.range.start_line;
		region["startColumn"] = diagnostic.range.start_column;
		region["endLine"] = diagnostic.range.end_line;
		region["endColumn"] = diagnostic.range.end_column;

		Dictionary physical_location;
		physical_location["artifactLocation"] = artifact_location;
		physical_location["region"] = region;

		Dictionary location;
		location["physicalLocation"] = physical_location;

		Array locations;
		locations.push_back(location);

		Dictionary result;
		result["ruleId"] = diagnostic.rule_id;
		result["level"] = sarif_level_for_severity(diagnostic.severity);
		result["message"] = message;
		result["locations"] = locations;
		results.push_back(result);
	}

	Array rule_ids = rules_by_id.keys();
	rule_ids.sort();

	Array rules;
	for (int i = 0; i < rule_ids.size(); i++) {
		rules.push_back(rules_by_id[rule_ids[i]]);
	}

	Dictionary driver;
	driver["name"] = "Foundry Script Lint";
	driver["rules"] = rules;

	Dictionary tool;
	tool["driver"] = driver;

	Dictionary run;
	run["tool"] = tool;
	run["results"] = results;

	Array runs;
	runs.push_back(run);

	Dictionary root;
	root["version"] = "2.1.0";
	root["$schema"] = "https://json.schemastore.org/sarif-2.1.0.json";
	root["runs"] = runs;
	return JSON::stringify(root, "\t", true, true) + "\n";
}

int FSLintCLI::Result::get_exit_code(const Options &p_options) const {
	if (had_command_error) {
		return 2;
	}
	for (const Diagnostic &diagnostic : diagnostics) {
		if (diagnostic.severity == SEVERITY_ERROR) {
			return 1;
		}
		if (p_options.fail_on == FAIL_ON_WARNING && diagnostic.severity == SEVERITY_WARNING) {
			return 1;
		}
	}
	return 0;
}

Error FSLintCLI::write_report(const Options &p_options, const Result &p_result) {
	const String report = p_options.output_format == OUTPUT_SARIF ? to_sarif(p_result.diagnostics) : to_json(p_result.diagnostics);
	const CharString report_utf8 = report.utf8();

	if (p_options.output_path.is_empty()) {
		clearerr(stdout);
		const size_t report_length = report_utf8.length();
		const size_t written = fwrite(report_utf8.get_data(), 1, report_length, stdout);
		if (written != report_length || ferror(stdout)) {
			fprintf(stderr, "foundry_script-lint: could not write report to stdout\n");
			return ERR_CANT_CREATE;
		}
		if (fflush(stdout) != 0 || ferror(stdout)) {
			fprintf(stderr, "foundry_script-lint: could not flush report to stdout\n");
			return ERR_CANT_CREATE;
		}
		return OK;
	}

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_options.output_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		fprintf(stderr, "foundry_script-lint: could not open report file '%s' for writing\n", p_options.output_path.utf8().get_data());
		return ERR_CANT_OPEN;
	}

	if (!file->store_string(report)) {
		fprintf(stderr, "foundry_script-lint: could not write report file '%s'\n", p_options.output_path.utf8().get_data());
		return ERR_CANT_CREATE;
	}

	return OK;
}

void FSLintCLI::run_from_cmdline() {
	String error;
	Options options = parse_options(OS::get_singleton()->get_cmdline_args(), error);
	if (!error.is_empty()) {
		fprintf(stderr, "foundry_script-lint: %s\n", error.utf8().get_data());
		OS::get_singleton()->set_exit_code(2);
		return;
	}

	if (options.paths.is_empty()) {
		options.paths.push_back(ProjectSettings::get_singleton()->get_resource_path());
	}

	Result result = lint_paths(options.paths, options);
	if (write_report(options, result) != OK) {
		result.had_command_error = true;
		if (result.command_error.is_empty()) {
			result.command_error = "Could not write lint report.";
		}
	}

	OS::get_singleton()->set_exit_code(result.get_exit_code(options));
}
