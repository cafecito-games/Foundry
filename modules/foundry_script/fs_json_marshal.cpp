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
#include "core/object/script_language.h"

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

StringName FSJsonObjectMarshaller::serializable_trait_name() {
	return SNAME("JsonSerializable");
}

// A script instance's `get_class()` is its native base, which says nothing about which script
// produced a bad node, so prefer the script's own identity when there is one.
static String _describe_marshal_source(Object *p_object) {
	const ScriptInstance *instance = p_object->get_script_instance();
	if (instance != nullptr) {
		const Ref<Script> script = instance->get_script();
		if (script.is_valid()) {
			const StringName global_name = script->get_global_name();
			if (global_name != StringName()) {
				return String(global_name);
			}
			const String path = script->get_path();
			if (!path.is_empty()) {
				return path;
			}
		}
	}
	return p_object->get_class();
}

bool FSJsonObjectMarshaller::lower_node(const Variant &p_node, Variant &r_result, int p_depth, const String &p_source_name) {
	// A well-formed node tree is finite, but a hand-built one need not be, so bound the recursion
	// the same way core bounds container nesting.
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		ERR_PRINT(vformat(R"(The JsonNode tree returned by to_json() on "%s" is too deep.)", p_source_name));
		return false;
	}

	if (p_node.get_type() != Variant::ARRAY) {
		ERR_PRINT(vformat(R"(to_json() on "%s" did not return a JsonNode.)", p_source_name));
		return false;
	}

	const Array node = p_node;
	if (node.is_empty() || node[0].get_type() != Variant::INT) {
		ERR_PRINT(vformat(R"(to_json() on "%s" returned a malformed JsonNode.)", p_source_name));
		return false;
	}

	// Every case checks its own payload arity: a mismatch means the value did not come from the
	// builtin declaration, which must be reported rather than trusted.
	const int64_t tag = node[0];
	switch (tag) {
		case TAG_NULL: {
			if (node.size() != 1) {
				break;
			}
			r_result = Variant();
			return true;
		}
		case TAG_BOOL: {
			if (node.size() != 2 || node[1].get_type() != Variant::BOOL) {
				break;
			}
			r_result = bool(node[1]);
			return true;
		}
		case TAG_INT: {
			if (node.size() != 2 || node[1].get_type() != Variant::INT) {
				break;
			}
			r_result = int64_t(node[1]);
			return true;
		}
		case TAG_FLOAT: {
			// An `int` reaching a `float` payload slot is still a Float node; coercing here is what
			// keeps `Float` and `Int` distinguishable in the output.
			const Variant::Type payload_type = node.size() == 2 ? node[1].get_type() : Variant::NIL;
			if (payload_type != Variant::FLOAT && payload_type != Variant::INT) {
				break;
			}
			r_result = double(node[1]);
			return true;
		}
		case TAG_STR: {
			const Variant::Type payload_type = node.size() == 2 ? node[1].get_type() : Variant::NIL;
			if (payload_type != Variant::STRING && payload_type != Variant::STRING_NAME) {
				break;
			}
			r_result = String(node[1]);
			return true;
		}
		case TAG_ARRAY: {
			if (node.size() != 2 || node[1].get_type() != Variant::ARRAY) {
				break;
			}
			const Array items = node[1];
			Array lowered;
			lowered.resize(items.size());
			for (int i = 0; i < items.size(); i++) {
				Variant lowered_item;
				if (!lower_node(items[i], lowered_item, p_depth + 1, p_source_name)) {
					return false;
				}
				lowered[i] = lowered_item;
			}
			r_result = lowered;
			return true;
		}
		case TAG_OBJECT: {
			if (node.size() != 2 || node[1].get_type() != Variant::DICTIONARY) {
				break;
			}
			const Dictionary entries = node[1];
			Dictionary lowered;
			for (const Variant &key : entries.get_key_list()) {
				Variant lowered_value;
				if (!lower_node(entries[key], lowered_value, p_depth + 1, p_source_name)) {
					return false;
				}
				lowered[String(key)] = lowered_value;
			}
			r_result = lowered;
			return true;
		}
		default:
			break;
	}

	ERR_PRINT(vformat(R"(to_json() on "%s" returned a JsonNode with an unknown tag or a mismatched payload (tag %d).)",
			p_source_name, tag));
	return false;
}

bool FSJsonObjectMarshaller::marshal_object(Object *p_object, Variant &r_result) {
	ERR_FAIL_NULL_V(p_object, false);

	const ScriptInstance *instance = p_object->get_script_instance();
	if (instance == nullptr) {
		return false;
	}
	const Ref<Script> script = instance->get_script();
	if (script.is_null() || !script->has_script_trait(serializable_trait_name())) {
		return false;
	}

	// From here the object is known to opt into custom marshaling, so a failure is reported and
	// encoded as `null` rather than declined: falling back to the quoted `to_string` would hide a
	// broken `to_json()` behind output that looks deliberate.
	Variant node;
	if (!FSJsonMarshal::call_to_json(p_object, node)) {
		r_result = Variant();
		return true;
	}
	if (!lower_node(node, r_result, 0, _describe_marshal_source(p_object))) {
		r_result = Variant();
	}
	return true;
}
