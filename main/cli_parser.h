/**************************************************************************/
/*  cli_parser.h                                                          */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class FoundryCLIParser {
public:
	struct CLIInvocation {
		enum Kind {
			NONE,
			EDITOR_OPEN,
			EDITOR_PROJECT_MANAGER,
			PROJECT_RUN,
			PROJECT_TEST,
			PROJECT_EXPORT,
			PROJECT_IMPORT,
			SCRIPT_FORMAT,
			SCRIPT_LINT,
			SCRIPT_MIGRATE,
			TEST_RUN,
			TEST_GENERATE_FIXTURES,
			TEST_GENERATE_FORMAT_FIXTURES,
			LSP_SERVE,
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

		String test_case;

		bool print_filenames = false;

		String lsp_port;

		bool automation = false;
		String automation_transport;
		int automation_port = -1;
		String automation_token;
		String automation_run_workflow;

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
		bool no_header = false;
		String error;
		PackedStringArray command_path;
		PackedStringArray global_args;
		PackedStringArray user_args;
		CLIInvocation invocation;
	};

	static ParseResult parse(const PackedStringArray &p_args);
	static ParseResult parse(int p_argc, char *p_argv[]);
	static bool is_new_cli_command(const String &p_arg);
};
