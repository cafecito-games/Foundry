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
	String trait_path;
	String conformance_path;

	ConformanceVisibilityFixture() {
		dir = OS::get_singleton()->get_temp_path().path_join("foundry_conformance_visibility");
		widget_path = write("fsv_widget.fs", R"(class_name FsvWidget
extends RefCounted
)");
		trait_path = write("fsv_markable.fs", R"(trait_name FsvMarkable

abstract static func fsv_mark() -> int
)");
		conformance_path = write("fsv_conformance.fs", R"(extend FsvWidget uses FsvMarkable:
	static func fsv_mark() -> int:
		return 5
)");
		register_global_class("FsvWidget", widget_path, "RefCounted", false);
		register_global_class("FsvMarkable", trait_path, "RefCounted", true);
	}

	~ConformanceVisibilityFixture() {
		ScriptServer::remove_global_class("FsvWidget");
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

	SUBCASE("a file that loads the declaring file sees it") {
		const String consumer_path = fixture.write("fsv_consumer_loading.fs", R"(extends RefCounted

const _Conformance = preload("fsv_conformance.fs")


func probe() -> int:
	var widget := FsvWidget.new()
	var markable: FsvMarkable = widget
	return FsvWidget.fsv_mark() + int(markable != null)
)");
		CHECK(ConformanceVisibilityFixture::analyze_is_clean(consumer_path));
	}

	SUBCASE("a file that does not load the declaring file does not") {
		const String consumer_path = fixture.write("fsv_consumer_not_loading.fs", R"(extends RefCounted

func probe() -> int:
	var widget := FsvWidget.new()
	var markable: FsvMarkable = widget
	return FsvWidget.fsv_mark() + int(markable != null)
)");
		CHECK_FALSE(ConformanceVisibilityFixture::analyze_is_clean(consumer_path));
	}
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
		// The registry has to hold the conformance for this to be the case under test at all: without
		// it the call is merely unresolved, which is a different (and pre-existing) diagnostic.
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
