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
	PackedStringArray legacy_prefix;
	FoundryCLIParser::ParseResult result;
};

static void append(PackedStringArray &r_args, const String &p_arg) {
	r_args.push_back(p_arg);
}

static void append_pair(PackedStringArray &r_args, const String &p_option, const String &p_value) {
	r_args.push_back(p_option);
	r_args.push_back(p_value);
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

static void append_project(PackedStringArray &r_args, const String &p_project_path) {
	if (!p_project_path.is_empty()) {
		append_pair(r_args, "--path", p_project_path);
	}
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

static void append_user_args(PackedStringArray &r_args, const PackedStringArray &p_user_args) {
	if (p_user_args.is_empty()) {
		return;
	}
	append(r_args, "--");
	for (int i = 0; i < p_user_args.size(); i++) {
		append(r_args, p_user_args[i]);
	}
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
			p_arg == "--no-header" || p_arg == "--headless" || p_arg == "--disable-crash-handler") {
		if (p_arg == "--no-header") {
			r_state.result.no_header = true;
		}
		append(r_state.legacy_prefix, p_arg);
		r_state.index++;
		return true;
	}

	for (const char *option : value_options) {
		if (p_arg == option) {
			String value;
			if (!require_value(r_state, p_arg, value)) {
				return true;
			}
			append_pair(r_state.legacy_prefix, p_arg, value);
			return true;
		}
	}

	return false;
}

static PackedStringArray base_args(const CLIParseState &p_state) {
	PackedStringArray normalized;
	if (p_state.has_executable_arg && !p_state.args.is_empty()) {
		append(normalized, p_state.args[0]);
	}
	append_trust(normalized, p_state.result.trusted);
	for (int i = 0; i < p_state.legacy_prefix.size(); i++) {
		append(normalized, p_state.legacy_prefix[i]);
	}
	append_json_defaults(normalized, p_state.result.json);
	return normalized;
}

static void set_command_path(FoundryCLIParser::ParseResult &r_result, const String &p_group, const String &p_command) {
	r_result.command_path.clear();
	append(r_result.command_path, p_group);
	append(r_result.command_path, p_command);
}

static void parse_project_run(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "run");
	String scene;
	String script;
	bool check_only = false;

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
			if (!require_value(r_state, arg, scene)) {
				return;
			}
		} else if (arg == "--script") {
			if (!require_value(r_state, arg, script)) {
				return;
			}
		} else if (arg == "--check-only") {
			check_only = true;
			r_state.index++;
		} else {
			fail(r_state.result, "Unknown option for project run: " + arg + ".");
			return;
		}
	}

	PackedStringArray normalized = base_args(r_state);
	append_project(normalized, r_state.project_path);
	if (!scene.is_empty()) {
		append_pair(normalized, "--scene", scene);
	}
	if (!script.is_empty()) {
		append_pair(normalized, "--script", script);
	}
	if (check_only) {
		append(normalized, "--check-only");
	}
	append_user_args(normalized, r_state.result.user_args);
	r_state.result.normalized_args = normalized;
}

static void parse_project_export(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "export");
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

	String export_option;
	if (mode == "release") {
		export_option = "--export-release";
	} else if (mode == "debug") {
		export_option = "--export-debug";
	} else if (mode == "pack") {
		export_option = "--export-pack";
	} else if (mode == "patch") {
		export_option = "--export-patch";
	} else {
		fail(r_state.result, "project export --mode must be release, debug, pack, or patch.");
		return;
	}

	PackedStringArray normalized = base_args(r_state);
	append_project(normalized, r_state.project_path);
	append(normalized, export_option);
	append(normalized, preset);
	append(normalized, output);
	if (!patches.is_empty()) {
		append_pair(normalized, "--patches", patches);
	}
	if (install_android_template) {
		append(normalized, "--install-android-build-template");
	}
	r_state.result.normalized_args = normalized;
}

static void parse_project_test(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "test");
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

	PackedStringArray normalized = base_args(r_state);
	append_project(normalized, r_state.project_path);
	append_pair(normalized, "--run-test-runner", runner);
	append_user_args(normalized, r_state.result.user_args);
	r_state.result.normalized_args = normalized;
}

