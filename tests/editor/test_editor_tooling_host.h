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
#include "editor/run/editor_run.h"
#include "editor/tooling/editor_tooling_host.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"

#ifdef UNIX_ENABLED
#include <sys/wait.h>
#include <cerrno>
#include <csignal>
#endif

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

// A minimal debug adapter client: `Content-Length` framed JSON over loopback TCP,
// enough to drive a real session against a running tooling host.
struct DebugAdapterClient {
	Ref<StreamPeerTCP> peer;
	String buffer;
	int next_seq = 1;
	// Every event received since the last `clear_events()`, in arrival order. Waiting
	// for a response never consumes them, so a test can assert on the exact lifecycle
	// sequence a launch produced.
	Vector<Dictionary> events;
	List<Dictionary> responses;
	Vector<Dictionary> received_messages;

	bool connect_to_port(int p_port) {
		peer.instantiate();
		if (peer->connect_to_host(IPAddress("127.0.0.1"), p_port) != OK) {
			return false;
		}
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 10000;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			peer->poll();
			const StreamPeerTCP::Status status = peer->get_status();
			if (status == StreamPeerTCP::STATUS_CONNECTED) {
				return true;
			}
			if (status == StreamPeerTCP::STATUS_ERROR || status == StreamPeerTCP::STATUS_NONE) {
				return false;
			}
			OS::get_singleton()->delay_usec(20000);
		}
		return false;
	}

	int send_request(const String &p_command, const Dictionary &p_arguments) {
		Dictionary request;
		const int seq = next_seq++;
		request["seq"] = seq;
		request["type"] = "request";
		request["command"] = p_command;
		request["arguments"] = p_arguments;

		const CharString payload = JSON::stringify(request).utf8();
		const CharString header = vformat("Content-Length: %d\r\n\r\n", payload.length()).utf8();
		if (peer->put_data((const uint8_t *)header.get_data(), header.length()) != OK) {
			return -1;
		}
		if (peer->put_data((const uint8_t *)payload.get_data(), payload.length()) != OK) {
			return -1;
		}
		return seq;
	}

	bool pump() {
		peer->poll();
		const int available = peer->get_available_bytes();
		if (available <= 0) {
			return peer->get_status() == StreamPeerTCP::STATUS_CONNECTED;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		int read = 0;
		if (peer->get_partial_data(chunk.ptrw(), available, read) != OK) {
			return false;
		}
		if (read > 0) {
			buffer += String::utf8((const char *)chunk.ptr(), read);
		}
		return true;
	}

	// Pops the next complete framed message, or an empty dictionary when the buffer
	// does not hold one yet.
	Dictionary take_message() {
		const int header_end = buffer.find("\r\n\r\n");
		if (header_end < 0) {
			return Dictionary();
		}
		const String header = buffer.substr(0, header_end);
		const int length_index = header.findn("content-length:");
		if (length_index < 0) {
			buffer = buffer.substr(header_end + 4);
			return Dictionary();
		}
		const int content_length = header.substr(length_index + 15).strip_edges().to_int();
		const String body_and_rest = buffer.substr(header_end + 4);
		if (body_and_rest.to_utf8_buffer().size() < content_length) {
			return Dictionary();
		}
		const String body = body_and_rest.substr(0, content_length);
		buffer = body_and_rest.substr(body.length());
		const Variant parsed = JSON::parse_string(body);
		if (parsed.get_type() != Variant::DICTIONARY) {
			return Dictionary();
		}
		return parsed;
	}

	// Reads everything that arrived, sorting events and responses into their own
	// queues so neither kind is lost while waiting for the other.
	bool drain() {
		if (!pump()) {
			return false;
		}
		for (Dictionary message = take_message(); !message.is_empty(); message = take_message()) {
			received_messages.push_back(message);
			if (String(message.get("type", "")) == "event") {
				events.push_back(message);
			} else {
				responses.push_back(message);
			}
		}
		return true;
	}

	void clear_events() {
		events.clear();
	}

	Dictionary await_response(int p_request_seq, uint64_t p_timeout_msec) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			if (!drain()) {
				return Dictionary();
			}
			for (List<Dictionary>::Element *E = responses.front(); E; E = E->next()) {
				if (int(E->get().get("request_seq", -1)) == p_request_seq) {
					const Dictionary response = E->get();
					responses.erase(E);
					return response;
				}
			}
			OS::get_singleton()->delay_usec(20000);
		}
		return Dictionary();
	}

	// Waits for an event by name without consuming it or anything received earlier.
	Dictionary await_event(const String &p_event, uint64_t p_timeout_msec) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			if (!drain()) {
				return Dictionary();
			}
			for (const Dictionary &event : events) {
				if (String(event.get("event", "")) == p_event) {
					return event;
				}
			}
			OS::get_singleton()->delay_usec(20000);
		}
		return Dictionary();
	}

	int event_index(const String &p_event) const {
		for (int i = 0; i < events.size(); i++) {
			if (String(events[i].get("event", "")) == p_event) {
				return i;
			}
		}
		return -1;
	}

	// The ordered lifecycle events of the current window, which is what the DAP
	// contract is expressed in.
	PackedStringArray lifecycle_events() const {
		PackedStringArray names;
		for (const Dictionary &event : events) {
			const String name = event.get("event", "");
			if (name == "process" || name == "exited" || name == "terminated") {
				names.push_back(name);
			}
		}
		return names;
	}

	int exit_code_of_first_exited() const {
		for (const Dictionary &event : events) {
			if (String(event.get("event", "")) == "exited") {
				const Dictionary body = event.get("body", Dictionary());
				return body.get("exitCode", -1);
			}
		}
		return -1;
	}

	void disconnect_from_host() {
		if (peer.is_valid()) {
			peer->disconnect_from_host();
		}
	}
};

