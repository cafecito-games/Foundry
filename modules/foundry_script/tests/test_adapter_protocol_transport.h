/**************************************************************************/
/*  test_adapter_protocol_transport.h                                     */
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

#include "fs_temporary_project_tree.h"

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

// Observable engine-side guarantees of the Foundry Test Adapter Protocol: the engine
// passes runner arguments through unchanged, lets the runner own its artifact files,
// and propagates the runner's exit code. The protocol itself lives entirely in the
// runner, so these tests never parse protocol semantics beyond what the transport
// must preserve.
namespace FSTests {
namespace TestAdapterProtocolTransport {

constexpr uint64_t SUBPROCESS_TIMEOUT_MSEC = 120000;
constexpr uint64_t POLL_INTERVAL_USEC = 20000;

// Builds a throwaway project in the shared test scratch space whose runner is one of
// the checked-in adapter fixture runners.
struct AdapterProject {
	TemporaryProjectTree tree;

	AdapterProject(const String &p_name, const String &p_fixture_runner) :
			tree(p_name) {
		tree.write_file("project.foundry",
				"config_version=5\n\n"
				"[application]\n\n"
				"config/name=\"Adapter Transport Project\"\n");
		const String fixture_path = TestUtils::get_data_path("test_adapter_runners").path_join(p_fixture_runner);
		Error error = OK;
		const String source = FileAccess::get_file_as_string(fixture_path, &error);
		REQUIRE_MESSAGE(error == OK, vformat("Cannot read adapter fixture runner '%s'", fixture_path));
		tree.write_file("runner.fs", source);
	}

	String path(const String &p_relative_path) const {
		return tree.root.path_join(p_relative_path);
	}

	List<String> base_arguments() const {
		List<String> arguments;
		arguments.push_back("--headless");
		arguments.push_back("--no-header");
		arguments.push_back("project");
		arguments.push_back("test");
		arguments.push_back("--project");
		arguments.push_back(tree.root);
		arguments.push_back("--runner");
		arguments.push_back("res://runner.fs");
		arguments.push_back("--");
		return arguments;
	}
};

static int run_foundry(const List<String> &p_arguments, String *r_output = nullptr) {
	int exit_code = -1;
	const Error error = OS::get_singleton()->execute(
			OS::get_singleton()->get_executable_path(), p_arguments, r_output, &exit_code, true);
	REQUIRE_MESSAGE(error == OK, "Cannot launch the foundry subprocess");
	return exit_code;
}

static OS::ProcessID launch_foundry(const List<String> &p_arguments) {
	OS::ProcessID pid = 0;
	const Error error = OS::get_singleton()->create_process(
			OS::get_singleton()->get_executable_path(), p_arguments, &pid);
	REQUIRE_MESSAGE(error == OK, "Cannot launch the foundry subprocess");
	return pid;
}

// Waits until `p_path` exists and contains `p_needle`, or the timeout elapses. Returns
// the file contents observed on the last read so a caller can assert on a partial,
// still-growing artifact.
static String wait_for_file_containing(const String &p_path, const String &p_needle, bool &r_found) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + SUBPROCESS_TIMEOUT_MSEC;
	String contents;
	r_found = false;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (FileAccess::exists(p_path)) {
			contents = FileAccess::get_file_as_string(p_path);
			if (contents.contains(p_needle)) {
				r_found = true;
				return contents;
			}
		}
		OS::get_singleton()->delay_usec(POLL_INTERVAL_USEC);
	}
	return contents;
}

static int wait_for_exit(OS::ProcessID p_pid, bool &r_exited) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + SUBPROCESS_TIMEOUT_MSEC;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (!OS::get_singleton()->is_process_running(p_pid)) {
			r_exited = true;
			return OS::get_singleton()->get_process_exit_code(p_pid);
		}
		OS::get_singleton()->delay_usec(POLL_INTERVAL_USEC);
	}
	r_exited = false;
	OS::get_singleton()->kill(p_pid);
	return -1;
}

