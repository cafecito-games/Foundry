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

#include "foundry_script.h"
#include "fs_cache.h"
#include "fs_conformance_registry.h"

#include "core/object/object.h"
#include "core/object/script_language.h"

namespace {

constexpr const char *JSON_NODE_BUILTIN_PATH = "foundry://builtin/json_node.fs";
constexpr const char *JSON_RESULT_BUILTIN_PATH = "foundry://builtin/json_result.fs";

Ref<FoundryScript> load_builtin_script(const char *p_builtin_path) {
	Error error = OK;
	const Ref<FoundryScript> script = FSCache::get_full_script(p_builtin_path, error);
	if (error != OK || script.is_null() || !script->is_valid()) {
		ERR_PRINT(vformat(R"(The builtin script "%s" is not available.)", p_builtin_path));
		return Ref<FoundryScript>();
	}
	return script;
}

// The builtin types are declared in Foundry Script, so native code reaches their static functions
// the same way it reaches an instance hook: by name, through `Object::callp()`. Keeping every such
// call in one place is what stops the shape of a builtin type from being re-implemented natively.
bool call_builtin_static(const char *p_builtin_path, const StringName &p_method,
		const Variant **p_arguments, int p_argument_count, Variant &r_result) {
	const Ref<FoundryScript> script = load_builtin_script(p_builtin_path);
	if (script.is_null()) {
		return false;
	}

	// `FoundryScript::callp` narrows `Object::callp` to protected, so dispatch through the base.
	Object *script_object = script.ptr();
	Callable::CallError call_error;
	const Variant value = script_object->callp(p_method, p_arguments, p_argument_count, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Calling %s() on the builtin script "%s" failed: %s)", String(p_method), p_builtin_path,
				Variant::get_call_error_text(script_object, p_method, p_arguments, p_argument_count, call_error)));
		return false;
	}

	r_result = value;
	return true;
}

// An enum's own functions are not class members — they are compiled into the declaring script's
// enum function table and dispatched by enum type — so they are unreachable through
// `Object::callp()` and are resolved the same way the interpreter resolves an enum call.
bool call_builtin_enum_static(const char *p_builtin_path, const StringName &p_enum_type,
		const StringName &p_method, const Variant **p_arguments, int p_argument_count, Variant &r_result) {
	const Ref<FoundryScript> script = load_builtin_script(p_builtin_path);
	if (script.is_null()) {
		return false;
	}

	FSFunction *enum_function = script->get_enum_function(p_enum_type, p_method, true);
	if (enum_function == nullptr) {
		ERR_PRINT(vformat(R"(The builtin script "%s" declares no static %s.%s().)", p_builtin_path,
				String(p_enum_type), String(p_method)));
		return false;
	}

	Callable::CallError call_error;
	const Variant value = enum_function->call(nullptr, p_arguments, p_argument_count, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Calling %s.%s() on the builtin script "%s" failed: %s)", String(p_enum_type),
				String(p_method), p_builtin_path,
				Variant::get_call_error_text(p_method, p_arguments, p_argument_count, call_error)));
		return false;
	}

	r_result = value;
	return true;
}

// The compiled witness that a retroactive conformance (`extend <EngineClass> uses JsonSerializable`)
// supplies for the instance hook, or `nullptr` when the object's engine class and its ancestors
// declare none. A static witness is rejected: the hook is an instance method, and dispatching a
// static one would drop the receiver.
FSFunction *find_native_to_json_witness(Object *p_object) {
	FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(
			p_object->get_class_name(), FSJsonMarshal::to_json_method_name());
	if (witness == nullptr || witness->is_static()) {
		return nullptr;
	}
	return witness;
}

} // namespace

StringName FSJsonMarshal::to_json_method_name() {
	return SNAME("to_json");
}

bool FSJsonMarshal::has_to_json(Object *p_object) {
	if (p_object == nullptr) {
		return false;
	}
	return p_object->has_method(to_json_method_name()) || find_native_to_json_witness(p_object) != nullptr;
}

bool FSJsonMarshal::call_to_json(Object *p_object, Variant &r_node) {
	ERR_FAIL_NULL_V(p_object, false);

	Callable::CallError call_error;
	Variant node = p_object->callp(to_json_method_name(), nullptr, 0, call_error);

	// A retroactive conformance declared on an engine class supplies the hook from outside the
	// object's own definition, so nothing is installed on the instance and the call above misses.
	// Resolve the compiled witness the way the interpreter does for a native receiver and dispatch
	// it with the object bound as `self`. Only a miss reaches here, so an implementation the object
	// does carry keeps precedence.
	if (call_error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
		FSFunction *witness = find_native_to_json_witness(p_object);
		if (witness != nullptr) {
			call_error = Callable::CallError();
			const Variant self = p_object;
			node = witness->call_witness(self, nullptr, 0, call_error);
		}
	}

	if (call_error.error != Callable::CallError::CALL_OK) {
		ERR_PRINT(vformat(R"(Calling to_json() on an instance of "%s" failed.)", p_object->get_class()));
		return false;
	}

	r_node = node;
	return true;
}

