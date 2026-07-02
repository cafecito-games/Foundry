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

#include "core/os/os.h"

#include <stdio.h>
#include <stdlib.h>

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

void FSLintCLI::run_from_cmdline() {
	String error;
	const Options options = parse_options(OS::get_singleton()->get_cmdline_args(), error);
	OS::get_singleton()->set_exit_code(error.is_empty() ? EXIT_SUCCESS : 2);
}