TEST_CASE("[Modules][FoundryScript][TestAdapter] Runner arguments reach the adapter unchanged") {
	AdapterProject project("test_adapter_capabilities", "capabilities.fs");
	const String output_path = project.path("capabilities.json");

	List<String> arguments = project.base_arguments();
	arguments.push_back("adapter");
	arguments.push_back("capabilities");
	arguments.push_back("--output");
	arguments.push_back(output_path);

	String output;
	const int exit_code = run_foundry(arguments, &output);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 0);
	REQUIRE(FileAccess::exists(output_path));
	const String document = FileAccess::get_file_as_string(output_path);
	CHECK(document.contains("\"protocol\":\"foundry-test-adapter\""));
	CHECK(document.contains("\"supported_versions\":[1]"));
}

TEST_CASE("[Modules][FoundryScript][TestAdapter] A rejected invocation propagates protocol exit code 2") {
	AdapterProject project("test_adapter_bad_invocation", "capabilities.fs");

	List<String> arguments = project.base_arguments();
	arguments.push_back("adapter");
	arguments.push_back("capabilities");

	String output;
	const int exit_code = run_foundry(arguments, &output);
	INFO("Subprocess output:\n", output);
	CHECK_EQ(exit_code, 2);
	CHECK_FALSE(FileAccess::exists(project.path("capabilities.json")));
}

TEST_CASE("[Modules][FoundryScript][TestAdapter] The report file is created and flushed incrementally") {
	AdapterProject project("test_adapter_streaming_run", "streaming_run.fs");
	const String report_path = project.path("report.tap");
	const String gate_path = project.path("gate.marker");

	// A stale report from a previous run must be replaced rather than appended to.
	project.tree.write_file("report.tap", "stale content that must be truncated\n");

	List<String> arguments = project.base_arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);
	arguments.push_back("--gate");
	arguments.push_back(gate_path);

	const OS::ProcessID pid = launch_foundry(arguments);

	bool found_first_point = false;
	const String partial = wait_for_file_containing(report_path, "transport::first", found_first_point);
	INFO("Partial report:\n", partial);
	CHECK(found_first_point);
	CHECK_FALSE(partial.contains("stale content"));
	// The second point cannot have been written yet: the runner is still waiting on the gate.
	CHECK_FALSE(partial.contains("transport::second"));
	CHECK(OS::get_singleton()->is_process_running(pid));

	project.tree.write_file("gate.marker", "go\n");

	bool exited = false;
	const int exit_code = wait_for_exit(pid, exited);
	REQUIRE(exited);
	CHECK_EQ(exit_code, 0);

	const String report = FileAccess::get_file_as_string(report_path);
	INFO("Final report:\n", report);
	CHECK(report.begins_with("TAP version 13\n# foundry-test-adapter: 1\n1..2\n"));
	CHECK(report.contains("transport::second"));
}

TEST_CASE("[Modules][FoundryScript][TestAdapter] An uncaught runner failure leaves an incomplete report") {
	AdapterProject project("test_adapter_uncaught_failure", "uncaught_failure_run.fs");
	const String report_path = project.path("report.tap");

	List<String> arguments = project.base_arguments();
	arguments.push_back("adapter");
	arguments.push_back("run");
	arguments.push_back("--protocol-version");
	arguments.push_back("1");
	arguments.push_back("--report");
	arguments.push_back(report_path);

	String output;
	const int exit_code = run_foundry(arguments, &output);
	INFO("Subprocess output:\n", output);
	CHECK_NE(exit_code, 0);
	REQUIRE(FileAccess::exists(report_path));

	// The flushed prefix survives, and the unsatisfied plan is what tells a client this
	// was infrastructure failure rather than a normal test failure.
	const String report = FileAccess::get_file_as_string(report_path);
	INFO("Report:\n", report);
	CHECK(report.contains("1..2"));
	CHECK(report.contains("transport::first"));
	CHECK_FALSE(report.contains("ok 2 "));
	CHECK_FALSE(report.contains("Bail out!"));
}

} // namespace TestAdapterProtocolTransport
} // namespace FSTests
