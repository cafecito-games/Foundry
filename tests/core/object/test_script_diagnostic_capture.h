/**************************************************************************/
/*  test_script_diagnostic_capture.h                                      */
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

#include "core/object/script_diagnostic_capture.h"

#include "tests/test_macros.h"

namespace TestScriptDiagnosticCapture {

TEST_CASE("[ScriptDiagnosticCapture] Captures errors and warnings while active") {
	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	CHECK_FALSE(ScriptDiagnosticCapture::has_active_capture());
	capture->start();
	CHECK(capture->is_active());
	CHECK(ScriptDiagnosticCapture::has_active_capture());

	ERR_PRINT("captured error");
	WARN_PRINT("captured warning");

	capture->stop();
	CHECK_FALSE(capture->is_active());
	CHECK_FALSE(ScriptDiagnosticCapture::has_active_capture());

	CHECK(capture->get_event_count() == 2);
	CHECK(capture->has_error("captured error"));
	CHECK(capture->has_warning("captured warning"));

	Dictionary first_event = capture->get_event(0);
	CHECK(int(first_event["severity"]) == ScriptDiagnosticCapture::SEVERITY_ERROR);
	CHECK(String(first_event["message"]) == "captured error");

	Dictionary second_event = capture->get_event(1);
	CHECK(int(second_event["severity"]) == ScriptDiagnosticCapture::SEVERITY_WARNING);
	CHECK(String(second_event["message"]) == "captured warning");
}

TEST_CASE("[ScriptDiagnosticCapture] Captures fatal diagnostics separately") {
	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	capture->start();
	_err_print_error(FUNCTION_STR, __FILE__, __LINE__, "captured fatal", false, ERR_HANDLER_FATAL);
	capture->stop();

	CHECK(capture->get_event_count() == 1);
	CHECK(capture->has_fatal("captured fatal"));
	CHECK_FALSE(capture->has_error("captured fatal"));
}

} // namespace TestScriptDiagnosticCapture
