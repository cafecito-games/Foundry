/**************************************************************************/
/*  test_call_argument_conversion_width.h                                 */
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

// Shared bytecode fixtures (in-process compile, test resolver). Every module test header is compiled
// into one generated test translation unit, so this include does not duplicate test registrations.
#include "test_bytecode_serialization.h"

#include "core/variant/variant.h"

#include "tests/test_macros.h"

// A dynamic call converts an argument that is not already a value of the parameter's type. The
// conversion answers the carrier question only, so a parameter that declares an integer width has to
// ask about the converted value as well: `2147483648.0` truncates to a magnitude an `int` cannot
// hold, and storing it would hand the callee a value outside its own declared type.
//
// Truncation toward zero is unchanged; only the acceptance of the truncated result is at issue. The
// same rule has to hold with no front end behind the function, so every case below runs twice: once
// against the compiled source and once against a script rebuilt from serialized bytecode alone.

namespace FSTests {

static Ref<FoundryScript> load_call_argument_width_bytecode(const String &p_source,
		FSBytecodeExternalResolver *p_resolver, Ref<FoundryScript> *r_original) {
	const Ref<FoundryScript> original = compile_bytecode_test_source(p_source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	*r_original = original;

	// Nothing can re-parse the source from here on, so the restored script answers from the compiled
	// function's own type records.
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);
	REQUIRE_FALSE(FileAccess::exists(script_path));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);

	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());
	CHECK(restored->is_compiled_binary());
	return restored;
}

static ScriptInstance *instantiate_call_argument_width_script(const Ref<FoundryScript> &p_script, Variant &r_owner) {
	Callable::CallError call_error;
	r_owner = p_script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *owner = r_owner;
	REQUIRE(owner != nullptr);
	ScriptInstance *instance = owner->get_script_instance();
	REQUIRE(instance != nullptr);
	return instance;
}

// Invokes `p_method` with a single argument through the dynamic call prologue, which is where an
// argument that is not already a value of the parameter's type gets converted.
static Variant call_with_single_argument(ScriptInstance *p_instance, const StringName &p_method,
		const Variant &p_argument, Callable::CallError &r_error) {
	const Variant *arguments[1] = { &p_argument };
	return p_instance->callp(p_method, arguments, 1, r_error);
}

