/**************************************************************************/
/*  test_container_type_class_handle.h                                    */
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

#include "core/object/class_db.h"
#include "core/object/class_handle.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"

namespace TestContainerTypeClassHandle {

// A minimal language-agnostic class handle: the engine only needs a value that reports which class it
// denotes, which is exactly what the abstraction promises.
class TestNativeClassHandle : public ClassHandle {
	FOUNDRY_CLASS(TestNativeClassHandle, ClassHandle);

	StringName represented_class;

public:
	virtual StringName get_represented_native_class() const override { return represented_class; }
	void set_represented_native_class(const StringName &p_class) { represented_class = p_class; }
};

// Captures the last engine error message so the rejection reasons can be asserted on directly.
struct ClassHandleErrorRecorder {
	ClassHandleErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~ClassHandleErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		ClassHandleErrorRecorder *self = static_cast<ClassHandleErrorRecorder *>(p_self);
		self->last_message = String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error);
	}

	ErrorHandlerList handler;
	String last_message;
};

static Ref<TestNativeClassHandle> make_handle(const StringName &p_class) {
	Ref<TestNativeClassHandle> handle;
	handle.instantiate();
	handle->set_represented_native_class(p_class);
	return handle;
}

static ContainerType make_handle_type(const StringName &p_class) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class;
	type.is_type_handle = true;
	return type;
}

static ContainerType make_instance_type(const StringName &p_class) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class;
	return type;
}

static ContainerType make_builtin_type(Variant::Type p_type) {
	ContainerType type;
	type.builtin_type = p_type;
	return type;
}

static ContainerType make_array_of(const ContainerType &p_element) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element);
	return type;
}

static ContainerType make_dictionary_of(const ContainerType &p_key, const ContainerType &p_value) {
	ContainerType type;
	type.builtin_type = Variant::DICTIONARY;
	type.element_types.push_back(p_key);
	type.element_types.push_back(p_value);
	return type;
}

TEST_CASE("[ContainerType] Class handle descriptors are distinct from instance descriptors") {
	const ContainerType handle_type = make_handle_type(SNAME("RefCounted"));
	const ContainerType instance_type = make_instance_type(SNAME("RefCounted"));

	CHECK(handle_type != instance_type);
	CHECK(handle_type == make_handle_type(SNAME("RefCounted")));

	const ContainerTypeValidate handle_validate(handle_type);
	const ContainerTypeValidate instance_validate(instance_type);
	CHECK(handle_validate != instance_validate);

	// Neither container may be referenced as the other: their value sets are disjoint.
	CHECK_FALSE(handle_validate.can_reference(instance_validate));
	CHECK_FALSE(instance_validate.can_reference(handle_validate));

	// A handle container still references a container of handles for a subtype.
	const ContainerTypeValidate derived_handle_validate(make_handle_type(SNAME("Resource")));
	CHECK(handle_validate.can_reference(derived_handle_validate));
}

TEST_CASE("[ContainerType] Class handle descriptors render Type[T] at every nesting depth") {
	CHECK(make_handle_type(SNAME("RefCounted")).get_type_name() == "Type[RefCounted]");
	CHECK(make_instance_type(SNAME("RefCounted")).get_type_name() == "RefCounted");

	CHECK(make_array_of(make_handle_type(SNAME("RefCounted"))).get_type_name() == "Array[Type[RefCounted]]");

	const ContainerType dictionary_type = make_dictionary_of(make_builtin_type(Variant::STRING), make_handle_type(SNAME("RefCounted")));
	CHECK(dictionary_type.get_type_name() == "Dictionary[String, Type[RefCounted]]");
	CHECK(make_array_of(dictionary_type).get_type_name() == "Array[Dictionary[String, Type[RefCounted]]]");

	// A specialized handle renders the arguments inside the handle wrapper, not beside it.
	ContainerType specialized = make_handle_type(SNAME("RefCounted"));
	specialized.type_arguments.push_back(make_builtin_type(Variant::INT));
	CHECK(specialized.get_type_name() == "Type[RefCounted[int]]");
}

TEST_CASE("[ContainerType] Class handle flag round-trips through ContainerTypeValidate") {
	const ContainerType nested = make_array_of(make_dictionary_of(make_builtin_type(Variant::STRING), make_handle_type(SNAME("RefCounted"))));

	const ContainerTypeValidate validate(nested);
	REQUIRE(validate.element_types.size() == 1);
	REQUIRE(validate.element_types[0].element_types.size() == 2);
	CHECK_FALSE(validate.is_type_handle);
	CHECK_FALSE(validate.element_types[0].element_types[0].is_type_handle);
	CHECK(validate.element_types[0].element_types[1].is_type_handle);

	CHECK(validate.get_container_type() == nested);
	CHECK(validate.get_type_name() == "Array[Dictionary[String, Type[RefCounted]]]");
}

