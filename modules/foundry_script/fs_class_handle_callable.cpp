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

// Two callables extracted from separately built handles for the same specialization mean the same
// thing, so identity is the specialization the handle denotes rather than the handle object.
static uint32_t _class_handle_callable_hash(const Ref<ClassHandle> &p_handle, const StringName &p_method) {
	uint32_t hash = p_method.hash();
	if (p_handle.is_null()) {
		return hash_fmix32(hash);
	}
	const Ref<Script> script = p_handle->get_represented_script();
	hash = hash_murmur3_one_64(uint64_t(uintptr_t(script.ptr())), hash);
	hash = hash_murmur3_one_32(p_handle->get_represented_native_class().hash(), hash);
	Vector<ContainerType> type_arguments;
	p_handle->get_represented_type_arguments(type_arguments);
	for (const ContainerType &type_argument : type_arguments) {
		hash = hash_murmur3_one_32(type_argument.get_type_name().hash(), hash);
	}
	return hash_fmix32(hash);
}

bool FSClassHandleCallable::compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
	return p_a->hash() == p_b->hash();
}

bool FSClassHandleCallable::compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
	return p_a->hash() < p_b->hash();
}

uint32_t FSClassHandleCallable::hash() const {
	return h;
}

String FSClassHandleCallable::get_as_text() const {
	const Ref<FSSpecializedClassHandle> specialized = handle;
	const String receiver_name = specialized.is_valid()
			? specialized->get_type_name()
			: FoundryScript::debug_get_script_name(handle.is_valid() ? handle->get_represented_script() : Ref<Script>());
	return receiver_name + "::" + String(method);
}

CallableCustom::CompareEqualFunc FSClassHandleCallable::get_compare_equal_func() const {
	return compare_equal;
}

CallableCustom::CompareLessFunc FSClassHandleCallable::get_compare_less_func() const {
	return compare_less;
}

bool FSClassHandleCallable::is_valid() const {
	return handle.is_valid();
}

ObjectID FSClassHandleCallable::get_object() const {
	return handle.is_valid() ? handle->get_instance_id() : ObjectID();
}

StringName FSClassHandleCallable::get_method() const {
	return method;
}

int FSClassHandleCallable::get_argument_count(bool &r_is_valid) const {
	const Ref<Script> script = handle.is_valid() ? handle->get_represented_script() : Ref<Script>();
	if (script.is_null()) {
		r_is_valid = false;
		return 0;
	}
	return Callable(script.ptr(), method).get_argument_count(&r_is_valid);
}

bool FSClassHandleCallable::is_async() const {
	const Ref<Script> script = handle.is_valid() ? handle->get_represented_script() : Ref<Script>();
	// The coroutine flag belongs to the selected function, which the specialization does not change,
	// so it is read through the represented script.
	return script.is_valid() && Callable(script.ptr(), method).is_async();
}

void FSClassHandleCallable::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value,
		Callable::CallError &r_call_error) const {
	if (handle.is_null()) {
		r_call_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
		r_return_value = Variant();
		return;
	}
	// Dispatched through the handle, not the script it represents: the handle's own entry point is what
	// delivers the specialization as the frame's static receiver.
	r_return_value = handle->callp(method, p_arguments, p_argcount, r_call_error);
}

FSClassHandleCallable::FSClassHandleCallable(const Ref<ClassHandle> &p_handle, const StringName &p_method) :
		handle(p_handle), method(p_method), h(_class_handle_callable_hash(p_handle, p_method)) {
}