TEST_CASE("[Modules][FoundryScript][CallArgumentWidth] A converted call argument is checked against the declared width") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_call_argument_width_bytecode(
			"extends RefCounted\n"
			"\n"
			"func take_int(value: int) -> int:\n"
			"\treturn value\n"
			"\n"
			"func take_long(value: long) -> long:\n"
			"\treturn value\n"
			"\n"
			"func take_unsigned(value: uint) -> uint:\n"
			"\treturn value\n"
			"\n"
			"func take_rest(...values: Array[int]) -> Array[int]:\n"
			"\treturn values\n",
			&resolver, &original);

	Variant original_owner;
	ScriptInstance *original_instance = instantiate_call_argument_width_script(original, original_owner);
	Variant restored_owner;
	ScriptInstance *restored_instance = instantiate_call_argument_width_script(restored, restored_owner);

	const auto check_accepted = [](ScriptInstance *p_instance, const char *p_method, const Variant &p_argument,
									const Variant &p_expected) {
		CAPTURE(p_method);
		Callable::CallError error;
		const Variant result = call_with_single_argument(p_instance, StringName(p_method), p_argument, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		CHECK(result.get_type() == p_expected.get_type());
		CHECK(result == p_expected);
	};

	// A rejection has to name the offending argument and the parameter's carrier, since a caller that
	// only sees `CALL_ERROR_INVALID_ARGUMENT` cannot tell which value it was.
	const auto check_rejected = [](ScriptInstance *p_instance, const char *p_method, const Variant &p_argument,
									Variant::Type p_expected_carrier) {
		CAPTURE(p_method);
		Callable::CallError error;
		ERR_PRINT_OFF;
		call_with_single_argument(p_instance, StringName(p_method), p_argument, error);
		ERR_PRINT_ON;
		CHECK(error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
		CHECK(error.argument == 0);
		CHECK(error.expected == int(p_expected_carrier));
	};

	for (ScriptInstance *instance : { original_instance, restored_instance }) {
		// The signed 32-bit boundary, from both directions, after truncation toward zero.
		check_accepted(instance, "take_int", 2147483647.0, int64_t(2147483647));
		check_rejected(instance, "take_int", 2147483648.0, Variant::INT);
		check_accepted(instance, "take_int", -2147483648.0, int64_t(-2147483648LL));
		check_rejected(instance, "take_int", -2147483649.0, Variant::INT);

		// Truncation itself is unchanged: a fractional value still drops toward zero rather than
		// rounding, in both signs, including where the whole part is zero.
		check_accepted(instance, "take_int", 1.9, int64_t(1));
		check_accepted(instance, "take_int", -1.9, int64_t(-1));
		check_accepted(instance, "take_int", 0.5, int64_t(0));
		check_accepted(instance, "take_int", -0.5, int64_t(0));

		// A non-numeric implicit conversion is untouched by the width check.
		check_accepted(instance, "take_int", true, int64_t(1));

		// The check is the declaration talking, not a blanket ceiling: the magnitudes the narrower
		// parameter rejects are ordinary values of the wider one.
		check_accepted(instance, "take_long", 2147483648.0, int64_t(2147483648LL));
		check_accepted(instance, "take_long", -2147483649.0, int64_t(-2147483649LL));

		// The unsigned carrier has no implicit source, so an unsigned parameter is reached on its own
		// carrier and keeps the same-carrier width rule.
		check_accepted(instance, "take_unsigned", Variant(uint64_t(4294967295ULL)), Variant(uint64_t(4294967295ULL)));
		check_rejected(instance, "take_unsigned", Variant(uint64_t(4294967296ULL)), Variant::UINT);
		check_rejected(instance, "take_unsigned", -1.0, Variant::UINT);
	}
}

TEST_CASE("[Modules][FoundryScript][CallArgumentWidth] A typed rest argument is checked against the declared width") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_call_argument_width_bytecode(
			"extends RefCounted\n"
			"\n"
			"func take_rest(...values: Array[int]) -> Array[int]:\n"
			"\treturn values\n",
			&resolver, &original);

	Variant original_owner;
	ScriptInstance *original_instance = instantiate_call_argument_width_script(original, original_owner);
	Variant restored_owner;
	ScriptInstance *restored_instance = instantiate_call_argument_width_script(restored, restored_owner);

	for (ScriptInstance *instance : { original_instance, restored_instance }) {
		const Variant first = 1.9;
		const Variant second = -1.9;
		const Variant *accepted_arguments[2] = { &first, &second };
		Callable::CallError error;
		const Variant collected = instance->callp(SNAME("take_rest"), accepted_arguments, 2, error);
		CHECK(error.error == Callable::CallError::CALL_OK);
		REQUIRE(collected.get_type() == Variant::ARRAY);
		const Array collected_array = collected;
		REQUIRE(collected_array.size() == 2);
		CHECK(collected_array[0] == Variant(int64_t(1)));
		CHECK(collected_array[1] == Variant(int64_t(-1)));

		// The rejected element is the second one, so the reported index is the absolute argument
		// position rather than the position inside the collected array.
		const Variant overflowing = 2147483648.0;
		const Variant *rejected_arguments[2] = { &first, &overflowing };
		Callable::CallError rest_error;
		ERR_PRINT_OFF;
		instance->callp(SNAME("take_rest"), rejected_arguments, 2, rest_error);
		ERR_PRINT_ON;
		CHECK(rest_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
		CHECK(rest_error.argument == 1);
		CHECK(rest_error.expected == int(Variant::INT));
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
