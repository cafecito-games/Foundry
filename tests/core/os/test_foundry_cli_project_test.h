/**************************************************************************/
/*  test_foundry_cli_project_test.h                                       */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestFoundryCLIProjectTest {

struct TemporaryNoMainSceneProject {
	String root;

	explicit TemporaryNoMainSceneProject(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name);
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
		write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"No Main Scene Project\"\n");
		write_file("runner.fs",
				"extends ScriptRunner\n\n"
				"func run(args: PackedStringArray) -> int:\n"
				"\treturn 0\n");
	}

	~TemporaryNoMainSceneProject() {
		remove_recursive(root);
	}

	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", absolute_path));
		file->store_string(p_contents);
	}

	static void remove_recursive(const String &p_path) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir()) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

static String run_foundry_subprocess(const List<String> &p_arguments, int &r_exit_code) {
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;

	Dictionary environment;
	if (OS::get_singleton()->has_environment("DISPLAY")) {
		const String display = OS::get_singleton()->get_environment("DISPLAY");
		if (!display.is_empty()) {
			environment["DISPLAY"] = display;
		}
	}

	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false, String(), environment, false);
	if (pipe_info.is_empty()) {
		r_exit_code = -1;
		return String();
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];

	auto pump_pipe = [](const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) -> uint64_t {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return 0;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return 0;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
		if (read > 0) {
			const int offset = r_bytes.size();
			r_bytes.resize(offset + read);
			memcpy(r_bytes.ptrw() + offset, chunk.ptr(), read);
		}
		return read;
	};

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 120000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump_pipe(stdout_pipe, stdout_bytes);
		pump_pipe(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump_pipe(stdout_pipe, stdout_bytes);
			pump_pipe(stderr_pipe, stderr_bytes);
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}

	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}

	r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
	String output = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
	if (!stderr_bytes.is_empty()) {
		if (!output.is_empty()) {
			output += "\n";
		}
		output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	}
	return output;
}

static String foundry_test_scratch_root() {
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			return configured.simplify_path();
		}
	}
	const String directory_name = vformat("foundry-cli-case-filter-tests-%d", OS::get_singleton()->get_process_id());
	return OS::get_singleton()->get_temp_path().path_join(directory_name).simplify_path();
}

// Reproduces the bug fixed by this suite: repeated `--case` occurrences must be additive
// (an OR set across doctest's own registered tests), never last-wins. `--list-test-cases`
// makes the selected set directly observable without executing every test.
TEST_CASE("[FoundryCLI][TestRun] Repeated case filters select the union of both patterns") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--case");
	arguments.push_back("*Test run records case filter*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("[FoundryCLIParser] Version query accepts JSON in either option order"));
	CHECK(output.contains("[FoundryCLIParser] Test run records case filter"));
	CHECK_FALSE(output.contains("[FoundryCLIParser] Test run has no case filters when option is absent"));
	CHECK(output.contains("unskipped test cases passing the current filters: 2"));
}

TEST_CASE("[FoundryCLI][TestRun] Reversing repeated case filters selects the same union") {
	// Registration order, not filter order, determines the listed order; both orderings
	// must therefore select the same two-case union.
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Test run records case filter*");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("[FoundryCLIParser] Version query accepts JSON in either option order"));
	CHECK(output.contains("[FoundryCLIParser] Test run records case filter"));
	CHECK(output.contains("unskipped test cases passing the current filters: 2"));
}

TEST_CASE("[FoundryCLI][TestRun] Three repeated case filters select the union of all three") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--case");
	arguments.push_back("*Test run records case filter*");
	arguments.push_back("--case");
	arguments.push_back("*build_doctest_case_filter merges filters with a single value unchanged*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("unskipped test cases passing the current filters: 3"));
}

TEST_CASE("[FoundryCLI][TestRun] Repeating the same case filter does not duplicate the match") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("unskipped test cases passing the current filters: 1"));
}

TEST_CASE("[FoundryCLI][TestRun] A comma list combined with a repeated filter selects all three") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*,*Test run records case filter*");
	arguments.push_back("--case");
	arguments.push_back("*build_doctest_case_filter merges filters with a single value unchanged*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("unskipped test cases passing the current filters: 3"));
}

