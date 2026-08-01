/**************************************************************************/
/*  test_editor_run_process_lifecycle.h                                   */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/run/editor_run.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestEditorRunProcessLifecycle {

constexpr uint64_t COMPLETION_TIMEOUT_MSEC = 180000;
constexpr uint64_t COMPLETION_POLL_USEC = 20000;

static String lifecycle_scratch_root() {
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			return configured.simplify_path();
		}
	}
	return TestUtils::get_temp_path("editor_run_process_lifecycle");
}

static void remove_recursive(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		const String child = p_path.path_join(entry);
		if (dir->current_is_dir()) {
			remove_recursive(child);
		} else {
			dir->remove(child);
		}
	}
	dir->list_dir_end();
	DirAccess::remove_absolute(p_path);
}

// Stages the checked-in transport runner beneath the shared scratch space, so every
// case owns a real project whose runner decides the child's exit code.
struct StagedRunnerProject {
	String project_root;

	explicit StagedRunnerProject(const String &p_name) {
		project_root = lifecycle_scratch_root()
							   .path_join(vformat("editor_run_lifecycle_%s_%d", p_name, OS::get_singleton()->get_process_id()))
							   .simplify_path();
		remove_recursive(project_root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir.is_valid());
		REQUIRE_EQ(dir->make_dir_recursive(project_root), OK);

		const String fixture_root = TestUtils::get_executable_dir()
											.path_join("../tests/fixtures/foundry_test_adapter_transport")
											.simplify_path();
		stage("project.foundry", fixture_root);
		stage("adapter_transport_runner.fs", fixture_root);
	}

	~StagedRunnerProject() {
		remove_recursive(project_root);
	}

	void stage(const String &p_file_name, const String &p_fixture_root) const {
		Error error = OK;
		const String source = FileAccess::get_file_as_string(p_fixture_root.path_join(p_file_name), &error);
		REQUIRE_MESSAGE(error == OK, vformat("Cannot read runner fixture '%s'", p_file_name));
		Ref<FileAccess> file = FileAccess::open(project_root.path_join(p_file_name), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(source);
	}

	String path(const String &p_relative_path) const {
		return project_root.path_join(p_relative_path);
	}

	// A headless engine child that runs the staged runner and returns `p_return_code`.
	// `p_continuation` makes the runner wait for that file before finishing.
	List<String> child_arguments(const String &p_report_name, int p_return_code, const String &p_continuation = String()) const {
		List<String> arguments;
		arguments.push_back("--headless");
		arguments.push_back("--no-header");
		arguments.push_back("project");
		arguments.push_back("test");
		arguments.push_back("--project");
		arguments.push_back(project_root);
		arguments.push_back("--runner");
		arguments.push_back("res://adapter_transport_runner.fs");
		arguments.push_back("--");
		arguments.push_back("adapter");
		arguments.push_back("run");
		arguments.push_back("--protocol-version");
		arguments.push_back("1");
		arguments.push_back("--report");
		arguments.push_back(path(p_report_name));
		arguments.push_back("--");
		arguments.push_back(vformat("return-code=%d", p_return_code));
		if (!p_continuation.is_empty()) {
			arguments.push_back("delayed-report=" + p_continuation);
		}
		return arguments;
	}
};

static OS::ProcessID spawn_child(const List<String> &p_arguments) {
	OS::ProcessID pid = 0;
	if (OS::get_singleton()->create_instance(p_arguments, &pid) != OK) {
		return 0;
	}
	return pid;
}

static void write_continuation(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("go");
}

static bool await_completion(EditorRun &r_run, EditorRun::ProcessCompletion &r_completion) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + COMPLETION_TIMEOUT_MSEC;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (r_run.poll_child_completion(r_completion)) {
			return true;
		}
		OS::get_singleton()->delay_usec(COMPLETION_POLL_USEC);
	}
	return false;
}

TEST_CASE("[Editor][EditorRun] A running child reports no completion") {
	StagedRunnerProject project("running");
	const String continuation = project.path("continue.marker");

	EditorRun run;
	const uint64_t launch = run.begin_launch();
	const OS::ProcessID pid = spawn_child(project.child_arguments("running.tap", 0, continuation));
	REQUIRE_MESSAGE(pid != 0, "Failed to spawn a runner child.");
	run.adopt_child_process(pid);

	EditorRun::ProcessCompletion completion;
	for (int i = 0; i < 10; i++) {
		CHECK_FALSE(run.poll_child_completion(completion));
		OS::get_singleton()->delay_usec(20000);
	}
	CHECK_EQ(run.get_child_process_count(), 1);

	write_continuation(continuation);
	REQUIRE(await_completion(run, completion));
	CHECK_EQ(completion.pid, pid);
	CHECK_EQ(completion.launch_id, launch);
	CHECK_EQ(completion.exit_code, 0);
}

