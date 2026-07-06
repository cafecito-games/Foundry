/**************************************************************************/
/*  test_analyzer_dependency_access.h                                     */
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

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

static FSParserRef::Status get_depended_parser_status_from_parser(FSParser *p_parser, const String &p_path) {
	if (p_parser == nullptr) {
		return FSParserRef::EMPTY;
	}
	const HashMap<String, Ref<FSParserRef>> &depended = p_parser->get_depended_parsers();
	if (const Ref<FSParserRef> *found = depended.getptr(p_path)) {
		if (found->is_valid()) {
			return (*found)->get_status();
		}
	}
	return FSParserRef::EMPTY;
}

static FSParserRef::Status get_transitive_depended_parser_status(const FSAnalyzer *p_analyzer, const String &p_direct_dependency_path, const String &p_path) {
	if (p_analyzer == nullptr) {
		return FSParserRef::EMPTY;
	}
	FSParserRef::Status status = FSAnalyzer::test_get_depended_parser_status(p_analyzer, p_path);
	if (status != FSParserRef::EMPTY) {
		return status;
	}
	const Ref<FSParserRef> direct = FSAnalyzer::test_get_depended_parser_ref(p_analyzer, p_direct_dependency_path);
	if (direct.is_null()) {
		return FSParserRef::EMPTY;
	}
	return get_depended_parser_status_from_parser(direct->get_parser(), p_path);
}

static String write_dependency_access_script(const String &p_dir, const String &p_file_name, const String &p_source) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	CHECK(dir.is_valid());
	if (dir.is_valid()) {
		CHECK_EQ(dir->make_dir_recursive(p_dir), OK);
	}
	const String path = p_dir.path_join(p_file_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	CHECK(file.is_valid());
	if (file.is_valid()) {
		file->store_string(p_source);
	}
	return path;
}

TEST_CASE("[Modules][FoundryScript][Analyzer] dependency access respects phase status ceilings") {
	const String dir = OS::get_singleton()->get_temp_path().path_join("analyzer_dependency_access_phases");

	const String root_path = write_dependency_access_script(dir, "dependency_access_root.fs", R"(
class RootType:
	const MARK := "root"
)");
	const String mid_path = write_dependency_access_script(dir, "dependency_access_mid.fs", R"(extends "dependency_access_root.fs")");
	const String leaf_path = write_dependency_access_script(dir, "dependency_access_leaf.fs", R"(
extends "dependency_access_mid.fs"

func read_mark() -> String:
	return RootType.new().MARK
)");

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);

	FSParser parser;
	REQUIRE_EQ(parser.parse(FileAccess::get_file_as_string(leaf_path), leaf_path, false), OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.resolve_inheritance(), OK);
	CHECK_EQ(FSAnalyzer::test_get_depended_parser_status(&analyzer, mid_path), FSParserRef::INHERITANCE_SOLVED);
	CHECK_EQ(get_transitive_depended_parser_status(&analyzer, mid_path, root_path), FSParserRef::INHERITANCE_SOLVED);
	CHECK(FSAnalyzer::test_get_depended_parser_status(&analyzer, mid_path) < FSParserRef::INTERFACE_SOLVED);
	CHECK(get_transitive_depended_parser_status(&analyzer, mid_path, root_path) < FSParserRef::INTERFACE_SOLVED);

	REQUIRE_EQ(analyzer.resolve_interface(), OK);
	CHECK(FSAnalyzer::test_get_depended_parser_status(&analyzer, mid_path) <= FSParserRef::INTERFACE_SOLVED);
	CHECK(get_transitive_depended_parser_status(&analyzer, mid_path, root_path) <= FSParserRef::INTERFACE_SOLVED);
	CHECK(FSAnalyzer::test_get_depended_parser_status(&analyzer, mid_path) < FSParserRef::FULLY_SOLVED);
	CHECK(get_transitive_depended_parser_status(&analyzer, mid_path, root_path) < FSParserRef::FULLY_SOLVED);

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);
}

TEST_CASE("[Modules][FoundryScript][Analyzer] dependency access caches transitive external parser lookups") {
	const String dir = OS::get_singleton()->get_temp_path().path_join("analyzer_dependency_access_cache");

	const String root_path = write_dependency_access_script(dir, "dependency_access_cache_root.fs", R"(
class CachedType:
	const TOKEN := "cached"
)");
	const String mid_path = write_dependency_access_script(dir, "dependency_access_cache_mid.fs", R"(extends "dependency_access_cache_root.fs")");
	const String leaf_path = write_dependency_access_script(dir, "dependency_access_cache_leaf.fs", R"(
extends "dependency_access_cache_mid.fs"

func use_cached() -> String:
	return CachedType.new().TOKEN
)");

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);

	FSParser parser;
	REQUIRE_EQ(parser.parse(FileAccess::get_file_as_string(leaf_path), leaf_path, false), OK);

	FSAnalyzer analyzer(&parser);
	CHECK_EQ(FSAnalyzer::test_get_external_parser_cache_size(&analyzer), 0);

	REQUIRE_EQ(analyzer.resolve_inheritance(), OK);
	CHECK(FSAnalyzer::test_get_external_parser_cache_size(&analyzer) > 0);

	REQUIRE_EQ(analyzer.resolve_interface(), OK);
	CHECK(FSAnalyzer::test_get_external_parser_cache_size(&analyzer) > 0);

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);
}

TEST_CASE("[Modules][FoundryScript][Analyzer] dependency access raises applied external traits to FULLY_SOLVED") {
	const String dir = OS::get_singleton()->get_temp_path().path_join("analyzer_dependency_access_trait");

	const String trait_path = write_dependency_access_script(dir, "dependency_access_trait.fs", R"(
trait_name Counting
func increment(value: int) -> int:
	return value + 1
)");
	const String user_path = write_dependency_access_script(dir, "dependency_access_user.fs", R"(
extends RefCounted
uses Counting

func bump(value: int) -> int:
	return increment(value)
)");

	FSCache::remove_parser(trait_path);
	FSCache::remove_parser(user_path);

	ScriptServer::add_global_class("Counting", "RefCounted", FSLanguage::get_singleton()->get_name(), trait_path, false, false, true);

	const String user_source = R"(
extends RefCounted
uses Counting

func bump(value: int) -> int:
	return increment(value)
)";
	FSParser parser;
	REQUIRE_EQ(parser.parse(user_source, user_path, false), OK);

	FSAnalyzer analyzer(&parser);
	REQUIRE_EQ(analyzer.resolve_inheritance(), OK);
	REQUIRE_EQ(analyzer.resolve_interface(), OK);

	const FSParserRef::Status status_before_body = FSAnalyzer::test_get_depended_parser_status(&analyzer, trait_path);
	CHECK(status_before_body <= FSParserRef::INTERFACE_SOLVED);
	CHECK(status_before_body < FSParserRef::FULLY_SOLVED);

	REQUIRE_EQ(analyzer.resolve_body(), OK);
	CHECK_EQ(FSAnalyzer::test_get_depended_parser_status(&analyzer, trait_path), FSParserRef::FULLY_SOLVED);

	if (ScriptServer::is_global_class("Counting")) {
		ScriptServer::remove_global_class("Counting");
	}
}

} // namespace FSTests
