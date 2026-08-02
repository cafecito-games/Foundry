/**************************************************************************/
/*  test_conformance_visibility.h                                         */
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

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

// A retroactive conformance takes effect for code that loads its declaring file, the way an import
// does. The registry backing it is process-global and fills up as a side effect of analyzing whatever
// files a process touches, so the property under test is only observable across *two* analyses in one
// process: analyze the declaring file, then analyze a file that never loads it.
//
// The `.fs` fixture runner cannot show this. It clears the registry before every fixture precisely so
// that fixtures stay isolated, which is the same leakage this scoping prevents.
namespace FSTests {

struct ConformanceVisibilityFixture {
	String dir;
	String widget_path;
	String root_trait_path;
	String trait_path;
	String conformance_path;

	ConformanceVisibilityFixture() {
		dir = OS::get_singleton()->get_temp_path().path_join("foundry_conformance_visibility");
		widget_path = write("fsv_widget.fs", R"(class_name FsvWidget
extends RefCounted
)");
		root_trait_path = write("fsv_root.fs", R"(trait_name FsvRoot

abstract static func fsv_mark() -> int
)");
		trait_path = write("fsv_markable.fs", R"(trait_name FsvMarkable

uses FsvRoot
)");
		conformance_path = write("fsv_conformance.fs", R"(extend FsvWidget uses FsvMarkable:
	static func fsv_mark() -> int:
		return 5
)");
		register_global_class("FsvWidget", widget_path, "RefCounted", false);
		register_global_class("FsvRoot", root_trait_path, "RefCounted", true);
		register_global_class("FsvMarkable", trait_path, "RefCounted", true);
	}

	~ConformanceVisibilityFixture() {
		ScriptServer::remove_global_class("FsvWidget");
		ScriptServer::remove_global_class("FsvRoot");
		ScriptServer::remove_global_class("FsvMarkable");
		FSConformanceRegistry::get_singleton()->clear();
	}

	String write(const String &p_file_name, const String &p_source) {
		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir_access.is_valid());
		REQUIRE_EQ(dir_access->make_dir_recursive(dir), OK);
		const String path = dir.path_join(p_file_name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_source);
		file->flush();
		FSCache::remove_parser(path);
		return path;
	}

	static void register_global_class(const String &p_name, const String &p_path, const String &p_base, bool p_is_trait) {
		ScriptServer::add_global_class(p_name, p_base, "FoundryScript", p_path, false, false, p_is_trait, false);
	}

	// Analyzes `p_path` to completion and reports whether the analysis was clean.
	static bool analyze_is_clean(const String &p_path) {
		FSCache::remove_parser(p_path);
		FSParser parser;
		if (parser.parse(FileAccess::get_file_as_string(p_path), p_path, false) != OK) {
			return false;
		}
		FSAnalyzer analyzer(&parser);
		const Error err = analyzer.analyze();
		return err == OK && parser.get_errors().is_empty();
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] a conformance reaches only the files that load it") {
	ConformanceVisibilityFixture fixture;

	// Analyzing the declaring file registers the conformance process-wide. Everything below runs with
	// that registration in place — which is exactly the situation a real project is in once any file
	// has pulled the conformance in.
	REQUIRE(ConformanceVisibilityFixture::analyze_is_clean(fixture.conformance_path));
	const Vector<FSConformanceRegistry::Conformance> entries =
			FSConformanceRegistry::get_singleton()->get_file_conformances(fixture.conformance_path);
	REQUIRE_EQ(entries.size(), 2);
	if (entries.size() != 2) {
		return;
	}
	const FSConformanceRegistry::Conformance *markable_entry = nullptr;
	const FSConformanceRegistry::Conformance *root_entry = nullptr;
	for (const FSConformanceRegistry::Conformance &entry : entries) {
		if (entry.trait_name == SNAME("FsvMarkable")) {
			markable_entry = &entry;
		} else if (entry.trait_name == SNAME("FsvRoot")) {
			root_entry = &entry;
		}
	}
	REQUIRE(markable_entry != nullptr);
	REQUIRE(root_entry != nullptr);
	if (markable_entry == nullptr || root_entry == nullptr) {
		return;
	}
	CHECK_EQ(root_entry->source_file, markable_entry->source_file);
	CHECK_EQ(root_entry->target_fqcn, markable_entry->target_fqcn);
	CHECK_EQ(root_entry->conformance_index, markable_entry->conformance_index);
	REQUIRE(root_entry->witnesses.has(SNAME("fsv_mark")));
	REQUIRE(markable_entry->witnesses.has(SNAME("fsv_mark")));
	CHECK_EQ(root_entry->witnesses[SNAME("fsv_mark")], markable_entry->witnesses[SNAME("fsv_mark")]);
	CHECK_EQ(FSConformanceRegistry::get_singleton()->get_conformance_source("FsvWidget", SNAME("FsvRoot")),
			fixture.conformance_path);
	CHECK(FSConformanceRegistry::get_singleton()->get_witnesses("FsvWidget", SNAME("FsvRoot")).has(SNAME("fsv_mark")));

