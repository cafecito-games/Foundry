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

} // namespace FSTests
