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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"

namespace FSTests {

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

} // namespace FSTests