	SUBCASE("a file that loads the declaring file sees it") {
		const String consumer_path = fixture.write("fsv_consumer_loading.fs", R"(extends RefCounted

const _Conformance = preload("fsv_conformance.fs")


func probe() -> int:
	var widget := FsvWidget.new()
	var root: FsvRoot = widget
	return FsvWidget.fsv_mark() + int(root != null)
)");
		CHECK(ConformanceVisibilityFixture::analyze_is_clean(consumer_path));
	}

	SUBCASE("a file that does not load the declaring file does not") {
		const String consumer_path = fixture.write("fsv_consumer_not_loading.fs", R"(extends RefCounted

func probe() -> int:
	var widget := FsvWidget.new()
	var root: FsvRoot = widget
	return FsvWidget.fsv_mark() + int(root != null)
)");
		CHECK_FALSE(ConformanceVisibilityFixture::analyze_is_clean(consumer_path));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] implied memberships keep coherence and clear with their source") {
	ConformanceVisibilityFixture fixture;
	REQUIRE(ConformanceVisibilityFixture::analyze_is_clean(fixture.conformance_path));

	const String duplicate_path = fixture.write("fsv_root_duplicate.fs", R"(extend FsvWidget uses FsvRoot:
	static func fsv_mark() -> int:
		return 9
)");
	CHECK_FALSE(ConformanceVisibilityFixture::analyze_is_clean(duplicate_path));

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	CHECK(registry->has_conformance("FsvWidget", SNAME("FsvMarkable")));
	CHECK(registry->has_conformance("FsvWidget", SNAME("FsvRoot")));
	registry->clear_file(fixture.conformance_path);
	CHECK_FALSE(registry->has_conformance("FsvWidget", SNAME("FsvMarkable")));
	CHECK_FALSE(registry->has_conformance("FsvWidget", SNAME("FsvRoot")));
}

// A conformance-only file exports no global class, so no consumer can name it and name resolution
// alone never reaches it. The namespace it declares its conformances in is the reachable identity:
// a file that imports that namespace — or is in it — loads the file, exactly as a `preload` would.
// The project-wide index that makes such a file discoverable is filled by the file-system scan, which
// this fixture stands in for by calling the same entry point.
//
// Only the conformance file is namespaced. Both the target and the trait are global classes, so no
// consumer picks the conformance up as a side effect of loading a *different* file in `fsn` — the
// namespace edge under test is the only route to it.
struct NamespacedConformanceFixture {
	String dir;
	String widget_path;
	String trait_path;
	String conformance_path;

	NamespacedConformanceFixture() {
		dir = OS::get_singleton()->get_temp_path().path_join("foundry_namespaced_conformance");
		widget_path = write("fsn_widget.fs", R"(final class_name FsnWidget extends RefCounted

var fsn_label: String = "widget"
)");
		trait_path = write("fsn_gadgetlike.fs", R"(trait_name FsnGadgetlike

abstract func fsn_gadget() -> String
)");
		conformance_path = write("fsn_conformance.fs", R"(namespace fsn

extend FsnWidget uses FsnGadgetlike:
	func fsn_gadget() -> String:
		return "gadget:" + fsn_label
)");
		ConformanceVisibilityFixture::register_global_class("FsnWidget", widget_path, "RefCounted", false);
		ConformanceVisibilityFixture::register_global_class("FsnGadgetlike", trait_path, "RefCounted", true);
		FSLanguage::get_singleton()->update_global_declaration_index(conformance_path, conformance_path);
	}

