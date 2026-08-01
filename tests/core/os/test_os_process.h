/**************************************************************************/
/*  test_os_process.h                                                     */
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

#include "tests/test_macros.h"
#include "tests/test_tools.h"
#include "tests/test_utils.h"

#ifdef UNIX_ENABLED

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace TestOSProcess {

constexpr uint64_t PROCESS_TIMEOUT_MSEC = 120000;
constexpr uint64_t PROCESS_POLL_USEC = 20000;

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

// A POSIX child created outside `OS::create_process()`, so nothing registers it in the
// Unix process table. Terminated and reaped on every exit path.
struct UntrackedChild {
	pid_t pid = -1;

	UntrackedChild() {
		pid = fork();
		if (pid == 0) {
			execl("/bin/sleep", "sleep", "120", (char *)nullptr);
			_exit(127);
		}
	}

	~UntrackedChild() { reap(); }

	bool is_valid() const { return pid > 0; }

	void reap() {
		if (pid <= 0) {
			return;
		}
		::kill(pid, SIGKILL);
		int status = 0;
		while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
			// Interrupted before the child could be collected.
		}
		pid = -1;
	}
};

TEST_CASE("[OS] Untracked Unix process liveness answers quietly") {
	const int self_pid = OS::get_singleton()->get_process_id();

	SUBCASE("A live process this instance never spawned is not a running child") {
		UntrackedChild child;
		REQUIRE_MESSAGE(child.is_valid(), "Failed to fork an untracked child.");

		ErrorDetector detector;
		// `is_process_running()` is a child-process API, so a live PID the engine does not
		// own answers `false` rather than becoming a system-wide liveness probe.
		CHECK_FALSE(OS::get_singleton()->is_process_running(child.pid));
		CHECK_FALSE(detector.has_error);

		CHECK_EQ(OS::get_singleton()->get_process_exit_code(child.pid), -1);
		CHECK_FALSE(detector.has_error);

		const pid_t reaped = child.pid;
		child.reap();

		// A completed, never-owned PID keeps answering the same way and stays quiet.
		CHECK_FALSE(OS::get_singleton()->is_process_running(reaped));
		CHECK_FALSE(OS::get_singleton()->is_process_running(reaped));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(reaped), -1);
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("A spawned child still reports liveness and its real exit code") {
		List<String> arguments;
		arguments.push_back("-c");
		arguments.push_back("sleep 0.3; exit 42");
		OS::ProcessID pid = 0;
		REQUIRE_EQ(OS::get_singleton()->create_process("/bin/sh", arguments, &pid), OK);
		REQUIRE(pid != 0);

		ErrorDetector detector;
		CHECK(OS::get_singleton()->is_process_running(pid));

		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + PROCESS_TIMEOUT_MSEC;
		while (OS::get_singleton()->is_process_running(pid) && OS::get_singleton()->get_ticks_msec() < deadline) {
			OS::get_singleton()->delay_usec(PROCESS_POLL_USEC);
		}

		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), 42);
		CHECK_FALSE(detector.has_error);

		OS::get_singleton()->release_finished_process(pid);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("A killed child reports no result and releases its PID") {
		List<String> arguments;
		arguments.push_back("-c");
		arguments.push_back("sleep 120");
		OS::ProcessID pid = 0;
		REQUIRE_EQ(OS::get_singleton()->create_process("/bin/sh", arguments, &pid), OK);
		REQUIRE(pid != 0);
		CHECK(OS::get_singleton()->is_process_running(pid));

		ErrorDetector detector;
		REQUIRE_EQ(OS::get_singleton()->kill(pid), OK);

		// The kill collected the child, so nothing may claim it is still running, and a
		// forced stop is not a natural result a fabricated zero could stand in for.
		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("A tracked child reaped behind the engine's back is still diagnosed") {
		List<String> arguments;
		arguments.push_back("-c");
		arguments.push_back("exit 7");
		OS::ProcessID pid = 0;
		REQUIRE_EQ(OS::get_singleton()->create_process("/bin/sh", arguments, &pid), OK);
		REQUIRE(pid != 0);

		int status = 0;
		while (waitpid((pid_t)pid, &status, 0) == -1 && errno == EINTR) {
			// Interrupted before the child could be collected.
		}

		// The engine still believes it owns this PID, so the failed `waitpid()` is a real
		// bookkeeping problem and must stay diagnostic.
		ErrorDetector detector;
		ERR_PRINT_OFF;
		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		ERR_PRINT_ON;
		CHECK(detector.has_error);

		OS::get_singleton()->release_finished_process(pid);
	}

	// A PID that was never spawned by this instance, including this very process, has no
	// child status to report.
	ErrorDetector detector;
	CHECK_EQ(OS::get_singleton()->get_process_exit_code(self_pid), -1);
	CHECK_FALSE(detector.has_error);
}

#ifdef MACOS_ENABLED

static String process_scratch_root() {
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			return configured.simplify_path();
		}
	}
	return TestUtils::get_temp_path("os_process");
}

