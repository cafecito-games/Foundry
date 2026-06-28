/**************************************************************************/
/*  test_migration_wizard.h                                               */
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

#include "../editor/fs_migration_wizard.h"
#include "../editor/fs_migration_wizard_plugin.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"

#include "test_migration_driver.h" // FSTests::TemporaryProjectSubtree, unguarded_options

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][MigrationWizard][Editor]") {
	TEST_CASE("Dialog class exposes the inherited confirmation signal") {
		CHECK(ClassDB::class_exists(FSMigrationWizardDialog::get_class_static()));
		CHECK(ClassDB::has_signal(FSMigrationWizardDialog::get_class_static(), SNAME("confirmed")));
	}
}

} // namespace FSTests

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

// Wizard options that share the driver tests' unguarded stance: the wizard runs against a
// `res://` subtree inside the engine's own (often dirty) checkout, so the version-control guard
// would otherwise block the apply stage. The guard has dedicated coverage in the driver tests.
static MigrationWizardOptions unguarded_wizard_options() {
	MigrationWizardOptions options;
	options.enforce_vcs_safety_guard = false;
	return options;
}

// Reads a `res://` file's full text so tests can assert the wizard actually rewrote (or left
// untouched) the source on disk.
static String read_resource_file(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot read '%s'", p_path));
	return file->get_as_text();
}

