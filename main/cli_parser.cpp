/**************************************************************************/
/*  cli_parser.cpp                                                        */
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

#include "cli_parser.h"

namespace {

struct CLIParseState {
	PackedStringArray args;
	bool has_executable_arg = false;
	int index = 0;
	String project_path;
	PackedStringArray global_prefix;
	FoundryCLIParser::ParseResult result;
};

static void append(PackedStringArray &r_args, const String &p_arg) {
	r_args.push_back(p_arg);
}

static bool has_arg(const PackedStringArray &p_args, const String &p_arg) {
	for (int i = 0; i < p_args.size(); i++) {
		if (p_args[i] == p_arg) {
			return true;
		}
	}
	return false;
}

static void fail(FoundryCLIParser::ParseResult &r_result, const String &p_message) {
	r_result.ok = false;
	r_result.error = p_message;
}

static bool is_help_flag(const String &p_arg) {
	return p_arg == "-h" || p_arg == "--help" || p_arg == "/?";
}

static bool is_help_global_flag(const String &p_arg) {
	return p_arg == "--json" || p_arg == "--no-header";
}

static void consume_help_global_flag(CLIParseState &r_state, const String &p_arg) {
	if (p_arg == "--json") {
		r_state.result.json = true;
		r_state.result.used_new_cli = true;
	} else if (p_arg == "--no-header") {
		r_state.result.no_header = true;
	}
}

static void scan_remaining_help_global_flags(CLIParseState &r_state, int p_start_index) {
	for (int i = p_start_index; i < r_state.args.size(); i++) {
		if (is_help_global_flag(r_state.args[i])) {
			consume_help_global_flag(r_state, r_state.args[i]);
		}
	}
}

static void request_help(CLIParseState &r_state) {
	r_state.result.help_requested = true;
	r_state.result.used_new_cli = true;
	scan_remaining_help_global_flags(r_state, r_state.index + 1);
}

static bool parse_stopped(const CLIParseState &p_state) {
	return !p_state.result.ok || p_state.result.help_requested;
}

static bool require_value(CLIParseState &r_state, const String &p_option, String &r_value) {
	if (r_state.index + 1 < r_state.args.size() && is_help_flag(r_state.args[r_state.index + 1])) {
		request_help(r_state);
		return false;
	}
	if (r_state.index + 1 >= r_state.args.size() || r_state.args[r_state.index + 1] == "--") {
		fail(r_state.result, "Missing value for " + p_option + ".");
		return false;
	}
	r_value = r_state.args[r_state.index + 1];
	r_state.index += 2;
	return true;
}

static void append_trust(PackedStringArray &r_args, bool p_trusted) {
	if (p_trusted && !has_arg(r_args, "--foundry-build-trusted")) {
		append(r_args, "--foundry-build-trusted");
	}
}

static void append_headless(PackedStringArray &r_args) {
	if (!has_arg(r_args, "--headless")) {
		append(r_args, "--headless");
	}
}

static void append_json_defaults(PackedStringArray &r_args, bool p_json) {
	if (p_json && !has_arg(r_args, "--no-header")) {
		append(r_args, "--no-header");
	}
}

static void finalize_global_args(CLIParseState &r_state) {
	PackedStringArray global_args;
	if (r_state.has_executable_arg && !r_state.args.is_empty()) {
		append(global_args, r_state.args[0]);
	}
	append_trust(global_args, r_state.result.trusted);
	for (int i = 0; i < r_state.global_prefix.size(); i++) {
		append(global_args, r_state.global_prefix[i]);
	}
	append_json_defaults(global_args, r_state.result.json);
	r_state.result.global_args = global_args;
}

static bool consume_common_project_option(CLIParseState &r_state, const String &p_arg) {
	if (p_arg != "--project") {
		return false;
	}
	String value;
	if (!require_value(r_state, p_arg, value)) {
		return true;
	}
	r_state.project_path = value;
	return true;
}

static bool consume_common_global_option(CLIParseState &r_state, const String &p_arg) {
	static const char *value_options[] = {
		"--audio-driver",
		"--audio-output-latency",
		"--display-driver",
		"--rendering-method",
		"--rendering-driver",
		"--gpu-index",
		"--text-driver",
		"--tablet-driver",
		"--log-file",
		"--language",
		"-l",
	};

	if (p_arg == "--json") {
		r_state.result.json = true;
		r_state.result.used_new_cli = true;
		r_state.index++;
		return true;
	}
	if (p_arg == "--trusted") {
		r_state.result.trusted = true;
		r_state.result.used_new_cli = true;
		r_state.index++;
		return true;
	}
	if (consume_common_project_option(r_state, p_arg)) {
		r_state.result.used_new_cli = true;
		return true;
	}
	if (p_arg == "--quiet" || p_arg == "-q" || p_arg == "--verbose" || p_arg == "-v" ||
			p_arg == "--no-header" || p_arg == "--headless" || p_arg == "--disable-crash-handler" ||
			p_arg == "--recovery-mode" || p_arg == "--quit") {
		if (p_arg == "--no-header") {
			r_state.result.no_header = true;
		}
		append(r_state.global_prefix, p_arg);
		r_state.index++;
		return true;
	}

	for (const char *option : value_options) {
		if (p_arg == option) {
			String value;
			if (!require_value(r_state, p_arg, value)) {
				return true;
			}
			append(r_state.global_prefix, p_arg);
			append(r_state.global_prefix, value);
			return true;
		}
	}

	return false;
}

static void set_command_path(FoundryCLIParser::ParseResult &r_result, const String &p_group, const String &p_command) {
	r_result.command_path.clear();
	append(r_result.command_path, p_group);
	append(r_result.command_path, p_command);
}

static void parse_project_run(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "run");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::PROJECT_RUN;
	PackedStringArray passthrough;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--scene") {
			if (!require_value(r_state, arg, r_state.result.invocation.scene)) {
				return;
			}
		} else if (arg == "--script") {
			if (!require_value(r_state, arg, r_state.result.invocation.script)) {
				return;
			}
		} else if (arg == "--check-only") {
			r_state.result.invocation.check_only = true;
			r_state.index++;
		} else {
			append(passthrough, arg);
			r_state.index++;
		}
	}

	r_state.result.invocation.project_path = r_state.project_path;
	r_state.result.invocation.passthrough_args = passthrough;
	finalize_global_args(r_state);
}

