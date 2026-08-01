/**************************************************************************/
/*  editor_tooling_host.h                                                 */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"

// Coordinates the two listeners owned by a `foundry tooling serve` process.
//
// The language server and the debug adapter are independent editor plugins that
// bind at different points of editor startup, but the tooling host contract is
// atomic: either both listeners bind and one readiness record is emitted, or the
// already-bound listener is closed, one error record is emitted, and the process
// exits nonzero. This class is the only place that knows about both.
class EditorToolingHost {
public:
	enum Service {
		SERVICE_LSP,
		SERVICE_DAP,
		SERVICE_MAX,
	};

	// Closes an already-bound listener when its sibling fails to bind. A plain
	// function pointer keeps the owning plugins free of a second base class, which
	// would break the single-Object-base assumption of the class bindings.
	typedef void (*CloseListenerCallback)(void *p_userdata);

	// Called from the command-line setup with the already-validated ports, where 0
	// requests an ephemeral port.
	static void configure(int p_lsp_port, int p_dap_port);
	static bool is_enabled();

	static int get_requested_port(Service p_service);
	static String get_service_name(Service p_service);

	static void register_listener(Service p_service, CloseListenerCallback p_close, void *p_userdata);
	static void unregister_listener(Service p_service, void *p_userdata);

	static void report_bound(Service p_service, int p_bound_port);
	static void report_bind_failure(Service p_service, int p_requested_port, Error p_error, const String &p_message);

	// Orderly shutdown on `SIGINT`/`SIGTERM`. The installed handler only latches the
	// request, because terminating child processes and quitting the scene tree are
	// not async-signal-safe; `process_pending_shutdown()` performs the actual work
	// from the main loop. Debuggees that were merely attached to are left running,
	// since only processes this host launched are tracked.
	static void install_shutdown_handlers();
	static void request_shutdown();
	static bool is_shutdown_requested();
	static void process_pending_shutdown();

	// Builds the readiness payload without emitting it, so tests can assert on the
	// record shape without starting a real editor process.
	static String build_readiness_record(const String &p_project, int p_process_id, int p_lsp_port, int p_dap_port);
	static String build_failure_record(Service p_service, int p_requested_port, Error p_error, const String &p_message);
};