TEST_CASE("[FoundryCLI][TestRun] An escaped comma inside one filter survives a repeated filter") {
	// This doctest suite has no test name containing a literal comma, so this proves the
	// escape only against a made-up pattern: the escaped comma must not split the first
	// value, and the second value must still contribute its own match.
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*nonexistent case\\, with an escaped comma*");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--list-test-cases");
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	CHECK(output.contains("[FoundryCLIParser] Version query accepts JSON in either option order"));
	CHECK(output.contains("unskipped test cases passing the current filters: 1"));
}

TEST_CASE("[FoundryCLI][TestRun] JSONL progress accounts for the exact union of two repeated filters") {
	const String scratch_root = foundry_test_scratch_root();
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE_EQ(dir->make_dir_recursive(scratch_root), OK);
	const String progress_path = scratch_root.path_join(
			vformat("foundry_cli_case_filter_progress_%d.jsonl", OS::get_singleton()->get_process_id()));
	if (FileAccess::exists(progress_path)) {
		DirAccess::remove_absolute(progress_path);
	}

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("run");
	arguments.push_back("--case");
	arguments.push_back("*Version query accepts JSON*");
	arguments.push_back("--case");
	arguments.push_back("*Test run records case filter*");
	arguments.push_back("--progress-format=jsonl");
	arguments.push_back("--progress-file");
	arguments.push_back(progress_path);
	arguments.push_back("--no-colors");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);

	REQUIRE_MESSAGE(FileAccess::exists(progress_path), "Progress file was not created");
	const String progress_contents = FileAccess::get_file_as_string(progress_path);
	INFO("Progress contents:\n", progress_contents);

	Dictionary run_start;
	Dictionary run_end;
	Vector<String> test_start_names;
	Vector<String> test_end_names;
	for (const String &line : progress_contents.split("\n", false)) {
		Variant parsed = JSON::parse_string(line);
		if (parsed.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary event = parsed;
		const String event_name = event.get("event", "");
		if (event_name == "run_start") {
			run_start = event;
		} else if (event_name == "run_end") {
			run_end = event;
		} else if (event_name == "test_start") {
			test_start_names.push_back(event.get("name", ""));
		} else if (event_name == "test_end") {
			test_end_names.push_back(event.get("name", ""));
		}
	}

	REQUIRE_MESSAGE(!run_start.is_empty(), "No run_start event observed");
	REQUIRE_MESSAGE(!run_end.is_empty(), "No run_end event observed");
	CHECK_EQ(int(run_start["test_count"]), 2);
	CHECK_EQ(test_start_names.size(), 2);
	CHECK(test_start_names.has("[FoundryCLIParser] Version query accepts JSON in either option order"));
	CHECK(test_start_names.has("[FoundryCLIParser] Test run records case filter"));
	CHECK_EQ(test_end_names.size(), 2);
	CHECK(test_end_names.has("[FoundryCLIParser] Version query accepts JSON in either option order"));
	CHECK(test_end_names.has("[FoundryCLIParser] Test run records case filter"));

	DirAccess::remove_absolute(progress_path);
}

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED

TEST_CASE("[FoundryCLI][ProjectTest] Runner works without a main scene") {
	TemporaryNoMainSceneProject project("foundry_cli_project_test_runner");

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("test");
	arguments.push_back("--project");
	arguments.push_back(project.root);
	arguments.push_back("--runner");
	arguments.push_back("res://runner.fs");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("no main scene defined"));
	CHECK_EQ(exit_code, 0);
}

TEST_CASE("[FoundryCLI][ProjectTest] Missing runner reports runner error without main scene gate") {
	TemporaryNoMainSceneProject project("foundry_cli_project_test_missing_runner");

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("test");
	arguments.push_back("--project");
	arguments.push_back(project.root);
	arguments.push_back("--runner");
	arguments.push_back("res://missing_runner.fs");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("no main scene defined"));
	CHECK(output.contains("Can't load script runner"));
	CHECK_NE(exit_code, 0);
}

TEST_CASE("[FoundryCLI][ProjectTest] Script format works without a main scene") {
	TemporaryNoMainSceneProject project("foundry_cli_project_test_script_format");
	project.write_file("scripts/sample.fs",
			"func f() -> int:\n"
			"\treturn 1\n");

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("format");
	arguments.push_back("--project");
	arguments.push_back(project.root);
	arguments.push_back("--check");
	arguments.push_back("scripts/sample.fs");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("no main scene defined"));
	CHECK_EQ(exit_code, 0);
}