TEST_CASE("[Editor][EditorRun] A completed child reports the status the OS recorded") {
	// The protocol-defined runner results: success, represented failure, and
	// infrastructure failure. Every one of them has to survive the lifecycle unchanged.
	for (int expected_code = 0; expected_code <= 2; expected_code++) {
		INFO("Expected runner result: ", expected_code);
		StagedRunnerProject project(vformat("code_%d", expected_code));

		EditorRun run;
		const uint64_t launch = run.begin_launch();
		const OS::ProcessID pid = spawn_child(project.child_arguments("result.tap", expected_code));
		REQUIRE_MESSAGE(pid != 0, "Failed to spawn a runner child.");
		run.adopt_child_process(pid);

		EditorRun::ProcessCompletion completion;
		REQUIRE(await_completion(run, completion));
		CHECK_EQ(completion.pid, pid);
		CHECK_EQ(completion.launch_id, launch);
		CHECK_EQ(completion.exit_code, expected_code);

		// The completion is reported once, and the exited child is no longer owned, so
		// nothing can kill it or report it again.
		CHECK_EQ(run.get_child_process_count(), 0);
		CHECK_FALSE(run.has_child_process(pid));
		EditorRun::ProcessCompletion duplicate;
		CHECK_FALSE(run.poll_child_completion(duplicate));

		// Consuming the result also releases whatever the platform still kept for the
		// finished process, so it no longer has a recoverable status.
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
	}
}

TEST_CASE("[Editor][EditorRun] A natural completion drops only the child that finished") {
	StagedRunnerProject project("siblings");
	const String continuation = project.path("continue.marker");

	EditorRun run;
	run.begin_launch();
	const OS::ProcessID finishing = spawn_child(project.child_arguments("finishing.tap", 0));
	REQUIRE_MESSAGE(finishing != 0, "Failed to spawn the finishing child.");
	const OS::ProcessID lingering = spawn_child(project.child_arguments("lingering.tap", 0, continuation));
	if (lingering == 0) {
		OS::get_singleton()->kill(finishing);
		FAIL("Failed to spawn the lingering child.");
		return;
	}
	run.adopt_child_process(finishing);
	run.adopt_child_process(lingering);

	EditorRun::ProcessCompletion completion;
	REQUIRE(await_completion(run, completion));
	CHECK_EQ(completion.pid, finishing);
	CHECK_EQ(run.get_child_process_count(), 1);
	CHECK(run.has_child_process(lingering));

	// A forced stop is cleanup, not a natural result: it terminates what remains and
	// produces no completion.
	run.stop();
	CHECK_EQ(run.get_child_process_count(), 0);
	EditorRun::ProcessCompletion after_stop;
	CHECK_FALSE(run.poll_child_completion(after_stop));
	CHECK_EQ(run.get_status(), EditorRun::STATUS_STOP);
	CHECK_EQ(run.get_launch_id(), (uint64_t)0);
}

TEST_CASE("[Editor][EditorRun] Every launch gets its own identity") {
	EditorRun run;
	const uint64_t first = run.begin_launch();
	const uint64_t second = run.begin_launch();
	CHECK_NE(first, (uint64_t)0);
	CHECK_GT(second, first);

	EditorRun other_run;
	CHECK_GT(other_run.begin_launch(), second);
}

TEST_CASE("[Editor][EditorRun] A process this instance never spawned has no status") {
	// The lifecycle must never mistake "no status available" for a successful exit, so
	// the platform reports the unavailable sentinel instead of a plausible zero.
	CHECK_EQ(OS::get_singleton()->get_process_exit_code(OS::get_singleton()->get_process_id()),
			DebugSessionResultCoordinator::UNAVAILABLE_EXIT_CODE);
}

TEST_CASE("[Editor][DebugSession] A known result ends the session exactly once") {
	DebugSessionResultCoordinator coordinator;
	coordinator.begin_owned_launch(7);

	const DebugSessionResultCoordinator::Outcome outcome = coordinator.observe_process_completed(7, 2);
	CHECK(outcome.ended);
	CHECK_EQ(outcome.launch_id, (uint64_t)7);
	CHECK(outcome.has_result);
	CHECK_EQ(outcome.exit_code, 2);
	CHECK_FALSE(coordinator.is_active());

	// Neither a duplicate completion nor the socket closing afterwards may end it again.
	CHECK_FALSE(coordinator.observe_process_completed(7, 2).ended);
	CHECK_FALSE(coordinator.observe_debugger_stopped().ended);
	CHECK_FALSE(coordinator.observe_forced_termination().ended);
}

