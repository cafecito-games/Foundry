/**************************************************************************/
/*  test_language_shutdown.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

// The function registry these cases inspect only exists in debug builds (see `FSFunction`'s
// constructor), and the in-process compile helper is editor-only.
#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

// `compile_bytecode_test_source()`, `bytecode_round_trip_member_function()` and the engine-error
// recorder. Every module test header is compiled into one translation unit, so reusing these
// helpers costs nothing and keeps a single in-process compile path.
#include "fs_test_language_lifecycle.h"
#include "test_bytecode_serialization.h"
// `ScopedProxyLanguage` is one of the fixtures whose initialization gate reads the reflection
// namespace, so it doubles as the migrated-gate subject exercised after a finish cycle.
#include "test_proxy.h"

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_function.h"

#include "core/object/script_language.h"

#include "tests/test_macros.h"

namespace FSTests {

// Reads the language's private registry of live `FSFunction`s. The list is intrusive and has no
// size counter, so the elements are walked under the language mutex that guards registration.
class TestFSLanguageFunctionListAccessor {
public:
	static int registered_function_count(FSLanguage *p_language) {
		MutexLock lock(p_language->mutex);
		int count = 0;
		for (SelfList<FSFunction> *element = p_language->function_list.first(); element; element = element->next()) {
			count++;
		}
		return count;
	}
};

TEST_CASE("[Modules][FoundryScript][Shutdown] finish_languages() empties the registered function list") {
	FSLanguage *language = FSLanguage::get_singleton();
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func first() -> int:\n"
			"\treturn 1\n"
			"\n"
			"func second() -> int:\n"
			"\treturn 2\n");
	REQUIRE(script.is_valid());
	CHECK_GT(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	// This is the step `Main::test_cleanup()` performs before the module that owns the language is
	// uninitialized. Draining the list here is what lets `~FSLanguage` destroy it while it is empty.
	ScriptServer::finish_languages();
	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	language->init();
}

TEST_CASE("[Modules][FoundryScript][Shutdown] Draining the function list does not destroy the functions") {
	FSLanguage *language = FSLanguage::get_singleton();
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func first() -> int:\n"
			"\treturn 1\n");
	REQUIRE(script.is_valid());

	// A round-tripped copy is registered in the language's list but owned by this test rather than
	// by the script, so language teardown cannot delete it. It is the only function that can prove
	// the drain unlinks instead of freeing.
	FSFunction *unowned = bytecode_round_trip_member_function(script, SNAME("first"));
	REQUIRE(unowned != nullptr);
	const StringName name = unowned->get_name();
	const int code_size = unowned->get_code().size();
	const ObjectID script_id = script->get_instance_id();

	ScriptServer::finish_languages();

	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);
	CHECK(ObjectDB::get_instance(script_id) == script.ptr());
	CHECK(unowned->get_name() == name);
	CHECK_EQ(unowned->get_code().size(), code_size);

	memdelete(unowned);
	language->init();
}

TEST_CASE("[Modules][FoundryScript][Shutdown] Releasing a function after the language is finished unregisters cleanly") {
	FSLanguage *language = FSLanguage::get_singleton();
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func first() -> int:\n"
			"\treturn 1\n");
	REQUIRE(script.is_valid());
	FSFunction *unowned = bytecode_round_trip_member_function(script, SNAME("first"));
	REQUIRE(unowned != nullptr);

	ScriptServer::finish_languages();

	{
		// Unregistering an already-unlinked node must be a silent no-op. Removing it through the
		// language's list instead would trip `SelfList::List::remove()`'s `ERR_FAIL_COND` and report
		// an engine error for a legitimate state.
		BytecodeErrorRecorder recorder;
		memdelete(unowned);
		CHECK(recorder.messages.is_empty());
	}

	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);
	language->init();
}

TEST_CASE("[Modules][FoundryScript][Shutdown] The function list is empty after a finish()/init() cycle") {
	FSLanguage *language = FSLanguage::get_singleton();
	language->finish();
	language->init();

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func only() -> int:\n"
			"\treturn 3\n");
	REQUIRE(script.is_valid());
	CHECK_GT(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	// The drain is repeatable, so a suite that re-initializes the language cannot leave residue for
	// the process-exit teardown to trip over.
	language->finish();
	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	language->init();
}

TEST_CASE("[Modules][FoundryScript][Shutdown] A reported stale cache entry does not disable later finish() calls") {
	FSLanguage *language = FSLanguage::get_singleton();

	// Start from a completed cycle so the entry forced below is the only one the sweep can find.
	language->finish();
	language->init();

	Ref<FoundryScript> stale = compile_bytecode_test_source(
			"func stale() -> int:\n"
			"\treturn 1\n");
	REQUIRE(stale.is_valid());
	const String stale_path = stale->get_path();
	REQUIRE_FALSE(stale_path.is_empty());

	// A finish() cycle unlinks every script from the language's list, so a script that outlives the
	// cycle is invisible to the next sweep. Re-registering it in ResourceCache reproduces exactly the
	// state the development-build post-condition check reports on, without any test-only seam.
	language->finish();
	language->init();
	stale->set_path(stale_path);
	REQUIRE(ResourceCache::has(stale_path));

	{
		BytecodeErrorRecorder recorder;
		ERR_PRINT_OFF;
		language->finish();
		ERR_PRINT_ON;
#ifdef DEV_ENABLED
		// The sweep still reports the offender; it just does not abandon finalization to do so.
		CHECK(recorder.messages.contains("still registered in ResourceCache"));
#endif // DEV_ENABLED
	}
	language->init();

	// Release the forced entry so the next cycle is clean, then prove finalization still runs.
	stale->set_path(String());
	stale = Ref<FoundryScript>();
	CHECK_FALSE(ResourceCache::has(stale_path));

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func only() -> int:\n"
			"\treturn 3\n");
	REQUIRE(script.is_valid());
	CHECK_GT(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	language->finish();
	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), 0);

	language->init();
}

TEST_CASE("[Modules][FoundryScript][Shutdown] The initialization gate brings a finished language back up") {
	FSLanguage *language = FSLanguage::get_singleton();
	language->finish();

	// The state that makes an ordinary global a useless initialization signal: `finish()` drops the
	// reflection namespace but leaves the plain globals behind.
	REQUIRE(language->has_any_global_constant(SNAME("RefCounted")));
	REQUIRE(language->get_namespace_singleton().is_null());

	CHECK(ensure_fs_language_initialized());
	CHECK(language->get_namespace_singleton().is_valid());

	// A language brought back up by the gate compiles again.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func only() -> int:\n"
			"\treturn 5\n");
	CHECK(script.is_valid());
}

TEST_CASE("[Modules][FoundryScript][Shutdown] The initialization gate leaves a live language untouched") {
	FSLanguage *language = FSLanguage::get_singleton();
	language->finish();
	language->init();

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func only() -> int:\n"
			"\treturn 7\n");
	REQUIRE(script.is_valid());

	const FSNamespace *namespace_before = language->get_namespace_singleton().ptr();
	const int function_count_before = TestFSLanguageFunctionListAccessor::registered_function_count(language);
	REQUIRE(namespace_before != nullptr);
	REQUIRE(function_count_before > 0);

	// A second gate on an already-initialized language must not re-run `init()`: that would rebuild
	// the reflection namespace and drop the state the live scripts were compiled against.
	CHECK_FALSE(ensure_fs_language_initialized());
	CHECK(language->get_namespace_singleton().ptr() == namespace_before);
	CHECK_EQ(TestFSLanguageFunctionListAccessor::registered_function_count(language), function_count_before);
	CHECK(script->is_valid());
}

TEST_CASE("[Modules][FoundryScript][Shutdown] A migrated fixture gate runs after a finish cycle") {
	FSLanguage *language = FSLanguage::get_singleton();
	language->finish();
	REQUIRE(language->get_namespace_singleton().is_null());

	{
		ScopedProxyLanguage scoped_language;
		CHECK(language->get_namespace_singleton().is_valid());

		const Ref<FoundryScript> script = compile_bytecode_test_source(
				"func only() -> int:\n"
				"\treturn 11\n");
		CHECK(script.is_valid());
	}

	// The fixture guard owns no teardown, so the language it revived stays usable for the next test.
	CHECK(language->get_namespace_singleton().is_valid());
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
