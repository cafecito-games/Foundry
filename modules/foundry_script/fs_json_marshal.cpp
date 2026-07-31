/**************************************************************************/
/*  fs_json_marshal.cpp                                                   */
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

#include "fs_json_marshal.h"

#include "core/object/object.h"

StringName FSJsonMarshal::to_json_method_name() {
	return SNAME("to_json");
}

bool FSJsonMarshal::has_to_json(Object *p_object) {
	if (p_object == nullptr) {
		return false;
	}
	return p_object->has_method(to_json_method_name());
}

bool FSJsonMarshal::call_to_json(Object *p_object, Variant &r_node) {
	ERR_FAIL_NULL_V(p_object, false);

	Callable::CallError call_error;
	const Variant node = p_object->callp(to_json_method_name(), nullptr, 0, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Calling to_json() on an instance of "%s" failed.)", p_object->get_class()));
		return false;
	}

	r_node = node;
	return true;
}
