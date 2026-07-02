/**************************************************************************/
/*  fs_test_runner.h                                                      */
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

#include "../foundry_script.h"

#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace FSTests {

void init_autoloads();
void init_language(const String &p_base_path);
void finish_language();

// Resets the per-case FoundryScript state (cache, global classes, annotations) that
// is shared when `init_language()` is hoisted to once-per-`TEST_SUITE`, without
// tearing down the stable language globals. No-op when the language is down.
void reset_language_state();

// Whether `init_language()` currently holds the language up. Used by the
// per-suite test fixture to decide between resetting and tearing down.
bool is_language_initialized();

// Number of times the heavy language setup has actually run; test-only
// instrumentation used to assert the per-suite hoist holds.
uint64_t get_init_language_count();

// Single test instance in a suite.
class FSTest {
public:
	enum TestStatus {
		FS_TEST_OK,
		FS_TEST_LOAD_ERROR,
		FS_TEST_PARSER_ERROR,
		FS_TEST_ANALYZER_ERROR,
		FS_TEST_COMPILER_ERROR,
		FS_TEST_RUNTIME_ERROR,
	};

	struct TestResult {
		TestStatus status;
		String output;
		bool passed;
	};

	enum TokenizerMode {
		TOKENIZER_TEXT,
		TOKENIZER_BUFFER,
	};

private:
	struct ErrorHandlerData {
		TestResult *result = nullptr;
		FSTest *self = nullptr;
		ErrorHandlerData(TestResult *p_result, FSTest *p_this) {
			result = p_result;
			self = p_this;
		}
	};

	String source_file;
	String output_file;
	String base_dir;

	PrintHandlerList _print_handler;
	ErrorHandlerList _error_handler;

	TokenizerMode tokenizer_mode = TOKENIZER_TEXT;
	bool use_compiled_bytecode = false;

	void enable_stdout();
	void disable_stdout();
	bool check_output(const String &p_output) const;
	String get_text_for_status(TestStatus p_status) const;

	TestResult execute_test_code(bool p_is_generating);

public:
	static void print_handler(void *p_this, const String &p_message, bool p_error, bool p_rich);
	static void error_handler(void *p_this, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type);
	TestResult run_test();
	bool generate_output();

	const String &get_source_file() const { return source_file; }
	const String get_source_relative_filepath() const { return source_file.trim_prefix(base_dir); }
	const String &get_output_file() const { return output_file; }

	void set_tokenizer_mode(TokenizerMode p_tokenizer_mode) { tokenizer_mode = p_tokenizer_mode; }
	TokenizerMode get_tokenizer_mode() const { return tokenizer_mode; }

	void set_use_compiled_bytecode(bool p_use_compiled_bytecode) { use_compiled_bytecode = p_use_compiled_bytecode; }
	bool is_using_compiled_bytecode() const { return use_compiled_bytecode; }

	FSTest(const String &p_source_path, const String &p_output_path, const String &p_base_dir);
	FSTest() :
			FSTest(String(), String(), String()) {} // Needed to use in Vector.
};

class FSTestRunner {
	String source_dir;
	Vector<FSTest> tests;

	bool is_generating = false;
	bool do_init_languages = false;
	bool print_filenames; // Whether filenames should be printed when generated/running tests
	bool binary_tokens; // Test with buffer tokenizer.
	bool compiled_bytecode; // Round-trip compiled scripts through the `.fsb` serializer before running.

	bool make_tests();
	bool make_tests_for_dir(const String &p_dir);
	bool generate_class_index();

public:
	static StringName test_function_name;

	// Registered as the `--foundry_script-generate-tests` test command so it runs
	// under the `--test` setup/teardown and the process can shut down cleanly.
	static void generate_outputs_for_cmdline();
	int run_tests();
	bool generate_outputs();

	FSTestRunner(const String &p_source_dir, bool p_init_language, bool p_print_filenames = false, bool p_use_binary_tokens = false, bool p_use_compiled_bytecode = false);
	~FSTestRunner();
};

} // namespace FSTests