static void parse_project_export(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "export");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::PROJECT_EXPORT;
	String preset;
	String output;
	String mode = "release";
	String patches;
	bool install_android_template = false;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--preset") {
			if (!require_value(r_state, arg, preset)) {
				return;
			}
		} else if (arg == "--output") {
			if (!require_value(r_state, arg, output)) {
				return;
			}
		} else if (arg == "--mode") {
			if (!require_value(r_state, arg, mode)) {
				return;
			}
		} else if (arg == "--patches") {
			if (!require_value(r_state, arg, patches)) {
				return;
			}
		} else if (arg == "--install-android-build-template") {
			install_android_template = true;
			r_state.index++;
		} else {
			fail(r_state.result, "Unknown option for project export: " + arg + ".");
			return;
		}
	}

	if (preset.is_empty()) {
		fail(r_state.result, "project export requires --preset.");
		return;
	}
	if (output.is_empty()) {
		fail(r_state.result, "project export requires --output.");
		return;
	}
	if (mode != "release" && mode != "debug" && mode != "pack" && mode != "patch") {
		fail(r_state.result, "project export --mode must be release, debug, pack, or patch.");
		return;
	}

	r_state.result.invocation.project_path = r_state.project_path;
	r_state.result.invocation.export_preset = preset;
	r_state.result.invocation.export_output = output;
	r_state.result.invocation.export_mode = mode;
	r_state.result.invocation.export_patches = patches;
	r_state.result.invocation.install_android_build_template = install_android_template;
	finalize_global_args(r_state);
}

static void parse_project_test(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "test");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::PROJECT_TEST;
	String runner;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (consume_common_global_option(r_state, arg)) {
			if (!r_state.result.ok) {
				return;
			}
			continue;
		}
		if (arg == "--runner") {
			if (!require_value(r_state, arg, runner)) {
				return;
			}
		} else {
			fail(r_state.result, "Unknown option for project test: " + arg + ".");
			return;
		}
	}

	if (runner.is_empty()) {
		fail(r_state.result, "project test requires --runner.");
		return;
	}

	r_state.result.invocation.project_path = r_state.project_path;
	r_state.result.invocation.runner = runner;
	finalize_global_args(r_state);
}

