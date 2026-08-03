/**************************************************************************/
/*  fs_class_handle_callable.cpp                                          */
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

#include "fs_class_handle_callable.h"

#include "foundry_script.h"

#include "core/templates/hashfuncs.h"

Ref<FSSpecializedClassHandle> FSClassHandleCallable::resolve_handle() const {
	const Ref<FoundryScript> script = Object::cast_to<FoundryScript>(ObjectDB::get_instance(script_id));
	if (script.is_null()) {
		return Ref<FSSpecializedClassHandle>();
	}
	return FSSpecializedClassHandle::create(script, type_arguments);
}

bool FSClassHandleCallable::compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
	// Two callables extracted from separately built handles for one specialization mean the same
	// thing, so identity is the specialization rather than the handle object that produced it. The
	// comparison is on the arguments themselves, not on their hashes, so two different specializations
	// whose display names coincide stay distinct.
	const FSClassHandleCallable *a = static_cast<const FSClassHandleCallable *>(p_a);
	const FSClassHandleCallable *b = static_cast<const FSClassHandleCallable *>(p_b);
	if (a->script_id != b->script_id || a->method != b->method ||
			a->type_arguments.size() != b->type_arguments.size()) {
		return false;
	}
	for (int i = 0; i < a->type_arguments.size(); i++) {
		if (!(a->type_arguments[i] == b->type_arguments[i])) {
			return false;
		}
	}
	return true;
}

bool FSClassHandleCallable::compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
	if (compare_equal(p_a, p_b)) {
		return false;
	}
	return p_a->hash() < p_b->hash();
}

uint32_t FSClassHandleCallable::hash() const {
	return h;
}

String FSClassHandleCallable::get_as_text() const {
	const Ref<FSSpecializedClassHandle> handle = resolve_handle();
	const String receiver_name = handle.is_valid() ? handle->get_type_name() : String("<freed class handle>");
	return receiver_name + "::" + String(method);
}

CallableCustom::CompareEqualFunc FSClassHandleCallable::get_compare_equal_func() const {
	return compare_equal;
}

CallableCustom::CompareLessFunc FSClassHandleCallable::get_compare_less_func() const {
	return compare_less;
}

bool FSClassHandleCallable::is_valid() const {
	const Ref<FoundryScript> script = Object::cast_to<FoundryScript>(ObjectDB::get_instance(script_id));
	return script.is_valid() && script->has_static_method(method);
}

ObjectID FSClassHandleCallable::get_object() const {
	return script_id;
}

StringName FSClassHandleCallable::get_method() const {
	return method;
}

int FSClassHandleCallable::get_argument_count(bool &r_is_valid) const {
	const Ref<FoundryScript> script = Object::cast_to<FoundryScript>(ObjectDB::get_instance(script_id));
	if (script.is_null()) {
		r_is_valid = false;
		return 0;
	}
	// The signature belongs to the selected function, which the specialization does not change.
	return script->get_script_method_argument_count(method, &r_is_valid);
}

bool FSClassHandleCallable::is_async() const {
	const Ref<FoundryScript> script = Object::cast_to<FoundryScript>(ObjectDB::get_instance(script_id));
	// The coroutine flag belongs to the selected function too, so it is read off the script.
	return script.is_valid() && Callable(script.ptr(), method).is_async();
}

void FSClassHandleCallable::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value,
		Callable::CallError &r_call_error) const {
	const Ref<FSSpecializedClassHandle> handle = resolve_handle();
	if (handle.is_null()) {
		// No fallback to the unspecialized script: dispatching there would answer with a different
		// specialization instead of reporting that the receiver is gone.
		r_call_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
		r_return_value = Variant();
		return;
	}
	// Dispatched through the handle rather than the script it represents: the handle's own entry point
	// is what delivers the specialization as the frame's static receiver.
	r_return_value = handle->callp(method, p_arguments, p_argcount, r_call_error);
}

FSClassHandleCallable::FSClassHandleCallable(const Ref<FoundryScript> &p_script,
		const Vector<ContainerType> &p_type_arguments, const StringName &p_method) :
		script_id(p_script.is_valid() ? p_script->get_instance_id() : ObjectID()),
		type_arguments(p_type_arguments),
		method(p_method) {
	h = method.hash();
	h = hash_murmur3_one_64(script_id, h);
	for (const ContainerType &type_argument : type_arguments) {
		h = hash_murmur3_one_32(type_argument.get_type_name().hash(), h);
	}
	h = hash_fmix32(h);
}
