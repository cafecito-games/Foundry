/**************************************************************************/
/*  script_diagnostic_capture_scope.h                                     */
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

#include "core/object/ref_counted.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/object/script_function_state.h"
#include "core/variant/callable.h"

class ScriptDiagnosticCaptureScope;

class ScriptDiagnosticCaptureResult : public RefCounted {
	FOUNDRY_CLASS(ScriptDiagnosticCaptureResult, RefCounted);

private:
	Variant return_value;
	Ref<ScriptDiagnosticCapture> capture;

protected:
	static void _bind_methods();

public:
	void configure(const Variant &p_return_value, const Ref<ScriptDiagnosticCapture> &p_capture);

	Variant get_return_value() const { return return_value; }
	Ref<ScriptDiagnosticCapture> get_capture() const { return capture; }
};

class ScriptDiagnosticCapturePendingState : public ScriptFunctionState {
	FOUNDRY_CLASS(ScriptDiagnosticCapturePendingState, ScriptFunctionState);

	friend class ScriptDiagnosticCaptureScope;

	Ref<ScriptDiagnosticCaptureScope> scope;
	Ref<ScriptFunctionState> coroutine_state;
	bool finalized = false;

	void _finalize(const Variant &p_return_value);
	void _on_coroutine_completed(const Variant &p_result);

protected:
	static void _bind_methods();

public:
	~ScriptDiagnosticCapturePendingState();
};

class ScriptDiagnosticCaptureScope : public RefCounted {
	FOUNDRY_CLASS(ScriptDiagnosticCaptureScope, RefCounted);

	friend class ScriptDiagnosticCapturePendingState;

private:
	Ref<ScriptDiagnosticCapture> diagnostic_capture;
	bool stopped = false;

	static Ref<ScriptDiagnosticCaptureResult> _make_result(const Variant &p_return_value, const Ref<ScriptDiagnosticCapture> &p_capture);

protected:
	static void _bind_methods();

public:
	static Ref<ScriptDiagnosticCaptureScope> start(bool p_quiet = false);
	void stop();
	bool is_active() const;
	Array get_events() const;
	Ref<ScriptDiagnosticCapture> get_capture() const { return diagnostic_capture; }

	static Ref<ScriptDiagnosticCaptureResult> capture(const Callable &p_callable, bool p_quiet = false);
	static Variant capture_async(const Callable &p_callable, bool p_quiet = false);

	~ScriptDiagnosticCaptureScope();
};