static String dap_messages_diagnostic(const DebugAdapterClient &p_client) {
	String diagnostic;
	for (const Dictionary &message : p_client.received_messages) {
		diagnostic += JSON::stringify(message) + "\n";
	}
	return diagnostic;
}

static String tooling_host_and_dap_diagnostic(HostProcess &r_host, const DebugAdapterClient &p_client) {
	drain_pipe(r_host.stdout_pipe, r_host.output);
	drain_pipe(r_host.stderr_pipe, r_host.output);
	return "Tooling host output:\n" + r_host.output + "Received DAP messages:\n" + dap_messages_diagnostic(p_client);
}

// Stages the checked-in debuggee that exposes locals and members at a stable
// executable breakpoint. The test only observes the real DAP protocol.
static String prepare_breakpoint_hit_project() {
	EditorWorkflowTestFixtures::DisposableProjectSpec spec;
	spec.fixture_name = "dap_breakpoint_hit";
	PackedStringArray paths;
	paths.push_back("project.foundry");
	paths.push_back("main.tscn");
	paths.push_back("breakpoint_hit.fs");
	spec.relative_paths = paths;
	return EditorWorkflowTestFixtures::prepare_disposable_project(spec);
}

struct BreakpointHitSession {
	HostProcess host;
	DebugAdapterClient client;
	String project_path;
	bool ready = false;
	OS::ProcessID debuggee_pid = 0;
	bool launch_started = false;
	bool debuggee_terminated = false;

	BreakpointHitSession() {
		project_path = prepare_breakpoint_hit_project();
		if (project_path.is_empty()) {
			return;
		}

		List<String> arguments;
		arguments.push_back("tooling");
		arguments.push_back("serve");
		arguments.push_back("--project");
		arguments.push_back(project_path);
		arguments.push_back("--lsp-port");
		arguments.push_back("0");
		arguments.push_back("--dap-port");
		arguments.push_back("0");
		host = launch_tooling_host(arguments);
		if (!host.is_valid() || !wait_for_marker(host, "FOUNDRY_TOOLING {", 180000)) {
			return;
		}

		const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING ");
		if (!client.connect_to_port(payload.get("dap_port", 0))) {
			return;
		}
		Dictionary initialize_arguments;
		initialize_arguments["adapterID"] = "foundry";
		initialize_arguments["linesStartAt1"] = true;
		initialize_arguments["columnsStartAt1"] = true;
		initialize_arguments["supportsVariableType"] = true;
		const Dictionary response = client.await_response(client.send_request("initialize", initialize_arguments), 30000);
		ready = bool(response.get("success", false));
	}

	~BreakpointHitSession() {
		if (launch_started && !debuggee_terminated && client.peer.is_valid()) {
			client.await_response(client.send_request("terminate", Dictionary()), 10000);
			client.await_event("terminated", 10000);
		}
		client.disconnect_from_host();
		shutdown_host(host);
		if (debuggee_pid != 0 && OS::get_singleton()->is_process_running(debuggee_pid)) {
			OS::get_singleton()->kill(debuggee_pid);
		}
	}

	void track_debuggee(const Dictionary &p_process_event) {
		debuggee_pid = Dictionary(p_process_event.get("body", Dictionary())).get("systemProcessId", 0);
	}
};

