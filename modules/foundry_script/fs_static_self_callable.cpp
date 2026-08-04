/**************************************************************************/
/*  fs_static_self_callable.cpp                                           */
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

#include "fs_static_self_callable.h"

#include "foundry_script.h"

#include "core/templates/hashfuncs.h"

Ref<FoundryScript> FSStaticSelfCallable::resolve_target() const {
	return Ref<FoundryScript>(Object::cast_to<FoundryScript>(ObjectDB::get_instance(target_id)));
}

// A receiver that names a script but can no longer produce it was freed after the extraction, which
// is a broken dispatch path rather than a reason to run against another class.
static bool _static_self_callable_receiver_is_live(const FSStaticSelfContext &p_receiver) {
	if (!p_receiver.is_valid()) {
		return false;
	}
	return p_receiver.get_kind() != FSStaticSelfContext::SCRIPT || p_receiver.get_script().is_valid();
}

bool FSStaticSelfCallable::compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
	// Two callables extracted separately for one target, receiver, and method mean the same thing.
	// Compared field by field rather than by hash, so two receivers whose display names coincide stay
	// distinct.
	const FSStaticSelfCallable *a = static_cast<const FSStaticSelfCallable *>(p_a);
	const FSStaticSelfCallable *b = static_cast<const FSStaticSelfCallable *>(p_b);
	return a->target_id == b->target_id && a->method == b->method && a->receiver == b->receiver;
}

bool FSStaticSelfCallable::compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
	if (compare_equal(p_a, p_b)) {
		return false;
	}
	return p_a->hash() < p_b->hash();
}

uint32_t FSStaticSelfCallable::hash() const {
	return h;
}

String FSStaticSelfCallable::get_as_text() const {
	return receiver.get_type_name() + "::" + String(method);
}

CallableCustom::CompareEqualFunc FSStaticSelfCallable::get_compare_equal_func() const {
	return compare_equal;
}

CallableCustom::CompareLessFunc FSStaticSelfCallable::get_compare_less_func() const {
	return compare_less;
}

bool FSStaticSelfCallable::is_valid() const {
	const Ref<FoundryScript> target = resolve_target();
	return target.is_valid() && target->has_static_method(method) && _static_self_callable_receiver_is_live(receiver);
}

ObjectID FSStaticSelfCallable::get_object() const {
	// The receiver is what the callable was extracted from, so it is what an observer asking for the
	// callable's object means. A receiver with no object of its own -- a native class or a builtin type
	// -- leaves the lookup target as the only object involved.
	if (receiver.get_kind() == FSStaticSelfContext::SCRIPT) {
		const Ref<Script> receiver_script = receiver.get_script();
		if (receiver_script.is_valid()) {
			return receiver_script->get_instance_id();
		}
	}
	return target_id;
}

StringName FSStaticSelfCallable::get_method() const {
	return method;
}

int FSStaticSelfCallable::get_argument_count(bool &r_is_valid) const {
	const Ref<FoundryScript> target = resolve_target();
	if (target.is_null()) {
		r_is_valid = false;
		return 0;
	}
	// The signature belongs to the selected function, which the receiver does not change.
	return target->get_script_method_argument_count(method, &r_is_valid);
}

bool FSStaticSelfCallable::is_async() const {
	const Ref<FoundryScript> target = resolve_target();
	// The coroutine flag belongs to the selected function too, so it is read off the lookup target.
	return target.is_valid() && Callable(target.ptr(), method).is_async();
}

void FSStaticSelfCallable::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value,
		Callable::CallError &r_call_error) const {
	const Ref<FoundryScript> target = resolve_target();
	if (target.is_null() || !_static_self_callable_receiver_is_live(receiver)) {
		r_call_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
		r_return_value = Variant();
		return;
	}
	// Lookup starts at the target and the receiver travels with it, which is what a direct call through
	// the same reference does.
	r_return_value = target->call_static_with_context(method, p_arguments, p_argcount, r_call_error, receiver);
}

FSStaticSelfCallable::FSStaticSelfCallable(const Ref<FoundryScript> &p_target, const FSStaticSelfContext &p_receiver,
		const StringName &p_method) :
		target_id(p_target.is_valid() ? p_target->get_instance_id() : ObjectID()),
		receiver(p_receiver),
		method(p_method) {
	h = method.hash();
	h = hash_murmur3_one_64(target_id, h);
	h = hash_murmur3_one_32(receiver.get_type_name().hash(), h);
	h = hash_fmix32(h);
}
