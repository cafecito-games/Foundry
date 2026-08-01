/**************************************************************************/
/*  test_json_marshaller.h                                                */
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

#include "core/io/json.h"
#include "core/object/object.h"

#include "thirdparty/doctest/doctest.h"

namespace TestJSONMarshaller {

// A marshaller that only participates in encoding. The parse side declines, which is what the
// encode-only cases below want and what makes them independent of the parse contract.
class EncodeOnlyMarshaller : public JSONObjectMarshaller {
public:
	virtual bool lift_variant(const Variant &p_parsed, Variant &r_node) override { return false; }
	virtual bool make_parse_failure(const String &p_message, int p_line, Variant &r_result) override { return false; }
	virtual bool make_parse_success(const Variant &p_node, Variant &r_result) override { return false; }
};

class RecordingMarshaller : public EncodeOnlyMarshaller {
public:
	bool handled = false;
	bool should_handle = true;
	Variant payload;

	virtual bool marshal_object(Object *p_object, Variant &r_result) override {
		handled = true;
		if (!should_handle) {
			return false;
		}
		r_result = payload;
		return true;
	}
};

// Records what core hands to the parse side of the seam, in language-neutral terms: core must own
// the parse and never learn what the language's tree or result value looks like.
class ParseRecordingMarshaller : public JSONObjectMarshaller {
public:
	bool can_lift = true;
	bool can_build_result = true;

	bool lifted = false;
	Variant lifted_value;
	bool failed = false;
	String failure_message;
	int failure_line = 0;
	bool succeeded = false;
	Variant success_node;

	virtual bool marshal_object(Object *p_object, Variant &r_result) override { return false; }

	virtual bool lift_variant(const Variant &p_parsed, Variant &r_node) override {
		lifted = true;
		lifted_value = p_parsed;
		if (!can_lift) {
			return false;
		}
		// A stand-in for the language's tree value; core must treat it as opaque.
		Array node;
		node.push_back("node");
		node.push_back(p_parsed);
		r_node = node;
		return true;
	}

	virtual bool make_parse_failure(const String &p_message, int p_line, Variant &r_result) override {
		failed = true;
		failure_message = p_message;
		failure_line = p_line;
		if (!can_build_result) {
			return false;
		}
		Dictionary result;
		result["error"] = p_message;
		result["line"] = p_line;
		r_result = result;
		return true;
	}

