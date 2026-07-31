/**************************************************************************/
/*  test_conformance_index_invalidation.h                                 */
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

#include "tests/test_macros.h"

#include "../foundry_script.h"
#include "../fs_cache.h"
#include "../fs_conformance_registry.h"
#include "../fs_parser.h"

#include "core/object/script_language.h"

#include "fs_test_runner_suite.h" // FSTests::TestFSCacheAccessor
#include "test_refactor.h" // FSTests::TemporaryScriptFile, initialize, root

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

// A file's retroactive-conformance reach is snapshotted from the project index at parse time: the
// parser records an edge to every conformance file that existed in its own namespace or in one it
// imports. A parser cached before that file set changed therefore has no edge to follow to the newly
// indexed file, so the per-path invalidation funnel can never reach it. These tests pin the
// namespace-keyed funnel that closes the gap, including the cases where it must *not* fire.
struct ConformanceIndexInvalidationFixture {
	static constexpr const char *CONFORMANCE_NAMESPACE = "fsci";
	static constexpr const char *WIDGET_PATH = "res://refactor/fsci_widget.fs";
	static constexpr const char *GADGET_PATH = "res://refactor/fsci_gadget.fs";
	static constexpr const char *TRAIT_PATH = "res://refactor/fsci_markable.fs";
	static constexpr const char *IMPORTER_PATH = "res://refactor/fsci_importer.fs";
	static constexpr const char *SIBLING_PATH = "res://refactor/fsci_sibling.fs";
	static constexpr const char *OUTSIDER_PATH = "res://refactor/fsci_outsider.fs";
	static constexpr const char *CONFORMANCE_PATH = "res://refactor/fsci_conformance.fs";
	static constexpr const char *SECOND_CONFORMANCE_PATH = "res://refactor/fsci_conformance_two.fs";
	static constexpr const char *GLOBAL_CONFORMANCE_PATH = "res://refactor/fsci_global_conformance.fs";

	EditorFileSystem *editor_file_system = nullptr;
	FSLanguageProtocol *protocol = nullptr;
	// Written only after the language server has pointed the project at the staged fixture tree, so
	// every `res://` path below resolves.
	List<TemporaryScriptFile *> files;
	List<String> indexed_paths;

	ConformanceIndexInvalidationFixture() {
		editor_file_system = memnew(EditorFileSystem);
		protocol = FSTests::initialize(FSTests::root);

		write(WIDGET_PATH, "final class_name FsciWidget extends RefCounted\n");
		write(GADGET_PATH, "final class_name FsciGadget extends RefCounted\n");
		write(TRAIT_PATH, "trait_name FsciMarkable\n\nabstract func fsci_mark() -> String\n");
		// Assigning the target to its trait type only type-checks when the conformance is reachable,
		// so a stale analysis and a fresh one are told apart by the diagnostic itself.
		write(IMPORTER_PATH,
				"import fsci\n"
				"\n"
				"extends RefCounted\n"
				"\n"
				"\n"
				"func probe() -> bool:\n"
				"\tvar widget := FsciWidget.new()\n"
				"\tvar markable: FsciMarkable = widget\n"
				"\treturn markable != null\n");
		// In the namespace without importing anything: reached through `namespace`, not `import`.
		write(SIBLING_PATH, "namespace fsci\n\nextends RefCounted\n");
		// Neither in the namespace nor importing it: nothing a change to `fsci` can make stale.
		write(OUTSIDER_PATH, "extends RefCounted\n");

		add_global_class("FsciWidget", WIDGET_PATH, false);
		add_global_class("FsciGadget", GADGET_PATH, false);
		add_global_class("FsciMarkable", TRAIT_PATH, true);
	}

	~ConformanceIndexInvalidationFixture() {
		for (const String &path : indexed_paths) {
			FSLanguage::get_singleton()->remove_conformance_file(path);
		}
		FSCache::clear();
		if (protocol != nullptr) {
			memdelete(protocol);
		}
		memdelete(editor_file_system);
		ScriptServer::remove_global_class("FsciWidget");
		ScriptServer::remove_global_class("FsciGadget");
		ScriptServer::remove_global_class("FsciMarkable");
		FSConformanceRegistry::get_singleton()->clear();
		for (TemporaryScriptFile *file : files) {
			memdelete(file);
		}
	}