TEST_CASE("[FoundryCLI][Docs] Script docs discover Foundry Script sources recursively") {
	TemporaryNoMainSceneProject project("foundry_cli_docs_generate_script");
	project.write_file("scripts/nested/docs_probe.fs",
			"## Documentation generated by the command-line probe.\n"
			"class_name FoundryDocsProbe\n"
			"extends RefCounted\n");

	const String source_dir = project.root.path_join("scripts");
	const String output_dir = project.root.path_join("docs");
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("docs");
	arguments.push_back("generate-script");
	arguments.push_back("--source");
	arguments.push_back(source_dir);
	arguments.push_back("--output");
	arguments.push_back(output_dir);

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("Couldn't find any FoundryScript files"));
	CHECK_EQ(exit_code, 0);
	CHECK(FileAccess::exists(output_dir.path_join("FoundryDocsProbe.xml")));
}

TEST_CASE("[FoundryCLI][ScriptEval] Inline eval prints and exits zero") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back("print(\"eval-ok\")");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("eval-ok"));
	CHECK_EQ(exit_code, 0);
}

TEST_CASE("[FoundryCLI][ScriptEval] Exposed native classes are available projectless") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back(
			"var classes := ClassDB.get_exposed_class_list(); "
			"print(\"has-node=\", classes.has(\"Node\")); "
			"print(\"has-fs-native-class=\", classes.has(\"FSNativeClass\")); "
			"print(\"has-theme-context=\", classes.has(\"ThemeContext\"))");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("has-node=true"));
	CHECK(output.contains("has-fs-native-class=false"));
	CHECK(output.contains("has-theme-context=false"));
	CHECK_EQ(exit_code, 0);
}

TEST_CASE("[FoundryCLI][ScriptEval] Inline eval return sets the exit code") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back("return 3");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 3);
}

TEST_CASE("[FoundryCLI][ScriptEval] Inline eval ignores a configured main scene") {
	// Regression: `script eval` must not resolve or load the project's main scene, even
	// when one is configured with an unresolvable uid:// that would otherwise abort startup.
	TemporaryNoMainSceneProject project("foundry_cli_script_eval_main_scene");
	project.write_file("project.foundry",
			"config_version=5\n\n"
			"[application]\n\n"
			"config/name=\"Eval Main Scene Project\"\n"
			"run/main_scene=\"uid://doesnotexist12345\"\n");

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back("--project");
	arguments.push_back(project.root);
	arguments.push_back("print(\"eval-ran\")");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("eval-ran"));
	CHECK_FALSE(output.contains("could not be resolved from UID"));
	CHECK_EQ(exit_code, 0);
}

TEST_CASE("[FoundryCLI][ScriptEval] Explicit project that fails to load is an error") {
	// A projectless eval is allowed, but an explicit `--project` pointing at a directory with
	// no project.foundry must fail rather than silently evaluating outside the requested project.
	const String empty_dir = OS::get_singleton()->get_temp_path().path_join("foundry_cli_script_eval_bad_project");
	TemporaryNoMainSceneProject::remove_recursive(empty_dir);
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE_EQ(dir->make_dir_recursive(empty_dir), OK);

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back("--project");
	arguments.push_back(empty_dir);
	arguments.push_back("print(\"should-not-run\")");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("should-not-run"));
	CHECK_NE(exit_code, 0);

	TemporaryNoMainSceneProject::remove_recursive(empty_dir);
}

TEST_CASE("[FoundryCLI][ScriptEval] Nonexistent project path is an error") {
	// The directory does not exist, so applying `--project` fails outright. Eval must not fall
	// back to an ambient project discovered from the original working directory.
	const String missing_dir = OS::get_singleton()->get_temp_path().path_join("foundry_cli_script_eval_missing_project_dir");
	TemporaryNoMainSceneProject::remove_recursive(missing_dir);

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back("--project");
	arguments.push_back(missing_dir);
	arguments.push_back("print(\"should-not-run\")");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_FALSE(output.contains("should-not-run"));
	CHECK_NE(exit_code, 0);
}

// Observable engine-side guarantees of the Foundry Test Adapter Protocol: runner
// arguments cross both separators unchanged, the runner owns its artifact files,
// deliberate process output stays outside them, flushed points are visible while the
// process is alive, and the runner's return value becomes the process exit code. The
// protocol itself lives entirely in the runner, so these tests assert only what the
// transport must preserve.

constexpr uint64_t ADAPTER_TIMEOUT_MSEC = 120000;
constexpr uint64_t ADAPTER_POLL_USEC = 20000;

static String adapter_scratch_root() {
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			return configured.simplify_path();
		}
	}
	const String directory_name = vformat("foundry-tests-%d", OS::get_singleton()->get_process_id());
	return OS::get_singleton()->get_temp_path().path_join(directory_name).simplify_path();
}

