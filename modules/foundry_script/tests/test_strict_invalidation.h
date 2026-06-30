/**************************************************************************/
/*  test_strict_invalidation.h                                            */
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

#include "../fs_cache.h"
#include "../fs_parser.h"
#include "../foundry_script.h"

#include "core/config/project_settings.h"

#include "fs_test_runner_suite.h" // FSTests::TestFSCacheAccessor
#include "test_refactor.h" // FSTests::TemporaryScriptFile, initialize, root, finish_language
#include "test_strict_activation.h" // FSTests::StrictSettingsGuard

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

namespace {

// Fully analyzes p_path through the shared FSCache (the same path the live editor session
// uses) and returns the parser ref so the caller can keep it alive (the cache's parser_map holds
// only a raw pointer, so an entry survives only while a Ref to it is held, mirroring how a live
// session keeps open scripts cached). The returned ref reports any analyzer error: a
// strict_dynamic_checks violation surfaces as an error only when the flag is effectively on.
Ref<FSParserRef> analyze(const String &p_path) {
	Error error = OK;
	return FSCache::get_parser(p_path, FSParserRef::FULLY_SOLVED, error);
}

bool reports_error(const Ref<FSParserRef> &p_parser_ref) {
	if (p_parser_ref.is_null()) {
		return false;
	}
	const FSParser *parser = p_parser_ref->get_parser();
	return parser != nullptr && !parser->get_errors().is_empty();
}

// Counts only error-severity LSP diagnostics, so warnings (which may exist regardless of strict
// mode) do not mask whether the strict violation is being reported.
int error_diagnostic_count(const ExtendFSParser *p_parser) {
	if (p_parser == nullptr) {
		return 0;
	}
	int count = 0;
	for (const LSP::Diagnostic &diagnostic : p_parser->get_diagnostics()) {
		if (diagnostic.severity == LSP::DiagnosticSeverity::Error) {
			count++;
		}
	}
	return count;
}

} // namespace

