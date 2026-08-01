/**************************************************************************/
/*  editor_workflow_test_fixtures.h                                       */
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

#include "tests/test_utils.h"

namespace EditorWorkflowTestFixtures {

struct DisposableProjectSpec {
	String fixture_name = "editor_automation_workflow";
	PackedStringArray relative_paths;
	bool include_layout = true;
	bool include_scripts = true;
	bool include_secondary_scene = true;
};

static String fixture_project_path(const String &p_fixture_name = "editor_automation_workflow") {
	return TestUtils::get_fixture_path(p_fixture_name);
}

static bool copy_relative_file(const String &p_source_root, const String &p_dest_root, const String &p_relative_path) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (da.is_null()) {
		return false;
	}
	const String source = p_source_root.path_join(p_relative_path);
	const String dest = p_dest_root.path_join(p_relative_path);
	const String dest_dir = dest.get_base_dir();
	if (da->make_dir_recursive(dest_dir) != OK) {
		return false;
	}
	return da->copy(source, dest) == OK;
}

static String prepare_disposable_project(const DisposableProjectSpec &p_spec = DisposableProjectSpec()) {
	const String source = fixture_project_path(p_spec.fixture_name);
	const String temp_project = TestUtils::get_temp_path(
			"editor_automation_workflow_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (da.is_null() || !DirAccess::exists(source)) {
		return String();
	}

	PackedStringArray paths = p_spec.relative_paths;
	if (paths.is_empty()) {
		paths.push_back("project.foundry");
		paths.push_back("scenes/main.tscn");
		if (p_spec.include_secondary_scene) {
			paths.push_back("scenes/secondary.tscn");
		}
		if (p_spec.include_scripts) {
			paths.push_back("scripts/player.fs");
		}
		if (p_spec.include_layout) {
			paths.push_back("layout/editor_layout.cfg");
		}
	}

	for (int i = 0; i < paths.size(); i++) {
		if (!copy_relative_file(source, temp_project, paths[i])) {
			return String();
		}
	}
	return temp_project;
}

static String prepare_basic_scene_project() {
	DisposableProjectSpec spec;
	spec.fixture_name = "editor_automation_mvp";
	spec.include_layout = false;
	spec.include_scripts = false;
	spec.include_secondary_scene = false;
	PackedStringArray paths;
	paths.push_back("project.foundry");
	paths.push_back("scenes/main.tscn");
	spec.relative_paths = paths;
	return prepare_disposable_project(spec);
}

static bool workflow_has_display() {
	return OS::get_singleton()->has_environment("DISPLAY") && !OS::get_singleton()->get_environment("DISPLAY").is_empty();
}

static String workflow_run_subprocess(const List<String> &p_arguments, int &r_exit_code, const String &p_working_directory = String()) {
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;

	Dictionary environment;
	if (workflow_has_display()) {
		environment["DISPLAY"] = OS::get_singleton()->get_environment("DISPLAY");
	}

	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false, p_working_directory, environment, false);
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

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 180000;
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
	output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return output;
}

} // namespace EditorWorkflowTestFixtures
