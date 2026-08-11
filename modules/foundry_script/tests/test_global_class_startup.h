/**************************************************************************/
/*  test_global_class_startup.h                                           */
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

#include "fs_test_python.h"
#include "fs_temporary_project_tree.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "main/global_class_scan_policy.h"
#include "tests/test_macros.h"

#include <cstring>

namespace FSTests {

struct GlobalClassStartupProcessResult {
	Error error = FAILED;
	int exit_code = -1;
	String output;
};

static GlobalClassStartupProcessResult run_global_class_startup_process(
		const List<String> &p_arguments, const String &p_working_directory = String()) {
	GlobalClassStartupProcessResult result;
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false,
			p_working_directory, Dictionary(), false);
	if (pipe_info.is_empty()) {
		return result;
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];
	auto pump = [](const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) {
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
		const int old_size = r_bytes.size();
		r_bytes.resize(old_size + read);
		if (read > 0) {
			memcpy(r_bytes.ptrw() + old_size, chunk.ptr(), read);
		}
	};

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 30000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump(stdout_pipe, stdout_bytes);
		pump(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump(stdout_pipe, stdout_bytes);
			pump(stderr_pipe, stderr_bytes);
			result.error = OK;
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	if (result.error != OK && OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}
	result.exit_code = OS::get_singleton()->get_process_exit_code(pid);
	result.output = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
	result.output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return result;
}

static List<String> global_class_project_run_args(const String &p_project, bool p_verbose = false) {
	List<String> args;
	args.push_back("--headless");
	args.push_back("--no-header");
	if (p_verbose) {
		args.push_back("--verbose");
	}
	args.push_back("project");
	args.push_back("run");
	args.push_back("--project");
	args.push_back(p_project);
	args.push_back("--quit-after");
	args.push_back("60");
	return args;
}

static int count_startup_scan_summaries(const String &p_output) {
	return p_output.count("ScriptServer: Scanned ");
}

static Error write_global_class_startup_cache(const TemporaryProjectTree &p_tree,
		const StringName &p_class, const String &p_path) {
	p_tree.write_file(".foundry/.keep", String());
	Dictionary entry;
	entry["base"] = SNAME("RefCounted");
	entry["class"] = p_class;
	entry["icon"] = String();
	entry["is_abstract"] = false;
	entry["is_tool"] = false;
	entry["is_trait"] = false;
	entry["is_enum"] = false;
	entry["language"] = SNAME("FoundryScript");
	entry["path"] = p_path;
	Array entries;
	entries.push_back(entry);
	Ref<ConfigFile> cache;
	cache.instantiate();
	cache->set_value(String(), "list", entries);
	return cache->save(p_tree.root.path_join(".foundry/global_script_class_cache.cfg"));
}

TEST_SUITE("[Modules][FoundryScript][GlobalClassStartup]") {
	TEST_CASE("GlobalClassStartup policy scans only standalone source consumers") {
		CHECK(GlobalClassScanPolicy::should_scan(true, false, false, false, true));
		CHECK_FALSE(GlobalClassScanPolicy::should_scan(false, false, false, false, true));
		CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, true, false, false, true));
		CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, true, false, true));
		CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, false, true, true));
		CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, false, false, false));
	}

	TEST_CASE("GlobalClassStartup cacheless main scene resolves current global classes") {
		TemporaryProjectTree tree("fs_global_class_startup_cacheless_scene");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Cacheless Scene\"\n"
				"run/main_scene=\"res://main.tscn\"\n");
		tree.write_file("main.tscn",
				"[gd_scene load_steps=2 format=3]\n\n"
				"[ext_resource path=\"res://main.fs\" type=\"Script\" id=\"1\"]\n\n"
				"[node name=\"Main\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");
		tree.write_file("main.fs",
				"extends Node\n\n"
				"func _ready() -> void:\n"
				"\tvar dependency := StartupCachelessDependency.new()\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_SCENE_OK:\" + dependency.value())\n"
				"\tget_tree().quit()\n");
		tree.write_file("dependency.fs",
				"class_name StartupCachelessDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");

		const GlobalClassStartupProcessResult result = run_global_class_startup_process(
				global_class_project_run_args(tree.root, true));
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_SCENE_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
		CHECK_FALSE(FileAccess::exists(tree.root.path_join(".foundry/global_script_class_cache.cfg")));
	}

	TEST_CASE("GlobalClassStartup stale cache yields to current main scene source") {
		TemporaryProjectTree tree("fs_global_class_startup_stale_scene");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Stale Scene\"\n"
				"run/main_scene=\"res://main.tscn\"\n");
		tree.write_file("main.tscn",
				"[gd_scene load_steps=2 format=3]\n\n"
				"[ext_resource path=\"res://main.fs\" type=\"Script\" id=\"1\"]\n\n"
				"[node name=\"Main\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");
		tree.write_file("main.fs",
				"extends Node\n\n"
				"func _ready() -> void:\n"
				"\tvar dependency := StartupCachelessDependency.new()\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_SCENE_OK:\" + dependency.value())\n"
				"\tget_tree().quit()\n");
		tree.write_file("dependency.fs",
				"class_name StartupCachelessDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");
		REQUIRE_EQ(write_global_class_startup_cache(
					   tree, SNAME("StartupCachelessDependency"), "res://deleted_dependency.fs"),
				OK);
		const String cache_path = tree.root.path_join(".foundry/global_script_class_cache.cfg");
		const Vector<uint8_t> stale_cache_bytes = FileAccess::get_file_as_bytes(cache_path);
		REQUIRE_FALSE(stale_cache_bytes.is_empty());

		const GlobalClassStartupProcessResult result = run_global_class_startup_process(
				global_class_project_run_args(tree.root, true));
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_SCENE_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
		CHECK(FileAccess::get_file_as_bytes(cache_path) == stale_cache_bytes);
	}

	TEST_CASE("GlobalClassStartup cacheless script main loop resolves before startup") {
		TemporaryProjectTree tree("fs_global_class_startup_main_loop");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Main Loop\"\n"
				"run/main_scene=\"res://empty.tscn\"\n"
				"run/main_loop_type=\"StartupCachelessMainLoop\"\n");
		tree.write_file("empty.tscn",
				"[gd_scene format=3]\n\n"
				"[node name=\"Empty\" type=\"Node\"]\n");
		tree.write_file("startup_loop.fs",
				"class_name StartupCachelessMainLoop\n"
				"extends SceneTree\n\n"
				"func _initialize() -> void:\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_MAIN_LOOP_OK\")\n"
				"\tquit()\n");

		const GlobalClassStartupProcessResult result = run_global_class_startup_process(
				global_class_project_run_args(tree.root, true));
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_MAIN_LOOP_OK"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
	}

	TEST_CASE("GlobalClassStartup scans provider imports before trusted pre_compile") {
		TemporaryProjectTree tree("fs_global_class_startup_provider_import");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Provider Import\"\n"
				"run/main_scene=\"res://main.tscn\"\n\n"
				"[build]\n\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"startup_provider\")\n\n"
				"[build/providers/startup.provider]\n\n"
				"script=\"res://build/provider.fs\"\n"
				"class_name=\"StartupBuildProvider\"\n\n"
				"[build/tasks/startup_provider]\n\n"
				"provider=\"startup.provider\"\n"
				"outputs=PackedStringArray(\"res://generated/provider.marker\")\n");
		tree.write_file("main.tscn",
				"[gd_scene load_steps=2 format=3]\n\n"
				"[ext_resource path=\"res://main.fs\" type=\"Script\" id=\"1\"]\n\n"
				"[node name=\"Main\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");
		tree.write_file("main.fs",
				"extends Node\n\n"
				"func _ready() -> void:\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_PROVIDER_RUNTIME_OK\")\n"
				"\tget_tree().quit()\n");
		tree.write_file("build/helper.fs",
				"namespace startup.provider\n"
				"class_name StartupProviderHelper extends RefCounted\n\n"
				"static func value() -> String:\n"
				"\treturn \"provider-ok\"\n");
		tree.write_file("build/provider.fs",
				"import startup.provider\n"
				"class_name StartupBuildProvider extends FoundryBuildTask\n\n"
				"func run(context: FoundryBuildContext) -> FoundryBuildResult:\n"
				"\tvar file := FileAccess.open(\"res://generated/provider.marker\", FileAccess.WRITE)\n"
				"\tfile.store_string(StartupProviderHelper.value())\n"
				"\tvar result := FoundryBuildResult.new()\n"
				"\tresult.success = true\n"
				"\tresult.fingerprint = StartupProviderHelper.value()\n"
				"\treturn result\n");
		tree.write_file("generated/.keep", String());

		List<String> args = global_class_project_run_args(tree.root);
		args.push_back("--trusted");
		const GlobalClassStartupProcessResult result = run_global_class_startup_process(args);
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_PROVIDER_RUNTIME_OK"), result.output);
		CHECK_EQ(FileAccess::get_file_as_string(tree.root.path_join("generated/provider.marker")), "provider-ok");
	}

	TEST_CASE("GlobalClassStartup direct script keeps one centralized scan") {
		TemporaryProjectTree tree("fs_global_class_startup_direct_script");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Direct Script\"\n");
		tree.write_file("dependency.fs",
				"class_name StartupDirectEntryDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");
		tree.write_file("entry.fs",
				"extends SceneTree\n\n"
				"func _initialize() -> void:\n"
				"\tvar dependency := StartupDirectEntryDependency.new()\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_SCRIPT_OK:\" + dependency.value())\n"
				"\tquit()\n");

		List<String> args = global_class_project_run_args(tree.root, true);
		args.push_back("--script");
		args.push_back("res://entry.fs");
		const GlobalClassStartupProcessResult result = run_global_class_startup_process(args);
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_SCRIPT_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
	}

	TEST_CASE("GlobalClassStartup project test keeps one centralized scan") {
		TemporaryProjectTree tree("fs_global_class_startup_project_test");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Project Test\"\n");
		tree.write_file("dependency.fs",
				"class_name StartupDirectEntryDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");
		tree.write_file("runner.fs",
				"extends ScriptRunner\n\n"
				"func run(args: PackedStringArray) -> int:\n"
				"\tvar dependency := StartupDirectEntryDependency.new()\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_PROJECT_TEST_OK:\" + dependency.value())\n"
				"\treturn 0\n");

		List<String> args;
		args.push_back("--headless");
		args.push_back("--no-header");
		args.push_back("--verbose");
		args.push_back("project");
		args.push_back("test");
		args.push_back("--project");
		args.push_back(tree.root);
		args.push_back("--runner");
		args.push_back("res://runner.fs");
		const GlobalClassStartupProcessResult result = run_global_class_startup_process(args);
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_PROJECT_TEST_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
	}

	TEST_CASE("GlobalClassStartup project backed eval keeps one centralized scan") {
		TemporaryProjectTree tree("fs_global_class_startup_eval");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Eval\"\n");
		tree.write_file("dependency.fs",
				"class_name StartupDirectEntryDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");

		List<String> args;
		args.push_back("--headless");
		args.push_back("--no-header");
		args.push_back("--verbose");
		args.push_back("script");
		args.push_back("eval");
		args.push_back("--project");
		args.push_back(tree.root);
		args.push_back(
				"print(\"GLOBAL_CLASS_STARTUP_EVAL_OK:\" + StartupDirectEntryDependency.new().value())");
		const GlobalClassStartupProcessResult result = run_global_class_startup_process(args);
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_EVAL_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 1, result.output);
	}

	TEST_CASE("GlobalClassStartup editor child trusts the prepared cache without scanning") {
		TemporaryProjectTree tree("fs_global_class_startup_editor_child");
		REQUIRE(tree.is_valid());
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Global Class Startup Editor Child\"\n"
				"run/main_scene=\"res://main.tscn\"\n");
		tree.write_file("main.tscn",
				"[gd_scene load_steps=2 format=3]\n\n"
				"[ext_resource path=\"res://main.fs\" type=\"Script\" id=\"1\"]\n\n"
				"[node name=\"Main\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");
		tree.write_file("main.fs",
				"extends Node\n\n"
				"func _ready() -> void:\n"
				"\tvar dependency := StartupEditorChildDependency.new()\n"
				"\tprint(\"GLOBAL_CLASS_STARTUP_EDITOR_CHILD_OK:\" + dependency.value())\n"
				"\tget_tree().quit()\n");
		tree.write_file("dependency.fs",
				"class_name StartupEditorChildDependency\n"
				"extends RefCounted\n\n"
				"func value() -> String:\n"
				"\treturn \"resolved\"\n");
		REQUIRE_EQ(write_global_class_startup_cache(
					   tree, SNAME("StartupEditorChildDependency"), "res://dependency.fs"),
				OK);

		List<String> args = global_class_project_run_args(tree.root, true);
		args.push_back("--editor-pid");
		args.push_back(itos(OS::get_singleton()->get_process_id()));
		const GlobalClassStartupProcessResult result = run_global_class_startup_process(args);
		REQUIRE_MESSAGE(result.error == OK, result.output);
		CHECK_MESSAGE(result.exit_code == 0, result.output);
		CHECK_MESSAGE(result.output.contains("GLOBAL_CLASS_STARTUP_EDITOR_CHILD_OK:resolved"), result.output);
		CHECK_MESSAGE(count_startup_scan_summaries(result.output) == 0, result.output);
	}
} // TEST_SUITE

} // namespace FSTests