	void write(const String &p_path, const String &p_source) {
		files.push_back(memnew(TemporaryScriptFile(p_path, p_source)));
	}

	static void add_global_class(const String &p_name, const String &p_path, bool p_is_trait) {
		ScriptServer::add_global_class(p_name, "RefCounted", "FoundryScript", p_path, false, false, p_is_trait, false);
	}

	// Runs the same entry point the editor file-system scan uses, so the delta travels the real
	// claim/commit seam rather than a test-only shortcut.
	void index(const String &p_path) {
		if (!indexed_paths.find(p_path)) {
			indexed_paths.push_back(p_path);
		}
		FSLanguage::get_singleton()->update_global_declaration_index(p_path, p_path);
	}

	// Analyzes through the shared cache, exactly as a live session does. The returned ref must be kept
	// alive by the caller: `parser_map` holds a raw pointer, so the entry survives only while a Ref to
	// it does.
	static Ref<FSParserRef> analyze_cached(const String &p_path) {
		Error error = OK;
		return FSCache::get_parser(p_path, FSParserRef::FULLY_SOLVED, error);
	}

	static bool has_analysis_errors(const Ref<FSParserRef> &p_parser_ref) {
		if (p_parser_ref.is_null()) {
			return true;
		}
		const FSParser *parser = p_parser_ref->get_parser();
		return parser == nullptr || !parser->get_errors().is_empty();
	}

	// Only error-severity diagnostics: warnings are unrelated to whether the conformance is reachable.
	static Vector<String> error_diagnostics(const ExtendFSParser *p_parser) {
		Vector<String> messages;
		if (p_parser == nullptr) {
			return messages;
		}
		for (const LSP::Diagnostic &diagnostic : p_parser->get_diagnostics()) {
			if (diagnostic.severity == LSP::DiagnosticSeverity::Error) {
				messages.push_back(diagnostic.message);
			}
		}
		return messages;
	}

	String conformance_source(const String &p_target, const String &p_result) const {
		return vformat("namespace fsci\n\nextend %s uses FsciMarkable:\n\tfunc fsci_mark() -> String:\n\t\treturn \"%s\"\n", p_target, p_result);
	}
};

