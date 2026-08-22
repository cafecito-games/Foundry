/**************************************************************************/
/*  test_type_completeness_selector.h                                     */
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
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_cli.h"
#include "fs_type_completeness_common.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

// Writes p_lines as the selector reads them: one path per line, LF separated, nothing appended.
static String completeness_selector_write_changed_paths(
		const TemporaryProjectTree &p_tree, const String &p_name, const Vector<String> &p_lines) {
	const String path = p_tree.root.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	String text;
	for (int index = 0; index < p_lines.size(); index++) {
		text += p_lines[index];
		text += "\n";
	}
	file->store_string(text);
	file->flush();
	return path;
}

static String completeness_selector_run(
		const TemporaryProjectTree &p_tree, const Vector<String> &p_lines, int &r_exit_code) {
	FSCompletenessSelectCLI::Options options;
	options.changed_paths_path = completeness_selector_write_changed_paths(p_tree, "changed.txt", p_lines);
	options.catalog_root = tracked_catalog_root();
	options.json = true;
	String document;
	r_exit_code = FSCompletenessSelectCLI::run(options, &document);
	return document;
}

static Dictionary completeness_selector_parse(const String &p_document) {
	JSON json;
	REQUIRE_EQ(json.parse(p_document), OK);
	REQUIRE_EQ(json.get_data().get_type(), Variant::DICTIONARY);
	return json.get_data();
}

