/**************************************************************************/
/*  cli_parser.h                                                          */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class FoundryCLIParser {
public:
	struct CLIInvocation {
		enum Kind {
			NONE,
			EDITOR_OPEN,
			PROJECT_RUN,
			PROJECT_TEST,
			PROJECT_EXPORT,
			PROJECT_IMPORT,
			SCRIPT_FORMAT,
			SCRIPT_LINT,
			SCRIPT_MIGRATE,
			SCRIPT_EVAL,
			TEST_RUN,
			TEST_GENERATE_FIXTURES,
			TEST_GENERATE_FORMAT_FIXTURES,
			TEST_BENCHMARK,
			TEST_FIXTURES,
			TEST_COMPLETENESS_RUN,
			TEST_COMPLETENESS_SELECT,
			TOOLING_SERVE,
			DOCS_GENERATE_API,
			DOCS_GENERATE_ENGINE,
			DOCS_GENERATE_SCRIPT,
			EXTENSION_DUMP_INTERFACE,
			EXTENSION_VALIDATE_API,
			DIAGNOSTICS_RENDER_DEVICE_SUPPORT,
			DIAGNOSTICS_RENDER_DEVICE_CREATE,
		};

		Kind kind = NONE;
		String project_path;
		PackedStringArray command_args;
		PackedStringArray passthrough_args;

		String scene;
		String script;
		bool check_only = false;

		String eval_source;

		String runner;

		String export_preset;
		String export_output;
		String export_mode;
		String export_patches;
		bool install_android_build_template = false;

		bool migrate_apply = false;
		bool migrate_strict_null = false;
		bool migrate_strict_dynamic = false;
		bool migrate_activate_strict = false;
		bool migrate_confirm = false;
		bool migrate_allow_violations = false;
		bool migrate_acknowledge_vcs = false;
		String migrate_follow_up;

		// Every exact `--case <pattern>` occurrence, retained in CLI order. Repeated
		// occurrences are additive: a test matching any retained pattern is selected.
		PackedStringArray test_cases;

		// Every exact `--suite <pattern>` occurrence, retained in CLI order. Suite patterns
		// match a test's doctest suite instead of its case name, and compose with
		// `test_cases` as a union: a test is selected when it matches either field.
		PackedStringArray test_suites;

		// 1-based shard selector for `test run --shard i/n`. Both stay at -1 when the
		// option is absent, which means "run everything in this process".
		int test_shard_index = -1;
		int test_shard_total = -1;

		bool test_progress = false;
		String test_progress_format;
		String test_progress_file;
		int test_progress_heartbeat_seconds = -1;

		bool print_filenames = false;

		// `test benchmark` artifact paths. Empty means "dump to stdout". The profile pass
		// is opt-in through `--profile`, which `--profile-output` implies.
		String benchmark_output;
		bool benchmark_profile = false;
		String benchmark_profile_output;

		// `test fixtures` selection. The positional patterns land in `command_args`; the
		// corpus directory is an option so patterns stay unambiguous. An empty
		// `fixtures_output` prints the report to stdout instead of writing an artifact.
		String fixtures_dir;
		String fixtures_output;
		bool fixtures_binary_tokens = false;
		// `all` (both corpus passes), `text`, or `bytecode`. Empty means `all`.
		String fixtures_pass;

		// `test completeness run` inputs. Each `--family` appends one entry; the remaining
		// options are the catalog, the owned scratch root, the report path, the optional
		// published surface, the required budget tier, and the optional timeout in seconds
		// (0 means "the tier's hard timeout").
		PackedStringArray completeness_families;
		String completeness_catalog;
		String completeness_scratch;
		String completeness_report;
		String completeness_surface;
		String completeness_tier;
		bool completeness_validate_census = false;
		int completeness_timeout_seconds = 0;

		// `test completeness select` inputs: the file listing one changed repository path per
		// line and the catalog whose capability map maps them to families. `completeness_json`
		// is the only supported output encoding and must be requested explicitly, so a future
		// human-readable rendering can never be mistaken for the machine-readable one.
		String completeness_changed_paths;
		bool completeness_json = false;

		// Empty means "use the tooling host default" (6005 for LSP, 6006 for DAP).
		// Otherwise a validated decimal port in [0, 65535], where 0 requests an
		// ephemeral port.
		String lsp_port;
		String dap_port;

		bool automation = false;
		String automation_transport;
		int automation_port = -1;
		String automation_token;
		String automation_run_workflow;
		bool automation_failure_screenshots = false;

		bool docs_include_docs = false;
		String docs_engine_output;
		bool docs_no_docbase = false;
		String docs_script_source;
		String docs_script_output;

		String extension_interface_format;
		String extension_validate_input;
	};

	struct ParseResult {
		bool ok = true;
		bool used_new_cli = false;
		bool json = false;
		bool trusted = false;
		bool help_requested = false;
		bool version_requested = false;
		bool no_header = false;
		String error;
		PackedStringArray command_path;
		PackedStringArray global_args;
		PackedStringArray user_args;
		CLIInvocation invocation;
	};

	// Default tooling-host listener ports, shared by `tooling serve` validation and
	// the editor-side host so a single override cannot silently collide with the
	// other service's default.
	static const int DEFAULT_LSP_PORT = 6005;
	static const int DEFAULT_DAP_PORT = 6006;

	static ParseResult parse(const PackedStringArray &p_args);
	static ParseResult parse(int p_argc, char *p_argv[]);
	static bool can_run_without_main_scene(const CLIInvocation &p_invocation);
	static bool is_new_cli_command(const String &p_arg);
};