bool FSJsonMarshal::make_json_node(const Variant &p_value, Variant &r_node) {
	Variant prepared;
	if (!FSJsonObjectMarshaller::prepare_parsed_value(p_value, 0, prepared)) {
		return false;
	}

	const Variant *arguments[1] = { &prepared };
	return call_builtin_enum_static(JSON_NODE_BUILTIN_PATH, SNAME("JsonNode"), SNAME("of"), arguments, 1, r_node);
}

bool FSJsonMarshal::make_result_ok(const Variant &p_value, Variant &r_result) {
	const Variant *arguments[1] = { &p_value };
	return call_builtin_static(JSON_RESULT_BUILTIN_PATH, SNAME("ok"), arguments, 1, r_result);
}

bool FSJsonMarshal::make_result_failure(const String &p_message, const String &p_path, Variant &r_result) {
	const Variant message = p_message;
	const Variant path = p_path;
	const Variant *arguments[2] = { &message, &path };
	return call_builtin_static(JSON_RESULT_BUILTIN_PATH, SNAME("fail"), arguments, 2, r_result);
}

StringName FSJsonObjectMarshaller::serializable_trait_name() {
	return SNAME("JsonSerializable");
}

bool FSJsonObjectMarshaller::conforms_to_serializable(Object *p_object) {
	ERR_FAIL_NULL_V(p_object, false);

	const ScriptInstance *instance = p_object->get_script_instance();
	if (instance != nullptr) {
		const Ref<Script> script = instance->get_script();
		if (script.is_valid() && script->has_script_trait(serializable_trait_name())) {
			return true;
		}
	}

	// `Script::has_script_trait` only reaches a script's own identities, so a conformance declared
	// retroactively on an engine class is invisible there — including for a scripted object whose
	// native base carries it. The registry answers on the engine class and walks its ancestors, the
	// same reach the type system uses when it accepts such a value as the trait.
	return FSConformanceRegistry::get_singleton()->native_class_conforms(
			p_object->get_class_name(), serializable_trait_name(), true);
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
				// The payload is declared `Dictionary[String, JsonNode]`. Coercing a key of another
				// type would let two distinct keys collapse into one member and silently drop data,
				// so a key that is not a string fails the whole node instead.
				const Variant::Type key_type = key.get_type();
				if (key_type != Variant::STRING && key_type != Variant::STRING_NAME) {
					ERR_PRINT(vformat(R"(to_json() on "%s" returned a JsonNode object with a non-string key.)", p_source_name));
					return false;
				}
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

	if (!conforms_to_serializable(p_object)) {
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

bool FSJsonObjectMarshaller::prepare_parsed_value(const Variant &p_value, int p_depth, Variant &r_prepared) {
	// Bound the recursion the same way lowering does, so a tree core would refuse to encode is
	// also refused on the way in rather than overflowing the script stack inside `JsonNode.of()`.
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return false;
	}

	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::STRING: {
			r_prepared = p_value;
			return true;
		}
		case Variant::FLOAT: {
			const double number = p_value;
			// Only a value an `int64_t` round-trips exactly becomes an `Int` node; anything larger
			// keeps its float form rather than being silently truncated.
			const bool is_whole = number == Math::floor(number) &&
					number >= -9223372036854775808.0 && number < 9223372036854775808.0 &&
					number == double(int64_t(number));
			r_prepared = is_whole ? Variant(int64_t(number)) : p_value;
			return true;
		}
		case Variant::ARRAY: {
			const Array items = p_value;
			Array prepared_items;
			prepared_items.resize(items.size());
			for (int i = 0; i < items.size(); i++) {
				Variant prepared_item;
				if (!prepare_parsed_value(items[i], p_depth + 1, prepared_item)) {
					return false;
				}
				prepared_items[i] = prepared_item;
			}
			r_prepared = prepared_items;
			return true;
		}
		case Variant::DICTIONARY: {
			const Dictionary entries = p_value;
			Dictionary prepared_entries;
			for (const Variant &key : entries.get_key_list()) {
				// A `JsonNode.Object` is keyed by `String`. Coercing another key type would let two
				// distinct keys collapse into one member, so the whole tree is refused instead.
				if (key.get_type() != Variant::STRING) {
					return false;
				}
				Variant prepared_value;
				if (!prepare_parsed_value(entries[key], p_depth + 1, prepared_value)) {
					return false;
				}
				prepared_entries[key] = prepared_value;
			}
			r_prepared = prepared_entries;
			return true;
		}
		default:
			return false;
	}
}

bool FSJsonObjectMarshaller::lift_variant(const Variant &p_parsed, Variant &r_node) {
	return FSJsonMarshal::make_json_node(p_parsed, r_node);
}

bool FSJsonObjectMarshaller::make_parse_failure(const String &p_message, int p_line, Variant &r_result) {
	// `JsonDecodeError` carries a message and a document path, not a line, so a parse failure
	// folds its line into the message and reports the document root as its path: a text that did
	// not parse has no position inside a document that does not exist.
	const String message = p_line >= 0 ? vformat("%s (line %d)", p_message, p_line) : p_message;
	return FSJsonMarshal::make_result_failure(message, "$", r_result);
}

bool FSJsonObjectMarshaller::make_parse_success(const Variant &p_node, Variant &r_result) {
	return FSJsonMarshal::make_result_ok(p_node, r_result);
}