static void parse_project_import(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "import");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::PROJECT_IMPORT;
	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		} else {
			fail(r_state.result, "Unknown option for project import: " + arg + ".");
			return;
		}
	}
	r_state.result.invocation.project_path = r_state.project_path;
	finalize_global_args(r_state);
}

static void parse_project(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "project requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command == "run") {
		parse_project_run(r_state);
	} else if (command == "export") {
		parse_project_export(r_state);
	} else if (command == "import") {
		parse_project_import(r_state);
	} else if (command == "test") {
		parse_project_test(r_state);
	} else {
		fail(r_state.result, "Unknown project command: " + command + ".");
	}
}

static void parse_script_format(CLIParseState &r_state) {
	set_command_path(r_state.result, "script", "format");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::SCRIPT_FORMAT;
	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		append(r_state.result.invocation.command_args, r_state.args[r_state.index++]);
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	finalize_global_args(r_state);
}

static void parse_script_lint(CLIParseState &r_state) {
	set_command_path(r_state.result, "script", "lint");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::SCRIPT_LINT;
	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		append(r_state.result.invocation.command_args, r_state.args[r_state.index++]);
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	finalize_global_args(r_state);
}

static void parse_script_migrate(CLIParseState &r_state) {
	set_command_path(r_state.result, "script", "migrate");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::SCRIPT_MIGRATE;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--apply") {
			r_state.result.invocation.migrate_apply = true;
			r_state.index++;
		} else if (arg == "--strict") {
			String value;
			if (!require_value(r_state, arg, value)) {
				return;
			}
			const Vector<String> entries = value.split(",", false);
			for (const String &entry : entries) {
				const String stripped = entry.strip_edges();
				if (stripped == "null") {
					r_state.result.invocation.migrate_strict_null = true;
				} else if (stripped == "dynamic") {
					r_state.result.invocation.migrate_strict_dynamic = true;
				} else {
					fail(r_state.result, "script migrate --strict accepts null and dynamic.");
					return;
				}
			}
		} else if (arg == "--activate-strict") {
			r_state.result.invocation.migrate_activate_strict = true;
			r_state.index++;
		} else if (arg == "--confirm") {
			r_state.result.invocation.migrate_confirm = true;
			r_state.index++;
		} else if (arg == "--allow-violations") {
			r_state.result.invocation.migrate_allow_violations = true;
			r_state.index++;
		} else if (arg == "--acknowledge-vcs") {
			r_state.result.invocation.migrate_acknowledge_vcs = true;
			r_state.index++;
		} else if (arg == "--follow-up") {
			if (!require_value(r_state, arg, r_state.result.invocation.migrate_follow_up)) {
				return;
			}
		} else {
			fail(r_state.result, "Unknown option for script migrate: " + arg + ".");
			return;
		}
	}

	if (r_state.project_path.is_empty()) {
		fail(r_state.result, "script migrate requires --project.");
		return;
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	finalize_global_args(r_state);
}

static void parse_script(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "script requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command == "format") {
		parse_script_format(r_state);
	} else if (command == "lint") {
		parse_script_lint(r_state);
	} else if (command == "migrate") {
		parse_script_migrate(r_state);
	} else {
		fail(r_state.result, "Unknown script command: " + command + ".");
	}
}

static void parse_test_generate_fixtures(CLIParseState &r_state) {
	set_command_path(r_state.result, "test", "generate-fixtures");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::TEST_GENERATE_FIXTURES;
	PackedStringArray paths;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--print-filenames") {
			r_state.result.invocation.print_filenames = true;
			r_state.index++;
		} else if (arg.begins_with("-")) {
			fail(r_state.result, "Unknown option for test generate-fixtures: " + arg + ".");
			return;
		} else {
			append(paths, arg);
			r_state.index++;
		}
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	if (paths.is_empty()) {
		append(r_state.result.invocation.command_args, "modules/foundry_script/tests/scripts");
	} else {
		r_state.result.invocation.command_args = paths;
	}
	finalize_global_args(r_state);
}

