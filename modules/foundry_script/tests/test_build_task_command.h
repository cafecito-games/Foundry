/**************************************************************************/
/*  test_build_task_command.h                                             */
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

#include "../foundry_build_task.h"

#include "core/config/project_build_state.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_file_system.h"
#endif // TOOLS_ENABLED

namespace FSTests {

struct ScopedCommandTaskProject {
	String old_resource_path;
	String root_path;

	explicit ScopedCommandTaskProject(const String &p_name) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		root_path = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		const Error err = DirAccess::make_dir_recursive_absolute(root_path);
		CHECK_EQ(err, OK);
		if (err == OK) {
			TestProjectSettingsInternalsAccessor::resource_path() = root_path;
		}
	}

	~ScopedCommandTaskProject() {
		remove_recursive(root_path);
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	String globalize(const String &p_path) const {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}

	void write_file(const String &p_path, const String &p_text) const {
		const String absolute_path = globalize(p_path);
		const Error mkdir_err = DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		CHECK_EQ(mkdir_err, OK);
		if (mkdir_err != OK) {
			return;
		}
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		CHECK_MESSAGE(file.is_valid(), vformat("Cannot write '%s'.", absolute_path));
		if (file.is_null()) {
			return;
		}
		file->store_string(p_text);
	}

	static void remove_recursive(const String &p_absolute_path) {
		Ref<DirAccess> dir = DirAccess::open(p_absolute_path);
		if (dir.is_null()) {
			return;
		}

		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}

			const String child = p_absolute_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_absolute_path);
	}
};

static Ref<FoundryBuildResult> run_command_task(const Dictionary &p_options) {
	Ref<FoundryBuildContext> context;
	context.instantiate();
	context->set_provider_id("command");
	context->set_task_name("test_command");
	context->set_trusted_execution(true);
	context->set_options(p_options);

	Ref<FoundryCommandBuildTask> task;
	task.instantiate();
	return task->run(context);
}

