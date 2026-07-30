/**************************************************************************/
/*  fs_script_test_execution.cpp                                          */
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

#include "fs_script_test_execution.h"

#include "fs_function.h"
#include "fs_script_test_guard.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/variant/variant_utility.h"

thread_local LocalVector<FSScriptTestGuard::GuardRecord *> FSScriptTestGuard::guard_stack;
thread_local bool FSScriptTestGuard::any_guard_active = false;
SelfList<ScriptTestExecutionPendingState>::List FSScriptTestGuard::pending_states;

static String _safe_utf8(const char *p_text) {
	return p_text ? String::utf8(p_text) : String();
}

static double _elapsed_seconds(uint64_t p_start_usec) {
	return (OS::get_singleton()->get_ticks_usec() - p_start_usec) / 1000000.0;
}

static void _release_function_state_chain(FSFunctionState *p_state) {
	FSFunctionState::abandon_chain(p_state);
}

FSScriptTestGuard::GuardRecord *FSScriptTestGuard::push(GuardRecord *p_record) {
	ERR_FAIL_NULL_V(p_record, nullptr);
	guard_stack.push_back(p_record);
	any_guard_active = true;
	return p_record;
}

void FSScriptTestGuard::pop(GuardRecord *p_record) {
	ERR_FAIL_NULL(p_record);
	unregister_fatal_handler(p_record);
	for (int i = guard_stack.size() - 1; i >= 0; i--) {
		if (guard_stack[i] == p_record) {
			guard_stack.remove_at(i);
			break;
		}
	}
	any_guard_active = !guard_stack.is_empty();
}

bool FSScriptTestGuard::is_active() {
	return any_guard_active;
}

FSScriptTestGuard::GuardRecord *FSScriptTestGuard::get_innermost() {
	if (guard_stack.is_empty()) {
		return nullptr;
	}
	return guard_stack[guard_stack.size() - 1];
}

bool FSScriptTestGuard::checkpoint(String &r_message, UnwindReason &r_reason) {
	GuardRecord *guard = get_innermost();
	if (!guard) {
		return false;
	}

	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (guard->abort_requested) {
		r_reason = UNWIND_ABORTED;
		r_message = guard->abort_message;
		return true;
	}
	if (guard->is_expired(now)) {
		r_reason = UNWIND_TIMED_OUT;
		r_message = vformat("Timed out after %ss.", rtos(guard->timeout_seconds));
		return true;
	}
	return false;
}

void FSScriptTestGuard::mark_unwind(UnwindReason p_reason, const String &p_message) {
	GuardRecord *guard = get_innermost();
	ERR_FAIL_NULL(guard);
	guard->unwind_reason = p_reason;
	guard->unwind_message = p_message;
	guard->suppress_error_report = true;
}

bool FSScriptTestGuard::request_abort(const String &p_message) {
	GuardRecord *guard = get_innermost();
	if (!guard) {
		return false;
	}
	guard->abort_requested = true;
	guard->abort_message = p_message;
	return true;
}

bool FSScriptTestGuard::notify_script_error(const String &p_message, bool &r_suppress_report) {
	GuardRecord *guard = get_innermost();
	if (!guard) {
		r_suppress_report = false;
		return false;
	}
	if (guard->suppress_error_report) {
		r_suppress_report = true;
		return true;
	}
	guard->runtime_error_occurred = true;
	guard->runtime_error_message = p_message;
	r_suppress_report = false;
	return true;
}

void FSScriptTestGuard::_diagnostic_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
	GuardRecord *guard = static_cast<GuardRecord *>(p_userdata);
	ERR_FAIL_NULL(guard);
	if (p_type != ERR_HANDLER_ERROR && p_type != ERR_HANDLER_FATAL) {
		return;
	}
	String message = _safe_utf8(p_explanation);
	if (message.is_empty()) {
		message = _safe_utf8(p_error);
	}
	guard->abort_requested = true;
	guard->abort_message = message;
}

void FSScriptTestGuard::register_fatal_handler(GuardRecord *p_record) {
	ERR_FAIL_NULL(p_record);
	if (p_record->error_handler_registered) {
		return;
	}
	p_record->error_handler.errfunc = _diagnostic_error_handler;
	p_record->error_handler.userdata = p_record;
	add_error_handler(&p_record->error_handler);
	p_record->error_handler_registered = true;
}

