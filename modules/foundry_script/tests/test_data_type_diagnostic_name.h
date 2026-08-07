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

} // namespace FSTests