static void parse_project_import(CLIParseState &r_state) {
	set_command_path(r_state.result, "project", "import");
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
	PackedStringArray normalized = base_args(r_state);
	append_project(normalized, r_state.project_path);
	append(normalized, "--import");
	r_state.result.normalized_args = normalized;
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
	PackedStringArray formatter_args;
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
		append(formatter_args, r_state.args[r_state.index++]);
	}

	PackedStringArray normalized = base_args(r_state);
	append_headless(normalized);
	append_project(normalized, r_state.project_path);
	append(normalized, "--foundry_script-format");
	for (int i = 0; i < formatter_args.size(); i++) {
		append(normalized, formatter_args[i]);
	}
	r_state.result.normalized_args = normalized;
}

static void parse_script_lint(CLIParseState &r_state) {
	set_command_path(r_state.result, "script", "lint");
	PackedStringArray lint_args;
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
		append(lint_args, r_state.args[r_state.index++]);
	}

	PackedStringArray normalized = base_args(r_state);
	append_headless(normalized);
	append_project(normalized, r_state.project_path);
	append(normalized, "--foundry_script-lint");
	for (int i = 0; i < lint_args.size(); i++) {
		append(normalized, lint_args[i]);
	}
	r_state.result.normalized_args = normalized;
}

static void parse_script_migrate(CLIParseState &r_state) {
	set_command_path(r_state.result, "script", "migrate");
	bool apply = false;
	bool strict_null = false;
	bool strict_dynamic = false;
	bool activate_strict = false;
	bool confirm = false;
	bool allow_violations = false;
	bool acknowledge_vcs = false;
	String follow_up;

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
			apply = true;
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
					strict_null = true;
				} else if (stripped == "dynamic") {
					strict_dynamic = true;
				} else {
					fail(r_state.result, "script migrate --strict accepts null and dynamic.");
					return;
				}
			}
		} else if (arg == "--activate-strict") {
			activate_strict = true;
			r_state.index++;
		} else if (arg == "--confirm") {
			confirm = true;
			r_state.index++;
		} else if (arg == "--allow-violations") {
			allow_violations = true;
			r_state.index++;
		} else if (arg == "--acknowledge-vcs") {
			acknowledge_vcs = true;
			r_state.index++;
		} else if (arg == "--follow-up") {
			if (!require_value(r_state, arg, follow_up)) {
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

	PackedStringArray normalized = base_args(r_state);
	append_headless(normalized);
	append_pair(normalized, "--foundry_script-migrate", r_state.project_path);
	if (apply) {
		append(normalized, "--foundry_script-migrate-apply");
	}
	if (strict_null) {
		append(normalized, "--foundry_script-migrate-strict-null-checks");
	}
	if (strict_dynamic) {
		append(normalized, "--foundry_script-migrate-strict-dynamic-checks");
	}
	if (activate_strict) {
		append(normalized, "--foundry_script-migrate-activate-strict");
	}
	if (confirm) {
		append(normalized, "--foundry_script-migrate-confirm");
	}
	if (allow_violations) {
		append(normalized, "--foundry_script-migrate-allow-violations");
	}
	if (acknowledge_vcs) {
		append(normalized, "--foundry_script-migrate-acknowledge-vcs");
	}
	if (!follow_up.is_empty()) {
		append_pair(normalized, "--foundry_script-migrate-follow-up", follow_up);
	}
	r_state.result.normalized_args = normalized;
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

static void parse_test_run(CLIParseState &r_state) {
	set_command_path(r_state.result, "test", "run");
	String test_case;
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
			if (!require_value(r_state, arg, test_case)) {
				return;
			}
		} else {
			append(passthrough, arg);
			r_state.index++;
		}
	}

	PackedStringArray normalized = base_args(r_state);
	append_headless(normalized);
	append_project(normalized, r_state.project_path);
	append(normalized, "--test");
	if (!test_case.is_empty()) {
		append(normalized, "--test-case=" + test_case);
	}
	for (int i = 0; i < passthrough.size(); i++) {
		append(normalized, passthrough[i]);
	}
	r_state.result.normalized_args = normalized;
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
			fail(r_state.result, "Unknown option for editor " + command + ": " + arg + ".");
			return;
		}
	}

	PackedStringArray normalized = base_args(r_state);
	if (command == "open") {
		append_project(normalized, r_state.project_path);
		append(normalized, "--editor");
	} else if (command == "project-manager") {
		append(normalized, "--project-manager");
	}
	r_state.result.normalized_args = normalized;
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

	String port;
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
			if (!require_value(r_state, arg, port)) {
				return;
			}
		} else {
			fail(r_state.result, "Unknown option for lsp serve: " + arg + ".");
			return;
		}
	}

	PackedStringArray normalized = base_args(r_state);
	append_project(normalized, r_state.project_path);
	append(normalized, "--editor");
	if (!port.is_empty()) {
		append_pair(normalized, "--lsp-port", port);
	}
	r_state.result.normalized_args = normalized;
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
		bool include_docs = false;
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
				include_docs = true;
				r_state.index++;
			} else {
				fail(r_state.result, "Unknown option for docs generate-api: " + arg + ".");
				return;
			}
		}
		PackedStringArray normalized = base_args(r_state);
		append(normalized, include_docs ? "--dump-extension-api-with-docs" : "--dump-extension-api");
		r_state.result.normalized_args = normalized;
		return;
	}

	if (command == "generate-engine") {
		String output;
		bool no_docbase = false;
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
				if (!require_value(r_state, arg, output)) {
					return;
				}
			} else if (arg == "--no-docbase") {
				no_docbase = true;
				r_state.index++;
			} else {
				fail(r_state.result, "Unknown option for docs generate-engine: " + arg + ".");
				return;
			}
		}
		PackedStringArray normalized = base_args(r_state);
		append(normalized, "--doctool");
		if (!output.is_empty()) {
			append(normalized, output);
		}
		if (no_docbase) {
			append(normalized, "--no-docbase");
		}
		r_state.result.normalized_args = normalized;
		return;
	}

	if (command == "generate-script") {
		String source;
		String output;
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
				if (!require_value(r_state, arg, source)) {
					return;
				}
			} else if (arg == "--output") {
				if (!require_value(r_state, arg, output)) {
					return;
				}
			} else {
				fail(r_state.result, "Unknown option for docs generate-script: " + arg + ".");
				return;
			}
		}
		if (source.is_empty()) {
			fail(r_state.result, "docs generate-script requires --source.");
			return;
		}
		PackedStringArray normalized = base_args(r_state);
		append(normalized, "--doctool");
		if (!output.is_empty()) {
			append(normalized, output);
		}
		append_pair(normalized, "--foundry_script-docs", source);
		r_state.result.normalized_args = normalized;
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
		PackedStringArray normalized = base_args(r_state);
		if (format == "header") {
			append(normalized, "--dump-foundryextension-interface");
		} else if (format == "json") {
			append(normalized, "--dump-foundryextension-interface-json");
		} else {
			fail(r_state.result, "extension dump-interface --format must be header or json.");
			return;
		}
		r_state.result.normalized_args = normalized;
		return;
	}

	if (command == "validate-api") {
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
		PackedStringArray normalized = base_args(r_state);
		append_pair(normalized, "--validate-extension-api", path);
		r_state.result.normalized_args = normalized;
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

	PackedStringArray normalized = base_args(r_state);
	if (command == "render-device-support") {
		append(normalized, "--test-rd-support");
	} else if (command == "render-device-create") {
		append(normalized, "--test-rd-creation");
	}
	r_state.result.normalized_args = normalized;
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
	state.result.normalized_args = p_args;
	if (p_args.is_empty()) {
		return state.result;
	}

	collect_user_args(state);

	if (state.args.is_empty()) {
		return state.result;
	}

	const String first_arg = state.args[0];
	state.has_executable_arg = !is_new_cli_command(first_arg) && first_arg != "help" && !is_help_flag(first_arg) && !first_arg.begins_with("-");
	state.index = state.has_executable_arg ? 1 : 0;
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
		return state.result; // Legacy invocation; leave it untouched.
	}

	if (state.result.used_new_cli) {
		fail(state.result, "Expected a Foundry command after global options.");
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