// The lowest descriptor the process can currently hand out. It rises whenever a
// descriptor is leaked, which makes it an observable proxy for tracker cleanup.
static int lowest_free_descriptor() {
	const int descriptor = open("/dev/null", O_RDONLY);
	if (descriptor == -1) {
		return -1;
	}
	close(descriptor);
	return descriptor;
}

// A real application bundle generated beneath the shared scratch space. Launch Services
// only reports a process identifier for an application that checks in, so the payload has
// to be a compiled Cocoa binary rather than a script.
struct GeneratedBundleApp {
	String root;
	String app_path;
	String executable_path;
	String build_output;
	int build_exit_code = -1;

	GeneratedBundleApp() {
		root = process_scratch_root()
					   .path_join(vformat("macos_bundle_process_%d", OS::get_singleton()->get_process_id()))
					   .simplify_path();
		remove_recursive(root);

		app_path = root.path_join("BundleProbe.app");
		const String macos_dir = app_path.path_join("Contents/MacOS");
		executable_path = macos_dir.path_join("BundleProbe");
		if (DirAccess::make_dir_recursive_absolute(macos_dir) != OK) {
			return;
		}

		const String fixture_root = TestUtils::get_fixture_path("macos_bundle_process");
		Error error = OK;
		const String plist = FileAccess::get_file_as_string(fixture_root.path_join("Info.plist"), &error);
		if (error != OK) {
			return;
		}
		Ref<FileAccess> plist_file = FileAccess::open(app_path.path_join("Contents/Info.plist"), FileAccess::WRITE);
		if (plist_file.is_null()) {
			return;
		}
		plist_file->store_string(plist);
		plist_file->close();

		List<String> arguments;
		arguments.push_back("-fobjc-arc");
		arguments.push_back("-framework");
		arguments.push_back("AppKit");
		arguments.push_back("-o");
		arguments.push_back(executable_path);
		arguments.push_back(fixture_root.path_join("bundle_probe.m"));
		OS::get_singleton()->execute("/usr/bin/clang", arguments, &build_output, &build_exit_code, true);
	}

	~GeneratedBundleApp() { remove_recursive(root); }

	bool is_built() const { return build_exit_code == 0 && FileAccess::exists(executable_path); }

	String artifact(const String &p_relative_path) const { return root.path_join(p_relative_path); }

	OS::ProcessID launch(int p_exit_code, const String &p_report_path, const String &p_continuation_path) const {
		List<String> arguments;
		arguments.push_back(vformat("--exit-code=%d", p_exit_code));
		arguments.push_back("--report=" + p_report_path);
		if (!p_continuation_path.is_empty()) {
			arguments.push_back("--wait-file=" + p_continuation_path);
		}
		OS::ProcessID pid = 0;
		if (OS::get_singleton()->create_process(app_path, arguments, &pid) != OK) {
			return 0;
		}
		return pid;
	}
};

static void write_marker(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("go");
}

static Dictionary await_report(const String &p_path) {
	Dictionary report;
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + PROCESS_TIMEOUT_MSEC;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (FileAccess::exists(p_path)) {
			const String contents = FileAccess::get_file_as_string(p_path);
			for (const String &line : contents.split("\n", false)) {
				const int separator = line.find_char('=');
				if (separator > 0) {
					report[line.substr(0, separator)] = line.substr(separator + 1).to_int();
				}
			}
			if (report.has("parent_pid")) {
				return report;
			}
		}
		OS::get_singleton()->delay_usec(PROCESS_POLL_USEC);
	}
	return report;
}

static bool await_termination(OS::ProcessID p_pid) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + PROCESS_TIMEOUT_MSEC;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (!OS::get_singleton()->is_process_running(p_pid)) {
			return true;
		}
		OS::get_singleton()->delay_usec(PROCESS_POLL_USEC);
	}
	return false;
}

