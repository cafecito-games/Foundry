/**************************************************************************/
/*  test_script_runner.h                                                  */
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

#include "../fs_inline_eval.h"
#include "../fs_script_extensible_native_hooks.h"
#include "fs_test_runner.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_function_state.h"
#include "core/object/script_runner.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

static const char *test_runner_scripts_root = "modules/foundry_script/tests/scripts";

struct ScriptRunnerProjectFixture {
	TestProjectSettingsRestoreScope project_settings;

	explicit ScriptRunnerProjectFixture() {
		const String scripts_path = String(test_runner_scripts_root);
		const Error err = ProjectSettings::get_singleton()->setup(scripts_path, String(), true);
		REQUIRE_MESSAGE(err == OK, "Failed to set up test runner project.");
		if (!is_fs_language_active()) {
			init_language(scripts_path);
		}
	}
};

TEST_CASE("[Modules][FoundryScript][ScriptRunner][Lifecycle] Direct fixture teardown restores exact project settings") {
	finish_language();
	TestProjectSettingsRestoreScope restore_process_settings;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String expected_resource_path;
	const bool expected_project_loaded = false;
	const String expected_app_name = "ScriptRunnerBeforeDirectCase";
	TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
	TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
	settings->set_setting("application/config/name", expected_app_name);

	{
		ScriptRunnerProjectFixture project;
		CHECK(is_fs_language_active());
		CHECK_NE(settings->get_resource_path(), expected_resource_path);
	}

	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);

	finish_language();
	CHECK_FALSE(is_fs_language_active());
	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner] Script runner hook is registered as script-extensible") {
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("ScriptRunner"), SNAME("run")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("get")));
}

static Ref<ScriptRunner> load_runner_script(const String &p_res_path) {
	Ref<Script> script_res = ResourceLoader::load(p_res_path);
	REQUIRE_MESSAGE(script_res.is_valid(), vformat("Failed to load runner script: %s", p_res_path));
	REQUIRE(script_res->can_instantiate());
	REQUIRE(ClassDB::is_parent_class(script_res->get_instance_base_type(), "ScriptRunner"));

	ScriptRunner *runner_object = memnew(ScriptRunner);
	Ref<ScriptRunner> runner(runner_object);
	runner->set_script(script_res);
	REQUIRE_FALSE(runner.is_null());
	return runner;
}

static int run_host_to_completion(const Ref<ScriptRunner> &p_runner, const PackedStringArray &p_user_args, int p_max_frames = 240, bool p_emit_proceed_signal = false) {
	SceneTree *tree = SceneTree::get_singleton();
	REQUIRE(tree != nullptr);

	OS::get_singleton()->set_exit_code(EXIT_SUCCESS);
	ScriptRunner::launch_host(tree, p_runner, p_user_args);

	for (int frame = 0; frame < p_max_frames; frame++) {
		if (tree->process(1.0 / 60.0)) {
			break;
		}
		if (p_emit_proceed_signal && frame == 0) {
			p_runner->call("emit_signal", "proceed");
		}
	}

	return OS::get_singleton()->get_exit_code();
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Sync run returns process exit code 0") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/sync_exit_0.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Sync run returns process exit code 3") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/sync_exit_3.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 3);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Async run returns process exit code 1") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/async_exit_1.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray(), 240, true), 1);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Sync runtime error exits with failure") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/sync_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), EXIT_FAILURE);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Guarded runtime error does not override runner exit code") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/sync_guarded_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Async runtime error exits with failure") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/async_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray(), 240, true), EXIT_FAILURE);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] User args passthrough") {
	ScriptRunnerProjectFixture project;
	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/user_args.notest.fs");
	PackedStringArray user_args;
	user_args.push_back("--filter");
	user_args.push_back("inventory");
	CHECK_EQ(run_host_to_completion(runner, user_args), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Async Object::call result casts to ScriptFunctionState") {
	ScriptRunnerProjectFixture project;

	const Ref<ScriptRunner> runner = load_runner_script("res://test_runner_host/async_contract.notest.fs");
	const Variant result = ScriptRunner::call_run_script_hook(runner, PackedStringArray());
	CHECK(Object::cast_to<ScriptFunctionState>(result) != nullptr);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner] Inline eval wraps the snippet in a runner body") {
	const String source = FSInlineEval::build_runner_source("print(\"ok\")");
	CHECK(source.contains("extends ScriptRunner"));
	CHECK(source.contains("func run(args: PackedStringArray) -> int:"));
	// The snippet is indented into the run() body and a default return is appended.
	CHECK(source.contains("\tprint(\"ok\")\n"));
	CHECK(source.contains("\treturn 0\n"));
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Inline eval prints and exits 0") {
	ScriptRunnerProjectFixture project;
	String error;
	const Ref<ScriptRunner> runner = FSInlineEval::compile_runner("print(\"ok\")", error);
	REQUIRE_MESSAGE(runner.is_valid(), error);
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Inline eval return sets the exit code") {
	ScriptRunnerProjectFixture project;
	String error;
	const Ref<ScriptRunner> runner = FSInlineEval::compile_runner("return 3", error);
	REQUIRE_MESSAGE(runner.is_valid(), error);
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 3);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner][SceneTree] Inline eval exposes user args") {
	ScriptRunnerProjectFixture project;
	String error;
	const Ref<ScriptRunner> runner = FSInlineEval::compile_runner("return args.size()", error);
	REQUIRE_MESSAGE(runner.is_valid(), error);
	PackedStringArray user_args;
	user_args.push_back("--first");
	user_args.push_back("--second");
	CHECK_EQ(run_host_to_completion(runner, user_args), 2);
}

TEST_CASE("[Modules][FoundryScript][ScriptRunner] Inline eval rejects a snippet that does not compile") {
	ScriptRunnerProjectFixture project;
	String error;
	const Ref<ScriptRunner> runner = FSInlineEval::compile_runner("this is not valid foundry script", error);
	CHECK(runner.is_null());
	CHECK_FALSE(error.is_empty());
}

} // namespace FSTests