static PackedStringArray make_args(const String &p_a, const String &p_b = String(), const String &p_c = String(),
		const String &p_d = String(), const String &p_e = String(), const String &p_f = String()) {
	PackedStringArray args;
	args.push_back(p_a);
	if (!p_b.is_empty()) {
		args.push_back(p_b);
	}
	if (!p_c.is_empty()) {
		args.push_back(p_c);
	}
	if (!p_d.is_empty()) {
		args.push_back(p_d);
	}
	if (!p_e.is_empty()) {
		args.push_back(p_e);
	}
	if (!p_f.is_empty()) {
		args.push_back(p_f);
	}
	return args;
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Schema exposes command runner fields") {
	Ref<FoundryCommandBuildTask> task;
	task.instantiate();
	Ref<FoundryBuildTaskConfigSchema> schema = task->get_config_schema();

	Dictionary properties = schema->get_properties();
	CHECK(properties.has("command"));
	CHECK(properties.has("args"));
	CHECK(properties.has("working_directory"));
	CHECK(properties.has("environment"));
	CHECK(properties.has("inputs"));
	CHECK(properties.has("outputs"));
	CHECK(properties.has("timeout_seconds"));
	CHECK(properties.has("tool_version_command"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Reports null context as a launch failure") {
	Ref<FoundryCommandBuildTask> task;
	task.instantiate();

	Ref<FoundryBuildResult> result = task->run(Ref<FoundryBuildContext>());
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_EQ(result->get_exit_code(), -1);
	CHECK_FALSE(result->get_launch_error().is_empty());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Trust mutation is reserved for native callers") {
	Ref<FoundryBuildContext> context;
	context.instantiate();

	CHECK_FALSE(context->has_method("set_trusted_execution"));
	List<PropertyInfo> properties;
	context->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		CHECK_NE(property.name, StringName("trusted_execution"));
	}

	context->set_trusted_execution(true);
	CHECK(context->is_trusted_execution());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Refuses external commands without explicit trust") {
	Ref<FoundryBuildContext> context;
	context.instantiate();
	context->set_provider_id("command");
	context->set_task_name("untrusted_command");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('should-not-run')\n");
	options["outputs"] = make_args("res://generated/untrusted.txt");
	context->set_options(options);

	Ref<FoundryCommandBuildTask> task;
	task.instantiate();
	Ref<FoundryBuildResult> result = task->run(context);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_EQ(result->get_exit_code(), -1);
	CHECK_FALSE(result->get_diagnostics().is_empty());
	if (result->get_diagnostics().is_empty()) {
		return;
	}
	Dictionary diagnostic = result->get_diagnostics()[0];
	CHECK_EQ(String(diagnostic["task_name"]), "untrusted_command");
	CHECK_EQ(String(diagnostic["provider_id"]), "command");
	CHECK(String(diagnostic["message"]).contains("trust"));
	CHECK_FALSE(String(diagnostic["stdout"]).contains("should-not-run"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Runs PATH command with argv and project path normalization") {
	ScopedCommandTaskProject project("build_task_command_paths");
	project.write_file("res://work/relative.txt", "relative-ok");
	project.write_file("res://input.txt", "input-ok");

	const String script =
			"import os, pathlib, sys\n"
			"assert pathlib.Path(os.getcwd()).resolve() == pathlib.Path(sys.argv[1]).resolve()\n"
			"assert sys.argv[2] == 'relative.txt'\n"
			"assert pathlib.Path(sys.argv[3]).is_absolute()\n"
			"assert pathlib.Path(sys.argv[4]).is_absolute()\n"
			"assert pathlib.Path(sys.argv[2]).read_text() == 'relative-ok'\n"
			"assert pathlib.Path(sys.argv[3]).read_text() == 'input-ok'\n"
			"assert os.environ['FOUNDRY_COMMAND_TASK_ENV'] == 'env-ok'\n"
			"pathlib.Path(sys.argv[4]).write_text('done')\n"
			"print('argv-ok')\n";

	Dictionary environment;
	environment["FOUNDRY_COMMAND_TASK_ENV"] = "env-ok";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", script, "res://work", "relative.txt", "res://input.txt", "res://out.txt");
	options["working_directory"] = "res://work";
	options["environment"] = environment;
	options["inputs"] = make_args("res://input.txt");
	options["outputs"] = make_args("res://out.txt");
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK_EQ(result->get_exit_code(), 0);
	CHECK(result->get_stdout().contains("argv-ok"));
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize("res://out.txt")), "done");

	Array commands = result->get_commands();
	CHECK_EQ(commands.size(), 1);
	if (commands.size() != 1) {
		return;
	}
	Ref<FoundryBuildCommand> command = commands[0];
	CHECK(command.is_valid());
	if (command.is_null()) {
		return;
	}
	CHECK(command->get_executable().is_absolute_path());
	CHECK_EQ(command->get_working_directory(), project.globalize("res://work"));
	PackedStringArray normalized_args = command->get_arguments();
	CHECK_EQ(normalized_args.size(), 6);
	if (normalized_args.size() != 6) {
		return;
	}
	CHECK_EQ(normalized_args[2], project.globalize("res://work"));
	CHECK_EQ(normalized_args[3], "relative.txt");
	CHECK_EQ(normalized_args[4], project.globalize("res://input.txt"));
	CHECK_EQ(normalized_args[5], project.globalize("res://out.txt"));
}

#if defined(UNIX_ENABLED) && !defined(WEB_ENABLED)
TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Resolves relative PATH entries from working directory") {
	ScopedCommandTaskProject project("build_task_command_relative_path");
	project.write_file("res://tools/foundry-test-tool",
			"#!/bin/sh\n"
			"printf 'relative-path-tool\\n'\n");
	const String tool_path = project.globalize("res://tools/foundry-test-tool");
	CHECK_EQ(FileAccess::set_unix_permissions(tool_path, 0755), OK);

	Dictionary environment;
	environment["PATH"] = "tools";

	Dictionary options;
	options["command"] = "foundry-test-tool";
	options["working_directory"] = "res://";
	options["environment"] = environment;
	options["outputs"] = make_args("res://generated/relative_path_tool.txt");
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains("relative-path-tool"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Skips non-executable PATH entries") {
	ScopedCommandTaskProject project("build_task_command_path_shadow");
	project.write_file("res://shadow/foundry-shadow-tool",
			"#!/bin/sh\n"
			"printf 'shadowed\\n'\n");
	const String shadow_path = project.globalize("res://shadow/foundry-shadow-tool");
	CHECK_EQ(FileAccess::set_unix_permissions(shadow_path, 0644), OK);

	project.write_file("res://tools/foundry-shadow-tool",
			"#!/bin/sh\n"
			"printf 'path-shadow-ok\\n'\n");
	const String tool_path = project.globalize("res://tools/foundry-shadow-tool");
	CHECK_EQ(FileAccess::set_unix_permissions(tool_path, 0755), OK);

	Dictionary environment;
	environment["PATH"] = "shadow:tools";

	Dictionary options;
	options["command"] = "foundry-shadow-tool";
	options["working_directory"] = "res://";
	options["environment"] = environment;
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains("path-shadow-ok"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Skips inaccessible PATH entries") {
	ScopedCommandTaskProject project("build_task_command_path_inaccessible_shadow");
	project.write_file("res://shadow/foundry-inaccessible-shadow",
			"#!/bin/sh\n"
			"printf 'inaccessible-shadow\\n'\n");
	const String shadow_path = project.globalize("res://shadow/foundry-inaccessible-shadow");
	CHECK_EQ(FileAccess::set_unix_permissions(shadow_path, 0001), OK);

	project.write_file("res://tools/foundry-inaccessible-shadow",
			"#!/bin/sh\n"
			"printf 'path-inaccessible-shadow-ok\\n'\n");
	const String tool_path = project.globalize("res://tools/foundry-inaccessible-shadow");
	CHECK_EQ(FileAccess::set_unix_permissions(tool_path, 0755), OK);

	Dictionary environment;
	environment["PATH"] = "shadow:tools";

	Dictionary options;
	options["command"] = "foundry-inaccessible-shadow";
	options["working_directory"] = "res://";
	options["environment"] = environment;
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains("path-inaccessible-shadow-ok"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Resolves relative command paths from working directory") {
	ScopedCommandTaskProject project("build_task_command_relative_executable");
	project.write_file("res://tools/foundry-direct-tool",
			"#!/bin/sh\n"
			"printf 'relative-command-tool\\n'\n");
	const String tool_path = project.globalize("res://tools/foundry-direct-tool");
	CHECK_EQ(FileAccess::set_unix_permissions(tool_path, 0755), OK);

	Dictionary environment;
	environment["PATH"] = "";

	Dictionary options;
	options["command"] = "./tools/foundry-direct-tool";
	options["working_directory"] = "res://";
	options["environment"] = environment;
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains("relative-command-tool"));

	Array commands = result->get_commands();
	CHECK_EQ(commands.size(), 1);
	if (commands.size() != 1) {
		return;
	}
	Ref<FoundryBuildCommand> command = commands[0];
	CHECK(command.is_valid());
	if (command.is_null()) {
		return;
	}
	CHECK_EQ(command->get_executable(), tool_path);
}
#endif

#ifdef WINDOWS_ENABLED
TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Rejects Windows PATHEXT batch shims") {
	ScopedCommandTaskProject project("build_task_command_windows_batch");
	project.write_file("res://tools/foundry-batch-tool.cmd",
			"@echo off\r\n"
			"echo batch-ok:%~1\r\n");

	Dictionary environment;
	environment["PATH"] = "tools";
	environment["PATHEXT"] = ".CMD";

	Dictionary options;
	options["command"] = "foundry-batch-tool";
	options["args"] = make_args("arg with & value");
	options["working_directory"] = "res://";
	options["environment"] = environment;
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_FALSE(result->get_launch_error().is_empty());
	CHECK(result->get_launch_error().contains("foundry-batch-tool"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Rejects direct Windows batch paths") {
	ScopedCommandTaskProject project("build_task_command_windows_direct_batch");
	project.write_file("res://tools/foundry-direct-batch.cmd",
			"@echo off\r\n"
			"echo direct-batch-ok:%~1\r\n");

	Dictionary options;
	options["command"] = "res://tools/foundry-direct-batch.cmd";
	options["args"] = make_args("arg with & value");
	options["working_directory"] = "res://";
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_FALSE(result->get_launch_error().is_empty());
	CHECK(result->get_launch_error().contains("res://tools/foundry-direct-batch.cmd"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Preserves Windows argv edge cases") {
	PackedStringArray args;
	args.push_back("-c");
	args.push_back(
			"import sys\n"
			"assert sys.argv[1] == ''\n"
			"assert sys.argv[2] == 'a\" b'\n"
			"assert sys.argv[3] == 'C:\\\\path with space\\\\'\n"
			"print('windows-argv-ok')\n");
	args.push_back("");
	args.push_back("a\" b");
	args.push_back("C:\\path with space\\");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = args;
	options["outputs"] = make_args("res://generated/windows_argv.txt");
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains("windows-argv-ok"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Windows kill terminates tracked descendants without a job") {
	ScopedCommandTaskProject project("build_task_command_windows_kill_tree");
	const String sentinel = project.globalize("res://generated/child_survived.txt");
	const String child_script =
			"import pathlib, sys, time\n"
			"time.sleep(2)\n"
			"pathlib.Path(sys.argv[1]).write_text('leaked')\n";
	const String parent_script =
			"import subprocess, sys, time\n"
			"subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]])\n"
			"time.sleep(10)\n";

	List<String> arguments;
	arguments.push_back("-c");
	arguments.push_back(parent_script);
	arguments.push_back(child_script);
	arguments.push_back(sentinel);

	OS::ProcessID pid = 0;
	const Error err = OS::get_singleton()->create_process("python3", arguments, &pid);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	OS::get_singleton()->delay_usec(500000);
	CHECK_EQ(OS::get_singleton()->kill(pid), OK);
	OS::get_singleton()->delay_usec(3000000);
	CHECK_FALSE(FileAccess::exists(sentinel));
}
#endif

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Passes args as argv entries without shell evaluation") {
	ScopedCommandTaskProject project("build_task_command_no_shell");
	const String sentinel = project.root_path.path_join("shell_eval_sentinel");
	const String malicious = String("$(touch \"") + sentinel + "\")";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c",
			"import sys\n"
			"assert sys.argv[2] == ' relative arg '\n"
			"print(sys.argv[1])\n",
			malicious, " relative arg ");
	options["working_directory"] = "res://";
	options["outputs"] = make_args("res://generated/no_shell.txt");
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK(result->get_stdout().contains(malicious));
	CHECK_FALSE(FileAccess::exists(sentinel));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Closes stdin for commands that read until EOF") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c",
			"import sys\n"
			"assert sys.stdin.read() == ''\n"
			"print('stdin-eof')\n");
	options["outputs"] = make_args("res://generated/stdin_eof.txt");
	options["timeout_seconds"] = 2;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK_FALSE(result->has_timed_out());
	CHECK(result->get_stdout().contains("stdin-eof"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Captures stdout stderr and non-zero exit diagnostics") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c",
			"import sys\n"
			"print('stdout-line')\n"
			"print('stderr-line', file=sys.stderr)\n"
			"sys.exit(7)\n");
	options["outputs"] = make_args("res://generated/nonzero.txt");
	options["timeout_seconds"] = 5;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_EQ(result->get_exit_code(), 7);
	CHECK(result->get_stdout().contains("stdout-line"));
	CHECK(result->get_stderr().contains("stderr-line"));
	CHECK_FALSE(result->get_diagnostics().is_empty());
	if (result->get_diagnostics().is_empty()) {
		return;
	}
	Dictionary diagnostic = result->get_diagnostics()[0];
	CHECK_EQ(String(diagnostic["task_name"]), "test_command");
	CHECK_EQ(String(diagnostic["provider_id"]), "command");
	CHECK_EQ(String(diagnostic["command"]), "python3");
	CHECK_EQ(int(diagnostic["exit_code"]), 7);
	CHECK(String(diagnostic["stdout_tail"]).contains("stdout-line"));
	CHECK(String(diagnostic["stderr_tail"]).contains("stderr-line"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Reports timeout diagnostics") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "import time\nprint('before-timeout', flush=True)\ntime.sleep(5)\n");
	options["outputs"] = make_args("res://generated/timeout.txt");
	options["timeout_seconds"] = 1;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK(result->has_timed_out());
	CHECK_FALSE(result->get_diagnostics().is_empty());
	if (result->get_diagnostics().is_empty()) {
		return;
	}
	Dictionary diagnostic = result->get_diagnostics()[0];
	CHECK(bool(diagnostic["timed_out"]));
	CHECK(String(diagnostic["stdout"]).contains("before-timeout"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Enforces timeout while draining continuous output") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c",
			"import os, sys, time\n"
			"deadline = time.monotonic() + 4\n"
			"chunk = b'x' * 131072\n"
			"while time.monotonic() < deadline:\n"
			"    os.write(sys.stdout.fileno(), chunk)\n"
			"time.sleep(10)\n");
	options["outputs"] = make_args("res://generated/continuous_timeout.txt");
	options["timeout_seconds"] = 1;

	const uint64_t start_msec = OS::get_singleton()->get_ticks_msec();
	Ref<FoundryBuildResult> result = run_command_task(options);
	const uint64_t elapsed_msec = OS::get_singleton()->get_ticks_msec() - start_msec;

	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK(result->has_timed_out());
	CHECK_LT(elapsed_msec, uint64_t(3000));
}

#if defined(UNIX_ENABLED) && !defined(WEB_ENABLED)
TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Timeout terminates the Unix process group") {
	ScopedCommandTaskProject project("build_task_command_timeout_group");
	const String sentinel = project.globalize("res://generated/grandchild.txt");
	const String child_script =
			"import pathlib, sys, time\n"
			"time.sleep(2)\n"
			"pathlib.Path(sys.argv[1]).write_text('leaked')\n";
	const String parent_script =
			"import subprocess, sys, time\n"
			"subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]])\n"
			"time.sleep(10)\n";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", parent_script, child_script, sentinel);
	options["outputs"] = make_args("res://generated/grandchild.txt");
	options["timeout_seconds"] = 1;

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK(result->has_timed_out());

	OS::get_singleton()->delay_usec(3000000);
	CHECK_FALSE(FileAccess::exists(sentinel));
}
#endif

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Reports missing executable diagnostics") {
	Dictionary options;
	options["command"] = "foundry_missing_executable_for_issue_738";
	options["outputs"] = make_args("res://generated/missing.txt");

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK_FALSE(result->is_success());
	CHECK_FALSE(result->get_launch_error().is_empty());
	CHECK_FALSE(result->get_diagnostics().is_empty());
	if (result->get_diagnostics().is_empty()) {
		return;
	}
	Dictionary diagnostic = result->get_diagnostics()[0];
	CHECK(String(diagnostic["launch_error"]).contains("foundry_missing_executable_for_issue_738"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Tool version output participates in fingerprint") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["outputs"] = make_args("res://generated/versioned.txt");
	options["tool_version_command"] = make_args("python3", "-c", "print('tool-v1')\n");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null()) {
		return;
	}
	CHECK(first->is_success());
	if (!first->is_success()) {
		return;
	}
	CHECK_FALSE(first->get_fingerprint().is_empty());

	options["tool_version_command"] = make_args("python3", "-c", "print('tool-v2')\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null()) {
		return;
	}
	CHECK(second->is_success());
	if (!second->is_success()) {
		return;
	}
	CHECK_FALSE(second->get_fingerprint().is_empty());
	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Tool version fingerprint changes mark persisted state dirty") {
	ScopedCommandTaskProject project("build_task_command_state_tool_version");

	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).write_text('generated\\n')\n";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", script, "res://generated/tool_version_state.txt");
	options["outputs"] = make_args("res://generated/tool_version_state.txt");
	options["tool_version_command"] = make_args("python3", "-c", "print('tool-v1')\n");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	ProjectBuildState state;
	state.record_task_result("generate_tool_versioned", first->get_fingerprint(), make_args("res://generated/tool_version_state.txt"), true);
	CHECK_FALSE(state.get_task_dirty_status("generate_tool_versioned", first->get_fingerprint(), make_args("res://generated/tool_version_state.txt")).dirty);

	options["tool_version_command"] = make_args("python3", "-c", "print('tool-v2')\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_tool_versioned", second->get_fingerprint(), make_args("res://generated/tool_version_state.txt"));
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_FINGERPRINT_CHANGED);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Timeout participates in fingerprint") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["outputs"] = make_args("res://generated/timeout_fingerprint.txt");
	options["timeout_seconds"] = 1;

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	options["timeout_seconds"] = 2;
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Reports non-zero tool version diagnostics") {
	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["outputs"] = make_args("res://generated/version_probe_diagnostic.txt");
	options["tool_version_command"] = make_args("python3", "-c",
			"import sys\n"
			"print('bad-version')\n"
			"sys.exit(9)\n");

	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_null()) {
		return;
	}
	CHECK(result->is_success());
	CHECK_FALSE(result->get_diagnostics().is_empty());
	if (result->get_diagnostics().is_empty()) {
		return;
	}

	Dictionary diagnostic = result->get_diagnostics()[0];
	CHECK_EQ(int(diagnostic["exit_code"]), 9);
	CHECK(String(diagnostic["stdout"]).contains("bad-version"));
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Successful res outputs request an editor filesystem scan") {
	ScopedCommandTaskProject project("build_task_command_scan_changes");
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);

	const String script =
			"import pathlib, sys\n"
			"path = pathlib.Path(sys.argv[1])\n"
			"path.parent.mkdir(parents=True, exist_ok=True)\n"
			"path.write_text('generated\\n')\n";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", script, "res://generated/scan_changes.txt");
	options["outputs"] = make_args("res://generated/scan_changes.txt");

	const int before_scan_count = editor_file_system->get_scan_changes_call_count_for_tests();
	Ref<FoundryBuildResult> result = run_command_task(options);
	CHECK(result.is_valid());
	if (result.is_valid()) {
		CHECK_MESSAGE(result->is_success(),
				vformat("message=%s\nstdout=%s\nstderr=%s\nlaunch_error=%s",
						result->get_message(), result->get_stdout(), result->get_stderr(), result->get_launch_error()));
	}
	CHECK_GT(editor_file_system->get_scan_changes_call_count_for_tests(), before_scan_count);

	memdelete(editor_file_system);
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Globalizes inputs and outputs before fingerprinting") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_paths");
	project.write_file("res://input.txt", "input-ok");

	Dictionary resource_options;
	resource_options["command"] = "python3";
	resource_options["args"] = make_args("-c", "print('build')\n");
	resource_options["inputs"] = make_args("res://input.txt");
	resource_options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> resource_result = run_command_task(resource_options);
	CHECK(resource_result.is_valid());
	if (resource_result.is_null()) {
		return;
	}
	CHECK(resource_result->is_success());
	if (!resource_result->is_success()) {
		return;
	}

	Dictionary absolute_options = resource_options.duplicate(true);
	absolute_options["inputs"] = make_args(project.globalize("res://input.txt"));
	absolute_options["outputs"] = make_args(project.globalize("res://generated/out.txt"));

	Ref<FoundryBuildResult> absolute_result = run_command_task(absolute_options);
	CHECK(absolute_result.is_valid());
	if (absolute_result.is_null()) {
		return;
	}
	CHECK(absolute_result->is_success());
	if (!absolute_result->is_success()) {
		return;
	}

	CHECK_EQ(resource_result->get_fingerprint(), absolute_result->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Input file contents participate in fingerprinting") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_contents");
	project.write_file("res://input.txt", "input-v1");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["inputs"] = make_args("res://input.txt");
	options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	project.write_file("res://input.txt", "input-v2");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Input content fingerprint changes mark persisted state dirty") {
	ScopedCommandTaskProject project("build_task_command_state_input");
	project.write_file("res://input.txt", "input-v1");

	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).write_text('generated\\n')\n";

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", script, "res://generated/input_state.txt");
	options["inputs"] = make_args("res://input.txt");
	options["outputs"] = make_args("res://generated/input_state.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	ProjectBuildState state;
	state.record_task_result("generate_from_input", first->get_fingerprint(), make_args("res://generated/input_state.txt"), true);
	CHECK_FALSE(state.get_task_dirty_status("generate_from_input", first->get_fingerprint(), make_args("res://generated/input_state.txt")).dirty);

	project.write_file("res://input.txt", "input-v2");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	const ProjectBuildState::DirtyStatus status =
			state.get_task_dirty_status("generate_from_input", second->get_fingerprint(), make_args("res://generated/input_state.txt"));
	CHECK(status.dirty);
	CHECK_EQ(status.reason, ProjectBuildState::DIRTY_FINGERPRINT_CHANGED);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Input glob matches participate in fingerprinting") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_globs");
	project.write_file("res://proto/a.proto", "message A {}\n");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["inputs"] = make_args("res://proto/**/*.proto");
	options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	project.write_file("res://proto/b.proto", "message B {}\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Non-recursive input globs ignore nested matches") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_flat_globs");
	project.write_file("res://proto/a.proto", "message A {}\n");
	project.write_file("res://proto/nested/b.proto", "message NestedV1 {}\n");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["inputs"] = make_args("res://proto/*.proto");
	options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	project.write_file("res://proto/nested/b.proto", "message NestedV2 {}\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_EQ(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Root input glob matches participate in fingerprinting") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_root_globs");
	project.write_file("res://root.proto", "message RootV1 {}\n");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["inputs"] = make_args("res://*.proto");
	options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	project.write_file("res://root.proto", "message RootV2 {}\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Hidden input glob matches participate in fingerprinting") {
	ScopedCommandTaskProject project("build_task_command_fingerprint_hidden_globs");
	project.write_file("res://.env", "SECRET=v1\n");

	Dictionary options;
	options["command"] = "python3";
	options["args"] = make_args("-c", "print('build')\n");
	options["inputs"] = make_args("res://**/.env");
	options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> first = run_command_task(options);
	CHECK(first.is_valid());
	if (first.is_null() || !first->is_success()) {
		return;
	}

	project.write_file("res://.env", "SECRET=v2\n");
	Ref<FoundryBuildResult> second = run_command_task(options);
	CHECK(second.is_valid());
	if (second.is_null() || !second->is_success()) {
		return;
	}

	CHECK_NE(first->get_fingerprint(), second->get_fingerprint());
}

TEST_CASE("[Modules][FoundryScript][BuildTaskCommand] Fingerprint fields are not newline-ambiguous") {
	Dictionary one_arg_options;
	one_arg_options["command"] = "python3";
	one_arg_options["args"] = make_args("-c", "print('build')\n", "a\narg\nb");
	one_arg_options["outputs"] = make_args("res://generated/out.txt");

	Ref<FoundryBuildResult> one_arg_result = run_command_task(one_arg_options);
	CHECK(one_arg_result.is_valid());
	if (one_arg_result.is_null() || !one_arg_result->is_success()) {
		return;
	}

	Dictionary two_arg_options = one_arg_options.duplicate(true);
	two_arg_options["args"] = make_args("-c", "print('build')\n", "a", "b");

	Ref<FoundryBuildResult> two_arg_result = run_command_task(two_arg_options);
	CHECK(two_arg_result.is_valid());
	if (two_arg_result.is_null() || !two_arg_result->is_success()) {
		return;
	}

	CHECK_NE(one_arg_result->get_fingerprint(), two_arg_result->get_fingerprint());
}

} // namespace FSTests