	~NamespacedConformanceFixture() {
		ScriptServer::remove_global_class("FsnWidget");
		ScriptServer::remove_global_class("FsnGadgetlike");
		FSLanguage::get_singleton()->remove_conformance_file(conformance_path);
		FSConformanceRegistry::get_singleton()->clear();
	}

	String write(const String &p_file_name, const String &p_source) {
		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir_access.is_valid());
		REQUIRE_EQ(dir_access->make_dir_recursive(dir), OK);
		const String path = dir.path_join(p_file_name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_source);
		file->flush();
		FSCache::remove_parser(path);
		return path;
	}

	// Analyzes `p_path` and returns every error message it reported, so a test can assert both that a
	// consumer is clean and that a rejected one is rejected for the stated reason.
	static Vector<String> analysis_errors(const String &p_path) {
		FSCache::remove_parser(p_path);
		Vector<String> messages;
		FSParser parser;
		if (parser.parse(FileAccess::get_file_as_string(p_path), p_path, false) != OK) {
			messages.push_back("parse failed");
			return messages;
		}
		FSAnalyzer analyzer(&parser);
		analyzer.analyze();
		for (const FSParser::ParserError &error : parser.get_errors()) {
			messages.push_back(error.message);
		}
		return messages;
	}

	static bool any_error_contains(const Vector<String> &p_messages, const String &p_needle) {
		for (const String &message : p_messages) {
			if (message.contains(p_needle)) {
				return true;
			}
		}
		return false;
	}

	// Both needles in *one* message, so a pair of unrelated diagnostics cannot stand in for the
	// hidden-witness error, which names the call and the file to load in the same sentence.
	static bool any_error_contains_both(const Vector<String> &p_messages, const String &p_first, const String &p_second) {
		for (const String &message : p_messages) {
			if (message.contains(p_first) && message.contains(p_second)) {
				return true;
			}
		}
		return false;
	}
};

