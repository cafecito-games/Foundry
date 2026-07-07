/**************************************************************************/
/*  test_foundry_cli_project_test.h                                       */
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

#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

} // namespace TestFoundryCLIProjectTest
