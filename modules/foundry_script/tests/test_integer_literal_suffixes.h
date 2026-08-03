/**************************************************************************/
/*  test_integer_literal_suffixes.h                                       */
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

#include "../fs_parser.h"
#include "../fs_tokenizer.h"
#include "../fs_tokenizer_buffer.h"

#include "tests/test_macros.h"

namespace TestIntegerLiteralSuffixes {

// The first literal or error token produced by scanning an expression, which is the whole observable
// result of a numeric literal: its Variant carrier, its value, and the width it declared.
static FSTokenizer::Token scan_first_number(const String &p_expression) {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(vformat("var value = %s\n", p_expression));

	FSTokenizer::Token token = tokenizer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF) {
		if (token.type == FSTokenizer::Token::LITERAL || token.type == FSTokenizer::Token::ERROR) {
			return token;
		}
		token = tokenizer.scan();
	}
	return token;
}

static void check_integer_literal(const String &p_expression, NumericType p_expected_numeric_type, const Variant &p_expected_value) {
	const FSTokenizer::Token token = scan_first_number(p_expression);
	CHECK_MESSAGE(token.type == FSTokenizer::Token::LITERAL, vformat("%s should scan as a literal, got: %s", p_expression, String(token.literal)));
	CHECK_MESSAGE(token.numeric_type == p_expected_numeric_type, vformat("%s declared numeric type %s", p_expression, numeric_type_name(token.numeric_type)));
	CHECK(token.literal.get_type() == p_expected_value.get_type());
	CHECK(token.literal == p_expected_value);
}

// The first error reported anywhere in the expression. Some numeric diagnostics are pushed onto the
// tokenizer's error stack and surface on a later `scan()` than the token they describe, so the whole
// source has to be consumed rather than only the first literal.
static String scan_error_message(const String &p_expression) {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(vformat("var value = %s\n", p_expression));

	FSTokenizer::Token token = tokenizer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF) {
		if (token.type == FSTokenizer::Token::ERROR) {
			return token.literal;
		}
		token = tokenizer.scan();
	}
	return String();
}

} // namespace TestIntegerLiteralSuffixes

