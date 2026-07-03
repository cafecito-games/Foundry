/**************************************************************************/
/*  test_script_test_execution.h                                          */
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

#include "../fs_script_test_execution.h"
#include "fs_test_runner.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/script_function_state.h"
#include "scene/main/scene_tree.h"
#include "tests/test_macros.h"

namespace FSTests {

static const char *script_test_execution_root = "modules/foundry_script/tests/scripts";

class ScriptTestExecutionCapture : public Object {
	FOUNDRY_CLASS(ScriptTestExecutionCapture, Object);

public:
	Ref<ScriptTestExecutionResult> result;
	bool done = false;

	void on_completed(const Variant &p_result) {
		result = p_result;
		done = true;
	}
};

struct ScriptExecutionFixture {
	Node *suite = nullptr;

	explicit ScriptExecutionFixture() {
		const String scripts_path = String(script_test_execution_root);
		const Error err = ProjectSettings::get_singleton()->setup(scripts_path, String(), true);
		REQUIRE_MESSAGE(err == OK, "Failed to set up script test execution project.");
		if (!is_fs_language_active()) {
			init_language(scripts_path);
		}

		Ref<Script> script = ResourceLoader::load("res://script_test_execution/fixture.notest.fs");
		REQUIRE(script.is_valid());
		REQUIRE(script->can_instantiate());
		suite = memnew(Node);
		suite->set_script(script);
		SceneTree::get_singleton()->get_root()->add_child(suite);
	}

	~ScriptExecutionFixture() {
		if (suite) {
			suite->queue_free();
			suite = nullptr;
		}
	}
};

static Ref<ScriptTestExecutionResult> run_guarded_call(Node *p_suite, const StringName &p_method, double p_timeout_seconds, bool p_abort_on_fatal = false, int p_max_frames = 360) {
	Ref<ScriptTestExecution> execution;
	execution.instantiate();
	execution->set_timeout_seconds(p_timeout_seconds);
	execution->set_abort_on_fatal(p_abort_on_fatal);

	ScriptTestExecutionCapture capture;
	const Variant call_result = execution->callv(p_suite, p_method, Array());

	Ref<ScriptTestExecutionResult> immediate = call_result;
	if (immediate.is_valid()) {
		return immediate;
	}

	ScriptTestExecutionPendingState *pending = Object::cast_to<ScriptTestExecutionPendingState>(call_result);
	REQUIRE(pending != nullptr);
	pending->connect(SNAME("completed"), callable_mp(&capture, &ScriptTestExecutionCapture::on_completed), Object::CONNECT_ONE_SHOT);

	SceneTree *tree = SceneTree::get_singleton();
	REQUIRE(tree != nullptr);
	for (int frame = 0; frame < p_max_frames && !capture.done; frame++) {
		tree->process(1.0 / 60.0);
	}

	REQUIRE(capture.done);
	REQUIRE(capture.result.is_valid());
	return capture.result;
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Sync method completes with return value") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("sync_return"), 0.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
	CHECK_EQ(int(result->get_return_value()), 42);
	CHECK(result->get_elapsed_seconds() >= 0.0);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Async method awaiting frames completes") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("async_frames"), 1.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
	CHECK_EQ(int(result->get_return_value()), 7);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Async await on a signal that never fires times out cleanly") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("async_never_signal"), 0.1);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_TIMED_OUT);
	CHECK(result->get_message().contains("0.1"));

	const Ref<ScriptTestExecutionResult> follow_up = run_guarded_call(fixture.suite, SNAME("sync_return"), 0.0);
	CHECK_EQ(follow_up->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] CPU loop times out without hanging") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("cpu_spin"), 0.1, false, 120);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_TIMED_OUT);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Sync abort stops before post-abort code") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("sync_abort"), 1.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_ABORTED);
	CHECK_EQ(result->get_message(), "sync abort");
	CHECK_FALSE(fixture.suite->get("abort_ran_after"));
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Async abort stops before post-abort code") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("async_abort"), 1.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_ABORTED);
	CHECK_EQ(result->get_message(), "async abort");
	CHECK_FALSE(fixture.suite->get("abort_ran_after"));
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Abort availability matches guard scope") {
	ScriptExecutionFixture fixture;
	CHECK_FALSE(ScriptTestAbort::is_available());
	CHECK_FALSE(ScriptTestAbort::abort_current("ignored"));

	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("probe_abort_available"), 0.0);
	CHECK(result->get_return_value());
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Guarded runtime error returns structured failure") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("runtime_error"), 1.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_RUNTIME_ERROR);
	CHECK_FALSE(result->get_message().is_empty());
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] abort_on_fatal converts push_error into abort") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> aborted = run_guarded_call(fixture.suite, SNAME("push_error_only"), 1.0, true);
	CHECK_EQ(aborted->get_status(), ScriptTestExecutionResult::STATUS_ABORTED);

	const Ref<ScriptTestExecutionResult> completed = run_guarded_call(fixture.suite, SNAME("push_error_only"), 1.0, false);
	CHECK_EQ(completed->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Nested guards time out independently") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("nested_outer"), 1.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Zero timeout never expires on a long await") {
	ScriptExecutionFixture fixture;
	const Ref<ScriptTestExecutionResult> result = run_guarded_call(fixture.suite, SNAME("long_await_no_timeout"), 0.0);
	CHECK_EQ(result->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
	CHECK_EQ(int(result->get_return_value()), 99);
}

TEST_CASE("[Modules][FoundryScript][ScriptTestExecution][SceneTree] Awaiting a ScriptFunctionState subclass resolves with completed payload") {
	ScriptExecutionFixture fixture;

	Ref<ScriptTestExecution> execution;
	execution.instantiate();
	execution->set_timeout_seconds(1.0);

	ScriptTestExecutionCapture capture;
	const Variant pending = execution->callv(fixture.suite, SNAME("async_frames"), Array());
	ScriptTestExecutionPendingState *pending_state = Object::cast_to<ScriptTestExecutionPendingState>(pending);
	REQUIRE(pending_state != nullptr);
	pending_state->connect(SNAME("completed"), callable_mp(&capture, &ScriptTestExecutionCapture::on_completed), Object::CONNECT_ONE_SHOT);

	SceneTree *tree = SceneTree::get_singleton();
	for (int frame = 0; frame < 240 && !capture.done; frame++) {
		tree->process(1.0 / 60.0);
	}

	REQUIRE(capture.done);
	REQUIRE(capture.result.is_valid());
	CHECK_EQ(capture.result->get_status(), ScriptTestExecutionResult::STATUS_COMPLETED);
	CHECK_EQ(int(capture.result->get_return_value()), 7);
}

} // namespace FSTests