static void parse_test_generate_format_fixtures(CLIParseState &r_state) {
	set_command_path(r_state.result, "test", "generate-format-fixtures");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::TEST_GENERATE_FORMAT_FIXTURES;
	PackedStringArray paths;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg.begins_with("-")) {
			fail(r_state.result, "Unknown option for test generate-format-fixtures: " + arg + ".");
			return;
		}
		append(paths, arg);
		r_state.index++;
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	if (paths.is_empty()) {
		append(r_state.result.invocation.command_args, "modules/foundry_script/tests/scripts/format");
	} else {
		r_state.result.invocation.command_args = paths;
	}
	finalize_global_args(r_state);
}

static void parse_test_run(CLIParseState &r_state) {
	set_command_path(r_state.result, "test", "run");
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::TEST_RUN;
	PackedStringArray passthrough;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--case") {
			if (!require_value(r_state, arg, r_state.result.invocation.test_case)) {
				return;
			}
		} else {
			append(passthrough, arg);
			r_state.index++;
		}
	}

	append_headless(r_state.global_prefix);
	r_state.result.invocation.project_path = r_state.project_path;
	r_state.result.invocation.passthrough_args = passthrough;
	finalize_global_args(r_state);
}

static void parse_test(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "test requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command == "run") {
		parse_test_run(r_state);
	} else if (command == "generate-fixtures") {
		parse_test_generate_fixtures(r_state);
	} else if (command == "generate-format-fixtures") {
		parse_test_generate_format_fixtures(r_state);
	} else {
		fail(r_state.result, "Unknown test command: " + command + ".");
	}
}

static void parse_editor(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "editor requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command != "open" && command != "project-manager") {
		fail(r_state.result, "Unknown editor command: " + command + ".");
		return;
	}
	set_command_path(r_state.result, "editor", command);
	r_state.result.invocation.kind = command == "open"
			? FoundryCLIParser::CLIInvocation::EDITOR_OPEN
			: FoundryCLIParser::CLIInvocation::EDITOR_PROJECT_MANAGER;

	PackedStringArray passthrough;
	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (command == "open" && !arg.begins_with("-")) {
			append(passthrough, arg);
			r_state.index++;
			continue;
		}
		fail(r_state.result, "Unknown option for editor " + command + ": " + arg + ".");
		return;
	}

	r_state.result.invocation.project_path = r_state.project_path;
	r_state.result.invocation.passthrough_args = passthrough;
	finalize_global_args(r_state);
}

static void parse_lsp(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "lsp requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command != "serve") {
		fail(r_state.result, "Unknown lsp command: " + command + ".");
		return;
	}
	set_command_path(r_state.result, "lsp", command);
	r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::LSP_SERVE;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		if (arg == "--port") {
			if (!require_value(r_state, arg, r_state.result.invocation.lsp_port)) {
				return;
			}
		} else {
			fail(r_state.result, "Unknown option for lsp serve: " + arg + ".");
			return;
		}
	}

	r_state.result.invocation.project_path = r_state.project_path;
	finalize_global_args(r_state);
}