TEST_CASE("[Editor][DebugSession] Either race order produces the same known result") {
	DebugSessionResultCoordinator process_first;
	process_first.begin_owned_launch(11);
	const DebugSessionResultCoordinator::Outcome from_process = process_first.observe_process_completed(11, 1);
	CHECK_FALSE(process_first.observe_debugger_stopped().ended);

	DebugSessionResultCoordinator debugger_first;
	debugger_first.begin_owned_launch(11);
	// The socket closing first is not proof of a result, so the session stays open.
	CHECK_FALSE(debugger_first.observe_debugger_stopped().ended);
	CHECK(debugger_first.is_active());
	const DebugSessionResultCoordinator::Outcome from_debugger = debugger_first.observe_process_completed(11, 1);

	CHECK_EQ(from_process.ended, from_debugger.ended);
	CHECK_EQ(from_process.has_result, from_debugger.has_result);
	CHECK_EQ(from_process.exit_code, from_debugger.exit_code);
	CHECK_EQ(from_process.launch_id, from_debugger.launch_id);
	CHECK(from_debugger.ended);
	CHECK_EQ(from_debugger.exit_code, 1);
}

TEST_CASE("[Editor][DebugSession] A stale launch cannot end the launch that replaced it") {
	DebugSessionResultCoordinator coordinator;
	coordinator.begin_owned_launch(1);
	coordinator.begin_owned_launch(2);

	CHECK_FALSE(coordinator.observe_process_completed(1, 0).ended);
	CHECK(coordinator.is_active());

	const DebugSessionResultCoordinator::Outcome outcome = coordinator.observe_process_completed(2, 0);
	CHECK(outcome.ended);
	CHECK_EQ(outcome.launch_id, (uint64_t)2);
	CHECK(outcome.has_result);
	CHECK_EQ(outcome.exit_code, 0);
}

TEST_CASE("[Editor][DebugSession] An unavailable status ends the session without a result") {
	DebugSessionResultCoordinator coordinator;
	coordinator.begin_owned_launch(3);

	const DebugSessionResultCoordinator::Outcome outcome = coordinator.observe_process_completed(
			3, DebugSessionResultCoordinator::UNAVAILABLE_EXIT_CODE);
	CHECK(outcome.ended);
	CHECK_FALSE(outcome.has_result);
}

TEST_CASE("[Editor][DebugSession] An unowned session ends without a result") {
	DebugSessionResultCoordinator coordinator;
	coordinator.begin_unowned_session();

	const DebugSessionResultCoordinator::Outcome outcome = coordinator.observe_debugger_stopped();
	CHECK(outcome.ended);
	CHECK_FALSE(outcome.has_result);
	CHECK_EQ(outcome.launch_id, (uint64_t)0);
	// An attached debuggee never supplies a process result, so a completion claiming
	// one is not accepted either.
	CHECK_FALSE(coordinator.observe_process_completed(0, 0).ended);
	CHECK_FALSE(coordinator.observe_debugger_stopped().ended);
}

TEST_CASE("[Editor][DebugSession] A forced termination supersedes a pending natural result") {
	DebugSessionResultCoordinator coordinator;
	coordinator.begin_owned_launch(5);
	CHECK_FALSE(coordinator.observe_debugger_stopped().ended);

	const DebugSessionResultCoordinator::Outcome outcome = coordinator.observe_forced_termination();
	CHECK(outcome.ended);
	CHECK_FALSE(outcome.has_result);
	CHECK_EQ(outcome.launch_id, (uint64_t)5);

	CHECK_FALSE(coordinator.observe_forced_termination().ended);
	CHECK_FALSE(coordinator.observe_process_completed(5, 0).ended);
}

TEST_CASE("[Editor][DebugSession] Nothing ends a session that never started") {
	DebugSessionResultCoordinator coordinator;
	CHECK_FALSE(coordinator.observe_debugger_stopped().ended);
	CHECK_FALSE(coordinator.observe_process_completed(1, 0).ended);
	CHECK_FALSE(coordinator.observe_forced_termination().ended);
}

} // namespace TestEditorRunProcessLifecycle
