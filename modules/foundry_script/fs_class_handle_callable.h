/**************************************************************************/
/*  fs_class_handle_callable.h                                            */
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

#include "core/variant/callable.h"
#include "core/variant/container_type_validate.h"

class FoundryScript;
class FSSpecializedClassHandle;

// A static callable extracted from a specialized generic class handle, e.g. `Crate[int].make`.
//
// A standard `Callable` records its receiver's `ObjectID`, which is enough for a script or a native
// class handle because both outlive any extraction: the script cache owns one, the language globals
// own the other. A specialized handle is a transient value object built per expression and owned by
// nothing else, so a standard callable extracted from one dangles as soon as the extraction scope
// releases it. This callable therefore records the *specialization* -- the represented script plus
// its concrete arguments -- and rebuilds an equivalent handle for each dispatch, so a callable that
// outlives its extraction scope still dispatches through the specialization it was extracted from.
//
// The script is held by id, never by reference, for the same reason a suspended call's receiver
// descriptor is: a callable kept in a static variable of the script it was extracted from would
// otherwise close a reference cycle that nothing tears down. A receiver that is gone by dispatch time
// is reported as an error rather than approximated by the unspecialized script.
class FSClassHandleCallable : public CallableCustom {
	ObjectID script_id;
	Vector<ContainerType> type_arguments;
	StringName method;
	uint32_t h = 0;

	Ref<FSSpecializedClassHandle> resolve_handle() const;

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b);
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b);

public:
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

	FSClassHandleCallable(const Ref<FoundryScript> &p_script, const Vector<ContainerType> &p_type_arguments,
			const StringName &p_method);
	virtual ~FSClassHandleCallable() = default;
};
