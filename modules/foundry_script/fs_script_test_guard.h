/**************************************************************************/
/*  fs_script_test_guard.h                                                */
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
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/templates/self_list.h"

class ScriptTestExecutionPendingState;

// Per-thread guard stack and frame polling for ScriptTestExecution.
class FSScriptTestGuard {
public:
	enum UnwindReason {
		UNWIND_NONE,
		UNWIND_TIMED_OUT,
		UNWIND_ABORTED,
	};

	struct GuardRecord {
		uint64_t start_usec = 0;
		uint64_t deadline_usec = 0;
		double timeout_seconds = 0.0;
		bool abort_requested = false;
		String abort_message;
		bool abort_on_fatal = false;
		UnwindReason unwind_reason = UNWIND_NONE;
		String unwind_message;
		bool runtime_error_occurred = false;
		String runtime_error_message;
		bool suppress_error_report = false;
		ScriptTestExecutionPendingState *pending_state = nullptr;
		ErrorHandlerList error_handler;
		bool error_handler_registered = false;

		bool has_timeout() const { return deadline_usec != 0; }
		bool is_expired(uint64_t p_now_usec) const { return has_timeout() && p_now_usec >= deadline_usec; }
	};

private:
	static thread_local LocalVector<GuardRecord *> guard_stack;
	static thread_local bool any_guard_active;

	static SelfList<ScriptTestExecutionPendingState>::List pending_states;

	static void _diagnostic_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type);

public:
	static GuardRecord *push(GuardRecord *p_record);
	static void pop(GuardRecord *p_record);

	static bool is_active();
	static GuardRecord *get_innermost();

	static bool checkpoint(String &r_message, UnwindReason &r_reason);
	static void mark_unwind(UnwindReason p_reason, const String &p_message);
	static bool request_abort(const String &p_message);

	static bool notify_script_error(const String &p_message, bool &r_suppress_report);

	static void register_pending(ScriptTestExecutionPendingState *p_state);
	static void unregister_pending(ScriptTestExecutionPendingState *p_state);

	static void register_fatal_handler(GuardRecord *p_record);
	static void unregister_fatal_handler(GuardRecord *p_record);

	static void poll_timeouts();
};