// Stages the checked-in transport fixture project beneath a unique scratch directory so
// each case owns its artifacts and an aborted run never pollutes the repository.
struct StagedAdapterProject {
	String project_root;

	explicit StagedAdapterProject(const String &p_name) {
		const String unique_name = vformat("adapter_transport_%s_%d", p_name, OS::get_singleton()->get_process_id());
		project_root = adapter_scratch_root().path_join(unique_name).simplify_path();
		TemporaryNoMainSceneProject::remove_recursive(project_root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(project_root), OK);

		const String fixture_root = TestUtils::get_fixture_path("foundry_test_adapter_transport");
		stage("project.foundry", fixture_root);
		stage("adapter_transport_runner.fs", fixture_root);
	}

	~StagedAdapterProject() {
		TemporaryNoMainSceneProject::remove_recursive(project_root);
	}

	void stage(const String &p_file_name, const String &p_fixture_root) const {
		Error error = OK;
		const String source = FileAccess::get_file_as_string(p_fixture_root.path_join(p_file_name), &error);
		REQUIRE_MESSAGE(error == OK, vformat("Cannot read transport fixture '%s'", p_file_name));
		write(p_file_name, source);
	}

	void write(const String &p_relative_path, const String &p_contents) const {
		Ref<FileAccess> file = FileAccess::open(project_root.path_join(p_relative_path), FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", p_relative_path));
		file->store_string(p_contents);
	}

	String path(const String &p_relative_path) const {
		return project_root.path_join(p_relative_path);
	}

	List<String> arguments() const {
		List<String> list;
		list.push_back("--headless");
		list.push_back("--no-header");
		list.push_back("project");
		list.push_back("test");
		list.push_back("--project");
		list.push_back(project_root);
		list.push_back("--runner");
		list.push_back("res://adapter_transport_runner.fs");
		list.push_back("--");
		return list;
	}
};

static String read_artifact(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return String();
	}
	return FileAccess::get_file_as_string(p_path);
}

// Owns one asynchronously launched adapter child, drains its pipes while polling, and
// always reaps the process even when a condition never becomes true.
struct AdapterChild {
	OS::ProcessID pid = 0;
	Ref<FileAccess> stdout_pipe;
	Ref<FileAccess> stderr_pipe;
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	bool running = false;

	explicit AdapterChild(const List<String> &p_arguments) {
		Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
				OS::get_singleton()->get_executable_path(), p_arguments, false, String(), Dictionary(), false);
		REQUIRE_MESSAGE(!pipe_info.is_empty(), "Cannot launch the adapter subprocess");
		stdout_pipe = pipe_info["stdio"];
		stderr_pipe = pipe_info["stderr"];
		pid = pipe_info["pid"];
		running = true;
	}

	~AdapterChild() {
		if (running && OS::get_singleton()->is_process_running(pid)) {
			OS::get_singleton()->kill(pid);
		}
		close_pipes();
	}

	void close_pipes() {
		if (stdout_pipe.is_valid()) {
			stdout_pipe->close();
			stdout_pipe.unref();
		}
		if (stderr_pipe.is_valid()) {
			stderr_pipe->close();
			stderr_pipe.unref();
		}
	}

	void pump() {
		drain(stdout_pipe, stdout_bytes);
		drain(stderr_pipe, stderr_bytes);
	}

	static void drain(const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
		if (read > 0) {
			const int offset = r_bytes.size();
			r_bytes.resize(offset + read);
			memcpy(r_bytes.ptrw() + offset, chunk.ptr(), read);
		}
	}

	String captured_output() const {
		String text = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
		text += "\n--- stderr ---\n";
		text += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
		return text;
	}

	// Polls until the artifact at `p_path` contains `p_needle`. Returns false on timeout
	// so the caller can report the captured output and the partial artifact.
	bool wait_for_artifact(const String &p_path, const String &p_needle) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + ADAPTER_TIMEOUT_MSEC;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			pump();
			if (read_artifact(p_path).contains(p_needle)) {
				return true;
			}
			OS::get_singleton()->delay_usec(ADAPTER_POLL_USEC);
		}
		return false;
	}

	bool wait_for_exit(int &r_exit_code) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + ADAPTER_TIMEOUT_MSEC;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			pump();
			if (!OS::get_singleton()->is_process_running(pid)) {
				pump();
				running = false;
				r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
				return true;
			}
			OS::get_singleton()->delay_usec(ADAPTER_POLL_USEC);
		}
		return false;
	}

	// `OS::kill()` reaps the child, so the process must not be polled afterwards.
	void terminate() {
		if (!running) {
			return;
		}
		pump();
		OS::get_singleton()->kill(pid);
		running = false;
		close_pipes();
	}
};