static void parse_docs(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "docs requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command != "generate-api" && command != "generate-engine" && command != "generate-script") {
		fail(r_state.result, "Unknown docs command: " + command + ".");
		return;
	}
	set_command_path(r_state.result, "docs", command);

	if (command == "generate-api") {
		r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::DOCS_GENERATE_API;
		while (r_state.index < r_state.args.size()) {
			const String arg = r_state.args[r_state.index];
			if (is_help_flag(arg)) {
				request_help(r_state);
				return;
			}
			if (consume_common_global_option(r_state, arg)) {
				if (parse_stopped(r_state)) {
					return;
				}
			} else if (arg == "--include-docs") {
				r_state.result.invocation.docs_include_docs = true;
				r_state.index++;
			} else {
				fail(r_state.result, "Unknown option for docs generate-api: " + arg + ".");
				return;
			}
		}
		finalize_global_args(r_state);
		return;
	}

	if (command == "generate-engine") {
		r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::DOCS_GENERATE_ENGINE;
		while (r_state.index < r_state.args.size()) {
			const String arg = r_state.args[r_state.index];
			if (is_help_flag(arg)) {
				request_help(r_state);
				return;
			}
			if (consume_common_global_option(r_state, arg)) {
				if (parse_stopped(r_state)) {
					return;
				}
			} else if (arg == "--output") {
				if (!require_value(r_state, arg, r_state.result.invocation.docs_engine_output)) {
					return;
				}
			} else if (arg == "--no-docbase") {
				r_state.result.invocation.docs_no_docbase = true;
				r_state.index++;
			} else {
				fail(r_state.result, "Unknown option for docs generate-engine: " + arg + ".");
				return;
			}
		}
		finalize_global_args(r_state);
		return;
	}

	if (command == "generate-script") {
		r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::DOCS_GENERATE_SCRIPT;
		while (r_state.index < r_state.args.size()) {
			const String arg = r_state.args[r_state.index];
			if (is_help_flag(arg)) {
				request_help(r_state);
				return;
			}
			if (consume_common_global_option(r_state, arg)) {
				if (parse_stopped(r_state)) {
					return;
				}
			} else if (arg == "--source") {
				if (!require_value(r_state, arg, r_state.result.invocation.docs_script_source)) {
					return;
				}
			} else if (arg == "--output") {
				if (!require_value(r_state, arg, r_state.result.invocation.docs_script_output)) {
					return;
				}
			} else {
				fail(r_state.result, "Unknown option for docs generate-script: " + arg + ".");
				return;
			}
		}
		if (r_state.result.invocation.docs_script_source.is_empty()) {
			fail(r_state.result, "docs generate-script requires --source.");
			return;
		}
		finalize_global_args(r_state);
	}
}

static void parse_extension(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "extension requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command != "dump-interface" && command != "validate-api") {
		fail(r_state.result, "Unknown extension command: " + command + ".");
		return;
	}
	set_command_path(r_state.result, "extension", command);

	if (command == "dump-interface") {
		r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::EXTENSION_DUMP_INTERFACE;
		String format = "header";
		while (r_state.index < r_state.args.size()) {
			const String arg = r_state.args[r_state.index];
			if (is_help_flag(arg)) {
				request_help(r_state);
				return;
			}
			if (consume_common_global_option(r_state, arg)) {
				if (parse_stopped(r_state)) {
					return;
				}
			} else if (arg == "--format") {
				if (!require_value(r_state, arg, format)) {
					return;
				}
			} else {
				fail(r_state.result, "Unknown option for extension dump-interface: " + arg + ".");
				return;
			}
		}
		if (format != "header" && format != "json") {
			fail(r_state.result, "extension dump-interface --format must be header or json.");
			return;
		}
		r_state.result.invocation.extension_interface_format = format;
		finalize_global_args(r_state);
		return;
	}

	if (command == "validate-api") {
		r_state.result.invocation.kind = FoundryCLIParser::CLIInvocation::EXTENSION_VALIDATE_API;
		String path;
		while (r_state.index < r_state.args.size()) {
			const String arg = r_state.args[r_state.index];
			if (is_help_flag(arg)) {
				request_help(r_state);
				return;
			}
			if (consume_common_global_option(r_state, arg)) {
				if (parse_stopped(r_state)) {
					return;
				}
			} else if (arg == "--input") {
				if (!require_value(r_state, arg, path)) {
					return;
				}
			} else {
				fail(r_state.result, "Unknown option for extension validate-api: " + arg + ".");
				return;
			}
		}
		if (path.is_empty()) {
			fail(r_state.result, "extension validate-api requires --input.");
			return;
		}
		r_state.result.invocation.extension_validate_input = path;
		finalize_global_args(r_state);
	}
}

