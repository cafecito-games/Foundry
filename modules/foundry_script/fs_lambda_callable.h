/**************************************************************************/
/*  fs_lambda_callable.h                                                  */
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

#include "foundry_script.h"

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"

class FSFunction;
class FSInstance;

class FSLambdaCallable : public CallableCustom {
	FoundryScript::UpdatableFuncPtr function;
	Ref<FoundryScript> script;
	uint32_t h;

	Vector<Variant> captures;
	// The exact receiver the creating static frame was invoked through, captured by value the way a
	// closure captures a lexical local. A static lambda is not an instance-self lambda (`use_self` is
	// left false), so this descriptor -- not an `FSInstance` -- is what `Self` resolves against while
	// the callable runs, including after the creating frame has returned. Scripts are held weakly at
	// every depth, matching `FSStaticSelfCallable` and `CallState::static_self`, so a callable stored
	// by the script it describes does not close an uncollectable cycle.
	FSStaticSelfContext static_self_context;

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b);
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b);

public:
	// The callable as this type when it is one, so a caller can reach the compiled function this
	// dispatches to. `CallableCustom` has no type discrimination of its own.
	static const FSLambdaCallable *get_from_callable(const Callable &p_callable);
	FSFunction *get_function() const { return function; }

	bool is_valid() const override;
	uint32_t hash() const override;
	String get_as_text() const override;
	CompareEqualFunc get_compare_equal_func() const override;
	CompareLessFunc get_compare_less_func() const override;
	ObjectID get_object() const override;
	StringName get_method() const override;
	int get_argument_count(bool &r_is_valid) const override;
	bool is_async() const override;
	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override;

	FSLambdaCallable(FSLambdaCallable &) = delete;
	FSLambdaCallable(const FSLambdaCallable &) = delete;
	FSLambdaCallable(Ref<FoundryScript> p_script, FSFunction *p_function, const Vector<Variant> &p_captures, const FSStaticSelfContext *p_static_self = nullptr);
	virtual ~FSLambdaCallable() = default;
};

// Lambda callable that references a particular object, so it can use `self` in the body.
class FSLambdaSelfCallable : public CallableCustom {
	FoundryScript::UpdatableFuncPtr function;
	Ref<RefCounted> reference; // For objects that are RefCounted, keep a reference.
	Object *object = nullptr; // For non RefCounted objects, use a direct pointer.
	uint32_t h;

	Vector<Variant> captures;

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b);
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b);

public:
	// The callable as this type when it is one, so a caller can reach the compiled function this
	// dispatches to. `CallableCustom` has no type discrimination of its own.
	static const FSLambdaSelfCallable *get_from_callable(const Callable &p_callable);
	FSFunction *get_function() const { return function; }

	bool is_valid() const override;
	uint32_t hash() const override;
	String get_as_text() const override;
	CompareEqualFunc get_compare_equal_func() const override;
	CompareLessFunc get_compare_less_func() const override;
	ObjectID get_object() const override;
	StringName get_method() const override;
	int get_argument_count(bool &r_is_valid) const override;
	bool is_async() const override;
	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override;

	FSLambdaSelfCallable(FSLambdaSelfCallable &) = delete;
	FSLambdaSelfCallable(const FSLambdaSelfCallable &) = delete;
	FSLambdaSelfCallable(Ref<RefCounted> p_self, FSFunction *p_function, const Vector<Variant> &p_captures);
	FSLambdaSelfCallable(Object *p_self, FSFunction *p_function, const Vector<Variant> &p_captures);
	virtual ~FSLambdaSelfCallable() = default;
};
