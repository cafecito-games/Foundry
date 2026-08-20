/**************************************************************************/
/*  test_self_contract_gradual_values.h                                   */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

// A destination that mentions `Self` is answered by the receiver contract rather than by ordinary
// compatibility, and an alternative of a union that names no `Self` admits a value whose static type
// promises nothing. The contract has to book that crossing the way ordinary validation books it, or a
// `Variant` reaches a typed destination unreported purely because the annotation mentions `Self`
// somewhere. Each case below states one program twice -- once with a `Self` alternative, once without
// -- and requires the two to be booked identically.
static bool self_contract_line_is_unsafe(const String &p_source, int p_line) {
	FSParser parser;
	REQUIRE(parser.parse(p_source, "user://test.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();
	return parser.get_unsafe_lines().has(p_line);
}

TEST_CASE("[Modules][FoundryScript][SelfContract] A dynamic argument is booked unsafe for a Self-bearing union") {
	const String self_union = R"(
class Receiver:
	func take(_value: int | (int, Self)) -> void:
		pass

	func drive() -> void:
		var dynamic: Variant = 5
		take(dynamic)
)";
	const String plain_union = R"(
class Receiver:
	func take(_value: int | String) -> void:
		pass

	func drive() -> void:
		var dynamic: Variant = 5
		take(dynamic)
)";
	CHECK(self_contract_line_is_unsafe(plain_union, 8));
	CHECK(self_contract_line_is_unsafe(self_union, 8));
}

TEST_CASE("[Modules][FoundryScript][SelfContract] A dynamic initializer is booked unsafe for a Self-bearing union") {
	const String self_union = R"(
class Receiver:
	func drive() -> void:
		var dynamic: Variant = 5
		var link: int | (int, Self) = dynamic
		print(link)
)";
	const String plain_union = R"(
class Receiver:
	func drive() -> void:
		var dynamic: Variant = 5
		var link: int | String = dynamic
		print(link)
)";
	CHECK(self_contract_line_is_unsafe(plain_union, 5));
	CHECK(self_contract_line_is_unsafe(self_union, 5));
}

TEST_CASE("[Modules][FoundryScript][SelfContract] A dynamic assignment is booked unsafe for a Self-bearing union") {
	const String self_union = R"(
class Receiver:
	func drive() -> void:
		var dynamic: Variant = 5
		var link: int | (int, Self) = 1
		link = dynamic
		print(link)
)";
	const String plain_union = R"(
class Receiver:
	func drive() -> void:
		var dynamic: Variant = 5
		var link: int | String = 1
		link = dynamic
		print(link)
)";
	CHECK(self_contract_line_is_unsafe(plain_union, 6));
	CHECK(self_contract_line_is_unsafe(self_union, 6));
}

TEST_CASE("[Modules][FoundryScript][SelfContract] A dynamic return is booked unsafe for a Self-bearing union") {
	const String self_union = R"(
class Receiver:
	func make() -> int | (int, Self):
		var dynamic: Variant = 5
		return dynamic
)";
	const String plain_union = R"(
class Receiver:
	func make() -> int | String:
		var dynamic: Variant = 5
		return dynamic
)";
	CHECK(self_contract_line_is_unsafe(plain_union, 5));
	CHECK(self_contract_line_is_unsafe(self_union, 5));
}

// Strict dynamic mode refuses the value before the contract can admit it, so the bookkeeping the
// permissive mode needs is never reached there and both spellings stay errors.
TEST_CASE("[Modules][FoundryScript][SelfContract] Strict dynamic mode refuses a dynamic value for either union") {
	const String self_union = R"(
class Receiver:
	func take(_value: int | (int, Self)) -> void:
		pass

	func drive() -> void:
		var dynamic: Variant = 5
		take(dynamic)
)";
	const String plain_union = R"(
class Receiver:
	func take(_value: int | String) -> void:
		pass

	func drive() -> void:
		var dynamic: Variant = 5
		take(dynamic)
)";
	auto strict_error_count = [](const String &p_source) {
		FSParser parser;
		REQUIRE(parser.parse(p_source, "user://test.fs", false) == OK);
		FSAnalyzer analyzer(&parser);
		analyzer.set_strict_dynamic_checks(true);
		analyzer.analyze();
		return parser.get_errors().size();
	};
	CHECK(strict_error_count(plain_union) > 0);
	CHECK(strict_error_count(self_union) > 0);
}

} // namespace FSTests
