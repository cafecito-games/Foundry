/**************************************************************************/
/*  script_diagnostic_capture_scope.cpp                                   */
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

#include "script_diagnostic_capture_scope.h"

#include "core/object/class_db.h"

void ScriptDiagnosticCaptureResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_return_value"), &ScriptDiagnosticCaptureResult::get_return_value);
	ClassDB::bind_method(D_METHOD("get_capture"), &ScriptDiagnosticCaptureResult::get_capture);

	ADD_PROPERTY(PropertyInfo(Variant::NIL, "return_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_return_value");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "capture", PROPERTY_HINT_RESOURCE_TYPE, "ScriptDiagnosticCapture"), "", "get_capture");
}

void ScriptDiagnosticCaptureResult::configure(const Variant &p_return_value, const Ref<ScriptDiagnosticCapture> &p_capture) {
	return_value = p_return_value;
	capture = p_capture;
}

void ScriptDiagnosticCapturePendingState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_coroutine_completed", "result"), &ScriptDiagnosticCapturePendingState::_on_coroutine_completed);
}

ScriptDiagnosticCapturePendingState::~ScriptDiagnosticCapturePendingState() {
	if (!finalized && scope.is_valid()) {
		scope->stop();
	}
	coroutine_state.unref();
}

void ScriptDiagnosticCapturePendingState::_finalize(const Variant &p_return_value) {
	if (finalized) {
		return;
	}
	finalized = true;

	Ref<ScriptDiagnosticCapture> stopped_capture;
	if (scope.is_valid()) {
		scope->stop();
		stopped_capture = scope->get_capture();
	}

	coroutine_state.unref();

	Ref<ScriptDiagnosticCaptureResult> result = ScriptDiagnosticCaptureScope::_make_result(p_return_value, stopped_capture);
	emit_signal(SNAME("completed"), result);
}

void ScriptDiagnosticCapturePendingState::_on_coroutine_completed(const Variant &p_result) {
	_finalize(p_result);
}

void ScriptDiagnosticCaptureScope::_bind_methods() {
	ClassDB::bind_static_method("ScriptDiagnosticCaptureScope", D_METHOD("start", "quiet"), &ScriptDiagnosticCaptureScope::start, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("stop"), &ScriptDiagnosticCaptureScope::stop);
	ClassDB::bind_method(D_METHOD("is_active"), &ScriptDiagnosticCaptureScope::is_active);
	ClassDB::bind_method(D_METHOD("get_events"), &ScriptDiagnosticCaptureScope::get_events);
	ClassDB::bind_method(D_METHOD("get_capture"), &ScriptDiagnosticCaptureScope::get_capture);

	ClassDB::bind_static_method("ScriptDiagnosticCaptureScope", D_METHOD("capture", "callable", "quiet"), &ScriptDiagnosticCaptureScope::capture, DEFVAL(false));
	ClassDB::bind_static_method("ScriptDiagnosticCaptureScope", D_METHOD("capture_async", "callable", "quiet"), &ScriptDiagnosticCaptureScope::capture_async, DEFVAL(false));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "capture", PROPERTY_HINT_RESOURCE_TYPE, "ScriptDiagnosticCapture"), "", "get_capture");
}

Ref<ScriptDiagnosticCaptureResult> ScriptDiagnosticCaptureScope::_make_result(const Variant &p_return_value, const Ref<ScriptDiagnosticCapture> &p_capture) {
	Ref<ScriptDiagnosticCaptureResult> result;
	result.instantiate();
	result->configure(p_return_value, p_capture);
	return result;
}

Ref<ScriptDiagnosticCaptureScope> ScriptDiagnosticCaptureScope::start(bool p_quiet) {
	Ref<ScriptDiagnosticCaptureScope> scope;
	scope.instantiate();
	scope->capture.instantiate();
	scope->capture->start(p_quiet);
	return scope;
}

void ScriptDiagnosticCaptureScope::stop() {
	if (stopped) {
		return;
	}
	stopped = true;
	if (capture.is_valid() && capture->is_active()) {
		capture->stop();
	}
}

bool ScriptDiagnosticCaptureScope::is_active() const {
	return !stopped && capture.is_valid() && capture->is_active();
}

Array ScriptDiagnosticCaptureScope::get_events() const {
	if (capture.is_valid()) {
		return capture->get_events();
	}
	return Array();
}

Ref<ScriptDiagnosticCaptureResult> ScriptDiagnosticCaptureScope::capture(const Callable &p_callable, bool p_quiet) {
	Ref<ScriptDiagnosticCaptureScope> scope = start(p_quiet);

	Callable::CallError err;
	const Variant return_value = p_callable.callp(nullptr, 0, err, nullptr);
	ERR_FAIL_COND_V(err.error != Callable::CallError::CALL_OK, Ref<ScriptDiagnosticCaptureResult>());

	scope->stop();
	return _make_result(return_value, scope->get_capture());
}

Variant ScriptDiagnosticCaptureScope::capture_async(const Callable &p_callable, bool p_quiet) {
	Ref<ScriptDiagnosticCaptureScope> scope = start(p_quiet);

	Callable::CallError err;
	const Variant ret = p_callable.callp(nullptr, 0, err, nullptr);
	ERR_FAIL_COND_V(err.error != Callable::CallError::CALL_OK, Variant());

	ScriptFunctionState *function_state = Object::cast_to<ScriptFunctionState>(ret);
	if (function_state) {
		Ref<ScriptDiagnosticCapturePendingState> pending = memnew(ScriptDiagnosticCapturePendingState);
		pending->scope = scope;
		pending->coroutine_state = Ref<ScriptFunctionState>(function_state);

		const Callable callback(pending.ptr(), "_on_coroutine_completed");
		function_state->connect(SNAME("completed"), callback, Object::CONNECT_ONE_SHOT);
		return pending;
	}

	scope->stop();
	return _make_result(ret, scope->get_capture());
}

ScriptDiagnosticCaptureScope::~ScriptDiagnosticCaptureScope() {
	stop();
}
