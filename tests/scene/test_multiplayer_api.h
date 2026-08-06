/**************************************************************************/
/*  test_multiplayer_api.h                                                */
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

#include "core/templates/local_vector.h"
#include "core/variant/variant.h"
#include "scene/main/multiplayer_api.h"

#include "tests/test_macros.h"

namespace TestMultiplayerAPI {

// Round-trips one value through the RPC argument codec exactly as a remote call would, and
// returns the value the receiving peer would observe.
static Variant rpc_round_trip(const Variant &p_value) {
	int required_size = 0;
	const Error measure_error = MultiplayerAPI::encode_and_compress_variant(p_value, nullptr, required_size, false);
	REQUIRE_EQ(measure_error, OK);
	REQUIRE_GT(required_size, 0);

	LocalVector<uint8_t> buffer;
	buffer.resize(required_size);
	int written_size = 0;
	const Error encode_error = MultiplayerAPI::encode_and_compress_variant(p_value, buffer.ptr(), written_size, false);
	REQUIRE_EQ(encode_error, OK);
	REQUIRE_EQ(written_size, required_size);

	Variant decoded;
	int consumed_size = 0;
	const Error decode_error = MultiplayerAPI::decode_and_decompress_variant(decoded, buffer.ptr(), written_size, &consumed_size, false);
	REQUIRE_EQ(decode_error, OK);
	CHECK_EQ(consumed_size, written_size);
	return decoded;
}

TEST_CASE("[MultiplayerAPI][UInt] Signed RPC arguments keep their carrier and value") {
	const int64_t values[] = { 0, 1, -1, INT8_MIN, INT8_MAX, INT16_MIN, INT16_MAX, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX };
	for (const int64_t value : values) {
		const Variant decoded = rpc_round_trip(Variant(value));
		CHECK_EQ(decoded.get_type(), Variant::INT);
		CHECK_EQ(decoded.operator int64_t(), value);
	}
}

TEST_CASE("[MultiplayerAPI][UInt] Unsigned RPC arguments keep their carrier and value") {
	const uint64_t values[] = {
		0,
		1,
		UINT8_MAX,
		uint64_t(UINT8_MAX) + 1,
		UINT16_MAX,
		uint64_t(UINT16_MAX) + 1,
		UINT32_MAX,
		uint64_t(UINT32_MAX) + 1,
		uint64_t(INT64_MAX),
		uint64_t(INT64_MAX) + 1,
		UINT64_MAX,
	};
	for (const uint64_t value : values) {
		const Variant decoded = rpc_round_trip(Variant(value));
		CHECK_EQ(decoded.get_type(), Variant::UINT);
		CHECK_EQ(decoded.operator uint64_t(), value);
	}
}

TEST_CASE("[MultiplayerAPI][UInt] Unsigned RPC arguments compress by magnitude") {
	struct ExpectedSize {
		uint64_t value;
		int size;
	};
	// One meta byte plus the smallest unsigned payload that still holds the value exactly.
	const ExpectedSize expectations[] = {
		{ 0, 2 },
		{ UINT8_MAX, 2 },
		{ uint64_t(UINT8_MAX) + 1, 3 },
		{ UINT16_MAX, 3 },
		{ uint64_t(UINT16_MAX) + 1, 5 },
		{ UINT32_MAX, 5 },
		{ uint64_t(UINT32_MAX) + 1, 9 },
		{ UINT64_MAX, 9 },
	};
	for (const ExpectedSize &expectation : expectations) {
		int size = 0;
		REQUIRE_EQ(MultiplayerAPI::encode_and_compress_variant(Variant(expectation.value), nullptr, size, false), OK);
		CHECK_MESSAGE(size == expectation.size, vformat("Unsigned value %s should encode in %d bytes.", String::num_uint64(expectation.value), expectation.size));
	}
}

TEST_CASE("[MultiplayerAPI][UInt] Unsigned RPC decoding rejects truncated payloads") {
	const uint64_t values[] = { UINT8_MAX, UINT16_MAX, UINT32_MAX, UINT64_MAX };
	for (const uint64_t value : values) {
		int size = 0;
		REQUIRE_EQ(MultiplayerAPI::encode_and_compress_variant(Variant(value), nullptr, size, false), OK);

		LocalVector<uint8_t> buffer;
		buffer.resize(size);
		int written_size = 0;
		REQUIRE_EQ(MultiplayerAPI::encode_and_compress_variant(Variant(value), buffer.ptr(), written_size, false), OK);

		Variant decoded;
		int consumed_size = 0;
		ERR_PRINT_OFF;
		const Error decode_error = MultiplayerAPI::decode_and_decompress_variant(decoded, buffer.ptr(), written_size - 1, &consumed_size, false);
		ERR_PRINT_ON;
		CHECK_EQ(decode_error, ERR_INVALID_DATA);
		CHECK_EQ(decoded.get_type(), Variant::NIL);
	}
}

TEST_CASE("[MultiplayerAPI][UInt] Mixed RPC argument lists keep every carrier") {
	const Variant signed_minimum = Variant(int64_t(INT64_MIN));
	const Variant signed_maximum = Variant(int64_t(INT64_MAX));
	const Variant unsigned_zero = Variant(uint64_t(0));
	const Variant unsigned_maximum = Variant(UINT64_MAX);
	const Variant *arguments[] = { &signed_minimum, &signed_maximum, &unsigned_zero, &unsigned_maximum };
	const int argument_count = 4;

	// The raw single-argument optimization only applies to a lone `PackedByteArray`, so a
	// multi-argument list always travels through the per-argument codec.
	int required_size = 0;
	REQUIRE_EQ(MultiplayerAPI::encode_and_compress_variants(arguments, argument_count, nullptr, required_size, nullptr, false), OK);

	LocalVector<uint8_t> buffer;
	buffer.resize(required_size);
	int written_size = 0;
	REQUIRE_EQ(MultiplayerAPI::encode_and_compress_variants(arguments, argument_count, buffer.ptr(), written_size, nullptr, false), OK);

	// The decoder fills a caller-sized argument list, matching how a receiving peer allocates the
	// slots from the callee's expected argument count.
	Vector<Variant> decoded;
	decoded.resize(argument_count);
	int consumed_size = 0;
	REQUIRE_EQ(MultiplayerAPI::decode_and_decompress_variants(decoded, buffer.ptr(), written_size, consumed_size, false, false), OK);
	REQUIRE_EQ(decoded.size(), argument_count);

	CHECK_EQ(decoded[0].get_type(), Variant::INT);
	CHECK_EQ(decoded[0].operator int64_t(), INT64_MIN);
	CHECK_EQ(decoded[1].get_type(), Variant::INT);
	CHECK_EQ(decoded[1].operator int64_t(), INT64_MAX);
	CHECK_EQ(decoded[2].get_type(), Variant::UINT);
	CHECK_EQ(decoded[2].operator uint64_t(), 0);
	CHECK_EQ(decoded[3].get_type(), Variant::UINT);
	CHECK_EQ(decoded[3].operator uint64_t(), UINT64_MAX);
}

TEST_CASE("[MultiplayerAPI][UInt] Containers carried by RPC keep unsigned elements exact") {
	Array array;
	array.push_back(Variant(UINT64_MAX));
	array.push_back(Variant(int64_t(-1)));

	Dictionary dictionary;
	dictionary["unsigned"] = Variant(uint64_t(INT64_MAX) + 1);
	dictionary["signed"] = Variant(int64_t(INT64_MIN));
	array.push_back(dictionary);

	const Variant decoded_value = rpc_round_trip(array);
	REQUIRE_EQ(decoded_value.get_type(), Variant::ARRAY);
	const Array decoded = decoded_value;
	REQUIRE_EQ(decoded.size(), 3);

	CHECK_EQ(decoded[0].get_type(), Variant::UINT);
	CHECK_EQ(decoded[0].operator uint64_t(), UINT64_MAX);
	CHECK_EQ(decoded[1].get_type(), Variant::INT);
	CHECK_EQ(decoded[1].operator int64_t(), -1);

	REQUIRE_EQ(decoded[2].get_type(), Variant::DICTIONARY);
	const Dictionary decoded_dictionary = decoded[2];
	CHECK_EQ(decoded_dictionary["unsigned"].get_type(), Variant::UINT);
	CHECK_EQ(decoded_dictionary["unsigned"].operator uint64_t(), uint64_t(INT64_MAX) + 1);
	CHECK_EQ(decoded_dictionary["signed"].get_type(), Variant::INT);
	CHECK_EQ(decoded_dictionary["signed"].operator int64_t(), INT64_MIN);
}

} // namespace TestMultiplayerAPI