TEST_SUITE("[Modules][FoundryScript][MigrationWizard]") {
	TEST_CASE("Preview-only run reports coverage and writes nothing") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_wizard_preview");
		const String path = tree.write_file("value.fs",
				"static func value():\n"
				"\treturn 42\n");
		const String before = read_resource_file(path);

		// Default options: apply is off, so the wizard is a pure preview.
		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_preview", unguarded_wizard_options());
		REQUIRE(result.ok);
		CHECK(result.succeeded());
		CHECK(result.report.ok);
		CHECK_FALSE(result.applied);
		CHECK_FALSE(result.strict_activated);
		// A preview-only run uses the truly read-only single-pass snapshot, not the
		// write-then-restore projection.
		CHECK_FALSE(result.report.projection);
		// The single-pass report counts the site the apply would type.
		CHECK_GT(result.report.inferable.total, 0);

		// Nothing on disk changed.
		CHECK_EQ(read_resource_file(path), before);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Apply run commits the inferred annotations to disk") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_wizard_apply");
		const String path = tree.write_file("typed.fs",
				"static func value():\n"
				"\treturn 42\n"
				"var x = value()\n");

		MigrationWizardOptions options = unguarded_wizard_options();
		options.apply = true;

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_apply", options);
		REQUIRE(result.ok);
		CHECK(result.applied);
		// The report stays the read-only single-pass snapshot even for an apply run; the
		// ground-truth committed edit set comes from the guarded apply stage, not an unguarded
		// projection.
		CHECK_FALSE(result.report.projection);
		CHECK(result.apply_result.ok);
		CHECK_GT(result.apply_result.total_annotations_applied, 0);

		// The on-disk file now carries an explicit annotation the apply inferred.
		const String after = read_resource_file(path);
		CHECK(after.contains(": int"));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Strict request is evaluated without flipping settings when activation is off") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const Variant prior_null = ProjectSettings::get_singleton()->get_setting("debug/foundry_script/analysis/strict_null_checks", false);

		TemporaryProjectSubtree tree("res://migration_wizard_strict_preview");
		const String clean_path = tree.write_file("clean.fs",
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n");
		CHECK_FALSE(clean_path.is_empty());

		MigrationWizardOptions options = unguarded_wizard_options();
		options.strict_null_checks = true;
		// activate_strict stays false: evaluate only.

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_strict_preview", options);
		REQUIRE(result.ok);
		CHECK(result.strict_evaluated);
		CHECK_FALSE(result.strict_activated);
		// Activation was never requested, so it is not considered blocked.
		CHECK_FALSE(result.strict_activation_blocked);
		// The setting was not touched.
		CHECK_EQ(ProjectSettings::get_singleton()->get_setting("debug/foundry_script/analysis/strict_null_checks", false), prior_null);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Unconfirmed strict activation is a no-op that reports why it was blocked") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const Variant prior_null = ProjectSettings::get_singleton()->get_setting("debug/foundry_script/analysis/strict_null_checks", false);

		TemporaryProjectSubtree tree("res://migration_wizard_strict_unconfirmed");
		const String clean_path = tree.write_file("clean.fs",
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n");
		CHECK_FALSE(clean_path.is_empty());

		MigrationWizardOptions options = unguarded_wizard_options();
		options.strict_null_checks = true;
		options.activate_strict = true;
		options.confirm_strict_activation = false; // The gate that blocks the flip.

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_strict_unconfirmed", options);
		REQUIRE(result.ok);
		CHECK(result.strict_evaluated);
		CHECK_FALSE(result.strict_activated);
		// A requested-but-gated flip is flagged so a CI caller can fail rather than report success.
		CHECK(result.strict_activation_blocked);
		// succeeded() is the CI signal: a gated activation is not a success even though ok is true.
		CHECK(result.ok);
		CHECK_FALSE(result.succeeded());
		CHECK_FALSE(result.strict_result.blocked_reason.is_empty());
		// Still untouched.
		CHECK_EQ(ProjectSettings::get_singleton()->get_setting("debug/foundry_script/analysis/strict_null_checks", false), prior_null);

		// The unconfirmed setting was not persisted, so no project.foundry restore is needed.

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Confirmed activation on a clean project flips and is not flagged blocked") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		ProjectSettings *settings = ProjectSettings::get_singleton();
		const Variant prior_null = settings->get_setting("debug/foundry_script/analysis/strict_null_checks", false);
		// A confirmed flip persists project.foundry, so snapshot and restore it byte-for-byte to keep
		// the curated test fixture intact.
		const String project_path = settings->globalize_path("res://project.foundry");
		const bool had_project_file = FileAccess::exists(project_path);
		const PackedByteArray project_bytes = had_project_file ? FileAccess::get_file_as_bytes(project_path) : PackedByteArray();

		TemporaryProjectSubtree tree("res://migration_wizard_strict_confirmed");
		const String clean_path = tree.write_file("clean.fs",
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n");
		CHECK_FALSE(clean_path.is_empty());

		MigrationWizardOptions options = unguarded_wizard_options();
		options.strict_null_checks = true;
		options.activate_strict = true;
		options.confirm_strict_activation = true;

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_strict_confirmed", options);
		REQUIRE(result.ok);
		CHECK(result.strict_activated);
		CHECK_FALSE(result.strict_activation_blocked);
		// A confirmed, clean, persisted activation is a full success.
		CHECK(result.strict_result.persisted);
		CHECK(result.succeeded());

		// Restore the settings and the on-disk project file so the flip does not leak into later
		// tests or onto disk.
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", prior_null);
		if (had_project_file) {
			Ref<FileAccess> file = FileAccess::open(project_path, FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_buffer(project_bytes.ptr(), project_bytes.size());
			}
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A fatal report (unreadable root) short-circuits the wizard") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		MigrationWizardOptions options = unguarded_wizard_options();
		options.apply = true;

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_does_not_exist", options);
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.applied);
		CHECK_FALSE(result.error_message.is_empty());

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report is persisted when a path is requested") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_wizard_follow_up");
		const String value_path = tree.write_file("value.fs",
				"static func value():\n"
				"\treturn 42\n");
		CHECK_FALSE(value_path.is_empty());

		const String follow_up_path = "res://migration_wizard_follow_up/follow_up.txt";
		MigrationWizardOptions options = unguarded_wizard_options();
		options.follow_up_path = follow_up_path;

		const MigrationWizardResult result = FSMigrationWizard::run("res://migration_wizard_follow_up", options);
		REQUIRE(result.ok);
		CHECK(FileAccess::exists(follow_up_path));

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace FSTests

#endif // !FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
