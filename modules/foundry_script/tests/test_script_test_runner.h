/**************************************************************************/
/*  test_script_test_runner.h                                             */
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

#include "../fs_script_extensible_native_hooks.h"
#include "fs_test_runner.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_function_state.h"
#include "core/object/script_test_runner.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "tests/test_macros.h"

namespace FSTests {

static const char *test_runner_scripts_root = "modules/foundry_script/tests/scripts";

static void prepare_test_runner_project() {
	const String scripts_path = String(test_runner_scripts_root);
	const Error err = ProjectSettings::get_singleton()->setup(scripts_path, String(), true);
	REQUIRE_MESSAGE(err == OK, "Failed to set up test runner project.");
	if (!is_fs_language_active()) {
		init_language(scripts_path);
	}
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner] Script test runner hook is registered as script-extensible") {
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("ScriptTestRunner"), SNAME("run")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("get")));
}

static Ref<ScriptTestRunner> load_runner_script(const String &p_res_path) {
	Ref<Script> script_res = ResourceLoader::load(p_res_path);
	REQUIRE_MESSAGE(script_res.is_valid(), vformat("Failed to load runner script: %s", p_res_path));
	REQUIRE(script_res->can_instantiate());
	REQUIRE(ClassDB::is_parent_class(script_res->get_instance_base_type(), "ScriptTestRunner"));

	ScriptTestRunner *runner_object = memnew(ScriptTestRunner);
	Ref<ScriptTestRunner> runner(runner_object);
	runner->set_script(script_res);
	REQUIRE_FALSE(runner.is_null());
	return runner;
}

static int run_host_to_completion(const Ref<ScriptTestRunner> &p_runner, const PackedStringArray &p_user_args, int p_max_frames = 240, bool p_emit_proceed_signal = false) {
	SceneTree *tree = SceneTree::get_singleton();
	REQUIRE(tree != nullptr);

	OS::get_singleton()->set_exit_code(EXIT_SUCCESS);
	ScriptTestRunner::launch_host(tree, p_runner, p_user_args);

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

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Sync run returns process exit code 0") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/sync_exit_0.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Sync run returns process exit code 3") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/sync_exit_3.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 3);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Async run returns process exit code 1") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/async_exit_1.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray(), 240, true), 1);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Sync runtime error exits with failure") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/sync_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), EXIT_FAILURE);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Guarded runtime error does not override runner exit code") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/sync_guarded_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray()), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Async runtime error exits with failure") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/async_runtime_error.notest.fs");
	CHECK_EQ(run_host_to_completion(runner, PackedStringArray(), 240, true), EXIT_FAILURE);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] User args passthrough") {
	prepare_test_runner_project();
	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/user_args.notest.fs");
	PackedStringArray user_args;
	user_args.push_back("--filter");
	user_args.push_back("inventory");
	CHECK_EQ(run_host_to_completion(runner, user_args), 0);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestRunner][SceneTree] Async Object::call result casts to ScriptFunctionState") {
	prepare_test_runner_project();

	const Ref<ScriptTestRunner> runner = load_runner_script("res://test_runner_host/async_contract.notest.fs");
	const Variant result = ScriptTestRunner::call_run_script_hook(runner, PackedStringArray());
	CHECK(Object::cast_to<ScriptFunctionState>(result) != nullptr);
}

} // namespace FSTests
