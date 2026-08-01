/**************************************************************************/
/*  editor_tooling_host.cpp                                               */
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

#include "editor_tooling_host.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/run/editor_run.h"
#include "scene/main/scene_tree.h"

#include <csignal>
#include <cstdio>

namespace {

struct ToolingHostState {
	bool enabled = false;
	bool failed = false;
	bool readiness_emitted = false;
	int requested_port[EditorToolingHost::SERVICE_MAX] = { -1, -1 };
	int bound_port[EditorToolingHost::SERVICE_MAX] = { -1, -1 };
	EditorToolingHost::CloseListenerCallback close_listener[EditorToolingHost::SERVICE_MAX] = { nullptr, nullptr };
	void *listener_userdata[EditorToolingHost::SERVICE_MAX] = { nullptr, nullptr };
};

ToolingHostState &state() {
	static ToolingHostState singleton_state;
	return singleton_state;
}

// Kept outside `ToolingHostState` on purpose: the signal handler must not touch a
// function-local static, whose lazy initialization guard is not async-signal-safe.
volatile sig_atomic_t shutdown_requested = 0;

extern "C" void tooling_host_signal_handler(int) {
	shutdown_requested = 1;
}

// The readiness and failure records are the host's machine-readable contract with
// its supervisor, so they must survive `--quiet` and a project that sets
// `application/run/disable_stdout`, which suppress the ordinary logging path.
void emit_record(FILE *p_stream, const char *p_marker, const String &p_payload) {
	const CharString line = (String(p_marker) + " " + p_payload + "\n").utf8();
	fwrite(line.get_data(), 1, line.length(), p_stream);
	fflush(p_stream);
}

} // namespace

void EditorToolingHost::configure(int p_lsp_port, int p_dap_port) {
	ToolingHostState &tooling_state = state();
	tooling_state.enabled = true;
	tooling_state.requested_port[SERVICE_LSP] = p_lsp_port;
	tooling_state.requested_port[SERVICE_DAP] = p_dap_port;
	install_shutdown_handlers();
}

void EditorToolingHost::install_shutdown_handlers() {
	signal(SIGINT, tooling_host_signal_handler);
	signal(SIGTERM, tooling_host_signal_handler);
}

void EditorToolingHost::request_shutdown() {
	shutdown_requested = 1;
}

bool EditorToolingHost::is_shutdown_requested() {
	return shutdown_requested != 0;
}

void EditorToolingHost::process_pending_shutdown() {
	if (shutdown_requested == 0) {
		return;
	}
	shutdown_requested = 0;

	EditorRun::stop_all_launched_children();

	if (state().enabled && SceneTree::get_singleton() != nullptr) {
		SceneTree::get_singleton()->quit(EXIT_SUCCESS);
	}
}

bool EditorToolingHost::is_enabled() {
	return state().enabled;
}

int EditorToolingHost::get_requested_port(Service p_service) {
	ERR_FAIL_INDEX_V(p_service, SERVICE_MAX, -1);
	return state().requested_port[p_service];
}

String EditorToolingHost::get_service_name(Service p_service) {
	switch (p_service) {
		case SERVICE_LSP:
			return "lsp";
		case SERVICE_DAP:
			return "dap";
		default:
			return "unknown";
	}
}

void EditorToolingHost::register_listener(Service p_service, CloseListenerCallback p_close, void *p_userdata) {
	ERR_FAIL_INDEX(p_service, SERVICE_MAX);
	state().close_listener[p_service] = p_close;
	state().listener_userdata[p_service] = p_userdata;
}

void EditorToolingHost::unregister_listener(Service p_service, void *p_userdata) {
	ERR_FAIL_INDEX(p_service, SERVICE_MAX);
	ToolingHostState &tooling_state = state();
	if (tooling_state.listener_userdata[p_service] != p_userdata) {
		return;
	}
	tooling_state.close_listener[p_service] = nullptr;
	tooling_state.listener_userdata[p_service] = nullptr;
}

String EditorToolingHost::build_readiness_record(const String &p_project, int p_process_id, int p_lsp_port, int p_dap_port) {
	Array services;
	services.push_back(get_service_name(SERVICE_LSP));
	services.push_back(get_service_name(SERVICE_DAP));

	Dictionary record;
	record["project"] = p_project;
	record["pid"] = p_process_id;
	record["local_only"] = true;
	record["services"] = services;
	record["lsp_port"] = p_lsp_port;
	record["dap_port"] = p_dap_port;
	return JSON::stringify(record, "", false);
}

String EditorToolingHost::build_failure_record(Service p_service, int p_requested_port, Error p_error, const String &p_message) {
	Dictionary record;
	record["error"] = "bind_failed";
	record["service"] = get_service_name(p_service);
	record["requested_port"] = p_requested_port;
	record["error_code"] = (int)p_error;
	record["message"] = p_message;
	return JSON::stringify(record, "", false);
}

void EditorToolingHost::report_bound(Service p_service, int p_bound_port) {
	ERR_FAIL_INDEX(p_service, SERVICE_MAX);
	ToolingHostState &tooling_state = state();
	if (!tooling_state.enabled || tooling_state.failed) {
		return;
	}
	tooling_state.bound_port[p_service] = p_bound_port;

	for (int i = 0; i < SERVICE_MAX; i++) {
		if (tooling_state.bound_port[i] < 0) {
			return;
		}
	}
	if (tooling_state.readiness_emitted) {
		return;
	}
	tooling_state.readiness_emitted = true;

	const String project = ProjectSettings::get_singleton() != nullptr
			? ProjectSettings::get_singleton()->get_resource_path()
			: String();
	const String record = build_readiness_record(
			project,
			(int)OS::get_singleton()->get_process_id(),
			tooling_state.bound_port[SERVICE_LSP],
			tooling_state.bound_port[SERVICE_DAP]);
	emit_record(stdout, "FOUNDRY_TOOLING", record);
}

void EditorToolingHost::report_bind_failure(Service p_service, int p_requested_port, Error p_error, const String &p_message) {
	ERR_FAIL_INDEX(p_service, SERVICE_MAX);
	ToolingHostState &tooling_state = state();
	if (!tooling_state.enabled || tooling_state.failed) {
		return;
	}
	// Latching before closing siblings keeps a cascade of stop() calls from emitting
	// a second failure record.
	tooling_state.failed = true;

	for (int i = 0; i < SERVICE_MAX; i++) {
		if (i == (int)p_service || tooling_state.close_listener[i] == nullptr) {
			continue;
		}
		tooling_state.close_listener[i](tooling_state.listener_userdata[i]);
		tooling_state.bound_port[i] = -1;
	}

	const CharString detail = (p_message + "\n").utf8();
	fwrite(detail.get_data(), 1, detail.length(), stderr);
	fflush(stderr);
	emit_record(stdout, "FOUNDRY_TOOLING_ERROR", build_failure_record(p_service, p_requested_port, p_error, p_message));

	if (SceneTree::get_singleton() != nullptr) {
		SceneTree::get_singleton()->quit(EXIT_FAILURE);
	}
}
