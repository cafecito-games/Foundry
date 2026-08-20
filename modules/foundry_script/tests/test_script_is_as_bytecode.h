/**************************************************************************/
/*  test_script_is_as_bytecode.h                                          */
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

// Shared bytecode fixtures and instance helpers (in-process compile, test resolver). Every module test
// header is compiled into one generated test translation unit, so this include does not duplicate test
// registrations.
#include "test_bytecode_script.h"

#include "tests/test_macros.h"

// A script `is` and a script `as` answer membership from one relation, and every source that relation
// draws on has to reach both of them the same way: a declared `uses`, an inherited one, a supertrait, a
// retroactive script, native or builtin conformance, and the specialization comparison a specialized
// target adds. A shipped game runs from a `.fsb` with no parser or analyzer behind it, so each answer is
// asserted twice: once against the compiled source and once against the same script rebuilt from bytes
// with the conformance registry emptied in between, which is what proves the answer came from the
// serialized metadata rather than from front-end state left over from the compile.

namespace FSTests {

static const char *script_is_as_source =
		"extends RefCounted\n"
		"\n"
		"trait Keeper[T]:\n"
		"\tfunc keeper_label() -> String:\n"
		"\t\treturn \"keeper\"\n"
		"\n"
		"trait SubKeeper[T]:\n"
		"\tuses Keeper[T]\n"
		"\n"
		"class DirectKeeper:\n"
		"\tuses Keeper[int]\n"
		"\n"
		"class DerivedKeeper extends DirectKeeper:\n"
		"\tvar extra: int = 0\n"
		"\n"
		"class RelayKeeper:\n"
		"\tuses SubKeeper[int]\n"
		"\n"
		"class ForwardingKeeper[T]:\n"
		"\tuses Keeper[T]\n"
		"\n"
		"class StringKeeper:\n"
		"\tuses Keeper[String]\n"
		"\n"
		"class Plain:\n"
		"\tvar value: int = 0\n"
		"\n"
		"class RetroTarget:\n"
		"\tvar value: int = 0\n"
		"\n"
		"extend RetroTarget uses Keeper[int]:\n"
		"\tpass\n"
		"\n"
		"extend Resource uses Keeper[int]:\n"
		"\tpass\n"
		"\n"
		"extend int uses Keeper[int]:\n"
		"\tpass\n"
		"\n"
		"func is_raw_keeper(value: Variant) -> bool:\n"
		"\treturn value is Keeper\n"
		"\n"
		"func is_int_keeper(value: Variant) -> bool:\n"
		"\treturn value is Keeper[int]\n"
		"\n"
		"func cast_raw_keeper_keeps(value: Variant) -> bool:\n"
		"\treturn (value as Keeper) != null\n"
		"\n"
		"func cast_int_keeper_keeps(value: Variant) -> bool:\n"
		"\treturn (value as Keeper[int]) != null\n"
		"\n"
		"func make_direct() -> Variant:\n"
		"\treturn DirectKeeper.new()\n"
		"\n"
		"func make_derived() -> Variant:\n"
		"\treturn DerivedKeeper.new()\n"
		"\n"
		"func make_relay() -> Variant:\n"
		"\treturn RelayKeeper.new()\n"
		"\n"
		"func make_retroactive_script() -> Variant:\n"
		"\treturn RetroTarget.new()\n"
		"\n"
		"func make_retroactive_native() -> Variant:\n"
		"\treturn Resource.new()\n"
		"\n"
		"func make_retroactive_builtin() -> Variant:\n"
		"\treturn 7\n"
		"\n"
		"func make_unspecialized() -> Variant:\n"
		"\treturn ForwardingKeeper.new()\n"
		"\n"
		"func make_projected() -> Variant:\n"
		"\treturn ForwardingKeeper[int].new()\n"
		"\n"
		"func make_conflicting() -> Variant:\n"
		"\treturn StringKeeper.new()\n"
		"\n"
		"func make_nonconformer() -> Variant:\n"
		"\treturn Plain.new()\n";

// One row of the membership matrix, asked of one script instance. `p_maker` names the function that
// produces the value under test, or is null for the value `null` itself. A cast is asserted to keep the
// value exactly when the matching test succeeds, which is the contract that ties `is` to `as`.
static void check_script_is_as_row(Object *p_instance, const char *p_maker, bool p_raw, bool p_specialized) {
	Vector<Variant> arguments;
	arguments.push_back(p_maker == nullptr ? Variant() : bytecode_instance_call(p_instance, StringName(p_maker), {}));

	CHECK(bool(bytecode_instance_call(p_instance, SNAME("is_raw_keeper"), arguments)) == p_raw);
	CHECK(bool(bytecode_instance_call(p_instance, SNAME("cast_raw_keeper_keeps"), arguments)) == p_raw);
	CHECK(bool(bytecode_instance_call(p_instance, SNAME("is_int_keeper"), arguments)) == p_specialized);
	CHECK(bool(bytecode_instance_call(p_instance, SNAME("cast_int_keeper_keeps"), arguments)) == p_specialized);
}

static void check_script_is_as_matrix(Object *p_instance) {
	// Declared, inherited and supertrait conformance all carry the trait's arguments.
	check_script_is_as_row(p_instance, "make_direct", true, true);
	check_script_is_as_row(p_instance, "make_derived", true, true);
	check_script_is_as_row(p_instance, "make_relay", true, true);
	// Retroactive conformance reaches the same answer from the registry, for a script target, a
	// scriptless native object, and a builtin value alike.
	check_script_is_as_row(p_instance, "make_retroactive_script", true, true);
	check_script_is_as_row(p_instance, "make_retroactive_native", true, true);
	check_script_is_as_row(p_instance, "make_retroactive_builtin", true, true);
	// A specialization projected through the implementer's own parameter proves the trait's argument.
	check_script_is_as_row(p_instance, "make_projected", true, true);
	// Absent evidence is not a wildcard: the nominal question still succeeds, the narrowing one does not.
	check_script_is_as_row(p_instance, "make_unspecialized", true, false);
	// Contradicting evidence fails only the specialized target; the conformance itself stands.
	check_script_is_as_row(p_instance, "make_conflicting", true, false);
	// A nonconformer and null fail both.
	check_script_is_as_row(p_instance, "make_nonconformer", false, false);
	check_script_is_as_row(p_instance, nullptr, false, false);
}

TEST_CASE("[FoundryScript][Bytecode][ScriptIsAs] Every membership source answers `is` and `as` identically from source and from bytecode") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(script_is_as_source);
	REQUIRE(original->is_valid());
	const String script_path = original->get_script_path();
	BytecodeConformanceRegistryScope registry_scope(script_path);

	const Variant original_owner = bytecode_new_instance(original);
	Object *original_instance = original_owner;
	REQUIRE(original_instance != nullptr);
	check_script_is_as_matrix(original_instance);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Emptying the registry for this file removes every retroactive conformance the compile registered,
	// so the reloaded script's answers can only come from what the bytecode carries.
	FSConformanceRegistry::get_singleton()->clear_file(script_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());
	CHECK(restored->is_compiled_binary());

	const Variant restored_owner = bytecode_new_instance(restored);
	Object *restored_instance = restored_owner;
	REQUIRE(restored_instance != nullptr);
	check_script_is_as_matrix(restored_instance);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