static int run_adapter(const List<String> &p_arguments, String &r_stdout, String &r_stderr) {
	AdapterChild child(p_arguments);
	int exit_code = -1;
	const bool exited = child.wait_for_exit(exit_code);
	child.close_pipes();
	r_stdout = String::utf8((const char *)child.stdout_bytes.ptr(), child.stdout_bytes.size());
	r_stderr = String::utf8((const char *)child.stderr_bytes.ptr(), child.stderr_bytes.size());
	REQUIRE_MESSAGE(exited, "The adapter subprocess did not exit before the timeout");
	return exit_code;
}

TEST_CASE("[FoundryCLI][Adapter] Runner arguments cross both separators unchanged") {
	StagedAdapterProject project("argument_passthrough");
	const String output_path = project.path("capabilities.json");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("capabilities");
	arguments.push_back("--output");
	arguments.push_back(output_path);
	arguments.push_back("--");
	// Every argument after the adapter separator is opaque framework input, including
	// arguments spelled like reserved options.
	arguments.push_back("--output");
	arguments.push_back("--select");
	arguments.push_back("--");
	arguments.push_back("--protocol-version");
	arguments.push_back("trailing value");

	String captured_stdout;
	String captured_stderr;
	const int exit_code = run_adapter(arguments, captured_stdout, captured_stderr);
	INFO("stdout:\n", captured_stdout, "\nstderr:\n", captured_stderr);
	CHECK_EQ(exit_code, 0);

	const String document = read_artifact(output_path);
	INFO("Capabilities:\n", document);
	CHECK(document.contains(
			"\"framework_args\":[\"--output\",\"--select\",\"--\",\"--protocol-version\",\"trailing value\"]"));
}

TEST_CASE("[FoundryCLI][Adapter] A reserved option consumes a separator-looking value") {
	StagedAdapterProject project("opaque_selection");
	const String report_path = project.path("report.tap");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);
	// `--select --` selects the opaque ID `--`; it must not begin framework arguments.
	arguments.push_back("--select");
	arguments.push_back("--");

	String captured_stdout;
	String captured_stderr;
	const int exit_code = run_adapter(arguments, captured_stdout, captured_stderr);
	INFO("stdout:\n", captured_stdout, "\nstderr:\n", captured_stderr);
	CHECK_EQ(exit_code, 0);

	const String report = read_artifact(report_path);
	INFO("Report:\n", report);
	CHECK(report.begins_with("TAP version 13\n# foundry-test-adapter: 1\n1..1\n"));
	CHECK(report.contains("id: \"--\""));
}

TEST_CASE("[FoundryCLI][Adapter] A valid operation truncates a pre-existing artifact") {
	StagedAdapterProject project("artifact_truncation");
	const String output_path = project.path("capabilities.json");
	project.write("capabilities.json", "stale content that must not survive\n");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("capabilities");
	arguments.push_back("--output");
	arguments.push_back(output_path);

	String captured_stdout;
	String captured_stderr;
	CHECK_EQ(run_adapter(arguments, captured_stdout, captured_stderr), 0);

	const String document = read_artifact(output_path);
	INFO("Capabilities:\n", document);
	CHECK_FALSE(document.contains("stale content"));
	CHECK(document.begins_with("{\"protocol\":\"foundry-test-adapter\""));
	CHECK(document.ends_with("}\n"));
}

TEST_CASE("[FoundryCLI][Adapter] Process output stays outside protocol artifacts") {
	StagedAdapterProject project("output_isolation");
	const String output_path = project.path("capabilities.json");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("capabilities");
	arguments.push_back("--output");
	arguments.push_back(output_path);
	arguments.push_back("--");
	arguments.push_back("noise");

	String captured_stdout;
	String captured_stderr;
	CHECK_EQ(run_adapter(arguments, captured_stdout, captured_stderr), 0);
	INFO("stdout:\n", captured_stdout, "\nstderr:\n", captured_stderr);
	CHECK(captured_stdout.contains("adapter-transport-stdout"));
	CHECK(captured_stderr.contains("adapter-transport-stderr"));

	const String document = read_artifact(output_path);
	CHECK_FALSE(document.contains("adapter-transport-stdout"));
	CHECK_FALSE(document.contains("adapter-transport-stderr"));
}

