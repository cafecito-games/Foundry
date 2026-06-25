/**************************************************************************/
/*  test_migration_report.h                                               */
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

#include "../editor/gdscript_migration_report.h"

#include "core/io/file_access.h"

// Reuses TemporaryProjectSubtree and the GDScript test-project bootstrap so a report can be
// generated against an isolated res:// subtree.
#include "test_migration_driver.h"

#ifndef GDSCRIPT_NO_LSP

namespace GDScriptTests {

TEST_SUITE("[Modules][GDScript][MigrationReport]") {
	TEST_CASE("Report tallies inferable sites by kind and skipped sites by reason, changing nothing") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_mixed");

		// One file exercising every inferable kind and the reliably reproducible skip reasons:
		//   const C = 42          -> inferable constant
		//   var x = 7             -> inferable variable
		//   var local = 5         -> inferable variable (local)
		//   func make(p = 3)      -> inferable parameter (from its default)
		//   func make(...) return -> inferable return
		//   var typed: int = 1    -> skipped: already typed
		//   var novalue           -> skipped: no inferred type
		//   var nullish = null    -> skipped: unrenderable type (NIL)
		const String source =
				"const C = 42\n"
				"var x = 7\n"
				"var typed: int = 1\n"
				"var novalue\n"
				"var nullish = null\n"
				"\n"
				"func make(p = 3):\n"
				"\tvar local = 5\n"
				"\treturn local + p\n";
		const String path = tree.write_file("mixed.gd", source);

		const MigrationReportResult report = GDScriptMigrationReport::generate("res://migration_report_mixed");
		REQUIRE(report.ok);

		CHECK_EQ(report.total_scripts_scanned, 1);
		CHECK_EQ(report.scanned_files.size(), 1);
		CHECK(report.unanalyzable_files.is_empty());

		// Inferable sites, bucketed by declaration kind.
		CHECK_EQ(report.inferable.variable, 2);
		CHECK_EQ(report.inferable.constant, 1);
		CHECK_EQ(report.inferable.parameter, 1);
		CHECK_EQ(report.inferable.return_type, 1);
		CHECK_EQ(report.inferable.total, 5);
		// The per-kind breakdown accounts for every inferable site.
		CHECK_EQ(report.inferable.variable + report.inferable.constant + report.inferable.parameter + report.inferable.return_type, report.inferable.total);

		// Skipped sites, bucketed by reason.
		CHECK_EQ(report.skipped.already_typed, 1);
		CHECK_EQ(report.skipped.no_inferred_type, 1);
		CHECK_EQ(report.skipped.unrenderable_type, 1);
		CHECK_EQ(report.skipped.total, 3);
		CHECK_EQ(report.skipped.already_typed + report.skipped.no_inferred_type + report.skipped.multi_line + report.skipped.unrenderable_type + report.skipped.other, report.skipped.total);

		// No strict projection was requested.
		CHECK_FALSE(report.strict.requested);
		CHECK_EQ(report.strict.total, 0);

		// The rendered report mentions the headline counts and the no-write guarantee.
		const String text = report.format();
		CHECK(text.contains("Scripts scanned: 1"));
		CHECK(text.contains("Inferable type annotations: 5"));
		CHECK(text.contains("No files were modified."));

		// The report is read-only: the script on disk is byte-for-byte unchanged.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Report excludes third-party code and records the skipped directory") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_addons");

		const String main_path = tree.write_file("main.gd",
				"var x = 1\n");
		const String plugin_path = tree.write_file("addons/vendor/plugin.gd",
				"var y = 2\n");

		const MigrationReportResult report = GDScriptMigrationReport::generate("res://migration_report_addons");
		REQUIRE(report.ok);

		// Only the first-party script is considered; the addons script is neither scanned nor tallied.
		CHECK_EQ(report.total_scripts_scanned, 1);
		CHECK(report.scanned_files.has(main_path));
		CHECK_FALSE(report.scanned_files.has(plugin_path));
		CHECK_EQ(report.inferable.variable, 1);

		bool addons_skipped = false;
		for (const String &directory : report.skipped_directories) {
			if (directory.contains("addons")) {
				addons_skipped = true;
			}
		}
		CHECK(addons_skipped);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Report projects strict-mode violations only when requested") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_strict");

		// Assigning a Variant (from an untyped function) to a typed local is allowed by default
		// analysis but is a hard error under strict_dynamic_checks: a projected variant boundary.
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		const String path = tree.write_file("strict.gd", source);

		// Without a strict flag the projection is not run.
		const MigrationReportResult baseline = GDScriptMigrationReport::generate("res://migration_report_strict");
		REQUIRE(baseline.ok);
		CHECK_FALSE(baseline.strict.requested);
		CHECK_EQ(baseline.strict.total, 0);

		// With strict dynamic checks the projection surfaces the variant-boundary violation.
		MigrationReportOptions options;
		options.strict_dynamic_checks = true;
		const MigrationReportResult strict = GDScriptMigrationReport::generate("res://migration_report_strict", options);
		REQUIRE(strict.ok);
		CHECK(strict.strict.requested);
		CHECK(strict.strict.error.is_empty());
		CHECK_GT(strict.strict.total, 0);
		CHECK_GT(strict.strict.variant_boundary, 0);
		CHECK_EQ(strict.strict.nullable, 0);

		// The projection is read-only.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Report records an unanalyzable file without aborting the rest") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_broken");

		const String good_path = tree.write_file("good.gd",
				"var x = 1\n");
		// An unterminated function signature cannot be parsed, so this file is unanalyzable.
		const String broken_path = tree.write_file("broken.gd",
				"func oops(\n");

		const MigrationReportResult report = GDScriptMigrationReport::generate("res://migration_report_broken");
		REQUIRE(report.ok);

		// Both files were scanned, the good one is still tallied, and the broken one is recorded
		// as unanalyzable rather than silently dropped.
		CHECK_EQ(report.total_scripts_scanned, 2);
		CHECK_EQ(report.inferable.variable, 1);
		CHECK(report.unanalyzable_files.has(broken_path));
		CHECK_FALSE(report.unanalyzable_files.has(good_path));

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Report edge cases: empty project and unreadable root") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		SUBCASE("an empty project is a successful report with zero counts") {
			TemporaryProjectSubtree tree("res://migration_report_empty");
			const MigrationReportResult report = GDScriptMigrationReport::generate("res://migration_report_empty");
			CHECK(report.ok);
			CHECK_EQ(report.total_scripts_scanned, 0);
			CHECK_EQ(report.inferable.total, 0);
			CHECK_EQ(report.skipped.total, 0);
		}

		SUBCASE("an unreadable root fails fatally with a message") {
			const MigrationReportResult report = GDScriptMigrationReport::generate("res://migration_report_does_not_exist");
			CHECK_FALSE(report.ok);
			CHECK_FALSE(report.error_message.is_empty());
			CHECK_EQ(report.total_scripts_scanned, 0);
			CHECK_EQ(report.inferable.total, 0);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // !GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
