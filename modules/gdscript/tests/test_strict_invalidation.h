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

#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/config/project_settings.h"

#include "gdscript_test_runner_suite.h" // GDScriptTests::TestGDScriptCacheAccessor
#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile, initialize, root, finish_language
#include "test_strict_activation.h" // GDScriptTests::StrictSettingsGuard

#ifndef GDSCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace GDScriptTests {

namespace {

// Fully analyzes p_path through the shared GDScriptCache (the same path the live editor session
// uses) and returns the parser ref so the caller can keep it alive (the cache's parser_map holds
// only a raw pointer, so an entry survives only while a Ref to it is held, mirroring how a live
// session keeps open scripts cached). The returned ref reports any analyzer error: a
// strict_dynamic_checks violation surfaces as an error only when the flag is effectively on.
Ref<GDScriptParserRef> analyze(const String &p_path) {
	Error error = OK;
	return GDScriptCache::get_parser(p_path, GDScriptParserRef::FULLY_SOLVED, error);
}

bool reports_error(const Ref<GDScriptParserRef> &p_parser_ref) {
	if (p_parser_ref.is_null()) {
		return false;
	}
	const GDScriptParser *parser = p_parser_ref->get_parser();
	return parser != nullptr && !parser->get_errors().is_empty();
}

// Counts only error-severity LSP diagnostics, so warnings (which may exist regardless of strict
// mode) do not mask whether the strict violation is being reported.
int error_diagnostic_count(const ExtendGDScriptParser *p_parser) {
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

TEST_SUITE("[Modules][GDScript][StrictInvalidation]") {
	TEST_CASE("Flipping strict_dynamic_checks mid-session re-reports a previously-analyzed script") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/gdscript/analysis/strict_dynamic_checks", false);
		// Seed the change-detector baseline at the current (off) value so the later flip is detected.
		GDScriptParser::invalidate_analysis_on_strict_settings_change();

		// Assigning a Variant (the return of an untyped function) to a typed local is a
		// strict_dynamic_checks violation, but clean under non-strict analysis.
		const String path = "res://refactor/strict_invalidation.gd";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		// First analysis under strict OFF: no error reported, and the parser is cached while a Ref
		// to it is held (as a live session would hold its open scripts).
		Ref<GDScriptParserRef> first = analyze(path);
		CHECK_FALSE(reports_error(first));
		CHECK(TestGDScriptCacheAccessor::has_parser(path));

		// Flip the setting on and run the shared invalidation hook.
		settings->set_setting("debug/gdscript/analysis/strict_dynamic_checks", true);
		const bool invalidated = GDScriptParser::invalidate_analysis_on_strict_settings_change();
		CHECK(invalidated);

		// The previously-cached parser was abandoned, so the live cache no longer maps the path to
		// the stale entry even though our Ref is still alive.
		CHECK_FALSE(TestGDScriptCacheAccessor::has_parser(path));

		// Re-analysis under strict ON now reports the violation within the same session.
		Ref<GDScriptParserRef> second = analyze(path);
		CHECK(reports_error(second));

		first.unref();
		second.unref();
		GDScriptCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A settings change that leaves the strict flags untouched does not invalidate") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/gdscript/analysis/strict_dynamic_checks", false);
		settings->set_setting("debug/gdscript/analysis/strict_null_checks", false);
		GDScriptParser::invalidate_analysis_on_strict_settings_change();

		const String path = "res://refactor/strict_invalidation_stable.gd";
		const String source =
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n";
		TemporaryScriptFile file(path, source);

		Ref<GDScriptParserRef> parser_ref = analyze(path);
		CHECK_FALSE(reports_error(parser_ref));
		CHECK(TestGDScriptCacheAccessor::has_parser(path));

		// Change an unrelated setting; the strict flags are unchanged, so nothing is invalidated and
		// the cached parser is left in place.
		settings->set_setting("debug/gdscript/warnings/enable", true);
		const bool invalidated = GDScriptParser::invalidate_analysis_on_strict_settings_change();
		CHECK_FALSE(invalidated);
		CHECK(TestGDScriptCacheAccessor::has_parser(path));

		parser_ref.unref();
		GDScriptCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Flipping a strict flag re-publishes diagnostics for an open LSP document") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);
		StrictSettingsGuard guard;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("debug/gdscript/analysis/strict_dynamic_checks", false);
		GDScriptParser::invalidate_analysis_on_strict_settings_change();

		// Open a document whose only problem is a strict_dynamic_checks violation. The buffer lives in
		// the LSP's managed_files; nothing is written to disk, so re-parsing reads this buffer.
		const String uri = GDScriptLanguageProtocol::get_singleton()->get_workspace()->get_file_uri(
				"res://lsp/strict_invalidation_open.gd");
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		protocol->get_text_document()->didOpen(make_did_open_params(uri, source));

		const String path = GDScriptLanguageProtocol::get_singleton()->get_workspace()->get_file_path(uri);

		// Under strict OFF the open document reports no diagnostics.
		ExtendGDScriptParser *before = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
		REQUIRE(before);
		CHECK_EQ(error_diagnostic_count(before), 0);

		// Flip the setting on, run the shared invalidation hook, and re-parse the open documents the
		// way the live settings-change handler does.
		settings->set_setting("debug/gdscript/analysis/strict_dynamic_checks", true);
		CHECK(GDScriptParser::invalidate_analysis_on_strict_settings_change());
		protocol->reparse_open_scripts();

		// The re-parse used the in-memory buffer and now reports the strict violation as a diagnostic.
		ExtendGDScriptParser *after = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
		REQUIRE(after);
		CHECK_GT(error_diagnostic_count(after), 0);

		GDScriptCache::clear();
		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