TEST_SUITE("[Modules][FoundryScript][Conformance]") {
	TEST_CASE("adding a namespace's first conformance evicts the importers cached without it") {
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		// Cached while `fsci` holds no conformance at all: the importer's snapshotted edges point at
		// nothing, so nothing about the file that appears next can reach it by dependency.
		Ref<FSParserRef> stale = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(stale.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		CHECK(ConformanceIndexInvalidationFixture::has_analysis_errors(stale));

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		CHECK_FALSE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));

		Ref<FSParserRef> refreshed = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		CHECK_FALSE(ConformanceIndexInvalidationFixture::has_analysis_errors(refreshed));

		// The re-parse snapshots the reach edge from the now-current index, which is what makes every
		// later edit of the conformance file reach the importer through the ordinary per-path funnel.
		FSParser *refreshed_parser = refreshed.is_valid() ? refreshed->get_parser() : nullptr;
		REQUIRE(refreshed_parser != nullptr);
		CHECK(refreshed_parser->get_dependencies().find(String(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH)) != nullptr);
	}

	TEST_CASE("adding a second conformance to a populated namespace evicts its importers") {
		// The general delta, not just the first-add extreme: the importer's edges point at the files
		// that existed when it parsed, and a brand-new file has no inverse-dependency entry at all.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		TemporaryScriptFile first_conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		Ref<FSParserRef> cached = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(cached.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		CHECK_FALSE(ConformanceIndexInvalidationFixture::has_analysis_errors(cached));

		TemporaryScriptFile second_conformance(ConformanceIndexInvalidationFixture::SECOND_CONFORMANCE_PATH, fixture.conformance_source("FsciGadget", "two"));
		fixture.index(ConformanceIndexInvalidationFixture::SECOND_CONFORMANCE_PATH);

		CHECK_FALSE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
	}

	TEST_CASE("removing a namespace's last conformance evicts its importers") {
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		Ref<FSParserRef> cached = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(cached.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		CHECK_FALSE(ConformanceIndexInvalidationFixture::has_analysis_errors(cached));

		// The funnel every removal path (rescan sweep, missing-file prune, moved namespace) goes
		// through.
		FSLanguage::get_singleton()->remove_conformance_file(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		CHECK_FALSE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));

		Ref<FSParserRef> refreshed = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		CHECK(ConformanceIndexInvalidationFixture::has_analysis_errors(refreshed));
	}

	TEST_CASE("a conformance delta evicts a same-namespace sibling that imports nothing") {
		// Membership in the namespace is a reach edge of its own, so the sweep cannot key on imports.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		Ref<FSParserRef> cached = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::SIBLING_PATH);
		REQUIRE(cached.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::SIBLING_PATH));

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		CHECK_FALSE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::SIBLING_PATH));
	}

	TEST_CASE("a conformance delta leaves a parser that does not reach the namespace cached") {
		// Over-invalidation is safe but wasteful; this pins the sweep against degenerating into
		// evict-everything.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		Ref<FSParserRef> cached = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::OUTSIDER_PATH);
		REQUIRE(cached.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::OUTSIDER_PATH));

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		CHECK(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::OUTSIDER_PATH));
	}

	TEST_CASE("a global-namespace conformance evicts nothing") {
		// The global namespace is not reached implicitly, so no parser holds a namespace-derived edge
		// to it and there is nothing a change to its file set can invalidate.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		Ref<FSParserRef> importer_ref = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		Ref<FSParserRef> sibling_ref = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::SIBLING_PATH);
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::SIBLING_PATH));

		TemporaryScriptFile global_conformance(ConformanceIndexInvalidationFixture::GLOBAL_CONFORMANCE_PATH,
				"extend FsciGadget uses FsciMarkable:\n\tfunc fsci_mark() -> String:\n\t\treturn \"global\"\n");
		fixture.index(ConformanceIndexInvalidationFixture::GLOBAL_CONFORMANCE_PATH);

		CHECK(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		CHECK(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::SIBLING_PATH));
	}

	TEST_CASE("re-indexing an unchanged conformance file evicts nothing") {
		// A rescan re-indexes every file it walks. Only a real membership change may cost a re-parse.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		Ref<FSParserRef> cached = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(cached.is_valid());
		REQUIRE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));

		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		CHECK(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
	}

	TEST_CASE("a conformance delta does not destroy a parser an analysis still holds") {
		// Eviction abandons the cache slot; the parser itself lives as long as any Ref to it does, so
		// an analysis in flight when the sweep runs keeps a valid parser.
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);

		Ref<FSParserRef> held = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(held.is_valid());
		const FSParser *parser_before = held->get_parser();
		REQUIRE(parser_before != nullptr);

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		REQUIRE_FALSE(TestFSCacheAccessor::has_parser(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
		CHECK(held.is_valid());
		CHECK_EQ(held->get_parser(), parser_before);
		CHECK_EQ(held->get_path(), String(ConformanceIndexInvalidationFixture::IMPORTER_PATH));
	}

	TEST_CASE("indexing a namespace's first conformance refreshes open LSP diagnostics for importers") {
		ConformanceIndexInvalidationFixture fixture;
		REQUIRE(fixture.protocol != nullptr);
		TestFSLanguageProtocolInitializer::mark_initialized(fixture.protocol);

		Ref<FSParserRef> primed = ConformanceIndexInvalidationFixture::analyze_cached(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(primed.is_valid());
		primed.unref();

		const String importer_source = FileAccess::get_file_as_string(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		const String importer_uri = fixture.protocol->get_workspace()->get_file_uri(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		fixture.protocol->get_text_document()->didOpen(make_did_open_params(importer_uri, importer_source));

		const ExtendFSParser *before = FSLanguageProtocol::get_singleton()->get_parse_result(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(before != nullptr);
		CHECK_GT(ConformanceIndexInvalidationFixture::error_diagnostics(before).size(), 0);

		TemporaryScriptFile conformance(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH, fixture.conformance_source("FsciWidget", "one"));
		fixture.index(ConformanceIndexInvalidationFixture::CONFORMANCE_PATH);

		const ExtendFSParser *after = FSLanguageProtocol::get_singleton()->get_parse_result(ConformanceIndexInvalidationFixture::IMPORTER_PATH);
		REQUIRE(after != nullptr);
		const Vector<String> remaining = ConformanceIndexInvalidationFixture::error_diagnostics(after);
		CHECK_MESSAGE(remaining.is_empty(), String(", ").join(remaining));
	}
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