void FSScriptTestGuard::unregister_fatal_handler(GuardRecord *p_record) {
	ERR_FAIL_NULL(p_record);
	if (!p_record->error_handler_registered) {
		return;
	}
	remove_error_handler(&p_record->error_handler);
	p_record->error_handler_registered = false;
}

void FSScriptTestGuard::register_pending(ScriptTestExecutionPendingState *p_state) {
	ERR_FAIL_NULL(p_state);
	pending_states.add(&p_state->pending_list);
}

void FSScriptTestGuard::unregister_pending(ScriptTestExecutionPendingState *p_state) {
	ERR_FAIL_NULL(p_state);
	if (p_state->pending_list.in_list()) {
		pending_states.remove(&p_state->pending_list);
	}
}

void FSScriptTestGuard::poll_timeouts() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	SelfList<ScriptTestExecutionPendingState> *elem = pending_states.first();
	while (elem) {
		ScriptTestExecutionPendingState *state = elem->self();
		SelfList<ScriptTestExecutionPendingState> *next = elem->next();
		if (!state->finalized && state->guard.abort_requested) {
			state->guard.unwind_reason = UNWIND_ABORTED;
			state->guard.unwind_message = state->guard.abort_message;
			state->_finalize_from_guard();
		} else if (!state->finalized && state->guard.is_expired(now)) {
			state->guard.unwind_reason = UNWIND_TIMED_OUT;
			state->guard.unwind_message = vformat("Timed out after %ss.", rtos(state->guard.timeout_seconds));
			state->_finalize_from_guard();
		}
		elem = next;
	}
}

void ScriptTestExecutionResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_status"), &ScriptTestExecutionResult::get_status);
	ClassDB::bind_method(D_METHOD("get_return_value"), &ScriptTestExecutionResult::get_return_value);
	ClassDB::bind_method(D_METHOD("get_message"), &ScriptTestExecutionResult::get_message);
	ClassDB::bind_method(D_METHOD("get_elapsed_seconds"), &ScriptTestExecutionResult::get_elapsed_seconds);
	ClassDB::bind_method(D_METHOD("get_timeout_seconds"), &ScriptTestExecutionResult::get_timeout_seconds);

	BIND_ENUM_CONSTANT(STATUS_COMPLETED);
	BIND_ENUM_CONSTANT(STATUS_TIMED_OUT);
	BIND_ENUM_CONSTANT(STATUS_ABORTED);
	BIND_ENUM_CONSTANT(STATUS_RUNTIME_ERROR);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "status", PROPERTY_HINT_ENUM, "Completed,Timed Out,Aborted,Runtime Error"), "", "get_status");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "return_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_return_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "message"), "", "get_message");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "elapsed_seconds"), "", "get_elapsed_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "timeout_seconds"), "", "get_timeout_seconds");
}

void ScriptTestExecutionResult::configure(Status p_status, const Variant &p_return_value, const String &p_message, double p_elapsed_seconds, double p_timeout_seconds) {
	status = p_status;
	return_value = p_return_value;
	message = p_message;
	elapsed_seconds = p_elapsed_seconds;
	timeout_seconds = p_timeout_seconds;
}

void ScriptTestExecutionPendingState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_coroutine_completed", "result"), &ScriptTestExecutionPendingState::_on_coroutine_completed);
}

ScriptTestExecutionPendingState::ScriptTestExecutionPendingState() :
		pending_list(this) {
}

ScriptTestExecutionPendingState::~ScriptTestExecutionPendingState() {
	FSScriptTestGuard::unregister_pending(this);
}

void ScriptTestExecutionPendingState::_release_coroutine_chain() {
	if (coroutine_state.is_valid()) {
		_release_function_state_chain(coroutine_state.ptr());
		coroutine_state.unref();
	}
}

