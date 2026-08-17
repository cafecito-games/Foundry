/**************************************************************************/
/*  fs_test_runner.h                                                      */
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

// Whether FoundryScript globals are currently live, including when a test called
// `FSLanguage::init()` directly instead of going through `init_language()`.
bool is_fs_language_active();

// Whether `init_language()` currently holds the language up. Used by the
// per-suite test fixture to decide between resetting and tearing down.
bool is_language_initialized();

// Number of times the heavy language setup has actually run; test-only
// instrumentation used to assert the per-suite hoist holds.
uint64_t get_init_language_count();

// Fixture-level half of the `test run --shard i/n` partition, shared by every suite that
// loops over a corpus inside a single doctest case. Those cases execute on all shards (see
// the run-everywhere allowlist in `tests/test_case_shard.cpp`) and each shard runs only the
// slice selected here.
//
// With the units sorted by a stable key, unit `p_sorted_index` belongs to shard
// `p_shard_index` of `p_shard_total`, both 1-based. A total below 2 selects every unit.
bool fs_test_shard_selects(int p_sorted_index, int p_shard_index, int p_shard_total);

// Reads the `--fs-shard=i/n` token that `test run --shard` pushes onto the test command
// line. Both outputs stay at -1 when the token is absent or malformed, meaning "run the
// whole corpus".
void fs_test_shard_from_cmdline(int &r_shard_index, int &r_shard_total);

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
	// Stable machine token for a status, used by the structured `foundry test fixtures`
	// report. Unlike the human-facing `FS_TEST_*` text baked into expected output, this is
	// consumed by automation, so it never changes shape with the report's prose.
	static String get_status_token(TestStatus p_status);

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
	// Directory the run was pointed at; only fixtures below it are collected.
	String source_dir;
	// Root of the corpus `source_dir` belongs to (the nearest ancestor holding
	// `project.foundry`). Script paths in expected output are recorded relative to this
	// root, and global classes are indexed from it, so running or generating a single
	// subdirectory produces exactly what a full-corpus run does.
	String corpus_root;
	Vector<FSTest> tests;

	bool is_generating = false;
	bool do_init_languages = false;
	bool print_filenames; // Whether filenames should be printed when generated/running tests
	bool binary_tokens; // Test with buffer tokenizer.
	bool compiled_bytecode; // Round-trip compiled scripts through the `.fsb` serializer before running.
	// 1-based `--shard` selector; -1 means the runner executes the whole corpus. Only a run
	// is ever partitioned: fixture regeneration always walks everything.
	int shard_index = -1;
	int shard_total = -1;
	// Corpus-relative glob patterns selecting the fixtures to run. Empty means "the whole
	// corpus". Only a run is ever filtered: fixture regeneration always walks everything.
	Vector<String> fixture_filters;

	bool make_tests();
	bool make_tests_for_dir(const String &p_dir);
	bool generate_class_index();

public:
	// Structured outcome of a single fixture execution. Backs `foundry test fixtures`, whose
	// report has to name the failing fixture as data rather than as a line of console text.
	struct FixtureOutcome {
		// Fixture path relative to the corpus root, e.g. `runtime/features/await.fs`.
		String path;
		// Which corpus pass produced this outcome: `text`, `binary-tokens`, or `bytecode`.
		String pass;
		bool passed = false;
		// `FSTest::get_status_token()` of the run's terminal stage.
		String status;
		// Actual and expected output, so a failure can be diffed without rerunning.
		String output;
		String expected;
	};

	static StringName test_function_name;

	// Registered as the `--foundry_script-generate-tests` test command (normalized from
	// `foundry test generate-fixtures`) so it runs under the `--test` setup/teardown.
	static void generate_outputs_for_cmdline();
	int run_tests();

	// Runs the selected fixtures and returns one outcome per execution instead of reporting
	// through doctest assertions, so the same corpus can be driven from a plain CLI command.
	// `r_setup_ok` reports whether collection and class indexing succeeded; when it is false
	// the returned vector is empty and says nothing about the corpus.
	Vector<FixtureOutcome> run_tests_collecting(bool &r_setup_ok);

	bool generate_outputs();

	void set_shard(int p_shard_index, int p_shard_total);

	// Restricts the run to fixtures whose corpus-relative path matches one of the patterns.
	// The class index still covers the whole corpus, so a scoped run resolves the same global
	// classes a full run does.
	void set_fixture_filters(const Vector<String> &p_patterns);

	// A pattern containing `*` or `?` is a glob matched against the whole corpus-relative
	// path; any other pattern matches as a substring. Matching is case-insensitive, and an
	// empty pattern list selects everything.
	static bool fixture_path_matches(const String &p_relative_path, const Vector<String> &p_patterns);

	// Keys of the fixtures this runner would execute, in run order. Backs the coverage that
	// proves the fixture-level partition covers the corpus and never repeats a fixture.
	Vector<String> collect_fixture_keys();

	FSTestRunner(const String &p_source_dir, bool p_init_language, bool p_print_filenames = false, bool p_use_binary_tokens = false, bool p_use_compiled_bytecode = false);
	~FSTestRunner();

private:
	// Runs one collected fixture and packages its result. Shared by the doctest-reporting
	// `run_tests()` and the structured `run_tests_collecting()` so both passes execute a
	// fixture and compare it against expected output in exactly one way.
	FixtureOutcome execute_fixture(FSTest &p_test) const;
};

} // namespace FSTests
