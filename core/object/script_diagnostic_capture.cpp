/**************************************************************************/
/*  script_diagnostic_capture.cpp                                         */
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

#include "script_diagnostic_capture.h"

#include "core/core_globals.h"

#include <atomic>

namespace {

std::atomic<int> active_capture_count = 0;
std::atomic<int> quiet_capture_count = 0;
bool saved_print_error_enabled = true;

String safe_utf8(const char *p_text) {
	return p_text ? String::utf8(p_text) : String();
}

} // namespace

void ScriptDiagnosticCapture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "quiet"), &ScriptDiagnosticCapture::start, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("stop"), &ScriptDiagnosticCapture::stop);
	ClassDB::bind_method(D_METHOD("is_active"), &ScriptDiagnosticCapture::is_active);
	ClassDB::bind_method(D_METHOD("is_quiet"), &ScriptDiagnosticCapture::is_quiet);

	ClassDB::bind_method(D_METHOD("clear"), &ScriptDiagnosticCapture::clear);
	ClassDB::bind_method(D_METHOD("get_event_count"), &ScriptDiagnosticCapture::get_event_count);
	ClassDB::bind_method(D_METHOD("get_event", "index"), &ScriptDiagnosticCapture::get_event);
	ClassDB::bind_method(D_METHOD("get_events"), &ScriptDiagnosticCapture::get_events);

	ClassDB::bind_method(D_METHOD("has_diagnostic", "severity", "message"), &ScriptDiagnosticCapture::has_diagnostic, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("has_error", "message"), &ScriptDiagnosticCapture::has_error, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("has_warning", "message"), &ScriptDiagnosticCapture::has_warning, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("has_fatal", "message"), &ScriptDiagnosticCapture::has_fatal, DEFVAL(String()));

	BIND_ENUM_CONSTANT(SEVERITY_ERROR);
	BIND_ENUM_CONSTANT(SEVERITY_WARNING);
	BIND_ENUM_CONSTANT(SEVERITY_SCRIPT_ERROR);
	BIND_ENUM_CONSTANT(SEVERITY_SHADER_ERROR);
	BIND_ENUM_CONSTANT(SEVERITY_FATAL);
}

ScriptDiagnosticCapture::Severity ScriptDiagnosticCapture::_severity_from_handler_type(ErrorHandlerType p_type) {
	switch (p_type) {
		case ERR_HANDLER_ERROR:
			return SEVERITY_ERROR;
		case ERR_HANDLER_WARNING:
			return SEVERITY_WARNING;
		case ERR_HANDLER_SCRIPT:
			return SEVERITY_SCRIPT_ERROR;
		case ERR_HANDLER_SHADER:
			return SEVERITY_SHADER_ERROR;
		case ERR_HANDLER_FATAL:
			return SEVERITY_FATAL;
	}
	return SEVERITY_ERROR;
}

void ScriptDiagnosticCapture::_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
	ScriptDiagnosticCapture *self = static_cast<ScriptDiagnosticCapture *>(p_userdata);
	ERR_FAIL_NULL(self);

	Event event;
	event.severity = _severity_from_handler_type(p_type);
	event.code = safe_utf8(p_error);
	event.rationale = safe_utf8(p_explanation);
	event.message = event.rationale.is_empty() ? event.code : event.rationale;
	event.function = safe_utf8(p_function);
	event.file = safe_utf8(p_file);
	event.line = p_line;
	event.editor_notify = p_editor_notify;
	self->events.push_back(event);
}

bool ScriptDiagnosticCapture::_has_diagnostic(Severity p_severity, const String &p_message) const {
	for (const Event &event : events) {
		if (event.severity == p_severity && (p_message.is_empty() || event.message == p_message)) {
			return true;
		}
	}
	return false;
}

Dictionary ScriptDiagnosticCapture::_event_to_dictionary(const Event &p_event) const {
	Dictionary result;
	result["severity"] = p_event.severity;
	result["message"] = p_event.message;
	result["code"] = p_event.code;
	result["rationale"] = p_event.rationale;
	result["function"] = p_event.function;
	result["file"] = p_event.file;
	result["line"] = p_event.line;
	result["editor_notify"] = p_event.editor_notify;
	return result;
}

bool ScriptDiagnosticCapture::has_active_capture() {
	return active_capture_count.load(std::memory_order_relaxed) > 0;
}

void ScriptDiagnosticCapture::start(bool p_quiet) {
	if (active) {
		return;
	}

	error_handler.errfunc = _error_handler;
	error_handler.userdata = this;
	add_error_handler(&error_handler);
	active = true;
	active_capture_count.fetch_add(1, std::memory_order_relaxed);

	quiet = p_quiet;
	if (quiet && quiet_capture_count.fetch_add(1, std::memory_order_relaxed) == 0) {
		saved_print_error_enabled = CoreGlobals::print_error_enabled;
		CoreGlobals::print_error_enabled = false;
	}
}

void ScriptDiagnosticCapture::stop() {
	if (!active) {
		return;
	}

	remove_error_handler(&error_handler);
	active = false;
	active_capture_count.fetch_sub(1, std::memory_order_relaxed);

	if (quiet) {
		quiet = false;
		if (quiet_capture_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
			CoreGlobals::print_error_enabled = saved_print_error_enabled;
		}
	}
}

void ScriptDiagnosticCapture::clear() {
	events.clear();
}

Dictionary ScriptDiagnosticCapture::get_event(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)events.size(), Dictionary());
	return _event_to_dictionary(events[p_index]);
}

Array ScriptDiagnosticCapture::get_events() const {
	Array result;
	result.resize(events.size());
	for (uint32_t i = 0; i < events.size(); i++) {
		result[i] = _event_to_dictionary(events[i]);
	}
	return result;
}

bool ScriptDiagnosticCapture::has_diagnostic(int p_severity, const String &p_message) const {
	ERR_FAIL_COND_V(p_severity < SEVERITY_ERROR || p_severity > SEVERITY_FATAL, false);
	return _has_diagnostic((Severity)p_severity, p_message);
}

bool ScriptDiagnosticCapture::has_error(const String &p_message) const {
	return _has_diagnostic(SEVERITY_ERROR, p_message);
}

bool ScriptDiagnosticCapture::has_warning(const String &p_message) const {
	return _has_diagnostic(SEVERITY_WARNING, p_message);
}

bool ScriptDiagnosticCapture::has_fatal(const String &p_message) const {
	return _has_diagnostic(SEVERITY_FATAL, p_message);
}

ScriptDiagnosticCapture::~ScriptDiagnosticCapture() {
	stop();
}
