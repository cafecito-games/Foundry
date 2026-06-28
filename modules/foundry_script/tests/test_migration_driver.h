/**************************************************************************/
/*  test_migration_driver.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "tests/test_macros.h"

#include "../editor/fs_migration_driver.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "test_refactor.h" // FSTests::TemporaryScriptFile

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

// Manages an isolated `res://` subtree the driver can scan without touching the shared
// fixtures under `res://refactor/`. The directory is created on construction and removed
// recursively on destruction so each run starts and ends clean, and scanning it never picks
// up unrelated checked-in scripts.
struct TemporaryProjectSubtree {
	String root; // A `res://` directory unique to one test.

	explicit TemporaryProjectSubtree(const String &p_root) {
		root = p_root;
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryProjectSubtree() {
		remove_recursive(ProjectSettings::get_singleton()->globalize_path(root));
	}

	// Writes p_contents to root/p_relative_path, creating intermediate directories as needed,
	// and returns the full `res://` path so the caller can assert on it later.
	String write_file(const String &p_relative_path, const String &p_contents) const {
		const String resource_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		REQUIRE_EQ(dir->make_dir_recursive(resource_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(resource_path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", resource_path));
		file->store_string(p_contents);
		return resource_path;
	}

	static void remove_recursive(const String &p_absolute_path) {
		Ref<DirAccess> dir = DirAccess::open(p_absolute_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_absolute_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_absolute_path);
	}
};

// Options that skip the version-control safety guard. The inference-focused tests run
// against a `res://` subtree inside the engine's own (often dirty) checkout, so the guard
// would otherwise block them; it has dedicated coverage in its own test cases below.
static MigrationDriverOptions unguarded_options() {
	MigrationDriverOptions options;
	options.enforce_vcs_safety_guard = false;
	return options;
}

static bool result_contains_file(const Vector<String> &p_files, const String &p_path) {
	for (const String &file : p_files) {
		if (file == p_path) {
			return true;
		}
	}
	return false;
}

static const FixpointFileChange *change_for(const MigrationDriverResult &p_result, const String &p_path) {
	for (const FixpointFileChange &change : p_result.changed_files) {
		if (change.path == p_path) {
			return &change;
		}
	}
	return nullptr;
}

TEST_SUITE("[Modules][FoundryScript][MigrationDriver]") {
	TEST_CASE("Driver scans a project and drives it to a stable, verified, deterministic edit set") {
		// Initialize the FoundryScript test project so res:// resolves to the test scripts directory
		// and cross-file analysis can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_basic");

		// C is the leaf; B relays C; A reads B. Only a dependency-ordered fixpoint types the
		// whole chain, so a passing run proves the driver orders the work correctly.
		const String path_c = tree.write_file("chain_c.fs",
				"static func value():\n"
				"\treturn 42\n");
		const String path_b = tree.write_file("chain_b.fs",
				"const C = preload(\"res://migration_driver_basic/chain_c.fs\")\n"
				"static func relay():\n"
				"\treturn C.value()\n");
		const String path_a = tree.write_file("chain_a.fs",
				"const B = preload(\"res://migration_driver_basic/chain_b.fs\")\n"
				"var x = B.relay()\n");

		const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_basic", unguarded_options());
		REQUIRE(result.ok);
		CHECK(result.converged);

		// The scan stage found exactly the three scripts, and the report exposes them.
		CHECK_EQ(result.scanned_files.size(), 3);
		CHECK(result_contains_file(result.scanned_files, path_a));
		CHECK(result_contains_file(result.scanned_files, path_b));
		CHECK(result_contains_file(result.scanned_files, path_c));

		// The whole chain is typed on disk.
		CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
		CHECK(FileAccess::get_file_as_string(path_b).contains("static func relay() -> int:"));
		CHECK(FileAccess::get_file_as_string(path_a).contains("var x: int = B.relay()"));

		// The edit set covers all three files and the per-file counts sum to the total.
		CHECK_EQ(result.changed_files.size(), 3);
		int summed = 0;
		for (const FixpointFileChange &change : result.changed_files) {
			CHECK(change.annotations_applied > 0);
			summed += change.annotations_applied;
		}
		CHECK_EQ(summed, result.total_annotations_applied);

		// Determinism / stability: a second run over the now-typed project changes nothing.
		const MigrationDriverResult second = FSMigrationDriver::run("res://migration_driver_basic", unguarded_options());
		REQUIRE(second.ok);
		CHECK(second.converged);
		CHECK_EQ(second.changed_files.size(), 0);
		CHECK_EQ(second.total_annotations_applied, 0);
		// The scan is deterministically ordered, so both runs enumerate the same list.
		CHECK_EQ(second.scanned_files, result.scanned_files);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver excludes third-party code by default and reports it as skipped") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_addons");

		const String path_main = tree.write_file("main.fs",
				"static func value():\n"
				"\treturn 1\n");
		// An addons script that is independently typeable; the driver must not rewrite it.
		const String path_plugin = tree.write_file("addons/vendor/plugin.fs",
				"static func value():\n"
				"\treturn 2\n");
		const String plugin_before = FileAccess::get_file_as_string(path_plugin);

		const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_addons", unguarded_options());
		REQUIRE(result.ok);
		CHECK(result.converged);

		// The ordinary script is considered and typed; the addons script is neither scanned
		// nor changed, and its directory is reported as skipped (honest reporting).
		CHECK(result_contains_file(result.scanned_files, path_main));
		CHECK_FALSE(result_contains_file(result.scanned_files, path_plugin));
		CHECK(FileAccess::get_file_as_string(path_main).contains("static func value() -> int:"));
		CHECK_EQ(FileAccess::get_file_as_string(path_plugin), plugin_before);
		CHECK(change_for(result, path_plugin) == nullptr);

		bool addons_skipped = false;
		for (const String &dir : result.skipped_directories) {
			if (dir.contains("addons")) {
				addons_skipped = true;
			}
		}
		CHECK(addons_skipped);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver reports a scanned file it cannot analyze instead of dropping it") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_unanalyzable");

		// A typeable leaf alongside a file with a malformed signature that fails to parse, so
		// analysis never succeeds. An all-zero edit set on such a project must not read as a
		// clean run: the unanalyzable file is reported explicitly (epic #29: honest reporting).
		const String path_good = tree.write_file("good.fs",
				"static func value():\n"
				"\treturn 1\n");
		const String path_broken = tree.write_file("broken.fs",
				"func broken( ->:\n"
				"\tpass\n");

		// Scanning the malformed sibling emits an expected "Parse Error" script error;
		// silence it so the deliberate bad input doesn't pollute the test log.
		ERR_PRINT_OFF;
		const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_unanalyzable", unguarded_options());
		ERR_PRINT_ON;
		REQUIRE(result.ok);

		// The analyzable leaf is still typed despite the broken sibling.
		CHECK(FileAccess::get_file_as_string(path_good).contains("static func value() -> int:"));

		// The broken file is scanned and reported as unanalyzed, exactly once, with a reason;
		// the good leaf is analyzable and absent from the list.
		CHECK(result_contains_file(result.scanned_files, path_broken));
		int broken_unanalyzed = 0;
		int good_unanalyzed = 0;
		for (const FixpointUnanalyzed &unanalyzed : result.unanalyzed_files) {
			if (unanalyzed.path == path_broken) {
				broken_unanalyzed++;
				CHECK_FALSE(unanalyzed.reason.is_empty());
			} else if (unanalyzed.path == path_good) {
				good_unanalyzed++;
			}
		}
		CHECK_EQ(broken_unanalyzed, 1);
		CHECK_EQ(good_unanalyzed, 0);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver does not commit an edit that would break a dependent and reports the skip") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_verify");

		const String provider_path = tree.write_file("provider.fs",
				"func get_value():\n"
				"\treturn 42\n");
		// The consumer assigns the provider's result to a String local, so giving the provider a
		// concrete `-> int` return type would introduce a new error in this dependent. The
		// verification harness must reject that edit and the driver must report it as a skip.
		const String consumer_path = tree.write_file("consumer.fs",
				"const Provider = preload(\"res://migration_driver_verify/provider.fs\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n");
		const String provider_before = FileAccess::get_file_as_string(provider_path);
		const String consumer_before = FileAccess::get_file_as_string(consumer_path);

		const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_verify", unguarded_options());
		REQUIRE(result.ok);
		CHECK(result.converged);

		// The breaking return-type edit was never committed, and the already-typed consumer is
		// left untouched.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_before);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_before);

		bool provider_skipped = false;
		for (const FixpointSkipped &skip : result.skipped) {
			if (skip.path == provider_path) {
				provider_skipped = true;
			}
		}
		CHECK(provider_skipped);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver edge cases: empty project and unreadable root") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		SUBCASE("an empty project is a successful, converged no-op") {
			TemporaryProjectSubtree tree("res://migration_driver_empty");
			const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_empty");
			CHECK(result.ok);
			CHECK(result.converged);
			CHECK_EQ(result.scanned_files.size(), 0);
			CHECK_EQ(result.changed_files.size(), 0);
			CHECK_EQ(result.total_annotations_applied, 0);
		}

		SUBCASE("an unreadable root fails fatally without changes") {
			const MigrationDriverResult result = FSMigrationDriver::run("res://migration_driver_does_not_exist");
			CHECK_FALSE(result.ok);
			CHECK_FALSE(result.error_message.is_empty());
			CHECK_EQ(result.changed_files.size(), 0);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver's VCS safety guard blocks writes on a dirty working tree, then proceeds once acknowledged (issue #42)") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_vcs_guard");

		const String path = tree.write_file("player.fs",
				"static func value():\n"
				"\treturn 1\n");
		const String before = FileAccess::get_file_as_string(path);

		// Inject a dirty working-tree state so the guard's block/acknowledge handling is
		// exercised deterministically, without inspecting the engine's own res:// checkout
		// or spawning git (which would make this test depend on the CI environment's git
		// state). The guard's own git-inspection logic is covered separately by the pure
		// evaluate() and inspect_project() unit tests.
		ScriptRefactorVCSGuard::WorkingTreeState dirty_state;
		dirty_state.git_available = true;
		dirty_state.inside_work_tree = true;
		dirty_state.git_status_exit_code = 0;
		dirty_state.git_status_output = " M player.fs\n"; // Non-empty porcelain output => DIRTY.

		MigrationDriverOptions guarded;
		guarded.enforce_vcs_safety_guard = true;
		guarded.vcs_state_override = &dirty_state;
		const MigrationDriverResult blocked = FSMigrationDriver::run("res://migration_driver_vcs_guard", guarded);

		CHECK(blocked.blocked_by_vcs_guard);
		CHECK_FALSE(blocked.ok);
		CHECK_EQ(blocked.vcs_guard.status, ScriptRefactorVCSGuard::Status::DIRTY);
		CHECK(blocked.vcs_guard.should_warn());
		CHECK_FALSE(blocked.error_message.is_empty());
		// No file was modified while blocked.
		CHECK_EQ(FileAccess::get_file_as_string(path), before);
		CHECK_EQ(blocked.changed_files.size(), 0);
		CHECK_EQ(blocked.total_annotations_applied, 0);

		// Acknowledging the warning lets the same run proceed and apply edits.
		MigrationDriverOptions acknowledged = guarded;
		acknowledged.acknowledge_vcs_warning = true;
		const MigrationDriverResult proceeded = FSMigrationDriver::run("res://migration_driver_vcs_guard", acknowledged);
		CHECK(proceeded.ok);
		CHECK_FALSE(proceeded.blocked_by_vcs_guard);
		CHECK(FileAccess::get_file_as_string(path).contains("static func value() -> int:"));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Driver's VCS safety guard proceeds on a clean working tree without consulting git (issue #42)") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_driver_vcs_guard_clean");

		const String path = tree.write_file("player.fs",
				"static func value():\n"
				"\treturn 1\n");

		// Inject a clean working-tree state: git ran, the project is inside a work tree, and
		// `git status --porcelain` produced no output. The guard returns SAFE and the run
		// proceeds to type the file, all without spawning git.
		ScriptRefactorVCSGuard::WorkingTreeState clean_state;
		clean_state.git_available = true;
		clean_state.inside_work_tree = true;
		clean_state.git_status_exit_code = 0;
		clean_state.git_status_output = ""; // Empty porcelain output => SAFE.

		MigrationDriverOptions guarded;
		guarded.enforce_vcs_safety_guard = true;
		guarded.vcs_state_override = &clean_state;
		const MigrationDriverResult proceeded = FSMigrationDriver::run("res://migration_driver_vcs_guard_clean", guarded);

		CHECK(proceeded.ok);
		CHECK_FALSE(proceeded.blocked_by_vcs_guard);
		CHECK_EQ(proceeded.vcs_guard.status, ScriptRefactorVCSGuard::Status::SAFE);
		CHECK(FileAccess::get_file_as_string(path).contains("static func value() -> int:"));

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace FSTests

#endif // !FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
