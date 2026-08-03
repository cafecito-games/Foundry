/**************************************************************************/
/*  test_typed_container_numeric_width.h                                  */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"

#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "core/variant/numeric_type.h"

#include "tests/test_macros.h"

// Coverage for issue #1680: a declared integer width on a typed-container element must survive the
// compiled constant that describes that element to the runtime. A scalar element type is emitted as a
// bare `script_type` constant, which transports only the Variant carrier, so a `Dictionary[String,
// ulong]` used to arrive at the VM as `Dictionary[String, uint]` -- the carrier's name with no width.
//
// Every assertion below names `numeric_type` explicitly. `ContainerType` and `FSDataType` equality
// compare widths with `numeric_types_agree()`, under which `NONE` agrees with every descriptor, so a
// test that only compares whole types passes even when the width was dropped.

namespace FSTests {

struct ScopedTypedContainerWidthLanguage {
	ScopedTypedContainerWidthLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> compile_typed_container_width_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_typed_container_numeric_width_%d.fs", unique_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

// Calls a zero-argument method on a fresh instance of `p_script` and returns its result.
static Variant call_typed_container_width_method(const Ref<FoundryScript> &p_script, const StringName &p_method) {
	Callable::CallError error;
	Variant instance = p_script->_new(nullptr, -1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	Variant result;
	instance.callp(p_method, nullptr, 0, result, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	return result;
}

TEST_CASE("[Modules][FoundryScript][NumericType] A declared width on a dictionary value slot reaches the runtime container") {
	ScopedTypedContainerWidthLanguage language;

	const char *source =
			"func from_empty_literal() -> Dictionary:\n"
			"\tvar lookup: Dictionary[String, ulong] = {}\n"
			"\treturn lookup\n"
			"\n"
			"func from_populated_literal() -> Dictionary:\n"
			"\tvar lookup: Dictionary[String, ulong] = {\"a\": 1UL}\n"
			"\treturn lookup\n"
			"\n"
			"func from_typed_return() -> Dictionary[String, ulong]:\n"
			"\treturn {}\n";

	Ref<FoundryScript> script = compile_typed_container_width_source(source);

	for (const StringName &method : { StringName("from_empty_literal"), StringName("from_populated_literal"), StringName("from_typed_return") }) {
		const Variant result = call_typed_container_width_method(script, method);
		REQUIRE(result.get_type() == Variant::DICTIONARY);
		const Dictionary lookup = result;
		REQUIRE(lookup.is_typed_value());

		const ContainerType value_type = lookup.get_value_type();
		CHECK_MESSAGE(value_type.builtin_type == Variant::UINT, vformat("%s value carrier", String(method)));
		CHECK_MESSAGE(value_type.numeric_type == NumericType::UINT64, vformat("%s value width: %s", String(method), numeric_type_name(value_type.numeric_type)));
		CHECK_MESSAGE(value_type.get_type_name() == "ulong", vformat("%s value type name: %s", String(method), value_type.get_type_name()));

		// The key slot declared no width, so it stays unconstrained rather than picking one up.
		const ContainerType key_type = lookup.get_key_type();
		CHECK(key_type.builtin_type == Variant::STRING);
		CHECK(key_type.numeric_type == NumericType::NONE);
	}
}

TEST_CASE("[Modules][FoundryScript][NumericType] A declared width on an array element slot reaches the runtime container") {
	ScopedTypedContainerWidthLanguage language;

	const char *source =
			"func from_empty_literal() -> Array:\n"
			"\tvar values: Array[ulong] = []\n"
			"\treturn values\n"
			"\n"
			"func from_populated_literal() -> Array:\n"
			"\tvar values: Array[ulong] = [1UL]\n"
			"\treturn values\n"
			"\n"
			"func from_typed_return() -> Array[ulong]:\n"
			"\treturn []\n";

	Ref<FoundryScript> script = compile_typed_container_width_source(source);

	for (const StringName &method : { StringName("from_empty_literal"), StringName("from_populated_literal"), StringName("from_typed_return") }) {
		const Variant result = call_typed_container_width_method(script, method);
		REQUIRE(result.get_type() == Variant::ARRAY);
		const Array values = result;
		REQUIRE(values.is_typed());

		const ContainerType element_type = values.get_element_type();
		CHECK_MESSAGE(element_type.builtin_type == Variant::UINT, vformat("%s element carrier", String(method)));
		CHECK_MESSAGE(element_type.numeric_type == NumericType::UINT64, vformat("%s element width: %s", String(method), numeric_type_name(element_type.numeric_type)));
		CHECK_MESSAGE(element_type.get_type_name() == "ulong", vformat("%s element type name: %s", String(method), element_type.get_type_name()));
	}
}

TEST_CASE("[Modules][FoundryScript][NumericType] Each applied integer width reaches the runtime container distinctly") {
	ScopedTypedContainerWidthLanguage language;

	const char *source =
			"func of_int() -> Array:\n"
			"\tvar values: Array[int] = []\n"
			"\treturn values\n"
			"\n"
			"func of_uint() -> Array:\n"
			"\tvar values: Array[uint] = []\n"
			"\treturn values\n"
			"\n"
			"func of_long() -> Array:\n"
			"\tvar values: Array[long] = []\n"
			"\treturn values\n"
			"\n"
			"func of_ulong() -> Array:\n"
			"\tvar values: Array[ulong] = []\n"
			"\treturn values\n";

	Ref<FoundryScript> script = compile_typed_container_width_source(source);

	struct Expectation {
		const char *method;
		Variant::Type carrier;
		NumericType numeric_type;
	};
	const Expectation expectations[] = {
		// `int` deliberately applies no width yet (`_applied_numeric_type()` in the analyzer), because
		// native integer boundaries still decode wide. It reaches the runtime unconstrained on the signed
		// carrier, which is what it did before widths existed.
		{ "of_int", Variant::INT, NumericType::NONE },
		{ "of_uint", Variant::UINT, NumericType::UINT32 },
		{ "of_long", Variant::INT, NumericType::INT64 },
		{ "of_ulong", Variant::UINT, NumericType::UINT64 },
	};

	for (const Expectation &expectation : expectations) {
		const Variant result = call_typed_container_width_method(script, StringName(expectation.method));
		REQUIRE(result.get_type() == Variant::ARRAY);
		const Array values = result;
		REQUIRE(values.is_typed());

		const ContainerType element_type = values.get_element_type();
		CHECK_MESSAGE(element_type.builtin_type == expectation.carrier, vformat("%s element carrier", expectation.method));
		CHECK_MESSAGE(element_type.numeric_type == expectation.numeric_type,
				vformat("%s element width: %s", expectation.method, numeric_type_name(element_type.numeric_type)));
	}
}

TEST_CASE("[Modules][FoundryScript][NumericType] A container built by a script enforces its declared width on insertion") {
	ScopedTypedContainerWidthLanguage language;

	const char *source =
			"func narrow() -> Array:\n"
			"\tvar values: Array[uint] = []\n"
			"\treturn values\n"
			"\n"
			"func wide() -> Array:\n"
			"\tvar values: Array[ulong] = []\n"
			"\treturn values\n";

	Ref<FoundryScript> script = compile_typed_container_width_source(source);

	// Both slots ride the unsigned carrier, so only the width tells them apart. Before the descriptor
	// reached the runtime, both accepted this value.
	const Variant beyond_uint32 = uint64_t(UINT32_MAX) + 1;

	Array narrow = call_typed_container_width_method(script, StringName("narrow"));
	REQUIRE(narrow.is_typed());
	ERR_PRINT_OFF;
	narrow.push_back(beyond_uint32);
	ERR_PRINT_ON;
	CHECK(narrow.is_empty());

	Array wide = call_typed_container_width_method(script, StringName("wide"));
	REQUIRE(wide.is_typed());
	wide.push_back(beyond_uint32);
	CHECK(wide.size() == 1);
}

TEST_CASE("[Modules][FoundryScript][NumericType] An element slot that declared no width stays unconstrained") {
	ScopedTypedContainerWidthLanguage language;

	const char *source =
			"func of_string() -> Array:\n"
			"\tvar values: Array[String] = []\n"
			"\treturn values\n"
			"\n"
			"func of_float() -> Array:\n"
			"\tvar values: Array[float] = []\n"
			"\treturn values\n";

	Ref<FoundryScript> script = compile_typed_container_width_source(source);

	const Array strings = call_typed_container_width_method(script, StringName("of_string"));
	REQUIRE(strings.is_typed());
	CHECK(strings.get_element_type().builtin_type == Variant::STRING);
	CHECK(strings.get_element_type().numeric_type == NumericType::NONE);

	const Array floats = call_typed_container_width_method(script, StringName("of_float"));
	REQUIRE(floats.is_typed());
	CHECK(floats.get_element_type().builtin_type == Variant::FLOAT);
	CHECK(floats.get_element_type().numeric_type == NumericType::NONE);
}

} // namespace FSTests
