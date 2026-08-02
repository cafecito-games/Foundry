/**************************************************************************/
/*  test_fs_builtin_runtime_dispatch.h                                    */
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

// Editor-only: the cases produce their artifact bytes with `FSBytecodeExporter`, which ships with
// the compiled-bytecode exporter and so exists only in `TOOLS_ENABLED` builds.
#ifdef TOOLS_ENABLED

// A stripped export template (`foundry_script_frontend=no`) has no parser, so a builtin type is
// loaded from the private `.fsb` companion the compiled-bytecode exporter packs instead of from its
// embedded source. That dispatch cannot be compiled into a test build — `SCsub` rejects
// `foundry_script_frontend=no` together with `tests=yes` — so these cases drive it through the
// forced-dispatch allow-list, which makes named builtin identities take the stripped path in a
// build that does have the front-end.

#include "../fs_builtin_sources.h"
#include "../fs_bytecode_export.h"
#include "../fs_cache.h"
#include "../foundry_script.h"
#include "fs_builtin_test_utils.h"
#include "fs_temporary_project_tree.h"
#include "fs_test_runner.h"
// TestFSCacheAccessor, for asserting which paths the cache holds entries for.
#include "fs_test_runner_suite.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace FSTests {

// Probe builtins live under a `zz_` prefix so they sort after every shipped builtin and can never
// be mistaken for one. They are registered per case and unregistered again, so no shipped builtin
// is ever served from bytecode.
static const char *BUILTIN_DISPATCH_ALPHA_PATH = "foundry://builtin/zz_dispatch_probe_alpha.fs";
static const char *BUILTIN_DISPATCH_BETA_PATH = "foundry://builtin/zz_dispatch_probe_beta.fs";

static const char *BUILTIN_DISPATCH_ALPHA_SOURCE =
		"class_name ZzDispatchProbeAlpha extends RefCounted\n"
		"\n"
		"static func probe_value() -> int:\n"
		"\treturn 41\n";

// Distinguishable from the source the artifact was compiled from, so a case can prove the loaded
// script came from the packaged bytes and not from the embedded source that is still registered.
static const char *BUILTIN_DISPATCH_ALPHA_REWRITTEN_SOURCE =
		"class_name ZzDispatchProbeAlpha extends RefCounted\n"
		"\n"
		"static func probe_value() -> int:\n"
		"\treturn 99\n";

static const char *BUILTIN_DISPATCH_BETA_SOURCE =
		"class_name ZzDispatchProbeBeta extends RefCounted\n"
		"\n"
		"static func probe_value() -> int:\n"
		"\treturn ZzDispatchProbeAlpha.probe_value() + 1\n";

struct BuiltinRuntimeDispatchFixture {
	TemporaryProjectTree tree;
	TestProjectSettingsRestoreScope project_settings;

	explicit BuiltinRuntimeDispatchFixture(const String &p_name) :
			tree("fs_builtin_runtime_dispatch_" + p_name + "_" + itos(OS::get_singleton()->get_ticks_usec())) {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
		// The mapped artifact path is a fixed `res://.foundry/builtin/` prefix, so `res://` is
		// pointed at the throwaway tree for the duration of the case. Writing the companions into
		// the real test project would leave build products in a tracked fixture directory.
		TestProjectSettingsInternalsAccessor::resource_path() = tree.root;
		TestProjectSettingsInternalsAccessor::project_loaded() = true;
	}

	// Compiles a registered builtin from its embedded source and serializes it exactly the way the
	// compiled-bytecode exporter does, so the cases consume real artifact bytes.
	Vector<uint8_t> compile_builtin(const String &p_builtin_path) const {
		Error error = OK;
		const Ref<FoundryScript> script = FSCache::get_full_script(p_builtin_path, error, String(), true);
		REQUIRE_EQ(error, OK);
		REQUIRE(script.is_valid());
		REQUIRE(script->is_valid());

		Vector<uint8_t> buffer;
		FSBytecodeExporter exporter;
		REQUIRE_EQ(exporter.serialize(script, buffer, false), OK);
		REQUIRE_FALSE(buffer.is_empty());

		// Drop the source-parsed script so a later forced load starts from an empty cache, the way
		// a stripped runtime does.
		FSCache::remove_script(p_builtin_path);
		return buffer;
	}