	virtual bool make_parse_success(const Variant &p_node, Variant &r_result) override {
		succeeded = true;
		success_node = p_node;
		if (!can_build_result) {
			return false;
		}
		Dictionary result;
		result["value"] = p_node;
		r_result = result;
		return true;
	}
};

// Saves and restores the process-wide registration so a case that installs a test marshaller does
// not clear whatever a module registered at startup for the rest of the run.
class MarshallerScope {
	JSONObjectMarshaller *previous = JSON::get_object_marshaller();

public:
	~MarshallerScope() { JSON::set_object_marshaller(previous); }
};

TEST_CASE("[JSONMarshal] No marshaller preserves existing output") {
	MarshallerScope marshaller_scope;
	JSON::set_object_marshaller(nullptr);

	Object *object = memnew(Object);
	String result = JSON::stringify(object);
	CHECK(result.begins_with("\""));
	CHECK(result.ends_with("\""));
	memdelete(object);
}

TEST_CASE("[JSONMarshal] No marshaller preserves existing output for null and freed objects") {
	MarshallerScope marshaller_scope;
	JSON::set_object_marshaller(nullptr);

	CHECK(JSON::stringify(Variant((Object *)nullptr)) == "\"<Object#null>\"");

	Object *object = memnew(Object);
	Variant object_variant = object;
	memdelete(object);
	CHECK(JSON::stringify(object_variant) == "\"<Freed Object>\"");
}

TEST_CASE("[JSONMarshal] Declining marshaller leaves output unchanged") {
	MarshallerScope marshaller_scope;
	RecordingMarshaller marshaller;
	marshaller.should_handle = false;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	String result = JSON::stringify(object);

	CHECK(marshaller.handled);
	CHECK(result.begins_with("\""));
	CHECK(result.ends_with("\""));

	memdelete(object);
}

TEST_CASE("[JSONMarshal] Marshaled tree is encoded") {
	MarshallerScope marshaller_scope;
	RecordingMarshaller marshaller;
	Dictionary payload;
	payload["level"] = 3;
	payload["name"] = "Captain";
	marshaller.payload = payload;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	String result = JSON::stringify(object);

	CHECK(marshaller.handled);
	CHECK(result == "{\"level\":3,\"name\":\"Captain\"}");

	memdelete(object);
}

TEST_CASE("[JSONMarshal] Marshaled tree respects indent, sort_keys, and full_precision") {
	MarshallerScope marshaller_scope;
	RecordingMarshaller marshaller;
	Dictionary payload;
	payload["b"] = 0.12345678901234568;
	payload["a"] = 1;
	marshaller.payload = payload;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	String result = JSON::stringify(object, "\t", true, true);

	CHECK(result == "{\n\t\"a\": 1,\n\t\"b\": 0.12345678901234568\n}");

	memdelete(object);
}

TEST_CASE("[JSONMarshal] Nested in a container") {
	MarshallerScope marshaller_scope;
	RecordingMarshaller marshaller;
	marshaller.payload = 7;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	Array array;
	array.push_back(object);
	String result = JSON::stringify(array);

	CHECK(result == "[7]");

	memdelete(object);
}

TEST_CASE("[JSONMarshal] Freed object encodes as null") {
	MarshallerScope marshaller_scope;
	RecordingMarshaller marshaller;
	marshaller.payload = 42;
	JSON::set_object_marshaller(&marshaller);

	Object *object = memnew(Object);
	Variant object_variant = object;
	memdelete(object);

	String result = JSON::stringify(object_variant);
	CHECK(result == "null");
	CHECK_FALSE(marshaller.handled);
}

TEST_CASE("[JSONMarshal] Object cycle is reported, not infinite-looped") {
	MarshallerScope marshaller_scope;
	class SelfReturningMarshaller : public EncodeOnlyMarshaller {
	public:
		Object *object = nullptr;

		virtual bool marshal_object(Object *p_object, Variant &r_result) override {
			r_result = Variant(object);
			return true;
		}
	};

	SelfReturningMarshaller marshaller;
	Object *object = memnew(Object);
	marshaller.object = object;
	JSON::set_object_marshaller(&marshaller);

	ERR_PRINT_OFF
	String result = JSON::stringify(object);
	ERR_PRINT_ON

	CHECK(result == "\"{...}\"");

	memdelete(object);
}

TEST_CASE("[JSONMarshal] Object-to-object marshaling chain is bounded, not unbounded") {
	MarshallerScope marshaller_scope;
	class ChainingMarshaller : public EncodeOnlyMarshaller {
	public:
		List<Object *> spawned;

		virtual bool marshal_object(Object *p_object, Variant &r_result) override {
			Object *next = memnew(Object);
			spawned.push_back(next);
			r_result = Variant(next);
			return true;
		}
	};

	ChainingMarshaller marshaller;
	JSON::set_object_marshaller(&marshaller);

	Object *root = memnew(Object);

	ERR_PRINT_OFF
	String result = JSON::stringify(root);
	ERR_PRINT_ON

	CHECK(result.contains("..."));

	memdelete(root);
	for (Object *spawned_object : marshaller.spawned) {
		memdelete(spawned_object);
	}
}

TEST_CASE("[JSONMarshal] parse_to_node fails loudly with no marshaller") {
	MarshallerScope marshaller_scope;
	JSON::set_object_marshaller(nullptr);

	ERR_PRINT_OFF
	const Variant result = JSON::parse_to_node("{}");
	ERR_PRINT_ON

	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[JSONMarshal] parse_to_node lifts the parsed tree and wraps it as a success") {
	MarshallerScope marshaller_scope;
	ParseRecordingMarshaller marshaller;
	JSON::set_object_marshaller(&marshaller);

	const Variant result = JSON::parse_to_node("{\"level\":3}");

	CHECK(marshaller.lifted);
	CHECK_FALSE(marshaller.failed);
	CHECK(marshaller.succeeded);

	// Core hands the marshaller the plain parsed Variant, unchanged.
	REQUIRE(marshaller.lifted_value.get_type() == Variant::DICTIONARY);
	const Dictionary parsed = marshaller.lifted_value;
	CHECK(int64_t(parsed["level"]) == 3);

	// And returns whatever the marshaller built, without inspecting it.
	REQUIRE(result.get_type() == Variant::DICTIONARY);
	const Dictionary wrapped = result;
	CHECK(wrapped.has("value"));
}

TEST_CASE("[JSONMarshal] parse_to_node reports a syntax error with its message and line") {
	MarshallerScope marshaller_scope;
	ParseRecordingMarshaller marshaller;
	JSON::set_object_marshaller(&marshaller);

	const Variant result = JSON::parse_to_node("{\n\"level\": }");

	CHECK(marshaller.failed);
	CHECK_FALSE(marshaller.succeeded);
	CHECK_FALSE(marshaller.lifted);
	CHECK_FALSE(marshaller.failure_message.is_empty());
	CHECK(marshaller.failure_line >= 0);

	REQUIRE(result.get_type() == Variant::DICTIONARY);
	const Dictionary wrapped = result;
	CHECK(String(wrapped["error"]) == marshaller.failure_message);
}

TEST_CASE("[JSONMarshal] parse_to_node reports a failure when the tree cannot be lifted") {
	MarshallerScope marshaller_scope;
	ParseRecordingMarshaller marshaller;
	marshaller.can_lift = false;
	JSON::set_object_marshaller(&marshaller);

	const Variant result = JSON::parse_to_node("[1, 2]");

	CHECK(marshaller.lifted);
	CHECK(marshaller.failed);
	CHECK_FALSE(marshaller.succeeded);
	// A failure that did not come from the parser carries no line.
	CHECK(marshaller.failure_line < 0);
	CHECK(result.get_type() == Variant::DICTIONARY);
}

TEST_CASE("[JSONMarshal] parse_to_node reports when the language cannot build a result") {
	MarshallerScope marshaller_scope;
	ParseRecordingMarshaller marshaller;
	marshaller.can_build_result = false;
	JSON::set_object_marshaller(&marshaller);

	ERR_PRINT_OFF
	const Variant success_result = JSON::parse_to_node("[]");
	const Variant failure_result = JSON::parse_to_node("[");
	ERR_PRINT_ON

	CHECK(success_result.get_type() == Variant::NIL);
	CHECK(failure_result.get_type() == Variant::NIL);
}

} // namespace TestJSONMarshaller