TEST_CASE("[Modules][FoundryScript][Conformance] a namespace import reaches the conformances declared in it") {
	NamespacedConformanceFixture fixture;

	SUBCASE("importing the namespace reports the declaring file as a dependency") {
		const String consumer_path = fixture.write("fsn_consumer_dependency.fs", R"(import fsn

extends RefCounted
)");
		FSParser parser;
		REQUIRE_EQ(parser.parse(FileAccess::get_file_as_string(consumer_path), consumer_path, false), OK);
		CHECK(parser.get_namespace_conformance_dependencies().find(fixture.conformance_path) != nullptr);
		// The generic dependency list carries it too, so cache invalidation and export packaging both
		// follow the same edge the analyzer does.
		CHECK(parser.get_dependencies().find(fixture.conformance_path) != nullptr);
	}

	SUBCASE("a file importing the namespace type-checks against the conformance") {
		const String consumer_path = fixture.write("fsn_consumer_importing.fs", R"(import fsn

extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	var gadget: FsnGadgetlike = widget
	return gadget.fsn_gadget() + str(widget is FsnGadgetlike) + widget.fsn_gadget()
)");
		CHECK(fixture.analysis_errors(consumer_path).is_empty());
	}

	SUBCASE("a file in the namespace reaches it without importing anything") {
		const String consumer_path = fixture.write("fsn_consumer_sibling.fs", R"(namespace fsn

extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	var gadget: FsnGadgetlike = widget
	return gadget.fsn_gadget()
)");
		CHECK(fixture.analysis_errors(consumer_path).is_empty());
	}

	SUBCASE("a file outside the namespace still does not reach it") {
		const String consumer_path = fixture.write("fsn_consumer_outside.fs", R"(extends RefCounted


func probe() -> bool:
	var widget := FsnWidget.new()
	var gadget: FsnGadgetlike = widget
	return gadget != null
)");
		CHECK_FALSE(fixture.analysis_errors(consumer_path).is_empty());
	}

	SUBCASE("an instance call on a witness it cannot reach is rejected, not deferred to run time") {
		// Analyzing the declaring file first puts its conformance in the process-global registry. The
		// cold counterpart — declaring file indexed but never analyzed — is covered by "the
		// hidden-witness diagnostic does not depend on analysis order" below.
		REQUIRE(fixture.analysis_errors(fixture.conformance_path).is_empty());

		const String consumer_path = fixture.write("fsn_consumer_hidden_call.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	return widget.fsn_gadget()
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);
		CHECK(NamespacedConformanceFixture::any_error_contains(errors, "fsn_gadget()"));
		// The diagnostic has to name the file to load, or it is no better than the run-time failure.
		CHECK(NamespacedConformanceFixture::any_error_contains(errors, fixture.conformance_path));
	}

	SUBCASE("a visible witness on the receiver shadows a hidden one on its base") {
		// Witness dispatch is most-derived-first, so a conformance the file *can* reach on the
		// receiver itself is what the call lands on. A hidden conformance further up the chain is
		// shadowed and must not turn a working call into an error.
		const String base_path = fixture.write("fsn_base_widget.fs", R"(class_name FsnBaseWidget extends RefCounted
)");
		const String derived_path = fixture.write("fsn_derived_widget.fs", R"(final class_name FsnDerivedWidget extends FsnBaseWidget
)");
		const String hidden_base_conformance = fixture.write("fsn_base_conformance.fs", R"(namespace fsn

extend FsnBaseWidget uses FsnGadgetlike:
	func fsn_gadget() -> String:
		return "base"
)");
		const String visible_conformance = fixture.write("fsn_derived_conformance.fs", R"(extend FsnDerivedWidget uses FsnGadgetlike:
	func fsn_gadget() -> String:
		return "derived"
)");
		ConformanceVisibilityFixture::register_global_class("FsnBaseWidget", base_path, "RefCounted", false);
		ConformanceVisibilityFixture::register_global_class("FsnDerivedWidget", derived_path, "FsnBaseWidget", false);
		FSLanguage::get_singleton()->add_conformance_file(hidden_base_conformance, "fsn");
		REQUIRE(fixture.analysis_errors(hidden_base_conformance).is_empty());

		const String consumer_path = fixture.write("fsn_consumer_shadowed.fs", R"(extends RefCounted

const _Conformance = preload("fsn_derived_conformance.fs")


func probe() -> String:
	var widget := FsnDerivedWidget.new()
	return widget.fsn_gadget()
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FsnBaseWidget");
		ScriptServer::remove_global_class("FsnDerivedWidget");
		FSLanguage::get_singleton()->remove_conformance_file(hidden_base_conformance);
		FSConformanceRegistry::get_singleton()->clear_file(visible_conformance);

		CHECK(errors.is_empty());
	}

	SUBCASE("an open receiver keeps its unsafe-but-legal call") {
		// `FsnOpenWidget` is not `final`, so a subtype could declare `fsn_gadget()` of its own and the
		// runtime would resolve it. Rejecting the call because a conformance this file cannot reach
		// happens to supply the same name would reject working code, so it stays merely unsafe.
		const String open_widget_path = fixture.write("fsn_open_widget.fs", R"(class_name FsnOpenWidget extends RefCounted
)");
		const String open_conformance_path = fixture.write("fsn_open_conformance.fs", R"(namespace fsn

extend FsnOpenWidget uses FsnGadgetlike:
	func fsn_gadget() -> String:
		return "open"
)");
		ConformanceVisibilityFixture::register_global_class("FsnOpenWidget", open_widget_path, "RefCounted", false);
		FSLanguage::get_singleton()->add_conformance_file(open_conformance_path, "fsn");
		REQUIRE(fixture.analysis_errors(open_conformance_path).is_empty());

		const String consumer_path = fixture.write("fsn_consumer_open_call.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnOpenWidget.new()
	return str(widget.fsn_gadget())
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FsnOpenWidget");
		FSLanguage::get_singleton()->remove_conformance_file(open_conformance_path);

		CHECK(errors.is_empty());
	}

	SUBCASE("a final receiver with no witness anywhere is a hard error") {
		// The contrast to the carve-out cases above: the class is closed, so no subtype can ever
		// declare the name, and no conformance — reachable or hidden — supplies it either. Nothing can
		// make this call resolve, and before this it produced no diagnostic at all by default.
		const String closed_widget_path = fixture.write("fsn_closed_widget.fs", R"(final class_name FsnClosedWidget extends RefCounted


func fsn_present() -> String:
	return "present"
)");
		ConformanceVisibilityFixture::register_global_class("FsnClosedWidget", closed_widget_path, "RefCounted", false);

		const String consumer_path = fixture.write("fsn_consumer_closed_call.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnClosedWidget.new()
	return widget.fsn_missing()
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FsnClosedWidget");

		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsn_missing()", "so no subtype can supply it"));
	}
}

// The registry is filled as a side effect of analyzing files, so before this the hidden-witness
// diagnostic could only fire when something else in the same process had already analyzed the
// declaring file. On a cold run the call degraded to an unsafe-access warning and failed at run time.
// Every subcase below analyzes the consumer with the declaring file never analyzed, only indexed.
TEST_CASE("[Modules][FoundryScript][Conformance] the hidden-witness diagnostic does not depend on analysis order") {
	NamespacedConformanceFixture fixture;
	// Nothing may carry over from an earlier test: a warm registry would answer these on its own.
	FSConformanceRegistry::get_singleton()->clear();

	SUBCASE("a cold registry still rejects a call on an unreachable namespace conformance") {
		const String consumer_path = fixture.write("fsn_consumer_cold_call.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	return widget.fsn_gadget()
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);
		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsn_gadget()", fixture.conformance_path));
	}

	SUBCASE("a cold registry still rejects a call on an unreachable global-namespace conformance") {
		// The flat shape: no `namespace` anywhere, so no import could have brought the conformance in
		// and nothing but the index knows the declaring file exists.
		const String widget_path = fixture.write("fsg_widget.fs", R"(final class_name FsgWidget extends RefCounted

var fsg_label: String = "widget"
)");
		const String trait_path = fixture.write("fsg_gadgetlike.fs", R"(trait_name FsgGadgetlike

abstract func fsg_gadget() -> String
)");
		const String conformance_path = fixture.write("fsg_conformance.fs", R"(extend FsgWidget uses FsgGadgetlike:
	func fsg_gadget() -> String:
		return "gadget:" + fsg_label
)");
		ConformanceVisibilityFixture::register_global_class("FsgWidget", widget_path, "RefCounted", false);
		ConformanceVisibilityFixture::register_global_class("FsgGadgetlike", trait_path, "RefCounted", true);
		FSLanguage::get_singleton()->add_conformance_file(conformance_path, "");

		const String consumer_path = fixture.write("fsg_consumer_cold_call.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsgWidget.new()
	return widget.fsg_gadget()
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FsgWidget");
		ScriptServer::remove_global_class("FsgGadgetlike");
		FSLanguage::get_singleton()->remove_conformance_file(conformance_path);

		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsg_gadget()", conformance_path));
	}

	SUBCASE("the probe does not make the hidden witness reachable") {
		// Registering a file's conformances must not put it in the dependency graph: that would make
		// the conformance *visible*, type-checking the call with no load edge behind it and bringing
		// back the run-time failure the diagnostic exists to prevent.
		const String consumer_path = fixture.write("fsn_consumer_cold_reach.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	var gadget: FsnGadgetlike = widget
	return widget.fsn_gadget() + str(gadget != null)
)");
		FSCache::remove_parser(consumer_path);
		FSParser parser;
		REQUIRE_EQ(parser.parse(FileAccess::get_file_as_string(consumer_path), consumer_path, false), OK);
		FSAnalyzer analyzer(&parser);
		analyzer.analyze();

		Vector<String> errors;
		for (const FSParser::ParserError &error : parser.get_errors()) {
			errors.push_back(error.message);
		}
		// The trait-typed assignment stays rejected, and the call is still named as hidden.
		CHECK(NamespacedConformanceFixture::any_error_contains(errors, "with specified type FsnGadgetlike"));
		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsn_gadget()", fixture.conformance_path));
		CHECK(parser.get_dependencies().find(fixture.conformance_path) == nullptr);
		CHECK_FALSE(parser.get_depended_parsers().has(fixture.conformance_path));
	}

	SUBCASE("an unindexed conformance file changes nothing") {
		const String widget_path = fixture.write("fsu_widget.fs", R"(final class_name FsuWidget extends RefCounted
)");
		const String trait_path = fixture.write("fsu_gadgetlike.fs", R"(trait_name FsuGadgetlike

abstract func fsu_gadget() -> String
)");
		const String conformance_path = fixture.write("fsu_conformance.fs", R"(extend FsuWidget uses FsuGadgetlike:
	func fsu_gadget() -> String:
		return "gadget"
)");
		ConformanceVisibilityFixture::register_global_class("FsuWidget", widget_path, "RefCounted", false);
		ConformanceVisibilityFixture::register_global_class("FsuGadgetlike", trait_path, "RefCounted", true);

		const String consumer_path = fixture.write("fsu_consumer.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsuWidget.new()
	return str(widget.fsu_gadget())
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FsuWidget");
		ScriptServer::remove_global_class("FsuGadgetlike");

		// The index is the only source of truth the probe has, so a file it does not list can neither
		// be named by a diagnostic nor make the call reachable. What is left is the plain closed-class
		// rejection: `FsuWidget` is `final` and nothing this file reaches supplies the name.
		CHECK_FALSE(NamespacedConformanceFixture::any_error_contains(errors, conformance_path));
		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsu_gadget()", "so no subtype can supply it"));
	}

	SUBCASE("a conformance file that fails to parse is skipped") {
		const String broken_path = fixture.write("fsn_broken_conformance.fs", R"(extend FsnWidget uses
	func (
)");
		FSLanguage::get_singleton()->add_conformance_file(broken_path, "");

		const String consumer_path = fixture.write("fsn_consumer_broken_probe.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FsnWidget.new()
	return str(widget.fsn_absent())
)");
		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		FSLanguage::get_singleton()->remove_conformance_file(broken_path);

		// A broken probed file contributes nothing, and its own errors stay in its own parser. The call
		// is left to the plain closed-class rejection, with nothing pointing at the broken file.
		CHECK_FALSE(NamespacedConformanceFixture::any_error_contains(errors, broken_path));
		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsn_absent()", "so no subtype can supply it"));
	}

	SUBCASE("a declaring file that preloads the consumer does not create a false cycle") {
		const String widget_path = fixture.write("fsc_widget.fs", R"(final class_name FscWidget extends RefCounted
)");
		ConformanceVisibilityFixture::register_global_class("FscWidget", widget_path, "RefCounted", false);
		const String consumer_path = fixture.write("fsc_consumer.fs", R"(extends RefCounted


func probe() -> String:
	var widget := FscWidget.new()
	return widget.fsn_gadget()
)");
		const String conformance_path = fixture.write("fsc_conformance.fs", R"(const _Consumer = preload("fsc_consumer.fs")

extend FscWidget uses FsnGadgetlike:
	func fsn_gadget() -> String:
		return "cycle"
)");
		FSLanguage::get_singleton()->add_conformance_file(conformance_path, "");

		const Vector<String> errors = fixture.analysis_errors(consumer_path);

		ScriptServer::remove_global_class("FscWidget");
		FSLanguage::get_singleton()->remove_conformance_file(conformance_path);

		CHECK(NamespacedConformanceFixture::any_error_contains_both(errors, "fsn_gadget()", conformance_path));
		CHECK_FALSE(NamespacedConformanceFixture::any_error_contains(errors, "Cyclic reference"));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] the conformance index survives a project cache round trip") {
	// An exported project never rescans its scripts: the cache written at export time is the only
	// record that a conformance-only file exists. Without this round trip a namespace import would
	// resolve in the editor and fail in the exported game.
	FSLanguage *language = FSLanguage::get_singleton();
	const String conformance_path = "res://exported/fsp_conformance.fs";
	language->add_conformance_file(conformance_path, "fsp");

	const Array persisted = ScriptServer::get_global_conformances();
	bool found = false;
	for (const Variant &entry : persisted) {
		const Dictionary conformance = entry;
		if (String(conformance.get("path", String())) == conformance_path) {
			found = true;
			CHECK_EQ(String(conformance.get("namespace", String())), "fsp");
			CHECK_EQ(String(conformance.get("language", String())), String(language->get_name()));
		}
	}
	CHECK(found);

	// Restoring from the cache is what an exported project does at startup.
	language->remove_conformance_file(conformance_path);
	REQUIRE(language->get_conformance_files_in_namespace("fsp").is_empty());
	language->add_indexed_conformance(conformance_path, "fsp");
	CHECK(language->get_conformance_files_in_namespace("fsp").has(conformance_path));

	// A file deleted between editor sessions comes back from the cache and is never visited by the
	// scan, so the prune the editor runs before rewriting the cache is what evicts it.
	ScriptServer::prune_missing_global_conformances();
	CHECK(language->get_conformance_files_in_namespace("fsp").is_empty());

	language->remove_conformance_file(conformance_path);
}

TEST_CASE("[Modules][FoundryScript][Conformance] a bootstrap root bounds namespace conformance reach") {
	// A build task bootstrap may only reach files inside its provider root. Namespace membership is
	// not a per-dependency opt-in, so a conformance file outside the root is simply not reachable
	// while a bootstrap is active — otherwise being in a shared namespace would be enough to pull an
	// arbitrary script into the bootstrap and run it.
	NamespacedConformanceFixture fixture;
	const String consumer_path = fixture.write("fsn_bootstrap_consumer.fs", R"(import fsn

extends RefCounted
)");
	FSParser parser;
	REQUIRE_EQ(parser.parse(FileAccess::get_file_as_string(consumer_path), consumer_path, false), OK);
	REQUIRE(parser.get_namespace_conformance_dependencies().find(fixture.conformance_path) != nullptr);

	FSAnalyzer::set_bootstrap_allowed_dependency_root(fixture.dir.path_join("provider_root"));
	const List<String> bounded = parser.get_namespace_conformance_dependencies();
	FSAnalyzer::set_bootstrap_allowed_dependency_root(String());

	CHECK(bounded.find(fixture.conformance_path) == nullptr);
	// The bound is the root, not a blanket refusal: the same file inside it stays reachable.
	CHECK(FSAnalyzer::is_bootstrap_path_allowed(fixture.conformance_path));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a rescan drops conformance files that no longer exist") {
	// A rescan of a root is the source of truth for it. A conformance file deleted since the last
	// scan has to stop being advertised: it is reached by importing its namespace, so a stale entry
	// would make the compiler try to load a path that is gone and fail an otherwise valid import.
	FSLanguage *language = FSLanguage::get_singleton();
	const String scanned_path = "res://scanned_root/fsr_conformance.fs";
	const String outside_path = "res://other_root/fsr_conformance.fs";
	language->add_conformance_file(scanned_path, "fsr");
	language->add_conformance_file(outside_path, "fsr");
	REQUIRE_EQ(language->get_conformance_files_in_namespace("fsr").size(), 2);

	language->clear_global_declaration_index_under("res://scanned_root/");

	const Vector<String> remaining = language->get_conformance_files_in_namespace("fsr");
	CHECK_EQ(remaining.size(), 1);
	CHECK(remaining.has(outside_path));
	CHECK_FALSE(remaining.has(scanned_path));

	language->remove_conformance_file(outside_path);
	CHECK(language->get_conformance_files_in_namespace("fsr").is_empty());
}

} // namespace FSTests
