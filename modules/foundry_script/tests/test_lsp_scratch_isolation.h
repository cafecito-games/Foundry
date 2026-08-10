/**************************************************************************/
/*  test_lsp_scratch_isolation.h                                          */
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

#ifdef TOOLS_ENABLED

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "tests/test_macros.h"

#include "fs_temporary_project_tree.h"
#include "test_lsp.h" // FSTests::LSP_SCRATCH_BARRIER_* environment seam.

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

namespace FSTests {

// The three `[Modules][FoundryScript][LSP scratch]` contract cases each install an explicit
// `FOUNDRY_TEST_SCRATCH` override and stage the fixture project below it. Sharded CI runs place them
// in separate processes, so a process-independent override made those processes remove and re-copy
// one another's staged project mid-run.
//
// This case reproduces that arrangement directly: three real `foundry` processes, one contract case
// each, all handed the same parent scratch directory and released together through a filesystem
// barrier. It asserts on process outcomes and on the directories the children actually produced, so
// it goes red against a process-independent contract root and green once each process owns its own.
#ifdef UNIX_ENABLED

namespace {

struct ConcurrentContractChild {
	String case_name;
	String log_path;
	OS::ProcessID process_id = 0;
	bool launched = false;
};

constexpr int CONCURRENT_CONTRACT_CHILD_COUNT = 3;
constexpr uint64_t CONCURRENT_CONTRACT_TIMEOUT_MILLISECONDS = 10 * 60 * 1000;
constexpr const char *CONTRACT_ROOT_PREFIX = "foundry_lsp_scratch_contract_";

Vector<String> list_directory_entries(const String &p_directory) {
	Vector<String> entries;
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	if (directory.is_null()) {
		return entries;
	}
	directory->set_include_hidden(true);
	directory->list_dir_begin();
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		entries.push_back(entry);
	}
	directory->list_dir_end();
	return entries;
}

} // namespace

TEST_CASE("[Modules][FoundryScript][Scratch isolation] concurrent contract processes stage independent roots") {
	OS *os = OS::get_singleton();
	const String executable_path = os->get_executable_path();
	if (executable_path.is_empty() || !FileAccess::exists(executable_path)) {
		return;
	}

	TemporaryProjectTree sandbox("foundry_lsp_scratch_isolation");
	REQUIRE(sandbox.is_valid());
	if (!sandbox.is_valid()) {
		return;
	}

	const String shared_scratch = sandbox.root.path_join("shared_scratch");
	const String barrier_directory = sandbox.root.path_join("barrier");
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(shared_scratch), OK);
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(barrier_directory), OK);

	ConcurrentContractChild children[CONCURRENT_CONTRACT_CHILD_COUNT];
	children[0].case_name = "[Modules][FoundryScript][LSP scratch] test project root is staged under scratch";
	children[1].case_name = "[Modules][FoundryScript][LSP scratch] temp files resolve inside staged project";
	children[2].case_name = "[Modules][FoundryScript][LSP scratch] temp file cleanup uses original resolved path after language teardown";

	for (int index = 0; index < CONCURRENT_CONTRACT_CHILD_COUNT; index++) {
		ConcurrentContractChild &child = children[index];
		child.log_path = sandbox.root.path_join(vformat("child_%d.log", index));
		// Each child writes straight to its own file instead of a pipe, so a chatty run can never
		// block on a full pipe buffer while this process is waiting for it to exit.
		const String command = vformat(
				"FOUNDRY_TEST_SCRATCH='%s' %s='%s' %s='%d' exec '%s' --headless test run --case '%s' > '%s' 2>&1",
				shared_scratch,
				LSP_SCRATCH_BARRIER_DIRECTORY_ENV, barrier_directory,
				LSP_SCRATCH_BARRIER_COUNT_ENV, CONCURRENT_CONTRACT_CHILD_COUNT,
				executable_path, child.case_name, child.log_path);
		List<String> arguments;
		arguments.push_back("-c");
		arguments.push_back(command);
		const Error launch_error = os->create_process("/bin/sh", arguments, &child.process_id);
		CHECK_MESSAGE(launch_error == OK, vformat("Cannot launch a child for '%s'", child.case_name));
		child.launched = launch_error == OK;
	}

	const uint64_t deadline = os->get_ticks_msec() + CONCURRENT_CONTRACT_TIMEOUT_MILLISECONDS;
	bool all_finished = false;
	while (!all_finished && os->get_ticks_msec() < deadline) {
		all_finished = true;
		for (const ConcurrentContractChild &child : children) {
			if (child.launched && os->is_process_running(child.process_id)) {
				all_finished = false;
			}
		}
		if (!all_finished) {
			os->delay_usec(50000);
		}
	}
	CHECK_MESSAGE(all_finished, "Concurrent contract children did not all finish within the timeout.");

	for (ConcurrentContractChild &child : children) {
		if (!child.launched) {
			continue;
		}
		if (os->is_process_running(child.process_id)) {
			os->kill(child.process_id);
			CHECK_MESSAGE(false, vformat("Child for '%s' was still running and has been terminated.", child.case_name));
			continue;
		}
		const int exit_code = os->get_process_exit_code(child.process_id);
		const String output = FileAccess::get_file_as_string(child.log_path);
		// A passing run can still exit non-zero while reporting leaks during cleanup, so the doctest
		// summary is the verdict. The exit code is reported only to make a crash legible.
		CHECK_MESSAGE(output.contains("[doctest] Status: SUCCESS!"),
				vformat("Child for '%s' exited with code %d without reporting a passing run:\n%s", child.case_name, exit_code, output));
	}

	// Passing children are not on their own proof of isolation, because a lucky interleaving can let
	// processes share a root and still succeed. The produced directories are: every child must have
	// staged below the parent scratch it was given, under a root no sibling also resolved.
	int contract_root_count = 0;
	const Vector<String> shared_entries = list_directory_entries(shared_scratch);
	for (const String &entry : shared_entries) {
		if (entry.begins_with(CONTRACT_ROOT_PREFIX)) {
			contract_root_count++;
		}
	}
	CHECK_MESSAGE(contract_root_count == CONCURRENT_CONTRACT_CHILD_COUNT,
			vformat("Expected %d distinct contract scratch roots under '%s', found %d: %s",
					CONCURRENT_CONTRACT_CHILD_COUNT, shared_scratch, contract_root_count, String(", ").join(shared_entries)));
}

#endif // UNIX_ENABLED

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
