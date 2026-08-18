/**************************************************************************/
/*  test_foundry_cli_benchmark_user_root.h                              */
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

// The benchmark verb exits with a failure when the Foundry Script module is disabled, so
// these tests only describe behavior a module-enabled build has.
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED

namespace TestFoundryCLIBenchmarkUserRoot {

// A benchmark run owns the `user://` root it is given, and recreates it clean at startup. These
// tests drive real `foundry test benchmark` subprocesses, since the collision they pin only
// exists between processes.

static String scratch_root() {
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		const String configured = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		if (!configured.is_empty()) {
			return configured.simplify_path();
		}
	}
	return OS::get_singleton()->get_temp_path().simplify_path();
}

static void remove_recursive(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return;
	}
	dir->set_include_hidden(true);
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		const String child = p_path.path_join(entry);
		if (dir->current_is_dir()) {
			remove_recursive(child);
		} else {
			DirAccess::remove_absolute(child);
		}
	}
	dir->list_dir_end();
	DirAccess::remove_absolute(p_path);
}

static void write_scratch_file(const String &p_path, const String &p_contents) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	REQUIRE_EQ(dir->make_dir_recursive(p_path.get_base_dir()), OK);
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", p_path));
	file->store_string(p_contents);
}

static Vector<String> list_directories(const String &p_path) {
	Vector<String> directories;
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return directories;
	}
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		if (dir->current_is_dir()) {
			directories.push_back(p_path.path_join(entry));
		}
	}
	dir->list_dir_end();
	directories.sort();
	return directories;
}

static bool contains_file_named(const String &p_directory, const String &p_file_name) {
	Ref<DirAccess> dir = DirAccess::open(p_directory);
	if (dir.is_null()) {
		return false;
	}
	Vector<String> nested;
	bool found = false;
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		if (dir->current_is_dir()) {
			nested.push_back(p_directory.path_join(entry));
		} else if (entry == p_file_name) {
			found = true;
		}
	}
	dir->list_dir_end();
	for (const String &child : nested) {
		found = found || contains_file_named(child, p_file_name);
	}
	return found;
}

// Writes a one-variant benchmark case whose workload does nothing but return, so a run over it
// finishes as soon as the engine is up.
static String write_quick_case(const String &p_corpus_root) {
	write_scratch_file(p_corpus_root.path_join("quick/case.cfg"),
			"[case]\niterations=1\nwarmup=0\noverhead_threshold_percent=-1\n");
	write_scratch_file(p_corpus_root.path_join("quick/quick.fs"),
			"extends RefCounted\n\n"
			"func run_benchmark(iterations: int) -> void:\n"
			"\tvar accumulator: int = 0\n"
			"\tfor index in iterations:\n"
			"\t\taccumulator += index\n");
	return p_corpus_root;
}

// Writes a one-variant benchmark case whose workload blocks until the harness creates the
// release file, so a test can hold the run alive while it inspects the `user://` tree it owns.
static String write_blocking_case(const String &p_corpus_root) {
	write_scratch_file(p_corpus_root.path_join("blocking/case.cfg"),
			"[case]\niterations=1\nwarmup=0\noverhead_threshold_percent=-1\n");
	write_scratch_file(p_corpus_root.path_join("blocking/blocking.fs"),
			"extends RefCounted\n\n"
			"func run_benchmark(iterations: int) -> void:\n"
			"\tvar release_path: String = OS.get_environment(\"FOUNDRY_BENCHMARK_RELEASE_FILE\")\n"
			"\tif release_path.is_empty():\n"
			"\t\treturn\n"
			"\tvar deadline: int = int(Time.get_ticks_msec()) + 120000\n"
			"\twhile int(Time.get_ticks_msec()) < deadline:\n"
			"\t\tif FileAccess.file_exists(release_path):\n"
			"\t\t\treturn\n"
			"\t\tOS.delay_msec(20)\n");
	return p_corpus_root;
}

struct BenchmarkProcess {
	OS::ProcessID pid = 0;
	Ref<FileAccess> stdout_pipe;
	Ref<FileAccess> stderr_pipe;
	Vector<uint8_t> output_bytes;

	bool is_started() const {
		return pid != 0;
	}

	bool is_running() const {
		return is_started() && OS::get_singleton()->is_process_running(pid);
	}

	// Drains both pipes so a chatty child never blocks on a full pipe buffer.
	void pump() {
		drain(stdout_pipe);
		drain(stderr_pipe);
	}

	// Pumps until the process exits or the timeout elapses. Returns false on timeout.
	bool wait_for_exit(uint64_t p_timeout_msec, int &r_exit_code) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
		while (OS::get_singleton()->get_ticks_msec() < deadline) {
			pump();
			if (!OS::get_singleton()->is_process_running(pid)) {
				pump();
				close_pipes();
				r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
				return true;
			}
			OS::get_singleton()->delay_usec(20000);
		}
		return false;
	}

	void terminate() {
		if (is_running()) {
			OS::get_singleton()->kill(pid);
		}
		close_pipes();
	}

	String output() const {
		return String::utf8((const char *)output_bytes.ptr(), output_bytes.size());
	}

private:
	void close_pipes() {
		if (stdout_pipe.is_valid()) {
			stdout_pipe->close();
		}
		if (stderr_pipe.is_valid()) {
			stderr_pipe->close();
		}
	}

	void drain(const Ref<FileAccess> &p_pipe) {
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
			const int offset = output_bytes.size();
			output_bytes.resize(offset + read);
			memcpy(output_bytes.ptrw() + offset, chunk.ptr(), read);
		}
	}
};