	void write_artifact(const String &p_builtin_path, const Vector<uint8_t> &p_bytes) const {
		const String artifact_path = FSBuiltinSources::get_exported_bytecode_path(p_builtin_path);
		REQUIRE_FALSE(artifact_path.is_empty());

		const String absolute_path = tree.root.path_join(artifact_path.trim_prefix("res://"));
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		if (file.is_null()) {
			return;
		}
		file->store_buffer(p_bytes.ptr(), p_bytes.size());
	}

	// Compiles and packages a builtin in one step, mirroring what an export produces.
	void package_builtin(const String &p_builtin_path) const {
		write_artifact(p_builtin_path, compile_builtin(p_builtin_path));
	}
};

static Variant builtin_dispatch_call_static(const Ref<FoundryScript> &p_script, const StringName &p_method) {
	// Through `Object` because `FoundryScript` keeps its `callp()` override protected; static
	// functions of a script are called on the script object itself.
	Object *script_object = p_script.ptr();
	REQUIRE(script_object != nullptr);
	Callable::CallError call_error;
	const Variant result = script_object->callp(p_method, nullptr, 0, call_error);
	CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
	return result;
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A forced builtin loads its skeleton from the packaged artifact") {
	BuiltinRuntimeDispatchFixture fixture("skeleton");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	fixture.package_builtin(alpha.path);

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_shallow_script(alpha.path, error);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());

	// Only the bytes come from the companion artifact: the script keeps its virtual identity, which
	// is what project bytecode and the global class table record.
	CHECK_EQ(script->get_path(), String(alpha.path));
	CHECK_EQ(script->get_fully_qualified_name(), String("ZzDispatchProbeAlpha"));
	CHECK(script->is_compiled_binary());

	// Cached under the identity, never under the artifact path, so a second request for the builtin
	// resolves without touching disk again.
	CHECK(TestFSCacheAccessor::has_shallow(alpha.path));
	CHECK_FALSE(TestFSCacheAccessor::has_shallow(FSBuiltinSources::get_exported_bytecode_path(alpha.path)));
	CHECK_EQ(FSCache::get_shallow_script(alpha.path, error), script);
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A forced builtin fully links and runs from the packaged artifact") {
	BuiltinRuntimeDispatchFixture fixture("full_link");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	fixture.package_builtin(alpha.path);

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(alpha.path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	CHECK(script->is_compiled_binary());
	CHECK_EQ(builtin_dispatch_call_static(script, SNAME("probe_value")), Variant(41));
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A packaged builtin is never refreshed from its embedded source") {
	BuiltinRuntimeDispatchFixture fixture("no_source_refresh");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	fixture.package_builtin(alpha.path);

	// The embedded source is still registered and still parseable in this build. A stripped runtime
	// has no such source, so the packaged bytes must win even when an update-from-disk load asks
	// for a refresh.
	FSBuiltinSources::register_source(alpha.path, BUILTIN_DISPATCH_ALPHA_REWRITTEN_SOURCE);

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(alpha.path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	CHECK(script->is_compiled_binary());
	CHECK_EQ(builtin_dispatch_call_static(script, SNAME("probe_value")), Variant(41));
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A packaged builtin resolves another builtin through its virtual identity") {
	BuiltinRuntimeDispatchFixture fixture("cross_reference");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	ScopedBuiltinGlobalClass alpha_global(
			SNAME("ZzDispatchProbeAlpha"), SNAME("RefCounted"), BUILTIN_DISPATCH_ALPHA_PATH);
	ScopedBuiltinSource beta(BUILTIN_DISPATCH_BETA_PATH, BUILTIN_DISPATCH_BETA_SOURCE);
	fixture.package_builtin(alpha.path);
	fixture.package_builtin(beta.path);

	ScopedForcedBuiltinBytecodeDispatch forced(Vector<String>({ alpha.path, beta.path }));
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(beta.path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());

	// Beta's external reference is recorded as alpha's virtual path, so linking it re-enters the
	// same dispatch rather than looking for a `foundry://builtin/*.fs` file.
	CHECK_EQ(builtin_dispatch_call_static(script, SNAME("probe_value")), Variant(42));
	const bool alpha_cached =
			TestFSCacheAccessor::has_shallow(alpha.path) || TestFSCacheAccessor::has_full(alpha.path);
	CHECK(alpha_cached);
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A missing artifact fails instead of falling back to source") {
	BuiltinRuntimeDispatchFixture fixture("missing_artifact");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	// Deliberately not packaged: this is a builtin whose companion never made it into the pack.

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);
	Error error = OK;
	ERR_PRINT_OFF;
	const Ref<FoundryScript> script = FSCache::get_shallow_script(alpha.path, error);
	ERR_PRINT_ON;

	CHECK(script.is_null());
	CHECK_EQ(error, ERR_FILE_CANT_READ);
	// Nothing is cached, so a later request retries rather than serving a half-built script.
	CHECK_FALSE(TestFSCacheAccessor::has_shallow(alpha.path));
	CHECK_FALSE(TestFSCacheAccessor::has_full(alpha.path));
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] A corrupt artifact fails through the bytecode loader") {
	BuiltinRuntimeDispatchFixture fixture("corrupt_artifact");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);

	Vector<uint8_t> garbage;
	for (int i = 0; i < 64; i++) {
		garbage.push_back(uint8_t(i));
	}
	fixture.write_artifact(alpha.path, garbage);

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);
	Error error = OK;
	ERR_PRINT_OFF;
	const Ref<FoundryScript> script = FSCache::get_shallow_script(alpha.path, error);
	ERR_PRINT_ON;

	CHECK(script.is_null());
	CHECK_NE(error, OK);
	CHECK_FALSE(TestFSCacheAccessor::has_shallow(alpha.path));
	CHECK_FALSE(TestFSCacheAccessor::has_full(alpha.path));
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] Builtins outside the forced set keep loading from source") {
	BuiltinRuntimeDispatchFixture fixture("blast_radius");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	fixture.package_builtin(alpha.path);

	ScopedForcedBuiltinBytecodeDispatch forced(alpha.path);

	// A shipped builtin still takes the front-end path while a probe is forced, so no shipped
	// builtin can pick up a compiled-binary cache entry from a test run.
	Error error = OK;
	const Ref<FoundryScript> shipped = FSCache::get_shallow_script("foundry://builtin/json_decode_error.fs", error);
	REQUIRE_EQ(error, OK);
	REQUIRE(shipped.is_valid());
	CHECK_FALSE(shipped->is_compiled_binary());
}

TEST_CASE("[FoundryScript][BuiltinRuntimeDispatch] Without forcing, a builtin loads from its embedded source") {
	BuiltinRuntimeDispatchFixture fixture("opt_in");
	ScopedBuiltinSource alpha(BUILTIN_DISPATCH_ALPHA_PATH, BUILTIN_DISPATCH_ALPHA_SOURCE);
	// Packaged, so the only reason the source wins is that this build has a front-end and the path
	// is not in the forced set.
	fixture.package_builtin(alpha.path);

	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(alpha.path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(script.is_valid());
	REQUIRE(script->is_valid());
	CHECK_FALSE(script->is_compiled_binary());
	CHECK_EQ(builtin_dispatch_call_static(script, SNAME("probe_value")), Variant(41));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
