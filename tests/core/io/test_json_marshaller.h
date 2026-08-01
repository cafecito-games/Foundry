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

class RecordingMarshaller : public JSONObjectMarshaller {
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
	class SelfReturningMarshaller : public JSONObjectMarshaller {
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
	class ChainingMarshaller : public JSONObjectMarshaller {
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

} // namespace TestJSONMarshaller