TEST_CASE("[FoundryCLI][Adapter] A flushed point is observable before the run completes") {
	StagedAdapterProject project("incremental_flush");
	const String report_path = project.path("report.tap");
	const String continuation_path = project.path("continue.marker");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);
	arguments.push_back("--");
	arguments.push_back("delayed-report=" + continuation_path);

	AdapterChild child(arguments);
	const bool observed = child.wait_for_artifact(report_path, "transport::first");
	const String partial = read_artifact(report_path);
	INFO("Partial report:\n", partial, "\nProcess output:\n", child.captured_output());
	REQUIRE(observed);
	CHECK(partial.ends_with("  ...\n"));
	CHECK_FALSE(partial.contains("transport::second"));
	CHECK(OS::get_singleton()->is_process_running(child.pid));

	project.write("continue.marker", "go\n");

	int exit_code = -1;
	const bool exited = child.wait_for_exit(exit_code);
	INFO("Process output:\n", child.captured_output());
	REQUIRE(exited);
	CHECK_EQ(exit_code, 0);

	const String report = read_artifact(report_path);
	INFO("Final report:\n", report);
	CHECK(report.begins_with("TAP version 13\n# foundry-test-adapter: 1\n1..2\n"));
	CHECK(report.contains("transport::second"));
}

TEST_CASE("[FoundryCLI][Adapter] Runner return values propagate as process exit codes") {
	const int expected_exit_codes[] = { 0, 1, 2 };
	for (int expected : expected_exit_codes) {
		StagedAdapterProject project(vformat("exit_%d", expected));
		const String output_path = project.path("capabilities.json");

		List<String> arguments = project.arguments();
		arguments.push_back("adapter");
		arguments.push_back("capabilities");
		arguments.push_back("--output");
		arguments.push_back(output_path);
		arguments.push_back("--");
		arguments.push_back(vformat("return-code=%d", expected));

		String captured_stdout;
		String captured_stderr;
		const int exit_code = run_adapter(arguments, captured_stdout, captured_stderr);
		INFO("Return code ", expected, " stdout:\n", captured_stdout, "\nstderr:\n", captured_stderr);
		CHECK_EQ(exit_code, expected);
		CHECK(FileAccess::exists(output_path));
	}
}

TEST_CASE("[FoundryCLI][Adapter] An uncaught runner failure exits 1 and leaves an incomplete report") {
	StagedAdapterProject project("uncaught_failure");
	const String report_path = project.path("report.tap");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);
	arguments.push_back("--");
	arguments.push_back("uncaught-error");

	String captured_stdout;
	String captured_stderr;
	const int exit_code = run_adapter(arguments, captured_stdout, captured_stderr);
	INFO("stdout:\n", captured_stdout, "\nstderr:\n", captured_stderr);
	CHECK_EQ(exit_code, 1);

	// The unsatisfied plan, not the exit code alone, is what marks this infrastructure
	// failure rather than a represented test failure.
	const String report = read_artifact(report_path);
	INFO("Report:\n", report);
	CHECK(report.contains("1..2"));
	CHECK(report.contains("transport::first"));
	CHECK_FALSE(report.contains("transport::second"));
	CHECK_FALSE(report.contains("Bail out!"));
}

TEST_CASE("[FoundryCLI][Adapter] Terminating the runner preserves only complete points") {
	StagedAdapterProject project("external_cancellation");
	const String report_path = project.path("report.tap");
	const String continuation_path = project.path("continue.marker");

	List<String> arguments = project.arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);
	arguments.push_back("--");
	arguments.push_back("delayed-report=" + continuation_path);

	AdapterChild child(arguments);
	const bool observed = child.wait_for_artifact(report_path, "transport::first");
	INFO("Process output:\n", child.captured_output());
	REQUIRE(observed);

	// The continuation file is never created, so the child is still between points.
	child.terminate();

	const String report = read_artifact(report_path);
	INFO("Report after termination:\n", report);
	CHECK(report.begins_with("TAP version 13\n# foundry-test-adapter: 1\n1..2\n"));
	CHECK(report.contains("transport::first"));
	CHECK_FALSE(report.contains("transport::second"));
	CHECK_EQ(report.count("  ...\n"), 1);
	CHECK(report.ends_with("  ...\n"));
}

#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

} // namespace TestFoundryCLIProjectTest