static PackedStringArray completeness_selector_strings(const Variant &p_value) {
	PackedStringArray values;
	const Array array = p_value;
	for (int index = 0; index < array.size(); index++) {
		values.push_back(array[index]);
	}
	return values;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Selector]") {
	TEST_CASE("TypeCompleteness selector maps a production path to its families") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_mapped_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(
				tree, Vector<String>({ "modules/foundry_script/fs_analyzer.cpp" }), exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(int(document["schema_version"]), 1);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), false);
		CHECK(completeness_selector_strings(document["validation_errors"]).is_empty());
		CHECK(completeness_selector_strings(document["families"]).has("union_destination_membership"));
	}

	// A rule manifest is a family's own definition, so a change set that only edits one has to select
	// that family and nothing else. A wrapper-parity rule that selected nothing would let a weakened
	// rule and its regenerated evidence through a gate that compared neither.
	TEST_CASE("TypeCompleteness selector maps a wrapper-parity rule manifest to its own family") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_rule_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(tree,
				Vector<String>({ "modules/foundry_script/tests/type_completeness/rules/"
								 "wrapper_parity_reflective_write.json" }),
				exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), false);
		CHECK(completeness_selector_strings(document["validation_errors"]).is_empty());
		const PackedStringArray families = completeness_selector_strings(document["families"]);
		REQUIRE_EQ(families.size(), 1);
		CHECK_EQ(families[0], "wrapper_parity_reflective_write");
	}

	// The document is the gate's input, so two change sets that differ only in the order or multiplicity
	// of their lines must not be able to produce two different gates.
	TEST_CASE("TypeCompleteness selector is invariant under reordering and duplication") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_stable_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int ordered_exit = -1;
		int shuffled_exit = -1;
		const String ordered = completeness_selector_run(tree,
				Vector<String>({ "modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/fs_vm.cpp" }),
				ordered_exit);
		const String shuffled = completeness_selector_run(tree,
				Vector<String>({ "modules/foundry_script/fs_vm.cpp", "modules/foundry_script/fs_analyzer.cpp",
						"modules/foundry_script/fs_vm.cpp" }),
				shuffled_exit);

		CHECK_EQ(ordered_exit, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(shuffled_exit, ordered_exit);
		CHECK_EQ(shuffled, ordered);
	}

	// A production path the map does not cover is the case the fallback exists for: the change is real
	// and unscoped, so the broad core runs and the selection still reports that it could not scope it.
	TEST_CASE("TypeCompleteness selector falls back to the broad core on an unmapped production path") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_unmapped_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(
				tree, Vector<String>({ "modules/foundry_script/fs_unmapped_surface.cpp" }), exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_REFUSED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), true);
		CHECK(completeness_selector_strings(document["families"]).has("union_destination_membership"));
		const PackedStringArray errors = completeness_selector_strings(document["validation_errors"]);
		REQUIRE_EQ(errors.size(), 1);
		CHECK_EQ(errors[0], "unmapped production path: modules/foundry_script/fs_unmapped_surface.cpp");
	}

	TEST_CASE("TypeCompleteness selector selects nothing for a nonproduction change set") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_nonproduction_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(tree,
				Vector<String>({ "docs/superpowers/notes.md", "modules/foundry_script/tests/test_selector.h" }),
				exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), false);
		CHECK(completeness_selector_strings(document["families"]).is_empty());
		CHECK(completeness_selector_strings(document["validation_errors"]).is_empty());
	}

	// A rule manifest is the definition of a family's matrix: editing it can remove or rekey cases,
	// which is exactly the coverage regression the comparison exists to catch, so it selects its family
	// rather than reading as a test-tree edit that scopes nothing.
	TEST_CASE("TypeCompleteness selector selects the owning family for a rule-only change set") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_rules_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(tree,
				Vector<String>({ "modules/foundry_script/tests/type_completeness/rules/union_destination_membership.json" }),
				exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), false);
		CHECK(completeness_selector_strings(document["families"]).has("union_destination_membership"));
		CHECK(completeness_selector_strings(document["validation_errors"]).is_empty());
	}

	// Partitions and dimensions are shared by every family, so a change to one is broad by construction.
	TEST_CASE("TypeCompleteness selector selects the broad core for a shared catalog change set") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_catalog_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(tree,
				Vector<String>({ "modules/foundry_script/tests/type_completeness/partitions/boundary.json",
						"modules/foundry_script/tests/type_completeness/dimensions/core.json" }),
				exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_SELECTED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), false);
		CHECK(completeness_selector_strings(document["families"]).has("union_destination_membership"));
		CHECK(completeness_selector_strings(document["validation_errors"]).is_empty());
	}

	// Nothing about a changed path is repaired: a path the map cannot interpret is reported, and the
	// broad core runs because an uninterpretable change set cannot be scoped.
	TEST_CASE("TypeCompleteness selector refuses a path it would have to normalize") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_invalid_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		int exit_code = -1;
		const Dictionary document = completeness_selector_parse(completeness_selector_run(
				tree, Vector<String>({ "./modules/foundry_script/fs_analyzer.cpp" }), exit_code));

		CHECK_EQ(exit_code, FSCompletenessSelectCLI::EXIT_REFUSED);
		CHECK_EQ(bool(document["used_broad_core_fallback"]), true);
		const PackedStringArray errors = completeness_selector_strings(document["validation_errors"]);
		REQUIRE_EQ(errors.size(), 1);
		CHECK_EQ(errors[0], "invalid changed path: ./modules/foundry_script/fs_analyzer.cpp");
	}

	TEST_CASE("TypeCompleteness selector refuses an unreadable changed-paths file") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_unreadable_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessSelectCLI::Options options;
		options.changed_paths_path = tree.root.path_join("absent.txt");
		options.catalog_root = tracked_catalog_root();
		options.json = true;
		String document;
		CHECK_EQ(FSCompletenessSelectCLI::run(options, &document), FSCompletenessSelectCLI::EXIT_REFUSED);
		const Dictionary parsed = completeness_selector_parse(document);
		CHECK(completeness_selector_strings(parsed["families"]).is_empty());
		CHECK_EQ(completeness_selector_strings(parsed["validation_errors"]).size(), 1);
	}

	// Without --json there is no encoding to publish, so the command refuses rather than inventing one.
	TEST_CASE("TypeCompleteness selector refuses an invocation without --json") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_nojson_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessSelectCLI::Options options;
		options.changed_paths_path =
				completeness_selector_write_changed_paths(tree, "changed.txt", Vector<String>());
		options.catalog_root = tracked_catalog_root();
		String document;
		CHECK_EQ(FSCompletenessSelectCLI::run(options, &document), FSCompletenessSelectCLI::EXIT_REFUSED);
		CHECK(completeness_selector_strings(completeness_selector_parse(document)["validation_errors"]).size() > 0);
	}

	// A capability map that disagrees with the rule directory cannot scope anything, so the selection
	// reports the disagreement itself rather than a families list drawn from a map nobody validated.
	TEST_CASE("TypeCompleteness selector reports a catalog whose rules the map cannot reach") {
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_catalog_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessSelectCLI::Options options;
		options.changed_paths_path = completeness_selector_write_changed_paths(
				tree, "changed.txt", Vector<String>({ "modules/foundry_script/fs_analyzer.cpp" }));
		options.catalog_root = tree.root.path_join("absent_catalog");
		options.json = true;
		String document;
		CHECK_EQ(FSCompletenessSelectCLI::run(options, &document), FSCompletenessSelectCLI::EXIT_REFUSED);
		const Dictionary parsed = completeness_selector_parse(document);
		CHECK(completeness_selector_strings(parsed["families"]).is_empty());
		CHECK(completeness_selector_strings(parsed["validation_errors"]).size() > 0);
	}

	// The document the presubmit wrapper parses is produced by a real process, so the encoding is proven
	// against that process rather than against an in-process call that never touched stdout.
	TEST_CASE("TypeCompleteness selector publishes its document as a command") {
		const String executable = OS::get_singleton()->get_executable_path();
		if (executable.is_empty() || !FileAccess::exists(executable)) {
			Completeness::fs_completeness_skip("the running executable is not available to re-invoke");
			return;
		}
		TemporaryProjectTree tree(
				vformat("type_completeness_selector_subprocess_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String changed_paths = completeness_selector_write_changed_paths(
				tree, "changed.txt", Vector<String>({ "modules/foundry_script/fs_analyzer.cpp" }));

		List<String> arguments;
		arguments.push_back("--headless");
		arguments.push_back("test");
		arguments.push_back("completeness");
		arguments.push_back("select");
		arguments.push_back("--changed-paths");
		arguments.push_back(changed_paths);
		arguments.push_back("--catalog");
		arguments.push_back(tracked_catalog_root());
		arguments.push_back("--json");
		String output;
		int exit_code = -1;
		REQUIRE_EQ(OS::get_singleton()->execute(executable, arguments, &output, &exit_code, true), OK);

#ifdef TOOLS_ENABLED
		CHECK_MESSAGE(exit_code == FSCompletenessSelectCLI::EXIT_SELECTED, output);
		const int document_start = output.find("{");
		REQUIRE_MESSAGE(document_start >= 0, output);
		const Dictionary document = completeness_selector_parse(output.substr(document_start));
		CHECK_EQ(int(document["schema_version"]), 1);
		CHECK(completeness_selector_strings(document["families"]).has("union_destination_membership"));
#else
		CHECK_MESSAGE(exit_code != FSCompletenessSelectCLI::EXIT_SELECTED, output);
		CHECK_MESSAGE(output.contains("requires an editor build"), output);
#endif
	}
}

} // namespace FSTests