static BenchmarkProcess launch_benchmark(const String &p_corpus_dir, const String &p_output_path,
		const String &p_user_scratch, const String &p_release_path) {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("test");
	arguments.push_back("benchmark");
	arguments.push_back(p_corpus_dir);
	arguments.push_back("--output");
	arguments.push_back(p_output_path);

	Dictionary environment;
	environment["FOUNDRY_TEST_SCRATCH"] = p_user_scratch;
	if (!p_release_path.is_empty()) {
		environment["FOUNDRY_BENCHMARK_RELEASE_FILE"] = p_release_path;
	}

	BenchmarkProcess process;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), arguments, false, String(), environment, false);
	if (pipe_info.is_empty()) {
		return process;
	}
	process.stdout_pipe = pipe_info["stdio"];
	process.stderr_pipe = pipe_info["stderr"];
	process.pid = pipe_info["pid"];
	return process;
}

TEST_CASE("[FoundryCLI][TestBenchmark] A benchmark run started next to another keeps its own user data tree") {
	const String base = scratch_root().path_join(vformat("foundry-cli-benchmark-user-root-%d", OS::get_singleton()->get_process_id()));
	remove_recursive(base);

	const String user_scratch = base.path_join("user-roots");
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	REQUIRE_EQ(dir->make_dir_recursive(user_scratch), OK);
	const String release_path = base.path_join("release.txt");
	const String blocking_corpus = write_blocking_case(base.path_join("blocking-corpus"));
	const String quick_corpus = write_quick_case(base.path_join("quick-corpus"));

	BenchmarkProcess first = launch_benchmark(blocking_corpus, "user://benchmark.json", user_scratch, release_path);
	REQUIRE_MESSAGE(first.is_started(), "The first benchmark run must start");

	// The first run's `user://` root is whatever directory it creates under the shared scratch;
	// the test never assumes its name.
	String first_root;
	const uint64_t root_deadline = OS::get_singleton()->get_ticks_msec() + 120000;
	while (OS::get_singleton()->get_ticks_msec() < root_deadline) {
		first.pump();
		const Vector<String> roots = list_directories(user_scratch);
		if (!roots.is_empty()) {
			first_root = roots[0];
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	if (first_root.is_empty()) {
		first.terminate();
		remove_recursive(base);
		FAIL("The first benchmark run never created a user-data root");
		return;
	}

	const String sibling_owned = first_root.path_join("sibling-owned.txt");
	write_scratch_file(sibling_owned, "first-run-owned");

	BenchmarkProcess second = launch_benchmark(quick_corpus, "user://benchmark.json", user_scratch, String());
	REQUIRE_MESSAGE(second.is_started(), "The second benchmark run must start");
	int second_exit_code = -1;
	const bool second_exited = second.wait_for_exit(240000, second_exit_code);
	INFO("Second run output:\n", second.output());
	CHECK_MESSAGE(second_exited, "The second benchmark run must finish");
	CHECK_EQ(second_exit_code, 0);

	// The whole point of the isolation: the first run is still alive, so the second run's
	// clean-at-startup must not have reached into its tree.
	CHECK_MESSAGE(first.is_running(), "The first benchmark run must still be running while the second one finishes");
	CHECK_MESSAGE(FileAccess::exists(sibling_owned), "A concurrent benchmark run must not erase the first run's user:// tree");

	const Vector<String> roots_after_second = list_directories(user_scratch);
	CHECK_MESSAGE(roots_after_second.size() == 2, "Two concurrent benchmark runs must own two separate user-data roots");

	write_scratch_file(release_path, "release");
	int first_exit_code = -1;
	const bool first_exited = first.wait_for_exit(240000, first_exit_code);
	INFO("First run output:\n", first.output());
	CHECK_MESSAGE(first_exited, "The first benchmark run must finish once released");
	CHECK_EQ(first_exit_code, 0);
	CHECK_MESSAGE(FileAccess::exists(sibling_owned), "The released first run must still own its user:// tree");
	// A `user://` artifact is what the run was asked to produce, so its root survives the run.
	CHECK_MESSAGE(contains_file_named(first_root, "benchmark.json"), "A user:// benchmark artifact must survive the run that produced it");

	first.terminate();
	second.terminate();
	remove_recursive(base);
}

TEST_CASE("[FoundryCLI][TestBenchmark] A filesystem output leaves no user data directory behind") {
	const String base = scratch_root().path_join(vformat("foundry-cli-benchmark-user-root-cleanup-%d", OS::get_singleton()->get_process_id()));
	remove_recursive(base);

	const String user_scratch = base.path_join("user-roots");
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	REQUIRE_EQ(dir->make_dir_recursive(user_scratch), OK);
	const String quick_corpus = write_quick_case(base.path_join("quick-corpus"));
	const String artifact_path = base.path_join("benchmark.json");

	BenchmarkProcess process = launch_benchmark(quick_corpus, artifact_path, user_scratch, String());
	REQUIRE_MESSAGE(process.is_started(), "The benchmark run must start");
	int exit_code = -1;
	const bool exited = process.wait_for_exit(240000, exit_code);
	INFO("Benchmark run output:\n", process.output());
	CHECK_MESSAGE(exited, "The benchmark run must finish");
	CHECK_EQ(exit_code, 0);
	CHECK_MESSAGE(FileAccess::exists(artifact_path), "The requested artifact must be written");
	CHECK_MESSAGE(list_directories(user_scratch).is_empty(), "A run whose artifact lives outside its user-data root must not leave that root behind");

	process.terminate();
	remove_recursive(base);
}

} // namespace TestFoundryCLIBenchmarkUserRoot

#endif // MODULE_FOUNDRY_SCRIPT_ENABLED