static void parse_diagnostics(CLIParseState &r_state) {
	if (r_state.index >= r_state.args.size()) {
		fail(r_state.result, "diagnostics requires a command.");
		return;
	}
	const String command = r_state.args[r_state.index++];
	if (is_help_flag(command)) {
		request_help(r_state);
		return;
	}
	if (command != "render-device-support" && command != "render-device-create") {
		fail(r_state.result, "Unknown diagnostics command: " + command + ".");
		return;
	}
	set_command_path(r_state.result, "diagnostics", command);
	r_state.result.invocation.kind = command == "render-device-support"
			? FoundryCLIParser::CLIInvocation::DIAGNOSTICS_RENDER_DEVICE_SUPPORT
			: FoundryCLIParser::CLIInvocation::DIAGNOSTICS_RENDER_DEVICE_CREATE;

	while (r_state.index < r_state.args.size()) {
		const String arg = r_state.args[r_state.index];
		if (is_help_flag(arg)) {
			request_help(r_state);
			return;
		}
		if (consume_common_global_option(r_state, arg)) {
			if (parse_stopped(r_state)) {
				return;
			}
			continue;
		}
		fail(r_state.result, "diagnostics " + command + " does not accept extra arguments.");
		return;
	}

	finalize_global_args(r_state);
}

static void collect_user_args(CLIParseState &r_state) {
	for (int i = 0; i < r_state.args.size(); i++) {
		if (r_state.args[i] == "--") {
			for (int user_index = i + 1; user_index < r_state.args.size(); user_index++) {
				append(r_state.result.user_args, r_state.args[user_index]);
			}
			r_state.args.resize(i);
			return;
		}
	}
}

static bool is_legacy_workflow_flag(const String &p_arg, String &r_replacement) {
	if (p_arg == "--editor" || p_arg == "-e") {
		r_replacement = "`foundry editor open --project <dir>`";
		return true;
	}
	if (p_arg == "--project-manager" || p_arg == "-p") {
		r_replacement = "`foundry editor project-manager`";
		return true;
	}
	if (p_arg == "--path") {
		r_replacement = "`--project <dir>` with a Foundry command";
		return true;
	}
	if (p_arg == "--import") {
		r_replacement = "`foundry project import --project <dir>`";
		return true;
	}
	if (p_arg == "--test") {
		r_replacement = "`foundry test run`";
		return true;
	}
	if (p_arg == "--export-release" || p_arg == "--export-debug" || p_arg == "--export-pack" || p_arg == "--export-patch") {
		r_replacement = "`foundry project export --project <dir> --preset <name> --output <path> --mode release|debug|pack|patch`";
		return true;
	}
	if (p_arg == "--run-test-runner") {
		r_replacement = "`foundry project test --project <dir> --runner <path>`";
		return true;
	}
	if (p_arg == "--foundry_script-format") {
		r_replacement = "`foundry script format`";
		return true;
	}
	if (p_arg == "--foundry_script-lint") {
		r_replacement = "`foundry script lint`";
		return true;
	}
	if (p_arg == "--foundry_script-migrate" || p_arg.begins_with("--foundry_script-migrate-")) {
		r_replacement = "`foundry script migrate`";
		return true;
	}
	if (p_arg == "--foundry_script-generate-tests") {
		r_replacement = "`foundry test generate-fixtures`";
		return true;
	}
	if (p_arg == "--foundry_script-generate-format-tests") {
		r_replacement = "`foundry test generate-format-fixtures`";
		return true;
	}
	if (p_arg == "--doctool") {
		r_replacement = "`foundry docs generate-engine` or `foundry docs generate-script`";
		return true;
	}
	if (p_arg == "--foundry_script-docs") {
		r_replacement = "`foundry docs generate-script --source <path>`";
		return true;
	}
	if (p_arg == "--dump-extension-api" || p_arg == "--dump-extension-api-with-docs") {
		r_replacement = "`foundry docs generate-api`";
		return true;
	}
	if (p_arg == "--dump-foundryextension-interface" || p_arg == "--dump-foundryextension-interface-json") {
		r_replacement = "`foundry extension dump-interface`";
		return true;
	}
	if (p_arg == "--validate-extension-api") {
		r_replacement = "`foundry extension validate-api --input <path>`";
		return true;
	}
	if (p_arg == "--test-rd-support") {
		r_replacement = "`foundry diagnostics render-device-support`";
		return true;
	}
	if (p_arg == "--test-rd-creation") {
		r_replacement = "`foundry diagnostics render-device-create`";
		return true;
	}
	if (p_arg == "--lsp-port") {
		r_replacement = "`foundry lsp serve --port <port>`";
		return true;
	}
	return false;
}

