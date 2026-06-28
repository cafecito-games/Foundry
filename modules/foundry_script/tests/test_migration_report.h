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

#include "../editor/fs_migration_report.h"

#include "core/io/file_access.h"

// Reuses TemporaryProjectSubtree and the FoundryScript test-project bootstrap so a report can be
// generated against an isolated res:// subtree.
#include "test_migration_driver.h"

#ifndef GDSCRIPT_NO_LSP

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][MigrationReport]") {
	TEST_CASE("Report tallies inferable sites by kind and skipped sites by reason, changing nothing") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
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
		const String path = tree.write_file("mixed.fs", source);

		const MigrationReportResult report = FSMigrationReport::generate("res://migration_report_mixed");
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
	}

	TEST_CASE("Report excludes third-party code and records the skipped directory") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_addons");

		const String main_path = tree.write_file("main.fs",
				"var x = 1\n");
		const String plugin_path = tree.write_file("addons/vendor/plugin.fs",
				"var y = 2\n");

		const MigrationReportResult report = FSMigrationReport::generate("res://migration_report_addons");
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
	}

	TEST_CASE("Report projects strict-mode violations only when requested") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_strict");

		// Assigning a Variant (from an untyped function) to a typed local is allowed by default
		// analysis but is a hard error under strict_dynamic_checks: a projected variant boundary.
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		const String path = tree.write_file("strict.fs", source);

		// Without a strict flag the projection is not run.
		const MigrationReportResult baseline = FSMigrationReport::generate("res://migration_report_strict");
		REQUIRE(baseline.ok);
		CHECK_FALSE(baseline.strict.requested);
		CHECK_EQ(baseline.strict.total, 0);

		// With strict dynamic checks the projection surfaces the variant-boundary violation.
		MigrationReportOptions options;
		options.strict_dynamic_checks = true;
		const MigrationReportResult strict = FSMigrationReport::generate("res://migration_report_strict", options);
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
	}

	TEST_CASE("Report counts nullable violations the satisfier can prove as auto-fixable") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_nullable");

		// `maybe()` returns `int?`; landing it in a non-nullable `int` local is a strict-null
		// violation whose widen-to-nullable fix (`int` -> `int?`) the harness can verify.
		const String source =
				"func maybe() -> int?:\n"
				"\treturn null\n"
				"func use() -> void:\n"
				"\tvar x: int = maybe()\n";
		const String path = tree.write_file("nullable.fs", source);

		MigrationReportOptions options;
		options.strict_null_checks = true;
		const MigrationReportResult strict = FSMigrationReport::generate("res://migration_report_nullable", options);
		REQUIRE(strict.ok);
		CHECK(strict.strict.requested);
		CHECK(strict.strict.error.is_empty());
		CHECK_GT(strict.strict.nullable, 0);
		// The single nullable boundary is provably satisfiable, so it is also counted as such.
		CHECK_EQ(strict.strict.nullable_satisfiable, strict.strict.nullable);
		CHECK_EQ(strict.strict.variant_boundary, 0);

		// The rendered report surfaces the auto-fixable subset.
		const String text = strict.format();
		CHECK(text.contains("auto-fixable:"));

		// The projection is read-only.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Report counts compatible-but-not-identical nullable boundaries as auto-fixable") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_nullable_compatible");

		// `maybe()` returns `Button?`; landing it in a non-nullable `Node` local is a strict-null
		// violation whose underlying type is assignment-compatible (covariant) rather than
		// identical. The widen-to-nullable fix (`Node` -> `Node?`) is still provable, so the
		// boundary must be counted as `nullable_satisfiable`.
		const String source =
				"func maybe() -> Button?:\n"
				"\treturn null\n"
				"func use() -> void:\n"
				"\tvar n: Node = maybe()\n";
		const String path = tree.write_file("nullable_compatible.fs", source);

		MigrationReportOptions options;
		options.strict_null_checks = true;
		const MigrationReportResult strict = FSMigrationReport::generate("res://migration_report_nullable_compatible", options);
		REQUIRE(strict.ok);
		CHECK(strict.strict.requested);
		CHECK(strict.strict.error.is_empty());
		CHECK_GT(strict.strict.nullable, 0);
		// The compatible-but-not-identical boundary is provably satisfiable, so the satisfiable
		// tally must keep pace with the nullable tally rather than undercounting it.
		CHECK_EQ(strict.strict.nullable_satisfiable, strict.strict.nullable);

		// The projection is read-only.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Report records an unanalyzable file without aborting the rest") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_broken");

		const String good_path = tree.write_file("good.fs",
				"var x = 1\n");
		// An unterminated function signature cannot be parsed, so this file is unanalyzable.
		const String broken_path = tree.write_file("broken.fs",
				"func oops(\n");

		const MigrationReportResult report = FSMigrationReport::generate("res://migration_report_broken");
		REQUIRE(report.ok);

		// Both files were scanned, the good one is still tallied, and the broken one is recorded
		// as unanalyzable rather than silently dropped.
		CHECK_EQ(report.total_scripts_scanned, 2);
		CHECK_EQ(report.inferable.variable, 1);
		CHECK(report.unanalyzable_files.has(broken_path));
		CHECK_FALSE(report.unanalyzable_files.has(good_path));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Projection report counts a cascade-only annotation the single-pass report under-counts") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_cascade");

		// C is the leaf; B relays C; A reads B. Only the dependency-ordered fixpoint types the
		// whole chain: A's `var x = B.relay()` is Variant until B (and C) gain return types, so
		// the single-pass report reports it skipped while the projection reports it inferable.
		const String path_c = tree.write_file("chain_c.fs",
				"static func value():\n"
				"\treturn 42\n");
		const String path_b = tree.write_file("chain_b.fs",
				"const C = preload(\"res://migration_report_cascade/chain_c.fs\")\n"
				"static func relay():\n"
				"\treturn C.value()\n");
		const String source_c = FileAccess::get_file_as_string(path_c);
		const String source_b = FileAccess::get_file_as_string(path_b);
		const String path_a = tree.write_file("chain_a.fs",
				"const B = preload(\"res://migration_report_cascade/chain_b.fs\")\n"
				"var x = B.relay()\n");
		const String source_a = FileAccess::get_file_as_string(path_a);

		// Single pass: only C's leaf `value()` resolves today. B's `relay()` forwards C's untyped
		// result and A's `var x` reads B's, so both stay Variant and are reported skipped. The two
		// `const = preload(...)` declarations carry a script type that has no renderable annotation,
		// so they are skipped in both modes.
		const MigrationReportResult single = FSMigrationReport::generate("res://migration_report_cascade");
		REQUIRE(single.ok);
		CHECK_FALSE(single.projection);
		CHECK_EQ(single.inferable.total, 1); // Only C's value() is inferable in isolation.
		CHECK_EQ(single.inferable.return_type, 1);

		// Projection: the dependency-ordered fixpoint types the whole chain, so C's value(), B's
		// relay(), and A's `var x` are all counted. The two preload consts remain unrenderable.
		MigrationReportOptions options;
		options.projection = true;
		const MigrationReportResult projected = FSMigrationReport::generate("res://migration_report_cascade", options);
		REQUIRE(projected.ok);
		CHECK(projected.projection);
		CHECK_EQ(projected.inferable.total, 3);
		CHECK_EQ(projected.inferable.variable, 1); // A's x, inferable only after the cascade.
		CHECK_EQ(projected.inferable.return_type, 2); // B's relay and C's value.
		// The projection covers strictly more than the single pass on this chain.
		CHECK_GT(projected.inferable.total, single.inferable.total);
		// The two preload consts have no renderable annotation, so they stay skipped in both modes.
		CHECK_EQ(projected.skipped.unrenderable_type, 2);
		// The per-kind split still sums to the total.
		CHECK_EQ(projected.inferable.variable + projected.inferable.constant + projected.inferable.parameter + projected.inferable.return_type, projected.inferable.total);

		// The projection is read-only: the dry-run restored every file it touched.
		CHECK_EQ(FileAccess::get_file_as_string(path_a), source_a);
		CHECK_EQ(FileAccess::get_file_as_string(path_b), source_b);
		CHECK_EQ(FileAccess::get_file_as_string(path_c), source_c);

		// The rendered report identifies itself as the accurate projection.
		CHECK(projected.format().contains("Fixpoint+verification-accurate projection"));
		CHECK(projected.format().contains("No files were modified."));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Projection report drops a verification-rejected annotation the single-pass report over-counts") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_report_reject");

		// provider.get_value() is inferable as `-> int` in isolation, but the consumer assigns its
		// result to a String local, so committing the return type breaks the dependent. The
		// verification harness rejects the edit, so the projection must NOT count it as inferable.
		const String provider_path = tree.write_file("provider.fs",
				"func get_value():\n"
				"\treturn 42\n");
		const String consumer_path = tree.write_file("consumer.fs",
				"const Provider = preload(\"res://migration_report_reject/provider.fs\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n");
		const String provider_before = FileAccess::get_file_as_string(provider_path);
		const String consumer_before = FileAccess::get_file_as_string(consumer_path);

		// Single pass: the return type is inferable in isolation, so it is over-counted here.
		const MigrationReportResult single = FSMigrationReport::generate("res://migration_report_reject");
		REQUIRE(single.ok);
		CHECK_EQ(single.inferable.return_type, 1);

		// Projection: verification rejects the breaking edit, so it is reported skipped, not
		// inferable.
		MigrationReportOptions options;
		options.projection = true;
		const MigrationReportResult projected = FSMigrationReport::generate("res://migration_report_reject", options);
		REQUIRE(projected.ok);
		CHECK(projected.projection);
		CHECK_EQ(projected.inferable.return_type, 0);
		CHECK_EQ(projected.inferable.total, 0);
		CHECK_GT(projected.skipped.total, 0);

		// The projection is read-only: the rejected edit was never left on disk.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_before);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_before);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report lists skipped sites grouped by category with file and line") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_followup_skips");

		// A mix of skip reasons that each map to a distinct follow-up category:
		//   var typed: int = 1   -> already typed (NOT a follow-up; no work owed)
		//   var novalue          -> no inferable type
		//   var nullish = null   -> unrenderable inferred type (NIL initializer)
		const String source =
				"var typed: int = 1\n"
				"var novalue\n"
				"var nullish = null\n";
		const String path = tree.write_file("skips.fs", source);

		const MigrationReportResult report = FSMigrationReport::generate("res://migration_followup_skips");
		REQUIRE(report.ok);

		// The already-typed site owes no manual follow-up; the other two do.
		int no_inferred = 0;
		int unrenderable_type = 0;
		bool every_entry_points_at_file = true;
		for (const MigrationFollowUpEntry &entry : report.follow_ups) {
			if (entry.path != path) {
				every_entry_points_at_file = false;
			}
			switch (entry.category) {
				case MigrationFollowUpCategory::NO_INFERRED_TYPE:
					no_inferred++;
					break;
				case MigrationFollowUpCategory::UNRENDERABLE_TYPE:
					unrenderable_type++;
					break;
				default:
					break;
			}
			// Every follow-up carries a usable 1-based line.
			CHECK_GT(entry.line, 0);
		}
		CHECK(every_entry_points_at_file);
		CHECK_EQ(no_inferred, 1);
		CHECK_EQ(unrenderable_type, 1);
		CHECK_EQ(report.follow_ups.size(), 2);

		// No entry is the already-typed declaration.
		for (const MigrationFollowUpEntry &entry : report.follow_ups) {
			CHECK_FALSE(entry.detail.contains("already has a"));
		}

		// The rendered punch-list groups by category and shows path:line.
		const String text = report.format_follow_up();
		CHECK(text.contains("Total follow-up sites: 2"));
		CHECK(text.contains("Unrenderable inferred type"));
		CHECK(text.contains("No inferable type:"));
		CHECK(text.contains(vformat("%s:", path)));

		// Reading the report changed nothing on disk.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report records strict violations as manual sites") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_followup_strict");

		// A variant-boundary violation under strict_dynamic_checks: a manual follow-up.
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		const String strict_path = tree.write_file("strict.fs", source);
		(void)strict_path;

		MigrationReportOptions options;
		options.strict_dynamic_checks = true;
		const MigrationReportResult report = FSMigrationReport::generate("res://migration_followup_strict", options);
		REQUIRE(report.ok);
		CHECK_GT(report.strict.variant_boundary, 0);

		int strict_variant = 0;
		for (const MigrationFollowUpEntry &entry : report.follow_ups) {
			if (entry.category == MigrationFollowUpCategory::STRICT_VARIANT_BOUNDARY) {
				strict_variant++;
				CHECK_GT(entry.line, 0);
			}
		}
		CHECK_EQ(strict_variant, report.strict.variant_boundary);

		const String text = report.format_follow_up();
		CHECK(text.contains("Strict-mode violations (variant boundary):"));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report excludes nullable violations the satisfier can auto-fix") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_followup_nullable");

		// A nullable boundary whose widen-to-nullable fix the harness can prove: no manual work owed.
		const String source =
				"func maybe() -> int?:\n"
				"\treturn null\n"
				"func use() -> void:\n"
				"\tvar x: int = maybe()\n";
		const String nullable_path = tree.write_file("nullable.fs", source);
		(void)nullable_path;

		MigrationReportOptions options;
		options.strict_null_checks = true;
		const MigrationReportResult report = FSMigrationReport::generate("res://migration_followup_nullable", options);
		REQUIRE(report.ok);
		// Every nullable violation here is provably satisfiable.
		CHECK_GT(report.strict.nullable, 0);
		CHECK_EQ(report.strict.nullable_satisfiable, report.strict.nullable);

		// A fully auto-fixable nullable boundary is not a manual follow-up.
		for (const MigrationFollowUpEntry &entry : report.follow_ups) {
			CHECK_NE(entry.category, MigrationFollowUpCategory::STRICT_NULLABLE);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report is regenerable: writes to a known location and overwrites in place") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_followup_write");

		const String first_source_path = tree.write_file("a.fs", "var novalue\n");
		(void)first_source_path;

		const MigrationReportResult first = FSMigrationReport::generate("res://migration_followup_write");
		REQUIRE(first.ok);
		CHECK_FALSE(first.follow_ups.is_empty());

		const String report_path = "res://migration_followup_write/followup.md";
		CHECK_EQ(FSMigrationReport::write_follow_up(first, report_path), OK);
		const String first_text = FileAccess::get_file_as_string(report_path);
		CHECK(first_text.contains("No inferable type:"));
		CHECK_EQ(first_text, first.format_follow_up());

		// Regenerate against a now-clean project: the report overwrites in place with the empty note.
		const String second_source_path = tree.write_file("a.fs", "var x: int = 1\n");
		(void)second_source_path;
		const MigrationReportResult second = FSMigrationReport::generate("res://migration_followup_write");
		REQUIRE(second.ok);
		CHECK(second.follow_ups.is_empty());
		CHECK_EQ(FSMigrationReport::write_follow_up(second, report_path), OK);
		const String second_text = FileAccess::get_file_as_string(report_path);
		CHECK(second_text.contains("No follow-up sites"));
		CHECK_FALSE(second_text.contains("No inferable type:"));

		// A fatal-failure report is refused rather than persisted.
		const MigrationReportResult failed = FSMigrationReport::generate("res://migration_followup_does_not_exist");
		CHECK_FALSE(failed.ok);
		CHECK_NE(FSMigrationReport::write_follow_up(failed, report_path), OK);

		// Clean up the artifact the test wrote (it lives under the temp subtree, but the file was
		// created by write_follow_up, not the tree helper).
		DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(report_path));

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Follow-up report does not claim a clean bill of health when a file is unanalyzable") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		TemporaryProjectSubtree tree("res://migration_followup_unanalyzable");

		// A fully-typed file (no per-site follow-ups) plus a parse-broken file the analyzer cannot
		// read. The broken file's declarations are invisible to the tally, so the report must warn
		// rather than imply everything was typed or auto-migratable.
		const String good_path = tree.write_file("good.fs", "var x: int = 1\n");
		const String broken_path = tree.write_file("broken.fs", "func oops(\n");

		const MigrationReportResult report = FSMigrationReport::generate("res://migration_followup_unanalyzable");
		REQUIRE(report.ok);
		CHECK(report.follow_ups.is_empty()); // The good file is typed; the broken file yields no sites.
		CHECK(report.unanalyzable_files.has(broken_path));

		const String text = report.format_follow_up();
		// The misleading "all clear" line is suppressed; the unanalyzable file is surfaced instead.
		CHECK_FALSE(text.contains("every scanned declaration was either typed or auto-migratable"));
		CHECK(text.contains("could not be analyzed"));
		CHECK(text.contains(broken_path));

		CHECK_EQ(FileAccess::get_file_as_string(good_path), "var x: int = 1\n");

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Report edge cases: empty project and unreadable root") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		SUBCASE("an empty project is a successful report with zero counts") {
			TemporaryProjectSubtree tree("res://migration_report_empty");
			const MigrationReportResult report = FSMigrationReport::generate("res://migration_report_empty");
			CHECK(report.ok);
			CHECK_EQ(report.total_scripts_scanned, 0);
			CHECK_EQ(report.inferable.total, 0);
			CHECK_EQ(report.skipped.total, 0);
		}

		SUBCASE("an unreadable root fails fatally with a message") {
			const MigrationReportResult report = FSMigrationReport::generate("res://migration_report_does_not_exist");
			CHECK_FALSE(report.ok);
			CHECK_FALSE(report.error_message.is_empty());
			CHECK_EQ(report.total_scripts_scanned, 0);
			CHECK_EQ(report.inferable.total, 0);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace FSTests

#endif // !GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
