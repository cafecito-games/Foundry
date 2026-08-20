/**************************************************************************/
/*  test_data_type_rendering.h                                            */
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

#include "modules/foundry_script/fs_parser.h"

#include "core/object/object.h"
#include "core/variant/variant.h"

#include "tests/test_macros.h"

// A gradual `...Array` tail is recorded only as `METHOD_FLAG_VARARG`, with the rich rest slot left
// empty (see `DataType::method_rest_parameter_type`). Rendering it as nothing at all collapsed a
// variadic callable onto a fixed-arity one, so a contrastive diagnostic could pit a type against
// what looked like itself. These pin the rendered spelling of both tail shapes.

namespace FSTests {

static FSParser::DataType data_type_rendering_builtin(Variant::Type p_builtin_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_builtin_type;
	return type;
}

static FSParser::DataType data_type_rendering_self() {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = Variant::OBJECT;
	type.type_parameter_name = SNAME("@Self");
	return type;
}

// Builds the shape the analyzer produces for an explicitly annotated callable type: a rich parameter
// list, a rich return type, and `method_info` carrying the arity bit.
static FSParser::DataType data_type_rendering_callable(Variant::Type p_carrier, const Vector<FSParser::DataType> &p_parameter_types, bool p_is_vararg) {
	FSParser::DataType type = data_type_rendering_builtin(p_carrier);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_parameter_types = p_parameter_types;
	type.method_return_type.push_back(data_type_rendering_builtin(Variant::NIL));
	if (p_is_vararg) {
		type.method_info.flags |= METHOD_FLAG_VARARG;
	}
	return type;
}

TEST_CASE("[Modules][FoundryScript][DataType] A gradual callable tail renders as ...Array") {
	const FSParser::DataType gradual = data_type_rendering_callable(Variant::CALLABLE, Vector<FSParser::DataType>(), true);
	CHECK_EQ(gradual.to_string(), "Callable[[...Array], void]");
	CHECK_EQ(gradual.to_string_diagnostic(), "Callable[[...Array], void]");

	// The tail follows the fixed prefix, so a `Self` parameter still leads the rendering.
	Vector<FSParser::DataType> self_parameter;
	self_parameter.push_back(data_type_rendering_self());
	const FSParser::DataType self_tail = data_type_rendering_callable(Variant::CALLABLE, self_parameter, true);
	CHECK_EQ(self_tail.to_string(), "Callable[[Self, ...Array], void]");
	CHECK_EQ(self_tail.to_string_diagnostic(), "Callable[[Self, ...Array], void]");

	// An async callable carries the same tail under its own name.
	FSParser::DataType async_gradual = data_type_rendering_callable(Variant::CALLABLE, Vector<FSParser::DataType>(), true);
	async_gradual.signature_is_async = true;
	CHECK_EQ(async_gradual.to_string(), "AsyncCallable[[...Array], void]");
	CHECK_EQ(async_gradual.to_string_diagnostic(), "AsyncCallable[[...Array], void]");
}

TEST_CASE("[Modules][FoundryScript][DataType] A narrowed callable tail renders its element type once") {
	FSParser::DataType rest_type = data_type_rendering_builtin(Variant::ARRAY);
	rest_type.set_container_element_type(0, data_type_rendering_builtin(Variant::INT));

	// A narrowing tail fills the rich slot *and* sets the arity bit, so the tail must not print twice.
	FSParser::DataType typed_tail = data_type_rendering_callable(Variant::CALLABLE, Vector<FSParser::DataType>(), true);
	typed_tail.set_method_rest_parameter_type(rest_type);
	CHECK_EQ(typed_tail.to_string(), "Callable[[...Array[int]], void]");
	CHECK_EQ(typed_tail.to_string_diagnostic(), "Callable[[...Array[int]], void]");
}

TEST_CASE("[Modules][FoundryScript][DataType] A fixed-arity callable renders no tail") {
	const FSParser::DataType fixed = data_type_rendering_callable(Variant::CALLABLE, Vector<FSParser::DataType>(), false);
	CHECK_EQ(fixed.to_string(), "Callable[[], void]");
	CHECK_EQ(fixed.to_string_diagnostic(), "Callable[[], void]");
}

TEST_CASE("[Modules][FoundryScript][DataType] A signal renders no tail even when it carries the vararg bit") {
	// The language has no variadic signal spelling to round-trip to, so a signal keeps its
	// parameter-only rendering regardless of the arity bit its `MethodInfo` happens to carry.
	FSParser::DataType signal_type = data_type_rendering_callable(Variant::SIGNAL, Vector<FSParser::DataType>(), true);
	signal_type.method_return_type.clear();
	CHECK_EQ(signal_type.to_string(), "Signal[[]]");
	CHECK_EQ(signal_type.to_string_diagnostic(), "Signal[[]]");
}

} // namespace FSTests
