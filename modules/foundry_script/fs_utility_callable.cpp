/**************************************************************************/
/*  fs_utility_callable.cpp                                               */
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

#include "fs_utility_callable.h"

const FSUtilityCallable *FSUtilityCallable::get_from_callable(const Callable &p_callable) {
	if (!p_callable.is_custom()) {
		return nullptr;
	}
	const CallableCustom *custom = p_callable.get_custom();
	// The comparison function pointer doubles as the type witness, the established pattern for
	// identifying a CallableCustom subclass without RTTI.
	if (custom == nullptr || custom->get_compare_equal_func() != &FSUtilityCallable::compare_equal) {
		return nullptr;
	}
	return static_cast<const FSUtilityCallable *>(custom);
}

bool FSUtilityCallable::compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) {
	return p_a->hash() == p_b->hash();
}

bool FSUtilityCallable::compare_less(const CallableCustom *p_a, const CallableCustom *p_b) {
	return p_a->hash() < p_b->hash();
}

uint32_t FSUtilityCallable::hash() const {
	return h;
}

String FSUtilityCallable::get_as_text() const {
	String scope;
	switch (type) {
		case TYPE_INVALID:
			scope = "<invalid scope>";
			break;
		case TYPE_GLOBAL:
			scope = "@GlobalScope";
			break;
		case TYPE_FOUNDRY_SCRIPT:
			scope = "@FoundryScript";
			break;
	}
	return vformat("%s::%s", scope, function_name);
}

CallableCustom::CompareEqualFunc FSUtilityCallable::get_compare_equal_func() const {
	return compare_equal;
}

CallableCustom::CompareLessFunc FSUtilityCallable::get_compare_less_func() const {
	return compare_less;
}

bool FSUtilityCallable::is_valid() const {
	return type != TYPE_INVALID;
}

StringName FSUtilityCallable::get_method() const {
	return function_name;
}

ObjectID FSUtilityCallable::get_object() const {
	return ObjectID();
}

int FSUtilityCallable::get_argument_count(bool &r_is_valid) const {
	switch (type) {
		case TYPE_INVALID:
			r_is_valid = false;
			return 0;
		case TYPE_GLOBAL:
			r_is_valid = true;
			return Variant::get_utility_function_argument_count(function_name);
		case TYPE_FOUNDRY_SCRIPT:
			r_is_valid = true;
			return FSUtilityFunctions::get_function_argument_count(function_name);
	}
	ERR_FAIL_V_MSG(0, "Invalid type.");
}

void FSUtilityCallable::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const {
	switch (type) {
		case TYPE_INVALID:
			r_return_value = vformat(R"(Trying to call invalid utility function "%s".)", function_name);
			r_call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
			r_call_error.argument = 0;
			r_call_error.expected = 0;
			break;
		case TYPE_GLOBAL:
			Variant::call_utility_function(function_name, &r_return_value, p_arguments, p_argcount, r_call_error);
			break;
		case TYPE_FOUNDRY_SCRIPT:
			fs_function(&r_return_value, p_arguments, p_argcount, r_call_error);
			break;
	}
}

FSUtilityCallable::FSUtilityCallable(const StringName &p_function_name) {
	function_name = p_function_name;
	if (FSUtilityFunctions::function_exists(p_function_name)) {
		type = TYPE_FOUNDRY_SCRIPT;
		fs_function = FSUtilityFunctions::get_function(p_function_name);
	} else if (Variant::has_utility_function(p_function_name)) {
		type = TYPE_GLOBAL;
	} else {
		ERR_PRINT(vformat(R"(Unknown utility function "%s".)", p_function_name));
	}
	h = p_function_name.hash();
}