TEST_CASE("[ContainerType] Descriptor decoding rejects class handles on non-object types") {
	Dictionary descriptor;
	descriptor["type"] = Variant::INT;
	descriptor["is_type_handle"] = true;

	ContainerType decoded;
	String error;
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
	CHECK(error == R"(Container type descriptor "is_type_handle" is only valid for Object types.)");

	descriptor["is_type_handle"] = 1;
	error = String();
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
	CHECK(error == R"(Container type descriptor "is_type_handle" must be a bool.)");

	// The same flag on an Object descriptor decodes normally.
	Dictionary object_descriptor;
	object_descriptor["type"] = Variant::OBJECT;
	object_descriptor["class_name"] = "RefCounted";
	object_descriptor["is_type_handle"] = true;
	error = String();
	REQUIRE(ContainerTypeDescriptor::from_variant(object_descriptor, decoded, &error));
	CHECK(decoded.is_type_handle);
	CHECK(decoded == make_handle_type(SNAME("RefCounted")));

	// Omitting the flag decodes as an instance descriptor.
	object_descriptor.erase("is_type_handle");
	error = String();
	REQUIRE(ContainerTypeDescriptor::from_variant(object_descriptor, decoded, &error));
	CHECK_FALSE(decoded.is_type_handle);
}

TEST_CASE("[ContainerType] ClassHandle is registered with ClassDB") {
	CHECK(ClassDB::class_exists(SNAME("ClassHandle")));
	CHECK(ClassDB::is_parent_class(SNAME("ClassHandle"), SNAME("RefCounted")));
}

TEST_CASE("[ContainerType] A handle-typed Array accepts only compatible class handles") {
	Array array;
	array.set_typed(make_handle_type(SNAME("RefCounted")));

	// A handle for the exact class and for a subclass are both accepted.
	array.push_back(make_handle(SNAME("RefCounted")));
	array.push_back(make_handle(SNAME("Resource")));
	CHECK(array.size() == 2);

	// Null is permissive, matching the instance-typed object slot.
	array.push_back(Variant());
	CHECK(array.size() == 3);

	ERR_PRINT_OFF;

	// A handle for an unrelated (here: broader) class is rejected.
	array.push_back(make_handle(SNAME("Object")));
	CHECK(array.size() == 3);

	// An instance of the represented class is not a handle for it.
	Ref<RefCounted> instance;
	instance.instantiate();
	array.push_back(instance);
	CHECK(array.size() == 3);

	// A non-object value cannot denote a class.
	array.push_back(42);
	CHECK(array.size() == 3);

	// A previously freed object carries no class evidence. A plain `Object` is used because a `Variant`
	// keeps a strong reference to a `RefCounted`, so a reference-counted handle cannot be observed freed.
	Object *freed_object = memnew(Object);
	const Variant freed_handle = freed_object;
	memdelete(freed_object);
	array.push_back(freed_handle);
	CHECK(array.size() == 3);

	ERR_PRINT_ON;
}

TEST_CASE("[ContainerType] A handle-typed Dictionary key enforces the same rule as a value") {
	Dictionary dictionary;
	dictionary.set_typed(make_handle_type(SNAME("RefCounted")), make_builtin_type(Variant::INT));

	CHECK(dictionary.set(make_handle(SNAME("RefCounted")), 1));
	CHECK(dictionary.set(make_handle(SNAME("Resource")), 2));
	CHECK(dictionary.size() == 2);

	// Null is permissive in the key position too.
	CHECK(dictionary.set(Variant(), 3));
	CHECK(dictionary.size() == 3);

	ERR_PRINT_OFF;

	CHECK_FALSE(dictionary.set(make_handle(SNAME("Object")), 4));

	Ref<RefCounted> instance;
	instance.instantiate();
	CHECK_FALSE(dictionary.set(instance, 5));

	CHECK_FALSE(dictionary.set(42, 6));

	Object *freed_object = memnew(Object);
	const Variant freed_handle = freed_object;
	memdelete(freed_object);
	CHECK_FALSE(dictionary.set(freed_handle, 7));

	ERR_PRINT_ON;

	CHECK(dictionary.size() == 3);
}

TEST_CASE("[ContainerType] Class handle rejections name the actual reason") {
	const ContainerTypeValidate validate(make_handle_type(SNAME("Resource")));

	ClassHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	// The represented type is incompatible; the message must describe the represented type rather than
	// claim the handle object itself failed to inherit.
	Variant incompatible = make_handle(SNAME("RefCounted"));
	CHECK_FALSE(validate.validate(incompatible, "use"));
	CHECK(recorder.last_message.contains("represented type is not compatible"));
	CHECK_FALSE(recorder.last_message.contains("does not inherit"));

	// The value is an object, but not a class handle at all.
	Ref<RefCounted> instance;
	instance.instantiate();
	Variant not_a_handle = instance;
	CHECK_FALSE(validate.validate(not_a_handle, "use"));
	CHECK(recorder.last_message.contains("requires a class handle"));
	CHECK_FALSE(recorder.last_message.contains("does not inherit"));

	// The candidate handle was freed before the write.
	Object *freed_object = memnew(Object);
	Variant freed_handle = freed_object;
	memdelete(freed_object);
	CHECK_FALSE(validate.validate(freed_handle, "use"));
	CHECK(recorder.last_message.contains("previously freed"));

	ERR_PRINT_ON;
}

} // namespace TestContainerTypeClassHandle
