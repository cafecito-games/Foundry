/**************************************************************************/
/*  test_editor_tooling_host.h                                            */
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

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/os/os.h"
#include "editor/debugger/debug_adapter/debug_adapter_protocol.h"
#include "editor/debugger/debug_adapter/debug_adapter_types.h"
#include "editor/tooling/editor_tooling_host.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"

namespace TestEditorToolingHost {

static Dictionary parse_marker_record(const String &p_output, const String &p_marker) {
	const int marker_index = p_output.find(p_marker);
	if (marker_index < 0) {
		return Dictionary();
	}
	const int payload_index = marker_index + p_marker.length();
	int end_index = p_output.find("\n", payload_index);
	if (end_index < 0) {
		end_index = p_output.length();
	}
	const String payload = p_output.substr(payload_index, end_index - payload_index).strip_edges();
	const Variant parsed = JSON::parse_string(payload);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return parsed;
}

// Reserves a loopback port by listening on it, so the tooling host started next
// is guaranteed to hit a bind conflict on that exact port.
static Ref<TCPServer> occupy_local_port(int &r_port) {
	Ref<TCPServer> blocker;
	blocker.instantiate();
	if (blocker->listen(0, IPAddress("127.0.0.1")) != OK) {
		return Ref<TCPServer>();
	}
	r_port = blocker->get_local_port();
	return blocker;
}

static bool can_connect_to_local_port(int p_port) {
	Ref<StreamPeerTCP> peer;
	peer.instantiate();
	if (peer->connect_to_host(IPAddress("127.0.0.1"), p_port) != OK) {
		return false;
	}
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 5000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		peer->poll();
		const StreamPeerTCP::Status status = peer->get_status();
		if (status == StreamPeerTCP::STATUS_CONNECTED) {
			peer->disconnect_from_host();
			return true;
		}
		if (status == StreamPeerTCP::STATUS_ERROR || status == StreamPeerTCP::STATUS_NONE) {
			return false;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	return false;
}

struct HostProcess {
	OS::ProcessID pid = 0;
	Ref<FileAccess> stdout_pipe;
	Ref<FileAccess> stderr_pipe;
	String output;

	bool is_valid() const { return pid != 0; }
};

static HostProcess launch_tooling_host(const List<String> &p_arguments) {
	HostProcess process;
	Dictionary environment;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false, String(), environment, false);
	if (pipe_info.is_empty()) {
		return process;
	}
	process.stdout_pipe = pipe_info["stdio"];
	process.stderr_pipe = pipe_info["stderr"];
	process.pid = pipe_info["pid"];
	return process;
}

static void drain_pipe(const Ref<FileAccess> &p_pipe, String &r_output) {
	if (p_pipe.is_null() || !p_pipe->is_open()) {
		return;
	}
	const uint64_t available = p_pipe->get_length();
	if (available == 0) {
		return;
	}
	Vector<uint8_t> chunk;
	chunk.resize(available);
	const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
	if (read > 0) {
		r_output += String::utf8((const char *)chunk.ptr(), read);
	}
}

static bool wait_for_marker(HostProcess &r_process, const String &p_marker, uint64_t p_timeout_msec) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		drain_pipe(r_process.stdout_pipe, r_process.output);
		drain_pipe(r_process.stderr_pipe, r_process.output);
		if (r_process.output.contains(p_marker)) {
			return true;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	return r_process.output.contains(p_marker);
}

static void shutdown_host(HostProcess &r_process) {
	if (r_process.stdout_pipe.is_valid()) {
		r_process.stdout_pipe->close();
	}
	if (r_process.stderr_pipe.is_valid()) {
		r_process.stderr_pipe->close();
	}
	if (r_process.pid != 0 && OS::get_singleton()->is_process_running(r_process.pid)) {
		OS::get_singleton()->kill(r_process.pid);
	}
}

TEST_CASE("[Editor][ToolingHost] Readiness record names both services and the bound ports") {
	const String record = EditorToolingHost::build_readiness_record("/canonical/project", 4321, 49152, 49153);
	const Variant parsed = JSON::parse_string(record);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary payload = parsed;
	CHECK_EQ(String(payload["project"]), "/canonical/project");
	CHECK_EQ(int(payload["pid"]), 4321);
	CHECK(bool(payload["local_only"]));
	CHECK_EQ(int(payload["lsp_port"]), 49152);
	CHECK_EQ(int(payload["dap_port"]), 49153);
	const Array services = payload["services"];
	REQUIRE_EQ(services.size(), 2);
	CHECK_EQ(String(services[0]), "lsp");
	CHECK_EQ(String(services[1]), "dap");
}

TEST_CASE("[Editor][ToolingHost] Failure record identifies the failing service and request") {
	const String record = EditorToolingHost::build_failure_record(
			EditorToolingHost::SERVICE_DAP, 6006, ERR_ALREADY_IN_USE, "port busy");
	const Variant parsed = JSON::parse_string(record);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary payload = parsed;
	CHECK_EQ(String(payload["error"]), "bind_failed");
	CHECK_EQ(String(payload["service"]), "dap");
	CHECK_EQ(int(payload["requested_port"]), 6006);
	CHECK_EQ(int(payload["error_code"]), (int)ERR_ALREADY_IN_USE);
	CHECK_EQ(String(payload["message"]), "port busy");
}

TEST_CASE("[Editor][ToolingHost] Optional DAP source checksums stay empty when omitted") {
	Dictionary source_json;
	source_json["name"] = "player.fs";
	source_json["path"] = "res://scripts/player.fs";

	DAP::Source source;
	source.from_json(source_json);
	CHECK_EQ(source.name, "player.fs");
	CHECK_EQ(source.path, "res://scripts/player.fs");
	CHECK(source.to_json().has("checksums"));
	CHECK_EQ(Array(source.to_json()["checksums"]).size(), 0);
}

TEST_CASE("[Editor][ToolingHost] DAP breakpoints register against project-local script paths") {
	// The debuggee matches breakpoints against the resource path stored on the
	// compiled script, so an absolute client path has to be localized first.
	CHECK_EQ(DebugAdapterProtocol::breakpoint_script_path("/home/dev/game/scripts/player.fs", "/home/dev/game"),
			"res://scripts/player.fs");
	CHECK_EQ(DebugAdapterProtocol::breakpoint_script_path("res://scripts/player.fs", "/home/dev/game"),
			"res://scripts/player.fs");
	// A source outside the project cannot resolve to a project script, so it is left alone.
	CHECK_EQ(DebugAdapterProtocol::breakpoint_script_path("/elsewhere/player.fs", "/home/dev/game"),
			"/elsewhere/player.fs");
	// A prefix match that is not a path boundary is not inside the project.
	CHECK_EQ(DebugAdapterProtocol::breakpoint_script_path("/home/dev/game_backup/player.fs", "/home/dev/game"),
			"/home/dev/game_backup/player.fs");
	CHECK_EQ(DebugAdapterProtocol::breakpoint_script_path("C:\\dev\\game\\scripts\\player.fs", "C:/dev/game"),
			"res://scripts/player.fs");
}

TEST_CASE("[Editor][ToolingHost] Breakpoints on unknown scripts are not reported as verified") {
	CHECK_FALSE(DebugAdapterProtocol::can_verify_breakpoint("res://does_not_exist_9f3c.fs", 12));
	CHECK_FALSE(DebugAdapterProtocol::can_verify_breakpoint("res://does_not_exist_9f3c.fs", 0));
}

TEST_CASE("[Editor][ToolingHost] Ephemeral ports are bound and reported") {
	const String project_path = EditorWorkflowTestFixtures::prepare_basic_scene_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a disposable tooling-host project.");

	List<String> arguments;
	arguments.push_back("tooling");
	arguments.push_back("serve");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--lsp-port");
	arguments.push_back("0");
	arguments.push_back("--dap-port");
	arguments.push_back("0");

	HostProcess host = launch_tooling_host(arguments);
	REQUIRE_MESSAGE(host.is_valid(), "Failed to launch the tooling host.");

	const bool ready = wait_for_marker(host, "FOUNDRY_TOOLING {", 180000);
	INFO("Tooling host output:\n", host.output);
	if (!ready) {
		shutdown_host(host);
		FAIL("The tooling host never emitted a readiness record.");
		return;
	}

	const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING ");
	CHECK(bool(payload["local_only"]));
	const int lsp_port = payload["lsp_port"];
	const int dap_port = payload["dap_port"];
	CHECK(lsp_port > 0);
	CHECK(dap_port > 0);
	CHECK(lsp_port != dap_port);
	CHECK_FALSE(host.output.contains("FOUNDRY_TOOLING_ERROR"));
	CHECK(can_connect_to_local_port(lsp_port));
	CHECK(can_connect_to_local_port(dap_port));

	shutdown_host(host);
}

TEST_CASE("[Editor][ToolingHost] The readiness record survives suppressed logging") {
	const String project_path = EditorWorkflowTestFixtures::prepare_basic_scene_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a disposable tooling-host project.");

	List<String> arguments;
	arguments.push_back("tooling");
	arguments.push_back("serve");
	arguments.push_back("--quiet");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--lsp-port");
	arguments.push_back("0");
	arguments.push_back("--dap-port");
	arguments.push_back("0");

	HostProcess host = launch_tooling_host(arguments);
	REQUIRE_MESSAGE(host.is_valid(), "Failed to launch the tooling host.");

	const bool ready = wait_for_marker(host, "FOUNDRY_TOOLING {", 180000);
	INFO("Tooling host output:\n", host.output);
	shutdown_host(host);
	REQUIRE_MESSAGE(ready, "A quiet tooling host still has to publish its bound ports.");

	const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING ");
	CHECK(int(payload["lsp_port"]) > 0);
	CHECK(int(payload["dap_port"]) > 0);
}

TEST_CASE("[Editor][ToolingHost] An occupied debug adapter port fails the whole host") {
	int occupied_port = 0;
	Ref<TCPServer> blocker = occupy_local_port(occupied_port);
	REQUIRE_MESSAGE(blocker.is_valid(), "Failed to reserve a loopback port.");

	const String project_path = EditorWorkflowTestFixtures::prepare_basic_scene_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a disposable tooling-host project.");

	List<String> arguments;
	arguments.push_back("tooling");
	arguments.push_back("serve");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--lsp-port");
	arguments.push_back("0");
	arguments.push_back("--dap-port");
	arguments.push_back(String::num_int64(occupied_port));

	HostProcess host = launch_tooling_host(arguments);
	REQUIRE_MESSAGE(host.is_valid(), "Failed to launch the tooling host.");

	const bool failed = wait_for_marker(host, "FOUNDRY_TOOLING_ERROR ", 180000);
	INFO("Tooling host output:\n", host.output);
	shutdown_host(host);
	blocker->stop();

	REQUIRE_MESSAGE(failed, "The tooling host did not report the bind failure.");
	const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING_ERROR ");
	CHECK_EQ(String(payload["error"]), "bind_failed");
	CHECK_EQ(String(payload["service"]), "dap");
	CHECK_EQ(int(payload["requested_port"]), occupied_port);
	CHECK_FALSE(host.output.contains("FOUNDRY_TOOLING {"));
}

} // namespace TestEditorToolingHost
