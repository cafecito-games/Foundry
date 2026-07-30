/**************************************************************************/
/*  script_diagnostic_capture.h                                           */
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

#pragma once

#include "core/error/error_macros.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class ScriptDiagnosticCapture : public RefCounted {
	FOUNDRY_CLASS(ScriptDiagnosticCapture, RefCounted);

public:
	enum Severity {
		SEVERITY_ERROR,
		SEVERITY_WARNING,
		SEVERITY_SCRIPT_ERROR,
		SEVERITY_SHADER_ERROR,
		SEVERITY_FATAL,
	};

private:
	struct Event {
		Severity severity = SEVERITY_ERROR;
		String message;
		String code;
		String rationale;
		String function;
		String file;
		int line = 0;
		bool editor_notify = false;
	};

	ErrorHandlerList error_handler;
	LocalVector<Event> events;
	bool active = false;
	bool quiet = false;

	static Severity _severity_from_handler_type(ErrorHandlerType p_type);
	static void _error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type);

	bool _has_diagnostic(Severity p_severity, const String &p_message) const;
	bool _has_diagnostic_containing(Severity p_severity, const String &p_text) const;
	static String _severity_name(Severity p_severity);
	Dictionary _event_to_dictionary(const Event &p_event) const;

protected:
	static void _bind_methods();

public:
	static bool has_active_capture();
	static bool is_supported();

	void start(bool p_quiet = false);
	void stop();
	bool is_active() const { return active; }
	bool is_quiet() const { return quiet; }

	void clear();
	int get_event_count() const { return events.size(); }
	Dictionary get_event(int p_index) const;
	Array get_events() const;

	bool has_diagnostic(int p_severity, const String &p_message = String()) const;
	bool has_diagnostic_containing(int p_severity, const String &p_text = String()) const;
	bool has_error(const String &p_message = String()) const;
	bool has_warning(const String &p_message = String()) const;
	bool has_fatal(const String &p_message = String()) const;
	bool has_error_containing(const String &p_text = String()) const;
	bool has_warning_containing(const String &p_text = String()) const;
	bool has_fatal_containing(const String &p_text = String()) const;

	~ScriptDiagnosticCapture();
};

VARIANT_ENUM_CAST(ScriptDiagnosticCapture::Severity);
