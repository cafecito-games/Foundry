/**************************************************************************/
/*  test_data_type_diagnostic_name.h                                      */
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

#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/variant/numeric_type.h"

#include "tests/test_macros.h"

// Coverage for the diagnostic-rendering collision (issue #1934): the 8- and 16-bit integer
// descriptors have no source spelling, so `DataType::to_string()` falls back to the carrier's name
// and renders `uint8`/`int16` identically to their 32-bit source-nameable siblings. A contrastive
// diagnostic ("should be X but is Y") must not stringify two distinct widths the same way, so
// `to_string_diagnostic()` names the width-only descriptor by its stable diagnostic name.

namespace FSTests {

static FSParser::DataType data_type_numeric_builtin(Variant::Type p_carrier, NumericType p_numeric_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_carrier;
	type.numeric_type = p_numeric_type;
	return type;
}

TEST_CASE("[Modules][FoundryScript][DataType] to_string collides width-only integers with their source-nameable siblings") {
	// The collision that motivates the diagnostic name: the source spelling is intentionally shared,
	// because `uint8`/`uint16` have no way to be written in source and fall back to the carrier.
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT8).to_string(), "uint");
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT16).to_string(), "uint");
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT32).to_string(), "uint");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT8).to_string(), "int");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT16).to_string(), "int");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT32).to_string(), "int");
}

TEST_CASE("[Modules][FoundryScript][DataType] to_string_diagnostic names width-only integers distinctly") {
	// A width-only descriptor renders by its stable diagnostic name so an expected/actual contrast
	// reads sensibly instead of "uint" versus "uint".
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT8).to_string_diagnostic(), "uint8");
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT16).to_string_diagnostic(), "uint16");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT8).to_string_diagnostic(), "int8");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT16).to_string_diagnostic(), "int16");

	// The width-only expected type no longer collides with a source-nameable actual type of a
	// different width, which is exactly the argument-error case from `PackedByteArray.append`.
	const String expected = data_type_numeric_builtin(Variant::UINT, NumericType::UINT8).to_string_diagnostic();
	const String actual = data_type_numeric_builtin(Variant::UINT, NumericType::UINT32).to_string_diagnostic();
	CHECK_NE(expected, actual);
	CHECK_EQ(expected, "uint8");
	CHECK_EQ(actual, "uint");
}

TEST_CASE("[Modules][FoundryScript][DataType] to_string_diagnostic leaves source-nameable and unconstrained integers alone") {
	// Source-nameable widths keep their source spelling; `to_string_diagnostic()` only rewrites the
	// descriptors that have none.
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT32).to_string_diagnostic(), "uint");
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::UINT64).to_string_diagnostic(), "ulong");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT32).to_string_diagnostic(), "int");
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::INT64).to_string_diagnostic(), "long");

	// A slot that never declared a width has nothing to disambiguate and keeps its carrier spelling.
	CHECK_EQ(data_type_numeric_builtin(Variant::INT, NumericType::NONE).to_string_diagnostic(), "int");
	CHECK_EQ(data_type_numeric_builtin(Variant::UINT, NumericType::NONE).to_string_diagnostic(), "uint");

	// A nullable width-only descriptor keeps the nullable suffix on the diagnostic name.
	FSParser::DataType nullable_byte = data_type_numeric_builtin(Variant::UINT, NumericType::UINT8);
	nullable_byte.is_nullable = true;
	CHECK_EQ(nullable_byte.to_string_diagnostic(), "uint8?");
}

TEST_CASE("[Modules][FoundryScript][DataType] A script trait is named by its declared name, not its identity") {
	// A trait identity carries the declaring file so two same-named file-local traits stay distinct
	// internally. That path is not source spelling, and rendering it would put an absolute build path
	// into a user-facing runtime diagnostic.
	FSDataType trait_type;
	trait_type.kind = FSDataType::FOUNDRY_SCRIPT;
	trait_type.builtin_type = Variant::OBJECT;
	trait_type.is_script_trait = true;
	trait_type.script_trait = StringName("/abs/path/to/holder.fs::Marker");
	CHECK_EQ(trait_type.get_source_type_name(), "Marker");

	// A namespaced trait keeps its namespace segments, which sit after the separator.
	trait_type.script_trait = StringName("/abs/path/to/holder.fs::game.ui.Marker");
	CHECK_EQ(trait_type.get_source_type_name(), "game.ui.Marker");

	// An identity with no declaring-file prefix is already the declared name.
	trait_type.script_trait = StringName("Marker");
	CHECK_EQ(trait_type.get_source_type_name(), "Marker");

	// A tuple element renders through the same rule.
	FSDataType tuple_type;
	tuple_type.kind = FSDataType::TUPLE;
	tuple_type.builtin_type = Variant::ARRAY;
	FSDataType int_element;
	int_element.kind = FSDataType::BUILTIN;
	int_element.builtin_type = Variant::INT;
	// A slot with no recorded width names the carrier's wide spelling, so the declared width is set
	// here to keep the assertion about the trait element rather than about integer naming.
	int_element.numeric_type = NumericType::INT32;
	tuple_type.container_element_types.push_back(int_element);
	tuple_type.container_element_types.push_back(trait_type);
	CHECK_EQ(tuple_type.get_source_type_name(), "(int, Marker)");
}

} // namespace FSTests