void ScriptTestExecutionPendingState::_finalize_from_guard() {
	if (finalized) {
		return;
	}
	finalized = true;

	Ref<ScriptTestExecutionPendingState> keep_alive(this);

	Ref<ScriptTestExecutionResult> result;
	result.instantiate();

	const double elapsed = _elapsed_seconds(start_usec);
	ScriptTestExecutionResult::Status status = ScriptTestExecutionResult::STATUS_COMPLETED;
	String message;

	if (guard.unwind_reason == FSScriptTestGuard::UNWIND_TIMED_OUT) {
		status = ScriptTestExecutionResult::STATUS_TIMED_OUT;
		message = guard.unwind_message;
	} else if (guard.unwind_reason == FSScriptTestGuard::UNWIND_ABORTED || guard.abort_requested) {
		status = ScriptTestExecutionResult::STATUS_ABORTED;
		message = guard.abort_requested ? guard.abort_message : guard.unwind_message;
	} else if (guard.runtime_error_occurred) {
		status = ScriptTestExecutionResult::STATUS_RUNTIME_ERROR;
		message = guard.runtime_error_message;
	} else {
		status = ScriptTestExecutionResult::STATUS_TIMED_OUT;
		message = vformat("Timed out after %ss.", rtos(guard.timeout_seconds));
	}

	_release_coroutine_chain();
	FSScriptTestGuard::pop(&guard);
	FSScriptTestGuard::unregister_pending(this);

	result->configure(status, Variant(), message, elapsed, guard.timeout_seconds);

	if (execution.is_valid()) {
		execution->_pending_finalized(this);
	}

	emit_signal(SNAME("completed"), result);
}

void ScriptTestExecutionPendingState::_on_coroutine_completed(const Variant &p_result) {
	if (finalized) {
		return;
	}
	finalized = true;

	Ref<ScriptTestExecutionPendingState> keep_alive(this);

	Ref<ScriptTestExecutionResult> result;
	result.instantiate();

	const double elapsed = _elapsed_seconds(start_usec);
	ScriptTestExecutionResult::Status status = ScriptTestExecutionResult::STATUS_COMPLETED;
	Variant return_value = p_result;
	String message;

	if (guard.unwind_reason == FSScriptTestGuard::UNWIND_TIMED_OUT) {
		status = ScriptTestExecutionResult::STATUS_TIMED_OUT;
		message = guard.unwind_message;
		return_value = Variant();
	} else if (guard.unwind_reason == FSScriptTestGuard::UNWIND_ABORTED || guard.abort_requested) {
		status = ScriptTestExecutionResult::STATUS_ABORTED;
		message = guard.abort_requested ? guard.abort_message : guard.unwind_message;
		return_value = Variant();
	} else if (guard.runtime_error_occurred) {
		status = ScriptTestExecutionResult::STATUS_RUNTIME_ERROR;
		message = guard.runtime_error_message;
		return_value = Variant();
	}

	coroutine_state.unref();
	FSScriptTestGuard::pop(&guard);
	FSScriptTestGuard::unregister_pending(this);

	result->configure(status, return_value, message, elapsed, guard.timeout_seconds);

	if (execution.is_valid()) {
		execution->_pending_finalized(this);
	}

	emit_signal(SNAME("completed"), result);
}

void ScriptTestExecution::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_timeout_seconds", "timeout_seconds"), &ScriptTestExecution::set_timeout_seconds);
	ClassDB::bind_method(D_METHOD("get_timeout_seconds"), &ScriptTestExecution::get_timeout_seconds);
	ClassDB::bind_method(D_METHOD("set_abort_on_fatal", "abort_on_fatal"), &ScriptTestExecution::set_abort_on_fatal);
	ClassDB::bind_method(D_METHOD("get_abort_on_fatal"), &ScriptTestExecution::get_abort_on_fatal);
	ClassDB::bind_method(D_METHOD("guard_callv", "object", "method", "args"), &ScriptTestExecution::guard_callv);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "timeout_seconds"), "set_timeout_seconds", "get_timeout_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "abort_on_fatal"), "set_abort_on_fatal", "get_abort_on_fatal");
}

Ref<ScriptTestExecutionResult> ScriptTestExecution::_make_result(ScriptTestExecutionResult::Status p_status, const Variant &p_return_value, const String &p_message, uint64_t p_start_usec) const {
	Ref<ScriptTestExecutionResult> result;
	result.instantiate();
	result->configure(p_status, p_return_value, p_message, _elapsed_seconds(p_start_usec), timeout_seconds);
	return result;
}