TEST_CASE("[Modules][FoundryScript][NumericTypes] Canonical suffixes select the four integer types") {
	using namespace TestIntegerLiteralSuffixes;

	check_integer_literal("42", NumericType::INT32, int64_t(42));
	check_integer_literal("42U", NumericType::UINT32, uint64_t(42));
	check_integer_literal("42L", NumericType::INT64, int64_t(42));
	check_integer_literal("42UL", NumericType::UINT64, uint64_t(42));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Only a suffix declares a literal's width") {
	using namespace TestIntegerLiteralSuffixes;

	CHECK(scan_first_number("42U").numeric_type_is_explicit);
	CHECK(scan_first_number("42L").numeric_type_is_explicit);
	CHECK(scan_first_number("42UL").numeric_type_is_explicit);
	CHECK(scan_first_number("0xFFU").numeric_type_is_explicit);

	// An inferred width records which type the value would take on its own; it is not a constraint the
	// literal declared, so the destination still decides which integer slot it may enter.
	CHECK_FALSE(scan_first_number("42").numeric_type_is_explicit);
	CHECK_FALSE(scan_first_number("2147483648").numeric_type_is_explicit);
	CHECK_FALSE(scan_first_number("0xFF").numeric_type_is_explicit);
	CHECK_FALSE(scan_first_number("1.5").numeric_type_is_explicit);
	CHECK(scan_first_number("1.5").numeric_type == NumericType::NONE);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Suffixes apply to every integer notation") {
	using namespace TestIntegerLiteralSuffixes;

	check_integer_literal("0xFFFF", NumericType::INT32, int64_t(0xFFFF));
	check_integer_literal("0xFFFFU", NumericType::UINT32, uint64_t(0xFFFF));
	check_integer_literal("0xFFFFL", NumericType::INT64, int64_t(0xFFFF));
	check_integer_literal("0xFFFFUL", NumericType::UINT64, uint64_t(0xFFFF));

	check_integer_literal("0b1010", NumericType::INT32, int64_t(10));
	check_integer_literal("0b1010U", NumericType::UINT32, uint64_t(10));
	check_integer_literal("0b1010L", NumericType::INT64, int64_t(10));
	check_integer_literal("0b1010UL", NumericType::UINT64, uint64_t(10));

	check_integer_literal("1_000", NumericType::INT32, int64_t(1000));
	check_integer_literal("1_000U", NumericType::UINT32, uint64_t(1000));
	check_integer_literal("1_000L", NumericType::INT64, int64_t(1000));
	check_integer_literal("1_000UL", NumericType::UINT64, uint64_t(1000));
	check_integer_literal("0xFF_FFU", NumericType::UINT32, uint64_t(0xFFFF));
	check_integer_literal("0b1010_1010UL", NumericType::UINT64, uint64_t(0xAA));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Unsuffixed literals infer int or long by range") {
	using namespace TestIntegerLiteralSuffixes;

	check_integer_literal("2147483647", NumericType::INT32, int64_t(INT32_MAX));
	check_integer_literal("2147483648", NumericType::INT64, int64_t(2147483648LL));
	check_integer_literal("-2147483648", NumericType::INT32, int64_t(INT32_MIN));
	check_integer_literal("-2147483649", NumericType::INT64, int64_t(-2147483649LL));
	check_integer_literal("9223372036854775807", NumericType::INT64, int64_t(INT64_MAX));
	check_integer_literal("-9223372036854775808", NumericType::INT64, int64_t(INT64_MIN));
	check_integer_literal("0x7FFFFFFF", NumericType::INT32, int64_t(INT32_MAX));
	check_integer_literal("0x80000000", NumericType::INT64, int64_t(2147483648LL));

	CHECK(scan_error_message("9223372036854775808").contains(R"(add the "UL" suffix)"));
	CHECK(scan_error_message("-9223372036854775809").contains(R"(out of range for "long")"));
	CHECK(scan_error_message("18446744073709551615").contains(R"(add the "UL" suffix)"));
	CHECK(scan_error_message("0xFFFFFFFFFFFFFFFF").contains(R"(add the "UL" suffix)"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Suffixed literals enforce their range without wrapping") {
	using namespace TestIntegerLiteralSuffixes;

	check_integer_literal("4294967295U", NumericType::UINT32, uint64_t(UINT32_MAX));
	check_integer_literal("18446744073709551615UL", NumericType::UINT64, uint64_t(UINT64_MAX));
	check_integer_literal("9223372036854775807L", NumericType::INT64, int64_t(INT64_MAX));
	check_integer_literal("-9223372036854775808L", NumericType::INT64, int64_t(INT64_MIN));

	// A value one past the selected type never wraps into it and never widens past it.
	CHECK(scan_error_message("4294967296U").contains(R"(out of range for "uint")"));
	CHECK(scan_error_message("0x100000000U").contains(R"(out of range for "uint")"));
	CHECK(scan_error_message("9223372036854775808L").contains(R"(out of range for "long")"));
	CHECK(scan_error_message("18446744073709551616UL").contains(R"(out of range for "ulong")"));

	// A negative magnitude is never reinterpreted as a large unsigned value.
	CHECK(scan_error_message("-1U").contains("negative"));
	CHECK(scan_error_message("-1UL").contains("negative"));
	CHECK(scan_error_message("-0x1U").contains("negative"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Noncanonical suffix spellings name their replacement") {
	using namespace TestIntegerLiteralSuffixes;

	CHECK(scan_error_message("1u").contains(R"(write "1U")"));
	CHECK(scan_error_message("1l").contains(R"(write "1L")"));
	CHECK(scan_error_message("1ul").contains(R"(write "1UL")"));
	CHECK(scan_error_message("1uL").contains(R"(write "1UL")"));
	CHECK(scan_error_message("1Ul").contains(R"(write "1UL")"));
	CHECK(scan_error_message("1LU").contains(R"(write "1UL")"));
	CHECK(scan_error_message("1lu").contains(R"(write "1UL")"));
	CHECK(scan_error_message("0xFFu").contains(R"(write "0xFFU")"));
	CHECK(scan_error_message("1_000l").contains(R"(write "1_000L")"));

	// Any other trailing letters stay an ordinary invalid-notation error rather than proposing a
	// replacement the tokenizer cannot justify.
	CHECK(scan_error_message("1ULL").contains("Invalid numeric notation"));
	CHECK(scan_error_message("1abc").contains("Invalid numeric notation"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Suffixes are rejected outside integer literals") {
	using namespace TestIntegerLiteralSuffixes;

	CHECK(scan_error_message("1.5U").contains("Invalid numeric notation"));
	CHECK(scan_error_message("1e5L").contains("Invalid numeric notation"));

	// A tuple index is a bare decimal integer, so the suffix grammar does not reach it.
	CHECK(scan_error_message("tuple.0U").contains("tuple index"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] The token buffer round-trips literal widths") {
	const String source = "var a = 1\nvar b = 1U\nvar c = 1L\nvar d = 1UL\n";
	const Vector<uint8_t> code = FSTokenizerBuffer::parse_code_string(source, FSTokenizerBuffer::COMPRESS_NONE);

	FSTokenizerBuffer buffer;
	REQUIRE(buffer.set_code_buffer(code) == OK);

	Vector<NumericType> literal_widths;
	Vector<bool> explicit_widths;
	Vector<Variant::Type> carriers;
	FSTokenizer::Token token = buffer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF) {
		if (token.type == FSTokenizer::Token::LITERAL) {
			literal_widths.push_back(token.numeric_type);
			explicit_widths.push_back(token.numeric_type_is_explicit);
			carriers.push_back(token.literal.get_type());
		}
		token = buffer.scan();
	}

	REQUIRE(literal_widths.size() == 4);
	CHECK(literal_widths[0] == NumericType::INT32);
	CHECK(literal_widths[1] == NumericType::UINT32);
	CHECK(literal_widths[2] == NumericType::INT64);
	CHECK(literal_widths[3] == NumericType::UINT64);

	CHECK_FALSE(explicit_widths[0]);
	CHECK(explicit_widths[1]);
	CHECK(explicit_widths[2]);
	CHECK(explicit_widths[3]);

	// The constant pool compares an INT and a UINT of equal value as one key, so the carrier survives
	// only if the pool distinguishes them.
	CHECK(carriers[0] == Variant::INT);
	CHECK(carriers[1] == Variant::UINT);
	CHECK(carriers[2] == Variant::INT);
	CHECK(carriers[3] == Variant::UINT);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] The token buffer rejects malformed literal widths") {
	const Vector<uint8_t> code = FSTokenizerBuffer::parse_code_string("var a = 1\n", FSTokenizerBuffer::COMPRESS_NONE);

	// Find the encoded `INT32` descriptor for the sole literal and corrupt it two ways: a descriptor
	// value no `NumericType` defines, and a valid descriptor whose range excludes the pooled constant.
	int descriptor_position = -1;
	for (int i = code.size() - 1; i >= 0; i--) {
		if (code[i] == uint8_t(NumericType::INT32)) {
			descriptor_position = i;
			break;
		}
	}
	REQUIRE(descriptor_position != -1);

	SUBCASE("Undefined descriptor value") {
		Vector<uint8_t> corrupted = code;
		corrupted.write[descriptor_position] = uint8_t(NumericType::MAX);
		FSTokenizerBuffer buffer;
		REQUIRE(buffer.set_code_buffer(corrupted) == OK);

		bool found_error = false;
		FSTokenizer::Token token = buffer.scan();
		while (token.type != FSTokenizer::Token::TK_EOF) {
			if (token.type == FSTokenizer::Token::ERROR) {
				found_error = true;
				break;
			}
			token = buffer.scan();
		}
		CHECK(found_error);
	}

	SUBCASE("Declared-width flag without a width") {
		Vector<uint8_t> corrupted = code;
		corrupted.write[descriptor_position] = FSTokenizerBuffer::NUMERIC_TYPE_EXPLICIT_FLAG | uint8_t(NumericType::NONE);
		FSTokenizerBuffer buffer;
		REQUIRE(buffer.set_code_buffer(corrupted) == OK);

		bool found_error = false;
		FSTokenizer::Token token = buffer.scan();
		while (token.type != FSTokenizer::Token::TK_EOF) {
			if (token.type == FSTokenizer::Token::ERROR) {
				found_error = true;
				break;
			}
			token = buffer.scan();
		}
		CHECK(found_error);
	}

	SUBCASE("Descriptor disagreeing with its constant") {
		Vector<uint8_t> corrupted = code;
		corrupted.write[descriptor_position] = uint8_t(NumericType::UINT64);
		FSTokenizerBuffer buffer;
		REQUIRE(buffer.set_code_buffer(corrupted) == OK);

		bool found_error = false;
		FSTokenizer::Token token = buffer.scan();
		while (token.type != FSTokenizer::Token::TK_EOF) {
			if (token.type == FSTokenizer::Token::ERROR) {
				found_error = true;
				break;
			}
			token = buffer.scan();
		}
		CHECK(found_error);
	}
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Source names resolve independently of carriers") {
	CHECK(FSParser::get_builtin_data_type("int").numeric_type == NumericType::INT32);
	CHECK(FSParser::get_builtin_data_type("uint").numeric_type == NumericType::UINT32);
	CHECK(FSParser::get_builtin_data_type("long").numeric_type == NumericType::INT64);
	CHECK(FSParser::get_builtin_data_type("ulong").numeric_type == NumericType::UINT64);

	CHECK(FSParser::get_builtin_data_type("int").builtin_type == Variant::INT);
	CHECK(FSParser::get_builtin_data_type("long").builtin_type == Variant::INT);
	CHECK(FSParser::get_builtin_data_type("uint").builtin_type == Variant::UINT);
	CHECK(FSParser::get_builtin_data_type("ulong").builtin_type == Variant::UINT);

	// Exactly four integer spellings exist, and non-numeric built-ins keep their unconstrained mapping.
	CHECK_FALSE(FSParser::get_builtin_data_type("int64").is_valid());
	CHECK_FALSE(FSParser::get_builtin_data_type("uint32").is_valid());
	CHECK_FALSE(FSParser::get_builtin_data_type("byte").is_valid());
	CHECK(FSParser::get_builtin_data_type("String").builtin_type == Variant::STRING);
	CHECK(FSParser::get_builtin_data_type("String").numeric_type == NumericType::NONE);
	CHECK(FSParser::get_builtin_data_type("float").numeric_type == NumericType::NONE);
}
