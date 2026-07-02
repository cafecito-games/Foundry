/**************************************************************************/
/*  fs_test_runner_suite.h                                                */
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

#include "fs_test_runner.h"

#include "modules/foundry_script/fs_cache.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

class TestFSCacheAccessor {
public:
	static bool has_shallow(String p_path) {
		return FSCache::singleton->shallow_fs_cache.has(p_path);
	}

	static bool has_full(String p_path) {
		return FSCache::singleton->full_fs_cache.has(p_path);
	}

	static bool has_parser(String p_path) {
		return FSCache::singleton->parser_map.has(p_path);
	}

	static Ref<FoundryScript> get_static(String p_fully_qualified_name) {
		const Ref<FoundryScript> *found = FSCache::singleton->static_fs_cache.getptr(p_fully_qualified_name);
		return found != nullptr ? *found : Ref<FoundryScript>();
	}
};

// The full `.fs` fixture suite stays editor-only; release/template VM runtime error
// coverage lives in `test_release_vm_runtime_error.h`, which runs under `tests=yes`
// without `TOOLS_ENABLED`. See: https://github.com/godotengine/godot/pull/88452
#ifdef TOOLS_ENABLED
TEST_SUITE("[Modules][FoundryScript]") {
	TEST_CASE("Script compilation and runtime") {
		bool print_filenames = OS::get_singleton()->get_cmdline_args().find("--print-filenames") != nullptr;
		bool use_binary_tokens = OS::get_singleton()->get_cmdline_args().find("--use-binary-tokens") != nullptr;
		FSTestRunner runner("modules/foundry_script/tests/scripts", true, print_filenames, use_binary_tokens);
		int fail_count = runner.run_tests();
		INFO("Make sure `*.out` files have expected results.");
		REQUIRE_MESSAGE(fail_count == 0, "All FoundryScript tests should pass.");
	}

	TEST_CASE("Script compilation and runtime with compiled bytecode round-trip") {
		bool print_filenames = OS::get_singleton()->get_cmdline_args().find("--print-filenames") != nullptr;
		FSTestRunner runner("modules/foundry_script/tests/scripts", true, print_filenames, false, true);
		int fail_count = runner.run_tests();
		INFO("Make sure `*.out` files have expected results.");
		REQUIRE_MESSAGE(fail_count == 0, "All FoundryScript tests should pass when round-tripped through compiled bytecode.");
	}
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript] Load source code dynamically and run it") {
	FSLanguage::get_singleton()->init();
	Ref<FoundryScript> foundry_script = memnew(FoundryScript);
	foundry_script->set_source_code(R"(
extends RefCounted

func _init():
	set_meta("result", 42)
)");
	// A spurious `Condition "err" is true` message is printed (despite parsing being successful and returning `OK`).
	// Silence it.
	ERR_PRINT_OFF;
	const Error error = foundry_script->reload();
	ERR_PRINT_ON;
	CHECK_MESSAGE(error == OK, "The script should parse successfully.");

	// Run the script by assigning it to a reference-counted object.
	Ref<RefCounted> ref_counted = memnew(RefCounted);
	ref_counted->set_script(foundry_script);
	CHECK_MESSAGE(int(ref_counted->get_meta("result")) == 42, "The script should assign object metadata successfully.");
}

TEST_CASE("[Modules][FoundryScript] Loading keeps ResourceCache and FSCache in sync") {
	const String path = TestUtils::get_temp_path("fs_load_test.fs");

	{
		Ref<FileAccess> fa = FileAccess::open(path, FileAccess::ModeFlags::WRITE);
		fa->store_string("extends Node\n");
		fa->close();
	}

	CHECK(!ResourceCache::has(path));
	CHECK(!TestFSCacheAccessor::has_shallow(path));
	CHECK(!TestFSCacheAccessor::has_full(path));

	Ref<FoundryScript> loaded = ResourceLoader::load(path);

	CHECK(ResourceCache::has(path));
	CHECK(!TestFSCacheAccessor::has_shallow(path));
	CHECK(TestFSCacheAccessor::has_full(path));
}

TEST_CASE("[Modules][FoundryScript] Source override map shadows disk in get_source_code") {
	FSLanguage::get_singleton()->init();

	const String path = TestUtils::get_temp_path("fs_source_override.fs");
	{
		Ref<FileAccess> fa = FileAccess::open(path, FileAccess::ModeFlags::WRITE);
		fa->store_string("extends Node\n# on disk\n");
		fa->close();
	}

	// With no override, get_source_code reads the on-disk content.
	CHECK(!FSCache::has_source_override(path));
	CHECK_EQ(FSCache::get_source_code(path), "extends Node\n# on disk\n");

	// An override shadows the disk content for that path only.
	const String overridden = "extends Node\n# in memory\n";
	FSCache::set_source_override(path, overridden);
	CHECK(FSCache::has_source_override(path));
	CHECK_EQ(FSCache::get_source_code(path), overridden);

	// The disk file is never written by the override.
	CHECK_EQ(FileAccess::get_file_as_string(path), "extends Node\n# on disk\n");

	// Clearing the override falls back to disk again.
	FSCache::clear_source_override(path);
	CHECK(!FSCache::has_source_override(path));
	CHECK_EQ(FSCache::get_source_code(path), "extends Node\n# on disk\n");
}

TEST_CASE("[Modules][FoundryScript] Source override guard installs and rolls back overrides") {
	FSLanguage::get_singleton()->init();

	const String path = TestUtils::get_temp_path("fs_override_guard.fs");
	{
		Ref<FileAccess> fa = FileAccess::open(path, FileAccess::ModeFlags::WRITE);
		fa->store_string("extends Node\n");
		fa->close();
	}

	HashMap<String, String> overrides;
	overrides[path] = "extends RefCounted\n";

	{
		FSCacheSourceOverrideGuard guard(overrides);
		CHECK(FSCache::has_source_override(path));
		CHECK_EQ(FSCache::get_source_code(path), "extends RefCounted\n");
	}

	// On scope exit the guard clears exactly the paths it installed.
	CHECK(!FSCache::has_source_override(path));
	CHECK_EQ(FSCache::get_source_code(path), "extends Node\n");
}

TEST_CASE("[Modules][FoundryScript] Validate built-in API") {
	FSLanguage *lang = FSLanguage::get_singleton();

	// Validate methods.
	List<MethodInfo> builtin_methods;
	lang->get_public_functions(&builtin_methods);

	SUBCASE("[Modules][FoundryScript] Validate built-in methods") {
		for (const MethodInfo &mi : builtin_methods) {
			for (int64_t i = 0; i < mi.arguments.size(); ++i) {
				TEST_COND((mi.arguments[i].name.is_empty() || mi.arguments[i].name.begins_with("_unnamed_arg")),
						vformat("Unnamed argument in position %d of built-in method '%s'.", i, mi.name));
			}
		}
	}

	// Validate annotations.
	List<MethodInfo> builtin_annotations;
	lang->get_public_annotations(&builtin_annotations);

	SUBCASE("[Modules][FoundryScript] Validate built-in annotations") {
		for (const MethodInfo &ai : builtin_annotations) {
			for (int64_t i = 0; i < ai.arguments.size(); ++i) {
				TEST_COND((ai.arguments[i].name.is_empty() || ai.arguments[i].name.begins_with("_unnamed_arg")),
						vformat("Unnamed argument in position %d of built-in annotation '%s'.", i, ai.name));
			}
		}
	}
}

} // namespace FSTests