void ScriptTestExecution::_pending_finalized(ScriptTestExecutionPendingState *p_pending) {
	if (pending.ptr() == p_pending) {
		pending.unref();
	}
}

Variant ScriptTestExecution::guard_callv(Object *p_object, const StringName &p_method, const Array &p_args) {
	if (pending.is_valid() && !pending->is_finalized()) {
		return _make_result(ScriptTestExecutionResult::STATUS_RUNTIME_ERROR, Variant(),
				"ScriptTestExecution.guard_callv() called while a previous call is still pending.", OS::get_singleton()->get_ticks_usec());
	}

	ERR_FAIL_NULL_V(p_object, _make_result(ScriptTestExecutionResult::STATUS_RUNTIME_ERROR, Variant(), "Invalid object.", OS::get_singleton()->get_ticks_usec()));

	const uint64_t start_usec = OS::get_singleton()->get_ticks_usec();
	FSScriptTestGuard::GuardRecord guard;
	guard.start_usec = start_usec;
	guard.timeout_seconds = timeout_seconds;
	guard.abort_on_fatal = abort_on_fatal;
	if (timeout_seconds > 0.0) {
		guard.deadline_usec = start_usec + (uint64_t)(timeout_seconds * 1000000.0);
	}
	FSScriptTestGuard::push(&guard);
	if (abort_on_fatal) {
		FSScriptTestGuard::register_fatal_handler(&guard);
	}

	const Variant ret = p_object->callv(p_method, p_args);

	if (guard.abort_requested) {
		FSScriptTestGuard::pop(&guard);
		return _make_result(ScriptTestExecutionResult::STATUS_ABORTED, Variant(), guard.abort_message, start_usec);
	}

	if (guard.unwind_reason != FSScriptTestGuard::UNWIND_NONE) {
		ScriptTestExecutionResult::Status status = guard.unwind_reason == FSScriptTestGuard::UNWIND_TIMED_OUT
				? ScriptTestExecutionResult::STATUS_TIMED_OUT
				: ScriptTestExecutionResult::STATUS_ABORTED;
		FSScriptTestGuard::pop(&guard);
		return _make_result(status, Variant(), guard.unwind_message, start_usec);
	}

	FSFunctionState *function_state = Object::cast_to<FSFunctionState>(ret);
	if (function_state) {
		Ref<ScriptTestExecutionPendingState> pending_state = memnew(ScriptTestExecutionPendingState);
		pending_state->execution = Ref<ScriptTestExecution>(this);
		pending_state->coroutine_state = Ref<FSFunctionState>(function_state);
		pending_state->guard = guard;
		pending_state->start_usec = start_usec;
		pending_state->guard.pending_state = pending_state.ptr();

		FSScriptTestGuard::pop(&guard);
		FSScriptTestGuard::push(&pending_state->guard);
		if (pending_state->guard.abort_on_fatal) {
			FSScriptTestGuard::register_fatal_handler(&pending_state->guard);
		}

		const Callable callback(pending_state.ptr(), "_on_coroutine_completed");
		function_state->connect(SNAME("completed"), callback, Object::CONNECT_ONE_SHOT);
		FSScriptTestGuard::register_pending(pending_state.ptr());

		pending = pending_state;
		return pending_state;
	}

	if (guard.runtime_error_occurred) {
		FSScriptTestGuard::pop(&guard);
		return _make_result(ScriptTestExecutionResult::STATUS_RUNTIME_ERROR, Variant(), guard.runtime_error_message, start_usec);
	}

	FSScriptTestGuard::pop(&guard);
	return _make_result(ScriptTestExecutionResult::STATUS_COMPLETED, ret, String(), start_usec);
}

void ScriptTestAbort::_bind_methods() {
	ClassDB::bind_static_method("ScriptTestAbort", D_METHOD("is_available"), &ScriptTestAbort::is_available);
	ClassDB::bind_static_method("ScriptTestAbort", D_METHOD("abort_current", "message"), &ScriptTestAbort::abort_current);
}

bool ScriptTestAbort::is_available() {
	return FSScriptTestGuard::is_active();
}

bool ScriptTestAbort::abort_current(const String &p_message) {
	return FSScriptTestGuard::request_abort(p_message);
}
