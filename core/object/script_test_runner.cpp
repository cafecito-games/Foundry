/**************************************************************************/
/*  script_test_runner.cpp                                                */
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

#include "script_test_runner.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/object/script_function_state.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

ScriptTestRunner::ScriptErrorGuardedCallback ScriptTestRunner::script_error_guarded_callback = nullptr;

void ScriptTestRunner::set_script_error_guarded_callback(ScriptErrorGuardedCallback p_callback) {
	script_error_guarded_callback = p_callback;
}

bool ScriptTestRunner::is_script_error_guarded() {
	return script_error_guarded_callback != nullptr && script_error_guarded_callback();
}

class ScriptTestRunnerInvoker : public Node {
	FOUNDRY_CLASS(ScriptTestRunnerInvoker, Node);

	Ref<ScriptTestRunner> runner;
	PackedStringArray user_args;
	SceneTree *scene_tree = nullptr;
	bool started = false;
	bool had_script_error = false;
	bool error_handler_registered = false;
	ErrorHandlerList error_handler;

	static void _error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		ScriptTestRunnerInvoker *self = static_cast<ScriptTestRunnerInvoker *>(p_userdata);
		if (p_type == ERR_HANDLER_SCRIPT && !ScriptTestRunner::is_script_error_guarded()) {
			self->had_script_error = true;
		}
	}

	void _register_error_handler() {
		if (error_handler_registered) {
			return;
		}
		error_handler.errfunc = _error_handler;
		error_handler.userdata = this;
		add_error_handler(&error_handler);
		error_handler_registered = true;
	}

	void _unregister_error_handler() {
		if (!error_handler_registered) {
			return;
		}
		remove_error_handler(&error_handler);
		error_handler_registered = false;
	}

	void _finish_with_result(const Variant &p_result) {
		ERR_FAIL_NULL(scene_tree);
		_unregister_error_handler();

		int exit_code = EXIT_FAILURE;
		if (!had_script_error && p_result.get_type() == Variant::INT) {
			exit_code = p_result;
		} else if (!had_script_error) {
			ERR_PRINT("Script test runner run() must return an int exit code.");
		}

		scene_tree->quit(exit_code);
		queue_free();
	}

	void _on_async_completed(const Variant &p_result) {
		_finish_with_result(p_result);
	}

	void _start_run() {
		if (started) {
			return;
		}
		started = true;

		ERR_FAIL_NULL(scene_tree);
		ERR_FAIL_COND(runner.is_null());

		_register_error_handler();
		const Variant result = ScriptTestRunner::call_run_script_hook(runner, user_args);
		ScriptFunctionState *function_state_object = Object::cast_to<ScriptFunctionState>(result);
		if (function_state_object != nullptr) {
			Ref<ScriptFunctionState> function_state(function_state_object);
			function_state->connect(SNAME("completed"), callable_mp(this, &ScriptTestRunnerInvoker::_on_async_completed), CONNECT_ONE_SHOT);
			return;
		}

		_finish_with_result(result);
	}

public:
	void setup(SceneTree *p_scene_tree, const Ref<ScriptTestRunner> &p_runner, const PackedStringArray &p_user_args) {
		scene_tree = p_scene_tree;
		runner = p_runner;
		user_args = p_user_args;
	}

	void start_on_process_frame() {
		_start_run();
	}
};

void ScriptTestRunner::_bind_methods() {
	// Script-extensible hook. Native callers must use call_run_script_hook() so FoundryScript
	// overrides dispatch through Object::call().
	ClassDB::bind_method(D_METHOD("run", "args"), &ScriptTestRunner::run);
}

Variant ScriptTestRunner::call_run_script_hook(const Ref<ScriptTestRunner> &p_runner, const PackedStringArray &p_args) {
	ERR_FAIL_COND_V(p_runner.is_null(), Variant());
	return p_runner->call(SNAME("run"), p_args);
}

void ScriptTestRunner::launch_host(SceneTree *p_scene_tree, const Ref<ScriptTestRunner> &p_runner, const PackedStringArray &p_user_args) {
	ERR_FAIL_NULL(p_scene_tree);
	ERR_FAIL_COND(p_runner.is_null());

	ScriptTestRunnerInvoker *invoker = memnew(ScriptTestRunnerInvoker);
	invoker->setup(p_scene_tree, p_runner, p_user_args);
	p_scene_tree->get_root()->add_child(invoker);
	p_scene_tree->connect(SNAME("process_frame"), callable_mp(invoker, &ScriptTestRunnerInvoker::start_on_process_frame), CONNECT_ONE_SHOT);
}

int ScriptTestRunner::run(const PackedStringArray &p_args) {
	ERR_PRINT("ScriptTestRunner.run() must be overridden by the runner script.");
	return EXIT_FAILURE;
}
