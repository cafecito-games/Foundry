/**************************************************************************/
/*  test_suite_language_hoist.h                                           */
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

#include "core/io/resource.h"
#include "modules/foundry_script/fs_cache.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

#include "fs_test_runner.h"
#include "test_refactor.h" // FSTests::initialize, root.
#include "test_suite_language_fixture.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace FSTests {

namespace {
// Mirrors the standard protocol-using case shape (EditorFileSystem + protocol
// per case) and asserts the heavy language setup is shared, not paid again, when
// a case re-enters `initialize()` after the suite already brought the language
// up. Phrased as an in-case invariant so it holds under any test ordering
// (`--order-by=rand` interleaves suites, legitimately tearing the language down
// and back up between this suite's cases).
void check_language_setup_is_shared() {
	EditorFileSystem *editor_file_system = memnew(EditorFileSystem);

	FSLanguageProtocol *protocol = FSTests::initialize(FSTests::root);
	REQUIRE(protocol);
	CHECK(FSTests::is_language_initialized());

	// A redundant `init_language()` while the language is already up must be a
	// no-op: the expensive setup counter does not move. This is the per-suite
	// hoist in miniature — every case after the first in a suite hits this fast
	// path instead of tearing the language down and rebuilding it.
	const uint64_t count_before = FSTests::get_init_language_count();
	FSTests::init_language(FSTests::root);
	CHECK_EQ(FSTests::get_init_language_count(), count_before);
	CHECK(FSTests::is_language_initialized());

	memdelete(protocol);
	memdelete(editor_file_system);
}
} // namespace

TEST_SUITE("[Modules][FoundryScript][SuiteLanguageHoist]") {
	TEST_CASE("Redundant language setup is skipped while the language is up") {
		check_language_setup_is_shared();
	}

	// Running the same check across several cases also exercises the per-suite
	// fixture's between-case reset: the language must stay usable afterwards.
	TEST_CASE("Language stays shared across a second case in the suite") {
		check_language_setup_is_shared();
	}

	TEST_CASE("Language stays shared across a third case in the suite") {
		check_language_setup_is_shared();
	}

	TEST_CASE("Empty-suite ownership survives repeated cases and tears down before a named suite") {
		finish_language();
		TestProjectSettingsRestoreScope restore_process_settings;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		const String expected_resource_path;
		const bool expected_project_loaded = false;
		const String expected_app_name = "BeforeEmptySuite";
		TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
		TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
		settings->set_setting("application/config/name", expected_app_name);

		doctest::ContextOptions context_options{};
		FSLanguageSuiteFixture listener(context_options);
		doctest::CurrentTestCaseStats stats{};
		doctest::TestCaseData empty_case{};
		empty_case.m_name = "direct empty-suite case";
		empty_case.m_test_suite = "";
		doctest::TestCaseData named_case{};
		named_case.m_name = "following named-suite case";
		named_case.m_test_suite = "[Modules][FoundryScript][FollowingSuite]";

		listener.test_case_start(empty_case);
		init_language(String(root));
		listener.test_case_end(stats);
		CHECK(is_language_initialized());

		const uint64_t count_before_repeat = get_init_language_count();
		listener.test_case_start(empty_case);
		CHECK(is_language_initialized());
		CHECK_EQ(get_init_language_count(), count_before_repeat);
		listener.test_case_end(stats);

		const String script_path = "res://test_runner_host/sync_exit_0.notest.fs";
		Error cache_error = OK;
		Ref<FoundryScript> cached_script = FSCache::get_full_script(script_path, cache_error);
		REQUIRE_EQ(cache_error, OK);
		REQUIRE(cached_script.is_valid());
		CHECK(FSCache::get_cached_script(script_path).is_valid());
		CHECK(ResourceCache::has(script_path));
		cached_script.unref();

		listener.test_case_start(named_case);
		CHECK_FALSE(is_fs_language_active());
		CHECK_FALSE(is_language_initialized());
		CHECK_EQ(settings->get_resource_path(), expected_resource_path);
		CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
		CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
		CHECK(FSCache::get_cached_script(script_path).is_null());
		CHECK_FALSE(ResourceCache::has(script_path));
		CHECK(FSLanguage::get_singleton()->get_reflection_singleton().is_null());
		CHECK(FSLanguage::get_singleton()->get_project_scripts_singleton().is_null());
		CHECK(FSLanguage::get_singleton()->get_namespace_singleton().is_null());
	}

	TEST_CASE("finish_language restores exact project settings after named-suite initialization") {
		finish_language();
		TestProjectSettingsRestoreScope restore_process_settings;

		ProjectSettings *settings = ProjectSettings::get_singleton();
		const String expected_resource_path;
		const bool expected_project_loaded = false;
		const String expected_app_name = "BeforeNamedSuite";
		TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
		TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
		settings->set_setting("application/config/name", expected_app_name);

		init_language(String(root));
		CHECK(is_fs_language_active());
		finish_language();

		CHECK_FALSE(is_fs_language_active());
		CHECK_FALSE(is_language_initialized());
		CHECK_EQ(settings->get_resource_path(), expected_resource_path);
		CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
		CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
	}

	TEST_CASE("finish_language tears down direct init() state") {
		if (!is_fs_language_active()) {
			FSLanguage::get_singleton()->init();
		}
		CHECK(is_fs_language_active());
		finish_language();
		CHECK(!is_fs_language_active());
	}
}

} // namespace FSTests

#endif // FOUNDRY_SCRIPT_NO_LSP

#endif // TOOLS_ENABLED
