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

// A soft value is the other gradual carrier: it has a type, but not one the destination can be checked
// against. It is admitted and booked wherever an alternative names no `Self`, and refused where every
// alternative needs `Self` resolved -- which is what a destination written as `Self` alone does.
TEST_CASE("[Modules][FoundryScript][SelfContract] A soft value is booked unsafe for a Self-bearing union") {
	const String self_union = R"(
class Receiver:
	var counter = 5

	func drive() -> void:
		var soft = counter
		var link: int | (int, Self) = soft
		print(link)
)";
	const String plain_union = R"(
class Receiver:
	var counter = 5

	func drive() -> void:
		var soft = counter
		var link: int | String = soft
		print(link)
)";
	CHECK(self_contract_line_is_unsafe(plain_union, 7));
	CHECK(self_contract_line_is_unsafe(self_union, 7));
}

TEST_CASE("[Modules][FoundryScript][SelfContract] A soft value is refused when every alternative names Self") {
	const String every_alternative = R"(
class Receiver:
	var counter = 5

	func drive() -> void:
		var soft = counter
		var link: (int, Self) | (String, Self) = soft
		print(link)
)";
	const String bare_self = R"(
class Receiver:
	var counter = 5

	func drive() -> void:
		var soft = counter
		var link: Self = soft
		print(link)
)";
	auto error_count = [](const String &p_source) {
		FSParser parser;
		REQUIRE(parser.parse(p_source, "user://test.fs", false) == OK);
		FSAnalyzer analyzer(&parser);
		analyzer.analyze();
		return parser.get_errors().size();
	};
	CHECK(error_count(bare_self) > 0);
	CHECK(error_count(every_alternative) > 0);
}

// The gradual admission books a promise that the run time will check what the analyzer could not.
// Strict dynamic mode exists to refuse that promise, so a destination mentioning `Self` refuses it
// there too rather than admitting through an alternative that names none.
TEST_CASE("[Modules][FoundryScript][SelfContract] Strict dynamic mode refuses a gradual initializer for either union") {
	const String self_union = R"(
class Receiver:
	func drive(source) -> void:
		var link: int | (int, Self) = source
		print(link)
)";
	const String plain_union = R"(
class Receiver:
	func drive(source) -> void:
		var link: int | String = source
		print(link)
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

// A value projected out of a raw generic receiver is the third gradual carrier: it has a type, but one
// naming a parameter the use site never bound, so the destination's declared type is a claim no store
// validates. Ordinary validation books the line unsafe at each position; a destination mentioning
// `Self` books it the same way rather than passing silently through an alternative that names none.
TEST_CASE("[Modules][FoundryScript][SelfContract] A raw generic projection is booked unsafe for a Self-bearing union") {
	SUBCASE("initializer") {
		const String self_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func drive(raw: Box) -> void:
		var link: int | (int, Self) = raw.value
		print(link)
)";
		const String plain_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func drive(raw: Box) -> void:
		var link: int | String = raw.value
		print(link)
)";
		CHECK(self_contract_line_is_unsafe(plain_union, 8));
		CHECK(self_contract_line_is_unsafe(self_union, 8));
	}
	SUBCASE("return") {
		const String self_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func make(raw: Box) -> int | (int, Self):
		return raw.value
)";
		const String plain_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func make(raw: Box) -> int | String:
		return raw.value
)";
		CHECK(self_contract_line_is_unsafe(plain_union, 8));
		CHECK(self_contract_line_is_unsafe(self_union, 8));
	}
	SUBCASE("assignment") {
		const String self_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func drive(raw: Box) -> void:
		var link: int | (int, Self) = 1
		link = raw.value
		print(link)
)";
		const String plain_union = R"(
class Box[T]:
	var value: T


class Receiver:
	func drive(raw: Box) -> void:
		var link: int | String = 1
		link = raw.value
		print(link)
)";
		CHECK(self_contract_line_is_unsafe(plain_union, 9));
		CHECK(self_contract_line_is_unsafe(self_union, 9));
	}
}

} // namespace FSTests
