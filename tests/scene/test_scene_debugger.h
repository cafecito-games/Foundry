/**************************************************************************/
/*  test_scene_debugger.h                                                 */
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

#ifdef DEBUG_ENABLED

#include "core/io/marshalls.h"
#include "scene/debugger/scene_debugger.h"
#include "scene/main/node.h"

#include "tests/test_macros.h"

namespace TestSceneDebugger {

TEST_CASE("[SceneDebugger] Remote object serialization round-trips the id and usage mask") {
	Node *node = memnew(Node);
	node->set_name("RemoteTarget");

	SceneDebuggerObject source(node);
	REQUIRE_FALSE(source.properties.is_empty());

	Array serialized;
	source.serialize(serialized);
	REQUIRE_EQ(serialized.size(), 3);

	// The wire protocol declares the object id and every property usage mask as signed integers,
	// and `deserialize()` refuses the message outright when either arrives on another carrier.
	CHECK_EQ(serialized[0].get_type(), Variant::INT);
	const Array properties = serialized[2];
	REQUIRE_FALSE(properties.is_empty());
	const Array first_property = properties[0];
	REQUIRE_EQ(first_property.size(), 6);
	CHECK_EQ(first_property[4].get_type(), Variant::INT);

	SceneDebuggerObject restored;
	restored.deserialize(serialized);
	CHECK_EQ(restored.id, node->get_instance_id());
	CHECK_EQ(restored.class_name, source.class_name);
	CHECK_EQ(restored.properties.size(), source.properties.size());

	memdelete(node);
}

// Sends one debugger message across the same binary hop the remote debugger uses, so the result is
// what an attached editor would actually inspect rather than an in-process copy.
static Variant debugger_wire_round_trip(const Variant &p_message) {
	int size = 0;
	REQUIRE_EQ(encode_variant(p_message, nullptr, size, false), OK);

	Vector<uint8_t> buffer;
	buffer.resize(size);
	REQUIRE_EQ(encode_variant(p_message, buffer.ptrw(), size, false), OK);

	Variant decoded;
	REQUIRE_EQ(decode_variant(decoded, buffer.ptr(), buffer.size(), nullptr, false), OK);
	return decoded;
}

TEST_CASE("[SceneDebugger][UInt] Remote object inspection distinguishes unsigned from signed values") {
	Node *node = memnew(Node);
	node->set_name("UnsignedRemoteTarget");
	node->set_meta("unsigned_maximum", Variant(UINT64_MAX));
	node->set_meta("unsigned_above_signed_max", Variant(uint64_t(INT64_MAX) + 1));
	node->set_meta("signed_minimum", Variant(int64_t(INT64_MIN)));

	SceneDebuggerObject source(node);

	Array serialized;
	source.serialize(serialized);

	SceneDebuggerObject restored;
	restored.deserialize(debugger_wire_round_trip(serialized));

	struct ExpectedProperty {
		const char *name;
		Variant::Type type;
		uint64_t unsigned_value;
		int64_t signed_value;
	};
	const ExpectedProperty expectations[] = {
		{ "metadata/unsigned_maximum", Variant::UINT, UINT64_MAX, 0 },
		{ "metadata/unsigned_above_signed_max", Variant::UINT, uint64_t(INT64_MAX) + 1, 0 },
		{ "metadata/signed_minimum", Variant::INT, 0, INT64_MIN },
	};

	for (const ExpectedProperty &expectation : expectations) {
		bool found = false;
		for (const SceneDebuggerObject::SceneDebuggerProperty &property : restored.properties) {
			if (property.first.name != expectation.name) {
				continue;
			}
			found = true;
			CHECK_MESSAGE(property.first.type == expectation.type, expectation.name);
			CHECK_MESSAGE(property.second.get_type() == expectation.type, expectation.name);
			if (expectation.type == Variant::UINT) {
				CHECK_MESSAGE(property.second.operator uint64_t() == expectation.unsigned_value, expectation.name);
				// The editor renders the inspected value through the Variant string conversion, so
				// the largest unsigned value must not surface as a wrapped signed one.
				CHECK_MESSAGE(property.second.operator String() == String::num_uint64(expectation.unsigned_value), expectation.name);
			} else {
				CHECK_MESSAGE(property.second.operator int64_t() == expectation.signed_value, expectation.name);
			}
			break;
		}
		CHECK_MESSAGE(found, expectation.name);
	}

	memdelete(node);
}

} // namespace TestSceneDebugger

#endif // DEBUG_ENABLED