TEST_SUITE("[Modules][FoundryScript][StrictInvalidation]") {
	TEST_CASE("Flipping strict_dynamic_checks mid-session re-reports a previously-analyzed script") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);
		// Seed the change-detector baseline at the current (off) value so the later flip is detected.
		FSParser::invalidate_analysis_on_strict_settings_change();

		// Assigning a Variant (the return of an untyped function) to a typed local is a
		// strict_dynamic_checks violation, but clean under non-strict analysis.
		const String path = "res://refactor/strict_invalidation.fs";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		// First analysis under strict OFF: no error reported, and the parser is cached while a Ref
		// to it is held (as a live session would hold its open scripts).
		Ref<FSParserRef> first = analyze(path);
		CHECK_FALSE(reports_error(first));
		CHECK(TestFSCacheAccessor::has_parser(path));

		// Flip the setting on and run the shared invalidation hook.
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", true);
		const bool invalidated = FSParser::invalidate_analysis_on_strict_settings_change();
		CHECK(invalidated);

		// The previously-cached parser was abandoned, so the live cache no longer maps the path to
		// the stale entry even though our Ref is still alive.
		CHECK_FALSE(TestFSCacheAccessor::has_parser(path));

		// Re-analysis under strict ON now reports the violation within the same session.
		Ref<FSParserRef> second = analyze(path);
		CHECK(reports_error(second));

		first.unref();
		second.unref();
		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("A settings change that leaves the strict flags untouched does not invalidate") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);
		settings->set_setting("debug/foundry_script/analysis/strict_null_checks", false);
		FSParser::invalidate_analysis_on_strict_settings_change();

		const String path = "res://refactor/strict_invalidation_stable.fs";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		Ref<FSParserRef> parser_ref = analyze(path);
		CHECK_FALSE(reports_error(parser_ref));
		CHECK(TestFSCacheAccessor::has_parser(path));

		// Change an unrelated setting; the strict flags are unchanged, so nothing is invalidated and
		// the cached parser is left in place.
		settings->set_setting("debug/foundry_script/warnings/enable", true);
		const bool invalidated = FSParser::invalidate_analysis_on_strict_settings_change();
		CHECK_FALSE(invalidated);
		CHECK(TestFSCacheAccessor::has_parser(path));

		parser_ref.unref();
		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Flipping a strict flag re-publishes diagnostics for an open LSP document") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", false);
		FSParser::invalidate_analysis_on_strict_settings_change();

		// Open a document whose only problem is a strict_dynamic_checks violation. The buffer lives in
		// the LSP's managed_files; nothing is written to disk, so re-parsing reads this buffer.
		const String uri = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_uri(
				"res://lsp/strict_invalidation_open.fs");
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		protocol->get_text_document()->didOpen(make_did_open_params(uri, source));

		const String path = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_path(uri);

		// Under strict OFF the open document reports no diagnostics.
		ExtendFSParser *before = FSLanguageProtocol::get_singleton()->get_parse_result(path);
		REQUIRE(before);
		CHECK_EQ(error_diagnostic_count(before), 0);

		// Flip the setting on, run the shared invalidation hook, and re-parse the open documents the
		// way the live settings-change handler does.
		settings->set_setting("debug/foundry_script/analysis/strict_dynamic_checks", true);
		CHECK(FSParser::invalidate_analysis_on_strict_settings_change());
		protocol->reparse_open_scripts();

		// The re-parse used the in-memory buffer and now reports the strict violation as a diagnostic.
		ExtendFSParser *after = FSLanguageProtocol::get_singleton()->get_parse_result(path);
		REQUIRE(after);
		CHECK_GT(error_diagnostic_count(after), 0);

		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Parser-level dependencies record inverse edges before full analysis") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String parent_path = "res://refactor/dep_invalidation_parent.fs";
		const String parent_source =
				"extends RefCounted\n"
				"func value() -> int:\n"
				"\treturn 1\n";
		TemporaryScriptFile parent(parent_path, parent_source);

		const String child_path = "res://refactor/dep_invalidation_child.fs";
		const String child_source =
				"const Parent = preload(\"res://refactor/dep_invalidation_parent.fs\")\n"
				"func use() -> void:\n"
				"\tvar x: int = Parent.value()\n";
		TemporaryScriptFile child(child_path, child_source);

		Error err = OK;
		Ref<FSParserRef> parsed = FSCache::get_parser(child_path, FSParserRef::PARSED, err);
		REQUIRE(err == OK);
		REQUIRE(parsed.is_valid());
		CHECK(FSCache::get_inverse_dependencies(parent_path).has(child_path));

		parsed.unref();
		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Editing a dependency invalidates and re-analyzes dependent parsers") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String parent_path = "res://refactor/dep_invalidation_parent_edit.fs";
		const String parent_v1 =
				"extends RefCounted\n"
				"func value() -> int:\n"
				"\treturn 1\n";
		TemporaryScriptFile parent(parent_path, parent_v1);

		const String child_path = "res://refactor/dep_invalidation_child_edit.fs";
		const String child_source =
				"extends \"res://refactor/dep_invalidation_parent_edit.fs\"\n"
				"func use() -> int:\n"
				"\treturn value()\n";
		TemporaryScriptFile child(child_path, child_source);

		Ref<FSParserRef> first = analyze(child_path);
		CHECK_FALSE(reports_error(first));
		CHECK(TestFSCacheAccessor::has_parser(child_path));
		CHECK(TestFSCacheAccessor::has_parser(parent_path));

		{
			Ref<FileAccess> file = FileAccess::open(parent_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(
					"extends RefCounted\n"
					"func value() -> int:\n"
					"\treturn \"not an int\"\n");
		}

		FSCache::remove_parser(parent_path);

		CHECK_FALSE(TestFSCacheAccessor::has_parser(child_path));
		CHECK_FALSE(TestFSCacheAccessor::has_parser(parent_path));

		Ref<FSParserRef> second = analyze(child_path);
		CHECK(reports_error(second));

		first.unref();
		second.unref();
		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Editing a dependency refreshes open LSP diagnostics for dependents") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);
		TestFSLanguageProtocolInitializer::mark_initialized(protocol);

		const String parent_path = "res://refactor/dep_invalidation_parent_lsp.fs";
		const String parent_v1 =
				"extends RefCounted\n"
				"func value() -> int:\n"
				"\treturn 1\n";
		TemporaryScriptFile parent(parent_path, parent_v1);

		const String child_path = "res://refactor/dep_invalidation_child_lsp.fs";
		const String child_source =
				"extends \"res://refactor/dep_invalidation_parent_lsp.fs\"\n"
				"func use() -> void:\n"
				"\tvar x: int = value()\n";
		TemporaryScriptFile child(child_path, child_source);

		// Prime dependency edges through the shared cache, then open the child in the LSP.
		Ref<FSParserRef> cached = analyze(child_path);
		REQUIRE(cached.is_valid());
		cached.unref();

		const String child_uri = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_uri(child_path);
		protocol->get_text_document()->didOpen(make_did_open_params(child_uri, child_source));

		ExtendFSParser *before = FSLanguageProtocol::get_singleton()->get_parse_result(child_path);
		REQUIRE(before);
		CHECK_EQ(error_diagnostic_count(before), 0);

		// Parent body no longer matches its declared int return type.
		{
			Ref<FileAccess> file = FileAccess::open(parent_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(
					"extends RefCounted\n"
					"func value() -> int:\n"
					"\treturn \"not an int\"\n");
		}

		FSLanguage::get_singleton()->notify_disk_source_changed(parent_path);

		ExtendFSParser *after = FSLanguageProtocol::get_singleton()->get_parse_result(child_path);
		REQUIRE(after);
		CHECK_GT(error_diagnostic_count(after), 0);

		FSCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("FSCache::clear() drops parser dependency edges") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
		REQUIRE(protocol);

		const String parent_path = "res://refactor/dep_invalidation_clear_parent.fs";
		const String parent_source =
				"extends RefCounted\n"
				"func value() -> int:\n"
				"\treturn 1\n";
		TemporaryScriptFile parent(parent_path, parent_source);

		const String child_path = "res://refactor/dep_invalidation_clear_child.fs";
		const String child_source =
				"const Parent = preload(\"res://refactor/dep_invalidation_clear_parent.fs\")\n"
				"func use() -> void:\n"
				"\tvar x: int = Parent.value()\n";
		TemporaryScriptFile child(child_path, child_source);

		Error err = OK;
		Ref<FSParserRef> parsed = FSCache::get_parser(child_path, FSParserRef::PARSED, err);
		REQUIRE(err == OK);
		CHECK(FSCache::get_inverse_dependencies(parent_path).has(child_path));

		parsed.unref();
		FSCache::clear();

		CHECK(FSCache::get_inverse_dependencies(parent_path).is_empty());
		CHECK(FSCache::get_inverse_dependencies(child_path).is_empty());

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
