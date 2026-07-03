/**************************************************************************/
/*  fs_script_test_execution.h                                            */
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

#include "fs_script_test_guard.h"

#include "core/object/ref_counted.h"
#include "core/object/script_function_state.h"
#include "core/templates/self_list.h"
#include "core/variant/variant.h"

class FSFunctionState;
class ScriptTestExecution;

class ScriptTestExecutionResult : public RefCounted {
	FOUNDRY_CLASS(ScriptTestExecutionResult, RefCounted);

public:
	enum Status {
		STATUS_COMPLETED,
		STATUS_TIMED_OUT,
		STATUS_ABORTED,
		STATUS_RUNTIME_ERROR,
	};

private:
	Status status = STATUS_COMPLETED;
	Variant return_value;
	String message;
	double elapsed_seconds = 0.0;
	double timeout_seconds = 0.0;

protected:
	static void _bind_methods();

public:
	void configure(Status p_status, const Variant &p_return_value, const String &p_message, double p_elapsed_seconds, double p_timeout_seconds);

	Status get_status() const { return status; }
	Variant get_return_value() const { return return_value; }
	String get_message() const { return message; }
	double get_elapsed_seconds() const { return elapsed_seconds; }
	double get_timeout_seconds() const { return timeout_seconds; }
};

class ScriptTestExecutionPendingState : public ScriptFunctionState {
	FOUNDRY_CLASS(ScriptTestExecutionPendingState, ScriptFunctionState);

	friend class ScriptTestExecution;
	friend class FSScriptTestGuard;
	friend class FSFunctionState;

	Ref<ScriptTestExecution> execution;
	Ref<FSFunctionState> coroutine_state;
	FSScriptTestGuard::GuardRecord guard;
	uint64_t start_usec = 0;
	bool finalized = false;
	SelfList<ScriptTestExecutionPendingState> pending_list;

	void _on_coroutine_completed(const Variant &p_result);
	void _finalize_from_guard();
	void _release_coroutine_chain();

protected:
	static void _bind_methods();

public:
	bool is_finalized() const { return finalized; }

	ScriptTestExecutionPendingState();
	~ScriptTestExecutionPendingState();
};

class ScriptTestExecution : public RefCounted {
	FOUNDRY_CLASS(ScriptTestExecution, RefCounted);

	double timeout_seconds = 0.0;
	bool abort_on_fatal = false;
	Ref<ScriptTestExecutionPendingState> pending;

	Ref<ScriptTestExecutionResult> _make_result(ScriptTestExecutionResult::Status p_status, const Variant &p_return_value, const String &p_message, uint64_t p_start_usec) const;

protected:
	static void _bind_methods();

public:
	void set_timeout_seconds(double p_timeout_seconds) { timeout_seconds = MAX(p_timeout_seconds, 0.0); }
	double get_timeout_seconds() const { return timeout_seconds; }

	void set_abort_on_fatal(bool p_abort_on_fatal) { abort_on_fatal = p_abort_on_fatal; }
	bool get_abort_on_fatal() const { return abort_on_fatal; }

	Variant callv(Object *p_object, const StringName &p_method, const Array &p_args);

	void _pending_finalized(ScriptTestExecutionPendingState *p_pending);
};

class ScriptTestAbort : public Object {
	FOUNDRY_CLASS(ScriptTestAbort, Object);

protected:
	static void _bind_methods();

public:
	static bool is_available();
	static bool abort_current(const String &p_message);
};

VARIANT_ENUM_CAST(ScriptTestExecutionResult::Status);
