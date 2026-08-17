/**************************************************************************/
/*  fs_static_self_callable.h                                             */
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

#include "fs_function.h"

#include "core/variant/callable.h"

class FoundryScript;

// An extracted static callable that keeps both halves a static call has: the class its lookup starts
// from, which decides *which* implementation runs, and the exact receiver the call was made through,
// which decides what `Self` means while it runs.
//
// A standard `Callable` can express only one object, so it has to conflate the two. Binding the
// declaring class loses the receiver, and binding the receiver changes the selection whenever the
// receiver declares its own function of that name. Neither half is always an object that outlives
// the extraction either: a specialized generic receiver is a transient value built per expression,
// and a retroactive-witness receiver may be a native class or a builtin type.
//
// Nothing here is owned. The lookup target and the receiver script are held by id, exactly as a
// suspended call's receiver descriptor holds its own: a callable stored in a static variable of the
// script it was extracted from would otherwise close a reference cycle that nothing tears down. A
// target or receiver that is gone by dispatch time is reported as an error rather than approximated
// by whichever class is still reachable.
class FSStaticSelfCallable : public CallableCustom {
	ObjectID target_id;
	FSStaticSelfContext receiver;
	StringName method;
	uint32_t h = 0;

	Ref<FoundryScript> resolve_target() const;

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b);
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b);

public:
	// The callable as this type when it is one, so a caller can reach the compiled function this
	// dispatches to. `CallableCustom` has no type discrimination of its own.
	static const FSStaticSelfCallable *get_from_callable(const Callable &p_callable);
	// The function a dispatch would select right now, or null when the target class is gone or does
	// not declare the method. Nothing is kept alive by this: the target is resolved through its id the
	// same way `call()` resolves it.
	const FSFunction *get_target_function() const;

	uint32_t hash() const override;
	String get_as_text() const override;
	CompareEqualFunc get_compare_equal_func() const override;
	CompareLessFunc get_compare_less_func() const override;
	bool is_valid() const override;
	ObjectID get_object() const override;
	StringName get_method() const override;
	int get_argument_count(bool &r_is_valid) const override;
	bool is_async() const override;
	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override;

	FSStaticSelfCallable(const Ref<FoundryScript> &p_target, const FSStaticSelfContext &p_receiver,
			const StringName &p_method);
	virtual ~FSStaticSelfCallable() = default;
};