TEST_CASE("[OS][macOS] macOS bundled process exit status survives the launch lifecycle") {
	GeneratedBundleApp app;
	INFO("Bundle build output:\n", app.build_output);
	REQUIRE_MESSAGE(app.is_built(), "Failed to generate the probe application bundle.");

	ErrorDetector detector;
	const int descriptors_before = lowest_free_descriptor();

	SUBCASE("A live bundled application has no result yet, then reports its own") {
		const String continuation = app.artifact("running.marker");
		const OS::ProcessID pid = app.launch(0, app.artifact("running.report"), continuation);
		REQUIRE_MESSAGE(pid != 0, "Failed to open the probe application bundle.");

		const Dictionary report = await_report(app.artifact("running.report"));
		REQUIRE_MESSAGE(report.has("parent_pid"), "The bundled application never reported its identity.");
		// Launch Services owns the launch, so the application cannot be a fork of this
		// process. That is what distinguishes the bundle route from `OS_Unix`.
		CHECK_EQ(int(report["pid"]), (int)pid);
		CHECK_NE(int(report["parent_pid"]), OS::get_singleton()->get_process_id());

		for (int i = 0; i < 5; i++) {
			CHECK(OS::get_singleton()->is_process_running(pid));
			CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
			OS::get_singleton()->delay_usec(PROCESS_POLL_USEC);
		}

		write_marker(continuation);
		REQUIRE(await_termination(pid));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), 0);
		CHECK_FALSE(detector.has_error);

		// The result stays available until it is released, and disappears afterwards.
		OS::get_singleton()->release_finished_process(pid);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("A distinctive nonzero result is decoded and cached") {
		const String continuation = app.artifact("nonzero.marker");
		const OS::ProcessID pid = app.launch(42, app.artifact("nonzero.report"), continuation);
		REQUIRE_MESSAGE(pid != 0, "Failed to open the probe application bundle.");
		REQUIRE(await_report(app.artifact("nonzero.report")).has("parent_pid"));

		write_marker(continuation);
		REQUIRE(await_termination(pid));

		for (int i = 0; i < 3; i++) {
			CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), 42);
			CHECK_FALSE(OS::get_singleton()->is_process_running(pid));
		}
		CHECK_FALSE(detector.has_error);

		OS::get_singleton()->release_finished_process(pid);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
	}

	SUBCASE("A second launch cannot consume the first launch's result") {
		const String first_continuation = app.artifact("first.marker");
		const OS::ProcessID first = app.launch(42, app.artifact("first.report"), first_continuation);
		REQUIRE_MESSAGE(first != 0, "Failed to open the probe application bundle.");
		REQUIRE(await_report(app.artifact("first.report")).has("parent_pid"));
		write_marker(first_continuation);
		REQUIRE(await_termination(first));

		// The first result is deliberately left unreleased while the next launch runs.
		const String second_continuation = app.artifact("second.marker");
		const OS::ProcessID second = app.launch(0, app.artifact("second.report"), second_continuation);
		REQUIRE_MESSAGE(second != 0, "Failed to open the probe application bundle.");
		CHECK_NE(second, first);
		REQUIRE(await_report(app.artifact("second.report")).has("parent_pid"));

		CHECK(OS::get_singleton()->is_process_running(second));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(second), -1);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(first), 42);

		write_marker(second_continuation);
		REQUIRE(await_termination(second));
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(second), 0);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(first), 42);
		CHECK_FALSE(detector.has_error);

		OS::get_singleton()->release_finished_process(first);
		OS::get_singleton()->release_finished_process(second);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(first), -1);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(second), -1);
	}

	SUBCASE("A short-lived bundled application never lingers as running") {
		// Launch Services reports the process identifier asynchronously, so an application
		// that ends immediately can outrun its own status registration.
		const OS::ProcessID pid = app.launch(0, app.artifact("short.report"), String());
		REQUIRE_MESSAGE(pid != 0, "Failed to open the probe application bundle.");
		REQUIRE(await_termination(pid));

		// Losing that race degrades to the unavailable sentinel. It must never surface a
		// value the application did not return, and never leave the PID reported as alive.
		const int result = OS::get_singleton()->get_process_exit_code(pid);
		CHECK((result == 0 || result == -1));
		CHECK_FALSE(OS::get_singleton()->is_process_running(pid));

		OS::get_singleton()->release_finished_process(pid);
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("A forced stop cleans up without exposing a natural result") {
		const OS::ProcessID pid = app.launch(9, app.artifact("forced.report"), app.artifact("never.marker"));
		REQUIRE_MESSAGE(pid != 0, "Failed to open the probe application bundle.");
		REQUIRE(await_report(app.artifact("forced.report")).has("parent_pid"));
		CHECK(OS::get_singleton()->is_process_running(pid));

		REQUIRE_EQ(OS::get_singleton()->kill(pid), OK);
		REQUIRE(await_termination(pid));
		// A killed application produced no trustworthy result, and its configured exit
		// code was never reached.
		CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		CHECK_FALSE(detector.has_error);
	}

	SUBCASE("Repeated launch cycles release every tracked descriptor") {
		const int descriptors_at_start = lowest_free_descriptor();
		for (int cycle = 0; cycle < 4; cycle++) {
			INFO("Cycle: ", cycle);
			const String continuation = app.artifact(vformat("cycle_%d.marker", cycle));
			const OS::ProcessID pid = app.launch(5, app.artifact(vformat("cycle_%d.report", cycle)), continuation);
			REQUIRE_MESSAGE(pid != 0, "Failed to open the probe application bundle.");
			REQUIRE(await_report(app.artifact(vformat("cycle_%d.report", cycle))).has("parent_pid"));
			write_marker(continuation);
			REQUIRE(await_termination(pid));
			CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), 5);
			OS::get_singleton()->release_finished_process(pid);
			CHECK_EQ(OS::get_singleton()->get_process_exit_code(pid), -1);
		}
		CHECK_EQ(lowest_free_descriptor(), descriptors_at_start);
		CHECK_FALSE(detector.has_error);
	}

	CHECK_EQ(lowest_free_descriptor(), descriptors_before);
}

#endif // MACOS_ENABLED

} // namespace TestOSProcess

#endif // UNIX_ENABLED
