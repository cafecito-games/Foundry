/**************************************************************************/
/*  fs_lint.h                                                             */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class FSLintCLI {
public:
	enum OutputFormat {
		OUTPUT_JSON,
		OUTPUT_SARIF,
	};

	enum FailOn {
		FAIL_ON_ERROR,
		FAIL_ON_WARNING,
	};

	enum Severity {
		SEVERITY_NOTE,
		SEVERITY_WARNING,
		SEVERITY_ERROR,
	};

	struct Range {
		int start_line = 1;
		int start_column = 1;
		int end_line = 1;
		int end_column = 1;
	};

	struct Diagnostic {
		String path;
		String sarif_path;
		Range range;
		Severity severity = SEVERITY_ERROR;
		String source = "foundry_script";
		String rule_id;
		String message;
	};

	struct Options {
		OutputFormat output_format = OUTPUT_JSON;
		FailOn fail_on = FAIL_ON_ERROR;
		String output_path;
		Vector<String> paths;
	};

	struct Result {
		Vector<Diagnostic> diagnostics;
		bool had_command_error = false;
		String command_error;

		int get_exit_code(const Options &p_options) const;
	};

	static Options parse_options(const List<String> &p_cmdline_args, String &r_error);
	static Vector<String> collect_files(const Vector<String> &p_paths, bool &r_had_error);
	static Result lint_paths(const Vector<String> &p_paths, const Options &p_options);
	static String severity_to_string(Severity p_severity);
	static String sarif_level_for_severity(Severity p_severity);
	static Dictionary diagnostic_to_dictionary(const Diagnostic &p_diagnostic);
	static String to_json(const Vector<Diagnostic> &p_diagnostics);
	static String to_sarif(const Vector<Diagnostic> &p_diagnostics);
	static void run_from_cmdline();

private:
	static void collect_files_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error);
};