TEST_CASE("[Editor][ToolingHost] A breakpoint exposes frame locals members and a clean exit") {
	BreakpointHitSession session;
	if (session.project_path.is_empty()) {
		FAIL(("Failed to stage the breakpoint-hit project.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (!session.ready) {
		FAIL(("The tooling host never accepted an initialized debug adapter session.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary initialized_event = session.client.await_event("initialized", 30000);
	if (initialized_event.is_empty()) {
		FAIL(("The host never emitted `initialized`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	const int breakpoint_line = 8;
	const String script_path = session.project_path.path_join("breakpoint_hit.fs").simplify_path();
	Dictionary source;
	source["path"] = script_path;
	Dictionary breakpoint;
	breakpoint["line"] = breakpoint_line;
	Array requested_breakpoints;
	requested_breakpoints.push_back(breakpoint);
	Dictionary set_breakpoints_arguments;
	set_breakpoints_arguments["source"] = source;
	set_breakpoints_arguments["breakpoints"] = requested_breakpoints;
	const Dictionary set_breakpoints_response = session.client.await_response(
			session.client.send_request("setBreakpoints", set_breakpoints_arguments), 30000);
	if (!bool(set_breakpoints_response.get("success", false))) {
		FAIL(("setBreakpoints did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary set_breakpoints_body = set_breakpoints_response.get("body", Dictionary());
	const Array registered_breakpoints = set_breakpoints_body.get("breakpoints", Array());
	if (registered_breakpoints.size() != 1) {
		FAIL(("setBreakpoints did not return exactly one breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary registered_breakpoint = registered_breakpoints[0];
	if (!bool(registered_breakpoint.get("verified", false))) {
		FAIL(("setBreakpoints returned an unverified breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (int(registered_breakpoint.get("line", -1)) != breakpoint_line) {
		FAIL(("setBreakpoints returned the wrong breakpoint line.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (!registered_breakpoint.has("id")) {
		FAIL(("setBreakpoints did not return a breakpoint ID.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int breakpoint_id = registered_breakpoint["id"];

	Dictionary launch_arguments;
	launch_arguments["noDebug"] = false;
	launch_arguments["scene"] = "main";
	const int launch_seq = session.client.send_request("launch", launch_arguments);
	session.launch_started = true;
	const int configuration_done_seq = session.client.send_request("configurationDone", Dictionary());
	const Dictionary launch_response = session.client.await_response(launch_seq, 60000);
	const Dictionary configuration_done_response = session.client.await_response(configuration_done_seq, 30000);
	if (!bool(launch_response.get("success", false))) {
		FAIL(("launch did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (!bool(configuration_done_response.get("success", false))) {
		FAIL(("configurationDone did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary process_event = session.client.await_event("process", 120000);
	if (process_event.is_empty()) {
		FAIL(("The debuggee never started.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	session.track_debuggee(process_event);
	const Dictionary stopped_event = session.client.await_event("stopped", 120000);
	if (stopped_event.is_empty()) {
		FAIL(("The debuggee never stopped at the breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int process_event_index = session.client.event_index("process");
	const int stopped_event_index = session.client.event_index("stopped");
	if (process_event_index < 0 || stopped_event_index <= process_event_index) {
		FAIL(("The stopped event did not follow the process event.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary stopped_body = stopped_event.get("body", Dictionary());
	if (String(stopped_body.get("reason", "")) != "breakpoint") {
		FAIL(("The debuggee stopped for a reason other than `breakpoint`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int stopped_thread_id = stopped_body.get("threadId", -1);
	if (stopped_thread_id != 1) {
		FAIL(("The stopped event did not identify the main thread.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array hit_ids = stopped_body.get("hitBreakpointIds", Array());
	bool hit_registered_breakpoint = false;
	for (const Variant &hit_id : hit_ids) {
		if (int(hit_id) == breakpoint_id) {
			hit_registered_breakpoint = true;
			break;
		}
	}
	if (!hit_registered_breakpoint) {
		FAIL(("The stopped event did not identify the registered breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	const Dictionary threads_response = session.client.await_response(
			session.client.send_request("threads", Dictionary()), 30000);
	if (!bool(threads_response.get("success", false))) {
		FAIL(("threads did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array threads = Dictionary(threads_response.get("body", Dictionary())).get("threads", Array());
	if (threads.is_empty()) {
		FAIL(("threads returned no main thread.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int thread_id = Dictionary(threads[0]).get("id", 0);
	if (thread_id == 0 || thread_id != stopped_thread_id) {
		FAIL(("threads returned an invalid thread ID or one different from `stopped`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	Dictionary stack_arguments;
	stack_arguments["threadId"] = thread_id;
	const Dictionary stack_response = session.client.await_response(
			session.client.send_request("stackTrace", stack_arguments), 30000);
	if (!bool(stack_response.get("success", false))) {
		FAIL(("stackTrace did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array frames = Dictionary(stack_response.get("body", Dictionary())).get("stackFrames", Array());
	if (frames.is_empty()) {
		FAIL(("stackTrace returned no frames.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary frame = frames[0];
	if (String(frame.get("name", "")) != "_ready") {
		FAIL(("The top stack frame was not `_ready`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary frame_source = frame.get("source", Dictionary());
	if (String(frame_source.get("path", "")).simplify_path() != script_path) {
		FAIL(("The top stack frame did not identify the staged fixture script.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (int(frame.get("line", -1)) != breakpoint_line) {
		FAIL(("The top stack frame did not identify the requested breakpoint line.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (!frame.has("id")) {
		FAIL(("The top stack frame did not return a frame ID.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int frame_id = frame["id"];

	Dictionary scopes_arguments;
	scopes_arguments["frameId"] = frame_id;
	const Dictionary scopes_response = session.client.await_response(
			session.client.send_request("scopes", scopes_arguments), 30000);
	if (!bool(scopes_response.get("success", false))) {
		FAIL(("scopes did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array scopes = Dictionary(scopes_response.get("body", Dictionary())).get("scopes", Array());
	Dictionary scopes_by_name;
	Dictionary expected_scope_hints;
	expected_scope_hints["Locals"] = "locals";
	expected_scope_hints["Members"] = "members";
	expected_scope_hints["Globals"] = "globals";
	for (Dictionary scope : scopes) {
		const String scope_name = scope.get("name", "");
		if (expected_scope_hints.has(scope_name)) {
			scopes_by_name[scope_name] = scope;
		}
	}
	if (!scopes_by_name.has("Locals") || !scopes_by_name.has("Members") || !scopes_by_name.has("Globals")) {
		FAIL(("scopes did not expose Locals, Members, and Globals.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary locals_scope = scopes_by_name["Locals"];
	const Dictionary members_scope = scopes_by_name["Members"];
	const Dictionary globals_scope = scopes_by_name["Globals"];
	if (String(locals_scope.get("presentationHint", "")) != "locals" ||
			String(members_scope.get("presentationHint", "")) != "members" ||
			String(globals_scope.get("presentationHint", "")) != "globals") {
		FAIL(("One or more scopes returned the wrong presentation hint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const int locals_reference = locals_scope.get("variablesReference", 0);
	const int members_reference = members_scope.get("variablesReference", 0);
	const int globals_reference = globals_scope.get("variablesReference", 0);
	if (locals_reference == 0 || members_reference == 0 || globals_reference == 0) {
		FAIL(("One or more scopes returned a zero variables reference.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (locals_reference == members_reference || locals_reference == globals_reference ||
			members_reference == globals_reference) {
		FAIL(("Locals, Members, and Globals did not return distinct variables references.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	auto request_variables = [&session](int p_reference) {
		Dictionary arguments;
		arguments["variablesReference"] = p_reference;
		return session.client.await_response(session.client.send_request("variables", arguments), 30000);
	};
	auto find_variable = [](const Array &p_variables, const String &p_name) {
		for (Dictionary variable : p_variables) {
			if (String(variable.get("name", "")) == p_name) {
				return variable;
			}
		}
		return Dictionary();
	};
	const Dictionary locals_response = request_variables(locals_reference);
	if (!bool(locals_response.get("success", false))) {
		FAIL(("variables did not succeed for Locals.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array local_variables = Dictionary(locals_response.get("body", Dictionary())).get("variables", Array());
	const Dictionary local_value = find_variable(local_variables, "local_value");
	if (local_value.is_empty()) {
		FAIL(("Locals did not contain `local_value`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (String(local_value.get("value", "")) != "7" || String(local_value.get("type", "")) != "int" ||
			int(local_value.get("variablesReference", -1)) != 0) {
		FAIL(("`local_value` did not have value 7, type int, and variablesReference 0.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	const Dictionary members_response = request_variables(members_reference);
	if (!bool(members_response.get("success", false))) {
		FAIL(("variables did not succeed for Members.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array member_variables = Dictionary(members_response.get("body", Dictionary())).get("variables", Array());
	const Dictionary member_value = find_variable(member_variables, "member_value");
	if (member_value.is_empty()) {
		FAIL(("Members did not contain `member_value`.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (String(member_value.get("value", "")) != "35" || String(member_value.get("type", "")) != "int" ||
			int(member_value.get("variablesReference", -1)) != 0) {
		FAIL(("`member_value` did not have value 35, type int, and variablesReference 0.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	Dictionary evaluate_arguments;
	evaluate_arguments["expression"] = "local_value + member_value";
	evaluate_arguments["frameId"] = frame_id;
	const Dictionary evaluate_response = session.client.await_response(
			session.client.send_request("evaluate", evaluate_arguments), 30000);
	if (!bool(evaluate_response.get("success", false))) {
		FAIL(("evaluate did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary evaluate_body = evaluate_response.get("body", Dictionary());
	if (String(evaluate_body.get("result", "")) != "42" ||
			int(evaluate_body.get("variablesReference", -1)) != 0) {
		FAIL(("evaluate did not return result 42 with variablesReference 0.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	// Restarting synchronously ends the current editor run before the replacement
	// starts. The adapter must retain the DAP breakpoint identity across that gap.
	session.client.clear_events();
	Dictionary restart_arguments;
	restart_arguments["arguments"] = launch_arguments;
	const Dictionary restart_response = session.client.await_response(
			session.client.send_request("restart", restart_arguments), 60000);
	if (!bool(restart_response.get("success", false))) {
		FAIL(("restart did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary restarted_process_event = session.client.await_event("process", 120000);
	if (restarted_process_event.is_empty()) {
		FAIL(("The restarted debuggee never started.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	session.track_debuggee(restarted_process_event);
	const Dictionary restarted_stopped_event = session.client.await_event("stopped", 30000);
	if (restarted_stopped_event.is_empty()) {
		FAIL(("The restarted debuggee never stopped at the breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary restarted_stopped_body = restarted_stopped_event.get("body", Dictionary());
	if (String(restarted_stopped_body.get("reason", "")) != "breakpoint" ||
			int(restarted_stopped_body.get("threadId", -1)) != thread_id) {
		FAIL(("The restarted debuggee did not stop on the expected thread and breakpoint.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Array restarted_hit_ids = restarted_stopped_body.get("hitBreakpointIds", Array());
	bool restart_hit_registered_breakpoint = false;
	for (const Variant &hit_id : restarted_hit_ids) {
		if (int(hit_id) == breakpoint_id) {
			restart_hit_registered_breakpoint = true;
			break;
		}
	}
	if (!restart_hit_registered_breakpoint) {
		FAIL(("The restarted debuggee did not retain the registered breakpoint ID.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	PackedStringArray running_restart_lifecycle;
	running_restart_lifecycle.push_back("process");
	if (session.client.lifecycle_events() != running_restart_lifecycle) {
		FAIL(("Restart exposed the replaced debuggee's exited or terminated event.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}

	Dictionary continue_arguments;
	continue_arguments["threadId"] = thread_id;
	const Dictionary continue_response = session.client.await_response(
			session.client.send_request("continue", continue_arguments), 30000);
	if (!bool(continue_response.get("success", false))) {
		FAIL(("continue did not succeed.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	const Dictionary terminated_event = session.client.await_event("terminated", 120000);
	if (terminated_event.is_empty()) {
		FAIL(("The debuggee never terminated.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	session.debuggee_terminated = true;
	PackedStringArray expected_lifecycle;
	expected_lifecycle.push_back("process");
	expected_lifecycle.push_back("exited");
	expected_lifecycle.push_back("terminated");
	if (session.client.lifecycle_events() != expected_lifecycle) {
		FAIL(("The debuggee lifecycle was not process, exited, terminated.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
	if (session.client.exit_code_of_first_exited() != 0) {
		FAIL(("The debuggee did not exit with code 0.\n" +
				tooling_host_and_dap_diagnostic(session.host, session.client)));
		return;
	}
}

TEST_CASE("[Editor][ToolingHost] A headless session answers pause and continue without a debuggee") {
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
	const int dap_port = payload["dap_port"];

	DebugAdapterClient client;
	if (!client.connect_to_port(dap_port)) {
		shutdown_host(host);
		FAIL("Failed to connect a debug adapter client.");
		return;
	}

	Dictionary initialize_arguments;
	initialize_arguments["adapterID"] = "foundry";
	initialize_arguments["linesStartAt1"] = true;
	initialize_arguments["columnsStartAt1"] = true;
	const Dictionary initialize_response = client.await_response(
			client.send_request("initialize", initialize_arguments), 30000);
	CHECK_MESSAGE(bool(initialize_response.get("success", false)), "The host did not answer `initialize`.");

	// Pausing with no debuggee used to run through the run bar's pause widget, which
	// a headless host never shows. The session now answers with a protocol error.
	Dictionary pause_arguments;
	pause_arguments["threadId"] = 1;
	const Dictionary pause_response = client.await_response(client.send_request("pause", pause_arguments), 30000);
	REQUIRE_MESSAGE(!pause_response.is_empty(), "The host never answered `pause`.");
	CHECK_FALSE(bool(pause_response.get("success", true)));
	CHECK_EQ(String(pause_response.get("message", "")), "not_running");

	const Dictionary continue_response = client.await_response(client.send_request("continue", pause_arguments), 30000);
	REQUIRE_MESSAGE(!continue_response.is_empty(), "The host never answered `continue`.");
	CHECK_FALSE(bool(continue_response.get("success", true)));
	CHECK_EQ(String(continue_response.get("message", "")), "not_running");

	// A `threads` request is always answerable, so a client can enumerate before a launch.
	const Dictionary threads_response = client.await_response(client.send_request("threads", Dictionary()), 30000);
	REQUIRE_MESSAGE(!threads_response.is_empty(), "The host never answered `threads`.");
	CHECK(bool(threads_response.get("success", false)));

	client.disconnect_from_host();
	shutdown_host(host);
}

TEST_CASE("[Editor][ToolingHost] Unsupported requests are refused instead of being dropped") {
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
	if (!ready) {
		INFO("Tooling host output:\n", host.output);
		shutdown_host(host);
		FAIL("The tooling host never emitted a readiness record.");
		return;
	}

	const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING ");
	const int dap_port = payload["dap_port"];

	DebugAdapterClient client;
	if (!client.connect_to_port(dap_port)) {
		INFO("Tooling host output:\n", host.output);
		shutdown_host(host);
		FAIL("Failed to connect a debug adapter client.");
		return;
	}

	Dictionary initialize_arguments;
	initialize_arguments["adapterID"] = "foundry";
	initialize_arguments["linesStartAt1"] = true;
	initialize_arguments["columnsStartAt1"] = true;
	const int initialize_seq = client.send_request("initialize", initialize_arguments);
	const Dictionary initialize_response = client.await_response(initialize_seq, 30000);
	if (!bool(initialize_response.get("success", false))) {
		FAIL(("The host did not answer `initialize`.\n" + tooling_host_and_dap_diagnostic(host, client)));
		client.disconnect_from_host();
		shutdown_host(host);
		return;
	}
	// The adapter has no runtime variable-mutation path, so it must not claim one.
	const Dictionary capabilities = initialize_response.get("body", Dictionary());
	CHECK_FALSE_MESSAGE(bool(capabilities.get("supportsSetVariable", false)),
			"`initialize` advertised set-variable support the adapter cannot honor.");

	// Asserts one correlated, unsuccessful response with an actionable error body.
	auto check_failed_response = [&](const String &p_command, const Dictionary &p_arguments,
									 const String &p_expected_message) {
		const int request_seq = client.send_request(p_command, p_arguments);
		const Dictionary response = client.await_response(request_seq, 30000);
		if (response.is_empty()) {
			FAIL_CHECK(("The host never answered `" + p_command + "`.\n" +
					tooling_host_and_dap_diagnostic(host, client)));
			return Dictionary();
		}
		CHECK_EQ(String(response.get("type", "")), "response");
		CHECK_EQ(int(response.get("request_seq", -1)), request_seq);
		CHECK_EQ(String(response.get("command", "")), p_command);
		CHECK_FALSE(bool(response.get("success", true)));
		CHECK_EQ(String(response.get("message", "")), p_expected_message);
		const Dictionary body = response.get("body", Dictionary());
		const Dictionary error = body.get("error", Dictionary());
		CHECK_FALSE_MESSAGE(error.is_empty(), "The failed response carried no `body.error`.");
		CHECK_FALSE(String(error.get("format", "")).is_empty());
		return response;
	};

	// Step-out before a launch has no debuggee to step out of.
	Dictionary step_out_arguments;
	step_out_arguments["threadId"] = 1;
	check_failed_response("stepOut", step_out_arguments, "not_running");

	// `setVariable` is unimplemented, so it falls through to the generic refusal.
	Dictionary set_variable_arguments;
	set_variable_arguments["variablesReference"] = 1;
	set_variable_arguments["name"] = "member_value";
	set_variable_arguments["value"] = "12";
	const Dictionary set_variable_response =
			check_failed_response("setVariable", set_variable_arguments, "unsupported_request");
	const Dictionary set_variable_error =
			Dictionary(set_variable_response.get("body", Dictionary())).get("error", Dictionary());
	CHECK_EQ(String(Dictionary(set_variable_error.get("variables", Dictionary())).get("command", "")), "setVariable");

	// The refusal is generic, not special-cased to a known command name.
	Dictionary synthetic_arguments;
	synthetic_arguments["payload"] = "unused";
	const Dictionary synthetic_response =
			check_failed_response("foundryNoSuchCommand", synthetic_arguments, "unsupported_request");
	const Dictionary synthetic_error =
			Dictionary(synthetic_response.get("body", Dictionary())).get("error", Dictionary());
	CHECK_EQ(String(Dictionary(synthetic_error.get("variables", Dictionary())).get("command", "")),
			"foundryNoSuchCommand");

	// A refusal is not a session event and must not end the session.
	for (const Dictionary &event : client.events) {
		const String name = event.get("event", "");
		const bool is_session_event =
				name == "stopped" || name == "continued" || name == "terminated" || name == "exited";
		CHECK_MESSAGE(!is_session_event, "An unsupported request emitted a session lifecycle event.");
	}

	const Dictionary threads_response = client.await_response(client.send_request("threads", Dictionary()), 30000);
	if (threads_response.is_empty()) {
		FAIL(("The host stopped answering after the refused requests.\n" +
				tooling_host_and_dap_diagnostic(host, client)));
	} else {
		CHECK(bool(threads_response.get("success", false)));
	}

	client.disconnect_from_host();
	shutdown_host(host);
}

TEST_CASE("[Editor][ToolingHost] A malformed project_test launch is refused over the wire") {
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
	const int dap_port = payload["dap_port"];

	DebugAdapterClient client;
	if (!client.connect_to_port(dap_port)) {
		shutdown_host(host);
		FAIL("Failed to connect a debug adapter client.");
		return;
	}

	Dictionary initialize_arguments;
	initialize_arguments["adapterID"] = "foundry";
	client.await_response(client.send_request("initialize", initialize_arguments), 30000);

	Dictionary adapter;
	adapter["protocolVersion"] = 99;
	Dictionary launch;
	launch["kind"] = "project_test";
	launch["runner"] = "res://addons/example/run.fs";
	launch["adapter"] = adapter;

	Dictionary launch_arguments;
	launch_arguments["foundry/launch"] = launch;
	const Dictionary launch_response = client.await_response(client.send_request("launch", launch_arguments), 30000);
	REQUIRE_MESSAGE(!launch_response.is_empty(), "The host never answered `launch`.");
	CHECK_FALSE(bool(launch_response.get("success", true)));
	CHECK_EQ(String(launch_response.get("message", "")), "invalid_launch");

	client.disconnect_from_host();
	shutdown_host(host);
}

// Stages the checked-in exit-status fixture project beneath the shared scratch space.
// Its runner and scenes decide the child's real result, so the lifecycle assertions
// below are about what the adapter reports, never about a script pinned in C++.
static String prepare_exit_status_project() {
	EditorWorkflowTestFixtures::DisposableProjectSpec spec;
	spec.fixture_name = "dap_exit_status";
	PackedStringArray paths;
	paths.push_back("project.foundry");
	paths.push_back("exit_status_runner.fs");
	paths.push_back("main.tscn");
	paths.push_back("quit_with_code.fs");
	paths.push_back("idle.tscn");
	paths.push_back("stay_running.fs");
	spec.relative_paths = paths;
	return EditorWorkflowTestFixtures::prepare_disposable_project(spec);
}

// Owns a tooling host serving the exit-status project, with a connected and
// initialized debug adapter client.
struct ExitStatusSession {
	HostProcess host;
	DebugAdapterClient client;
	String project_path;
	bool ready = false;

	ExitStatusSession() {
		project_path = prepare_exit_status_project();
		if (project_path.is_empty()) {
			return;
		}

		List<String> arguments;
		arguments.push_back("tooling");
		arguments.push_back("serve");
		arguments.push_back("--project");
		arguments.push_back(project_path);
		arguments.push_back("--lsp-port");
		arguments.push_back("0");
		arguments.push_back("--dap-port");
		arguments.push_back("0");

		host = launch_tooling_host(arguments);
		if (!host.is_valid()) {
			return;
		}
		if (!wait_for_marker(host, "FOUNDRY_TOOLING {", 180000)) {
			return;
		}

		const Dictionary payload = parse_marker_record(host.output, "FOUNDRY_TOOLING ");
		if (!client.connect_to_port(payload["dap_port"])) {
			return;
		}

		Dictionary initialize_arguments;
		initialize_arguments["adapterID"] = "foundry";
		initialize_arguments["linesStartAt1"] = true;
		initialize_arguments["columnsStartAt1"] = true;
		const Dictionary response = client.await_response(client.send_request("initialize", initialize_arguments), 30000);
		ready = bool(response.get("success", false));
	}

	~ExitStatusSession() {
		client.disconnect_from_host();
		shutdown_host(host);
	}

	String artifact(const String &p_relative_path) const {
		return project_path.path_join(p_relative_path);
	}

	// Runs one launch to completion and returns the lifecycle events it produced.
	PackedStringArray run_launch(const Dictionary &p_launch_arguments, uint64_t p_timeout_msec = 180000) {
		client.clear_events();
		const int launch_seq = client.send_request("launch", p_launch_arguments);
		client.await_response(client.send_request("configurationDone", Dictionary()), 30000);
		client.await_response(launch_seq, 60000);
		client.await_event("terminated", p_timeout_msec);
		return client.lifecycle_events();
	}
};

static Dictionary make_project_test_launch(const String &p_report_path, const String &p_selection) {
	Dictionary adapter;
	adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
	adapter["report"] = p_report_path;
	if (!p_selection.is_empty()) {
		Array test_ids;
		test_ids.push_back(p_selection);
		adapter["testIds"] = test_ids;
	}

	Dictionary launch;
	launch["kind"] = "project_test";
	launch["runner"] = "res://exit_status_runner.fs";
	launch["adapter"] = adapter;

	Dictionary arguments;
	arguments["noDebug"] = false;
	arguments["foundry/launch"] = launch;
	return arguments;
}

static PackedStringArray expected_known_result_lifecycle() {
	PackedStringArray expected;
	expected.push_back("process");
	expected.push_back("exited");
	expected.push_back("terminated");
	return expected;
}

TEST_CASE("[Editor][ToolingHost] A structured test launch reports the runner's real result") {
	ExitStatusSession session;
	REQUIRE_MESSAGE(!session.project_path.is_empty(), "Failed to stage the exit-status project.");
	INFO("Tooling host output:\n", session.host.output);
	REQUIRE_MESSAGE(session.ready, "The tooling host never accepted a debug adapter session.");

	// A passing run.
	const String passing_report = session.artifact("passing.tap");
	CHECK_EQ(session.run_launch(make_project_test_launch(passing_report, "exit::0")), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 0);

	// A represented test failure.
	const String failing_report = session.artifact("failing.tap");
	CHECK_EQ(session.run_launch(make_project_test_launch(failing_report, "exit::1")), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 1);

	// A real infrastructure failure: a report path inside a directory that does not
	// exist, which the runner cannot open, so it returns the protocol's `2`.
	const String broken_report = session.artifact("missing_directory/broken.tap");
	CHECK_EQ(session.run_launch(make_project_test_launch(broken_report, String())), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 2);
	CHECK_FALSE(FileAccess::exists(broken_report));

	// Both complete reports stay available for independent validation.
	REQUIRE(FileAccess::exists(passing_report));
	const String passing_tap = FileAccess::get_file_as_string(passing_report);
	CHECK(passing_tap.begins_with("TAP version 13"));
	CHECK(passing_tap.contains("1..1"));
	CHECK(passing_tap.contains("ok 1 - exit_status.point_1"));

	REQUIRE(FileAccess::exists(failing_report));
	const String failing_tap = FileAccess::get_file_as_string(failing_report);
	CHECK(failing_tap.begins_with("TAP version 13"));
	CHECK(failing_tap.contains("not ok 1 - exit_status.point_1"));
}

TEST_CASE("[Editor][ToolingHost] A replaced launch cannot leak its result into the next one") {
	ExitStatusSession session;
	REQUIRE_MESSAGE(!session.project_path.is_empty(), "Failed to stage the exit-status project.");
	INFO("Tooling host output:\n", session.host.output);
	REQUIRE_MESSAGE(session.ready, "The tooling host never accepted a debug adapter session.");

	const String infrastructure_report = session.artifact("missing_directory/first.tap");
	CHECK_EQ(session.run_launch(make_project_test_launch(infrastructure_report, String())), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 2);

	const String passing_report = session.artifact("second.tap");
	CHECK_EQ(session.run_launch(make_project_test_launch(passing_report, "exit::0")), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 0);

	// A finished session must not take the tooling host with it.
	const Dictionary threads_response = session.client.await_response(
			session.client.send_request("threads", Dictionary()), 30000);
	REQUIRE_MESSAGE(!threads_response.is_empty(), "The host stopped answering after a session ended.");
	CHECK(bool(threads_response.get("success", false)));
}

TEST_CASE("[Editor][ToolingHost] A scene launch reports the debuggee's own exit code") {
	ExitStatusSession session;
	REQUIRE_MESSAGE(!session.project_path.is_empty(), "Failed to stage the exit-status project.");
	INFO("Tooling host output:\n", session.host.output);
	REQUIRE_MESSAGE(session.ready, "The tooling host never accepted a debug adapter session.");

	// The main scene ends itself with a distinctive result, proving the lifecycle is
	// not special-cased to structured test launches.
	Dictionary arguments;
	arguments["noDebug"] = false;
	arguments["scene"] = "main";
	CHECK_EQ(session.run_launch(arguments), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 3);
}

TEST_CASE("[Editor][ToolingHost] A forcibly terminated launch reports no exit code") {
	ExitStatusSession session;
	REQUIRE_MESSAGE(!session.project_path.is_empty(), "Failed to stage the exit-status project.");
	INFO("Tooling host output:\n", session.host.output);
	REQUIRE_MESSAGE(session.ready, "The tooling host never accepted a debug adapter session.");

	Dictionary arguments;
	arguments["noDebug"] = false;
	arguments["scene"] = "res://idle.tscn";

	session.client.clear_events();
	const int launch_seq = session.client.send_request("launch", arguments);
	session.client.await_response(session.client.send_request("configurationDone", Dictionary()), 30000);
	session.client.await_response(launch_seq, 60000);
	REQUIRE_MESSAGE(!session.client.await_event("process", 120000).is_empty(), "The debuggee never started.");

	const Dictionary terminate_response = session.client.await_response(
			session.client.send_request("terminate", Dictionary()), 60000);
	CHECK(bool(terminate_response.get("success", false)));
	REQUIRE_MESSAGE(!session.client.await_event("terminated", 60000).is_empty(), "The session never ended.");

	// A killed debuggee has no trustworthy result, and a fabricated zero would be
	// indistinguishable from a successful run.
	PackedStringArray expected;
	expected.push_back("process");
	expected.push_back("terminated");
	CHECK_EQ(session.client.lifecycle_events(), expected);
}

#ifdef UNIX_ENABLED
// A process that stays alive until it is signaled, standing in for a debuggee the
// host launched. `sleep` is used directly rather than a second engine instance so
// the check stays about process ownership, not about engine startup.
static OS::ProcessID spawn_idle_child() {
	List<String> arguments;
	arguments.push_back("120");
	OS::ProcessID pid = 0;
	if (OS::get_singleton()->create_process("/bin/sleep", arguments, &pid) != OK) {
		return 0;
	}
	return pid;
}

// Reaps directly rather than through `OS::is_process_running()`, which reports a
// stale result for headless children on macOS (tracked separately).
static bool has_exited(OS::ProcessID p_pid) {
	int status = 0;
	const pid_t reaped = waitpid((pid_t)p_pid, &status, WNOHANG);
	if (reaped == (pid_t)p_pid) {
		return true;
	}
	if (reaped == -1 && errno == ECHILD) {
		return true;
	}
	return kill((pid_t)p_pid, 0) != 0;
}

static bool wait_until_gone(OS::ProcessID p_pid, uint64_t p_timeout_msec) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (has_exited(p_pid)) {
			return true;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	return has_exited(p_pid);
}

TEST_CASE("[Editor][ToolingHost] An orderly shutdown terminates the processes the host launched") {
	const OS::ProcessID launched = spawn_idle_child();
	REQUIRE_MESSAGE(launched != 0, "Failed to spawn a stand-in debuggee process.");
	const OS::ProcessID attached = spawn_idle_child();
	if (attached == 0) {
		OS::get_singleton()->kill(launched);
		FAIL("Failed to spawn a stand-in attached process.");
		return;
	}
	CHECK_EQ(kill((pid_t)launched, 0), 0);

	{
		EditorRun run;
		run.begin_launch();
		run.adopt_child_process(launched);

		CHECK_FALSE(EditorToolingHost::is_shutdown_requested());
		EditorToolingHost::request_shutdown();
		CHECK(EditorToolingHost::is_shutdown_requested());

		EditorToolingHost::process_pending_shutdown();

		CHECK_FALSE(EditorToolingHost::is_shutdown_requested());
		CHECK_EQ(run.get_child_process_count(), 0);
		CHECK_EQ(run.get_status(), EditorRun::STATUS_STOP);
	}

	CHECK(wait_until_gone(launched, 5000));
	// A process the host never launched is not tracked, so it is left running.
	CHECK_EQ(kill((pid_t)attached, 0), 0);
	kill((pid_t)attached, SIGKILL);
	wait_until_gone(attached, 5000);

	// A second pass with no pending request must not touch anything.
	EditorToolingHost::process_pending_shutdown();
	CHECK_FALSE(EditorToolingHost::is_shutdown_requested());
}

TEST_CASE("[Editor][ToolingHost] A signaled host shuts itself down") {
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

	REQUIRE_EQ(kill((pid_t)host.pid, SIGINT), 0);
	const bool exited = wait_until_gone(host.pid, 60000);
	if (!exited) {
		shutdown_host(host);
	}
	CHECK_MESSAGE(exited, "The tooling host ignored SIGINT.");
}
#endif // UNIX_ENABLED

} // namespace TestEditorToolingHost