static bool reject_legacy_workflow_flags(CLIParseState &r_state, int p_start_index) {
	for (int i = p_start_index; i < r_state.args.size(); i++) {
		String replacement;
		if (is_legacy_workflow_flag(r_state.args[i], replacement)) {
			fail(r_state.result, vformat("`%s` has been removed. Use %s.", r_state.args[i], replacement));
			return true;
		}
	}
	return false;
}

static void pass_through_global_args(CLIParseState &r_state, int p_start_index) {
	PackedStringArray global_args;
	for (int i = 0; i < r_state.args.size(); i++) {
		append(global_args, r_state.args[i]);
	}
	r_state.result.global_args = global_args;
	r_state.index = p_start_index;
}

} // namespace

bool FoundryCLIParser::is_new_cli_command(const String &p_arg) {
	return p_arg == "editor" ||
			p_arg == "project" ||
			p_arg == "script" ||
			p_arg == "test" ||
			p_arg == "lsp" ||
			p_arg == "docs" ||
			p_arg == "extension" ||
			p_arg == "diagnostics";
}

FoundryCLIParser::ParseResult FoundryCLIParser::parse(const PackedStringArray &p_args) {
	CLIParseState state;
	state.args = p_args;
	if (p_args.is_empty()) {
		return state.result;
	}

	collect_user_args(state);

	if (state.args.is_empty()) {
		return state.result;
	}

	const String first_arg = state.args[0];
	state.has_executable_arg = !is_new_cli_command(first_arg) && first_arg != "help" && !is_help_flag(first_arg) && !first_arg.begins_with("-");
	const int start_index = state.has_executable_arg ? 1 : 0;

	if (reject_legacy_workflow_flags(state, start_index)) {
		return state.result;
	}

	state.index = start_index;
	while (state.index < state.args.size()) {
		const String arg = state.args[state.index];
		if (is_help_flag(arg)) {
			request_help(state);
			return state.result;
		}
		if (arg == "help") {
			request_help(state);
			for (int scope_index = state.index + 1; scope_index < state.args.size(); scope_index++) {
				const String &scope_arg = state.args[scope_index];
				if (!is_help_global_flag(scope_arg)) {
					append(state.result.command_path, scope_arg);
				}
			}
			return state.result;
		}
		if (is_new_cli_command(arg)) {
			state.result.used_new_cli = true;
			state.index++;
			state.result.command_path.clear();
			append(state.result.command_path, arg);
			if (arg == "project") {
				parse_project(state);
			} else if (arg == "script") {
				parse_script(state);
			} else if (arg == "test") {
				parse_test(state);
			} else if (arg == "editor") {
				parse_editor(state);
			} else if (arg == "lsp") {
				parse_lsp(state);
			} else if (arg == "docs") {
				parse_docs(state);
			} else if (arg == "extension") {
				parse_extension(state);
			} else if (arg == "diagnostics") {
				parse_diagnostics(state);
			} else {
				fail(state.result, "Command group is not implemented yet: " + arg + ".");
			}
			return state.result;
		}
		if (consume_common_global_option(state, arg)) {
			if (parse_stopped(state)) {
				return state.result;
			}
			continue;
		}
		if (state.result.used_new_cli) {
			fail(state.result, "Expected a Foundry command after global options, got: " + arg + ".");
			return state.result;
		}
		pass_through_global_args(state, start_index);
		return state.result;
	}

	if (state.result.used_new_cli) {
		fail(state.result, "Expected a Foundry command after global options.");
	} else if (!state.global_prefix.is_empty()) {
		finalize_global_args(state);
	} else {
		pass_through_global_args(state, start_index);
	}
	return state.result;
}

FoundryCLIParser::ParseResult FoundryCLIParser::parse(int p_argc, char *p_argv[]) {
	PackedStringArray args;
	for (int i = 0; i < p_argc; i++) {
		args.push_back(String::utf8(p_argv[i]));
	}
	return parse(args);
}
