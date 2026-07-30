/**************************************************************************/
/*  container_type_validate.cpp                                           */
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

#include "container_type_validate.h"

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant_internal.h"

// Compares a value's projected type arguments against an expected specialization. A bound projected
// argument that differs from the expected one is a definite invariance violation; an unbound argument
// (an unspecialized-leaf open parameter, or a dependent/erased fixed argument) carries no evidence and
// is left to gradual acceptance. `p_argument_bound` is parallel to `p_projected`; `p_expected` is the
// expected element specialization (always fully bound here).
static bool _projected_type_arguments_conflict(const Vector<ContainerType> &p_expected, const Vector<ContainerType> &p_projected, const Vector<bool> &p_argument_bound) {
	for (int i = 0; i < p_projected.size() && i < p_expected.size(); i++) {
		if (!p_argument_bound[i]) {
			continue;
		}
		if (p_projected[i] != p_expected[i]) {
			return true;
		}
	}
	return false;
}

bool ContainerType::operator==(const ContainerType &p_type) const {
	return builtin_type == p_type.builtin_type &&
			class_name == p_type.class_name &&
			script == p_type.script &&
			element_types == p_type.element_types &&
			type_arguments == p_type.type_arguments;
}

bool ContainerType::operator!=(const ContainerType &p_type) const {
	return !(*this == p_type);
}

String ContainerType::get_type_name() const {
	ContainerTypeValidate validate(*this);
	String name = validate.get_type_name();
	if (!type_arguments.is_empty()) {
		String arguments;
		for (int i = 0; i < type_arguments.size(); i++) {
			if (i > 0) {
				arguments += ", ";
			}
			arguments += type_arguments[i].get_type_name();
		}
		name += "[" + arguments + "]";
	}
	return name;
}

ContainerTypeValidate::ContainerTypeValidate(const ContainerType &p_type) {
	type = p_type.builtin_type;
	class_name = p_type.class_name;
	script = p_type.script;
	for (const ContainerType &element_type : p_type.element_types) {
		element_types.push_back(ContainerTypeValidate(element_type));
	}
	type_arguments = p_type.type_arguments;
}

ContainerType ContainerTypeValidate::get_container_type() const {
	ContainerType result;
	result.builtin_type = type;
	result.class_name = class_name;
	result.script = script;
	for (const ContainerTypeValidate &element_type : element_types) {
		result.element_types.push_back(element_type.get_container_type());
	}
	result.type_arguments = type_arguments;
	return result;
}

String ContainerTypeValidate::get_type_name() const {
	if (type == Variant::NIL) {
		return "Variant";
	}
	if (type == Variant::ARRAY && !element_types.is_empty()) {
		return vformat("Array[%s]", element_types[0].get_type_name());
	}
	if (type == Variant::DICTIONARY && !element_types.is_empty()) {
		const String key = element_types.size() > 0 ? element_types[0].get_type_name() : String("Variant");
		const String value = element_types.size() > 1 ? element_types[1].get_type_name() : String("Variant");
		return vformat("Dictionary[%s, %s]", key, value);
	}
	if (type == Variant::OBJECT) {
		if (script.is_valid() && script->get_global_name() != StringName()) {
			return script->get_global_name();
		}
		if (class_name != StringName()) {
			return class_name;
		}
	}
	return Variant::get_type_name(type);
}

Variant ContainerTypeValidate::make_default() const {
	if (type == Variant::ARRAY) {
		Array array;
		if (!element_types.is_empty()) {
			array.set_typed(element_types[0].get_container_type());
		}
		return array;
	}
	if (type == Variant::DICTIONARY) {
		Dictionary dictionary;
		if (!element_types.is_empty()) {
			const ContainerType key_type = element_types[0].get_container_type();
			const ContainerType value_type = element_types.size() > 1 ? element_types[1].get_container_type() : ContainerType();
			dictionary.set_typed(key_type, value_type);
		}
		return dictionary;
	}

	Variant result;
	if (type != Variant::NIL && type != Variant::OBJECT) {
		VariantInternal::initialize(&result, type);
	}
	return result;
}

bool ContainerTypeValidate::_internal_validate(Variant &inout_variant, const char *p_operation, bool p_output_errors) const {
	if (type == Variant::NIL) {
		return true;
	}

	if (type != inout_variant.get_type()) {
		if (inout_variant.get_type() == Variant::NIL && type == Variant::OBJECT) {
			return true;
		}

		if (Variant::can_convert_strict(inout_variant.get_type(), type)) {
			Variant converted_to;
			const Variant *converted_from = &inout_variant;
			Callable::CallError call_error;
			Variant::construct(type, converted_to, &converted_from, 1, call_error);

			if (call_error.error == Callable::CallError::CALL_OK) {
				inout_variant = converted_to;
			}
		}

		if (type != inout_variant.get_type()) {
			if (p_output_errors) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a variable of type '%s' into a %s of type '%s'.", String(p_operation), Variant::get_type_name(inout_variant.get_type()), where, get_type_name()));
			}
			return false;
		}
	}

	if (type == Variant::OBJECT) {
		return _internal_validate_object(inout_variant, p_operation, p_output_errors);
	}
	if (type == Variant::ARRAY) {
		return _internal_validate_array(inout_variant, p_operation, p_output_errors);
	}
	if (type == Variant::DICTIONARY) {
		return _internal_validate_dictionary(inout_variant, p_operation, p_output_errors);
	}

	return true;
}

bool ContainerTypeValidate::_internal_validate_object(const Variant &p_variant, const char *p_operation, bool p_output_errors) const {
	ERR_FAIL_COND_V(p_variant.get_type() != Variant::OBJECT, false);

#ifdef DEBUG_ENABLED
	const ObjectID object_id = p_variant;
	if (object_id == ObjectID()) {
		return true;
	}
	Object *object = ObjectDB::get_instance(object_id);
	if (object == nullptr) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an invalid (previously freed?) object instance into a '%s'.", String(p_operation), String(where)));
		}
		return false;
	}
#else
	Object *object = p_variant;
	if (object == nullptr) {
		return true;
	}
#endif
	if (class_name == StringName()) {
		return true;
	}

	const StringName &obj_class = object->get_class_name();
	if (obj_class != class_name && !ClassDB::is_parent_class(obj_class, class_name)) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object of type '%s' into a %s, which does not inherit from '%s'.", String(p_operation), object->get_class(), where, String(class_name)));
		}
		return false;
	}

	if (script.is_null()) {
		return true;
	}

	Ref<Script> other_script = object->get_script();
	if (script->is_trait_type()) {
		if (other_script.is_null() || !other_script->has_script_trait(script->get_trait_type_name())) {
			if (p_output_errors) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object into a %s, that does not use the trait '%s'.", String(p_operation), String(where), String(script->get_trait_type_name())));
			}
			return false;
		}
	} else if (other_script.is_null() || !other_script->inherits_script(script)) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object into a %s, that does not inherit from '%s'.", String(p_operation), String(where), String(script->get_class_name())));
		}
		return false;
	}

	// The expected element type is a specialized script handle (e.g. `Box[int]`). Type arguments are
	// invariant, so a value whose effective arguments for this base differ (`Box[String]`) must be
	// rejected. The value's reified arguments are expressed against ITS OWN leaf type parameters, so a
	// subclass value (`StringBox extends Box[String]`, or a generic `PairBox[A, B] extends Box[A]`) is
	// first projected onto the expected base's parameters via the inheritance-reification table before
	// comparison. A slot left unbound (an unspecialized generic leaf, or a raw `Box.new()`) carries no
	// argument evidence and is accepted under gradual typing rather than risking a spurious rejection.
	if (!type_arguments.is_empty()) {
		ScriptInstance *instance = object->get_script_instance();
		Vector<ContainerType> reified_type_arguments;
		if (instance != nullptr) {
			instance->get_reified_type_arguments(reified_type_arguments);
		}
		Vector<ContainerType> projected_type_arguments;
		Vector<bool> projected_argument_bound;
		if (other_script->project_type_arguments_onto_base(script, reified_type_arguments, projected_type_arguments, projected_argument_bound) &&
				_projected_type_arguments_conflict(type_arguments, projected_type_arguments, projected_argument_bound)) {
			if (p_output_errors) {
				ContainerType expected;
				expected.builtin_type = type;
				expected.class_name = class_name;
				expected.script = script;
				expected.type_arguments = type_arguments;
				ContainerType actual = expected;
				actual.type_arguments = projected_type_arguments;
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object specialized as '%s' into a %s of '%s'.", String(p_operation), actual.get_type_name(), String(where), expected.get_type_name()));
			}
			return false;
		}
	}

	return true;
}

bool ContainerTypeValidate::_internal_validate_array(Variant &inout_variant, const char *p_operation, bool p_output_errors) const {
	if (element_types.is_empty()) {
		return true;
	}

	Array array = inout_variant;
	const ContainerTypeValidate &expected_element_type = element_types[0];
	if (array.is_typed()) {
		const ContainerTypeValidate source_element_type(array.get_element_type());
		if (expected_element_type == source_element_type || expected_element_type.can_reference(source_element_type)) {
			return true;
		}
	}

	Array converted;
	converted.set_typed(expected_element_type.get_container_type());
	for (int i = 0; i < array.size(); i++) {
		Variant value = array[i];
		if (!expected_element_type._internal_validate(value, p_operation, p_output_errors)) {
			return false;
		}
		converted.push_back(value);
	}
	inout_variant = converted;
	return true;
}

bool ContainerTypeValidate::_internal_validate_dictionary(Variant &inout_variant, const char *p_operation, bool p_output_errors) const {
	if (element_types.is_empty()) {
		return true;
	}

	Dictionary dictionary = inout_variant;
	const ContainerTypeValidate &expected_key_type = element_types[0];
	const ContainerTypeValidate expected_value_type = element_types.size() > 1 ? element_types[1] : ContainerTypeValidate();
	if (dictionary.is_typed()) {
		const ContainerTypeValidate source_key_type(dictionary.get_key_type());
		const ContainerTypeValidate source_value_type(dictionary.get_value_type());
		if ((expected_key_type == source_key_type || expected_key_type.can_reference(source_key_type)) &&
				(expected_value_type == source_value_type || expected_value_type.can_reference(source_value_type))) {
			return true;
		}
	}

	Dictionary converted;
	converted.set_typed(expected_key_type.get_container_type(), expected_value_type.get_container_type());
	for (const Variant &raw_key : dictionary.get_key_list()) {
		Variant key = raw_key;
		if (!expected_key_type._internal_validate(key, p_operation, p_output_errors)) {
			return false;
		}
		Variant value = dictionary[raw_key];
		if (!expected_value_type._internal_validate(value, p_operation, p_output_errors)) {
			return false;
		}
		if (!converted.set(key, value)) {
			return false;
		}
	}
	inout_variant = converted;
	return true;
}

bool ContainerTypeValidate::can_reference(const ContainerTypeValidate &p_type) const {
	if (type != p_type.type) {
		return false;
	}

	if (type == Variant::ARRAY) {
		if (element_types.is_empty()) {
			return true;
		}
		if (p_type.element_types.is_empty()) {
			return false;
		}
		return element_types[0].can_reference(p_type.element_types[0]);
	}

	if (type == Variant::DICTIONARY) {
		if (element_types.is_empty()) {
			return true;
		}
		if (p_type.element_types.is_empty()) {
			return false;
		}
		const ContainerTypeValidate value_type = element_types.size() > 1 ? element_types[1] : ContainerTypeValidate();
		const ContainerTypeValidate other_value_type = p_type.element_types.size() > 1 ? p_type.element_types[1] : ContainerTypeValidate();
		return element_types[0].can_reference(p_type.element_types[0]) && value_type.can_reference(other_value_type);
	}

	if (type != Variant::OBJECT) {
		return true;
	}

	if (class_name == StringName()) {
		return true;
	} else if (p_type.class_name == StringName()) {
		return false;
	} else if (class_name != p_type.class_name && !ClassDB::is_parent_class(p_type.class_name, class_name)) {
		return false;
	}

	if (script.is_null()) {
		return true;
	} else if (p_type.script.is_null()) {
		return false;
	} else if (script->is_trait_type()) {
		if (!p_type.script->has_script_trait(script->get_trait_type_name())) {
			return false;
		}
	} else if (script != p_type.script && !p_type.script->inherits_script(script)) {
		return false;
	}

	// Type arguments are invariant: a `Box[int]` container can only reference another `Box[int]`
	// container without conversion.
	if (!type_arguments.is_empty()) {
		if (script == p_type.script) {
			// Same script: arguments are compared directly (a raw, unspecialized source is conservatively
			// rejected here, since an `Array[Box]` may hold a differently specialized element).
			if (type_arguments != p_type.type_arguments) {
				return false;
			}
		} else if (p_type.script.is_valid()) {
			// Subclass source (`StringBox extends Box[String]`, or generic `PairBox[A, B] extends Box[A]`):
			// project its specialization onto this expected base's parameters before comparing. An unbound
			// slot (unspecialized source) carries no evidence and is accepted by reference under gradual typing.
			Vector<ContainerType> projected_type_arguments;
			Vector<bool> projected_argument_bound;
			if (p_type.script->project_type_arguments_onto_base(script, p_type.type_arguments, projected_type_arguments, projected_argument_bound) &&
					_projected_type_arguments_conflict(type_arguments, projected_type_arguments, projected_argument_bound)) {
				return false;
			}
		}
	}

	return true;
}

bool ContainerTypeValidate::operator==(const ContainerTypeValidate &p_type) const {
	return type == p_type.type &&
			class_name == p_type.class_name &&
			script == p_type.script &&
			element_types == p_type.element_types &&
			type_arguments == p_type.type_arguments;
}

bool ContainerTypeValidate::operator!=(const ContainerTypeValidate &p_type) const {
	return !(*this == p_type);
}

namespace ContainerTypeDescriptor {

static bool _fail(String *r_error, const String &p_error) {
	if (r_error != nullptr) {
		*r_error = p_error;
	}
	return false;
}

bool from_variant(const Variant &p_descriptor, ContainerType &r_type, String *r_error) {
	if (p_descriptor.get_type() != Variant::DICTIONARY) {
		return _fail(r_error, "Container type descriptor must be a Dictionary.");
	}

	const Dictionary descriptor = p_descriptor;
	if (!descriptor.has("type")) {
		return _fail(r_error, R"(Container type descriptor is missing required "type" key.)");
	}

	const Variant type_value = descriptor["type"];
	if (type_value.get_type() != Variant::INT) {
		return _fail(r_error, R"(Container type descriptor "type" must be an int.)");
	}

	const int64_t type_id = type_value;
	if (type_id < Variant::NIL || type_id >= Variant::VARIANT_MAX) {
		return _fail(r_error, vformat("Container type descriptor has invalid type id %d.", type_id));
	}

	ContainerType type;
	type.builtin_type = Variant::Type(type_id);

	if (descriptor.has("class_name")) {
		const Variant class_name_value = descriptor["class_name"];
		if (class_name_value.get_type() == Variant::STRING_NAME) {
			type.class_name = class_name_value;
		} else if (class_name_value.get_type() == Variant::STRING) {
			type.class_name = String(class_name_value);
		} else {
			return _fail(r_error, R"(Container type descriptor "class_name" must be a StringName or String.)");
		}
		if (type.builtin_type != Variant::OBJECT && type.class_name != StringName()) {
			return _fail(r_error, R"(Container type descriptor "class_name" is only valid for Object types.)");
		}
	}

	if (descriptor.has("script")) {
		const Variant script_value = descriptor["script"];
		if (script_value.get_type() != Variant::NIL) {
			if (type.builtin_type != Variant::OBJECT) {
				return _fail(r_error, R"(Container type descriptor "script" is only valid for Object types.)");
			}
			Ref<Script> script = script_value;
			if (script.is_null()) {
				return _fail(r_error, R"(Container type descriptor "script" must be nil or a Script object.)");
			}
			type.script = script;
			type.class_name = script->get_instance_base_type();
		}
	}

	Array element_types;
	if (descriptor.has("element_types")) {
		const Variant element_types_value = descriptor["element_types"];
		if (element_types_value.get_type() != Variant::ARRAY) {
			return _fail(r_error, R"(Container type descriptor "element_types" must be an Array.)");
		}
		element_types = element_types_value;
	}

	const int child_count = element_types.size();
	if (type.builtin_type == Variant::ARRAY) {
		if (child_count != 0 && child_count != 1) {
			return _fail(r_error, "Array container type descriptors must have zero or one child descriptor.");
		}
	} else if (type.builtin_type == Variant::DICTIONARY) {
		if (child_count != 0 && child_count != 2) {
			return _fail(r_error, "Dictionary container type descriptors must have zero or two child descriptors.");
		}
	} else if (child_count != 0) {
		return _fail(r_error, "Only Array and Dictionary container type descriptors can have child descriptors.");
	}

	for (int i = 0; i < child_count; i++) {
		ContainerType child_type;
		if (!from_variant(element_types[i], child_type, r_error)) {
			return false;
		}
		type.element_types.push_back(child_type);
	}

	if (descriptor.has("type_arguments")) {
		const Variant type_arguments_value = descriptor["type_arguments"];
		if (type_arguments_value.get_type() != Variant::ARRAY) {
			return _fail(r_error, R"(Container type descriptor "type_arguments" must be an Array.)");
		}
		const Array type_arguments = type_arguments_value;
		for (int i = 0; i < type_arguments.size(); i++) {
			ContainerType argument_type;
			if (!from_variant(type_arguments[i], argument_type, r_error)) {
				return false;
			}
			type.type_arguments.push_back(argument_type);
		}
	}

	r_type = type;
	return true;
}

Variant to_variant(const ContainerType &p_type) {
	Dictionary descriptor;
	descriptor["type"] = p_type.builtin_type;
	if (p_type.class_name != StringName()) {
		descriptor["class_name"] = p_type.class_name;
	}
	if (p_type.script.is_valid()) {
		descriptor["script"] = p_type.script;
	}
	if (!p_type.element_types.is_empty()) {
		Array element_types;
		for (const ContainerType &element_type : p_type.element_types) {
			element_types.push_back(to_variant(element_type));
		}
		descriptor["element_types"] = element_types;
	}
	if (!p_type.type_arguments.is_empty()) {
		Array type_arguments;
		for (const ContainerType &argument_type : p_type.type_arguments) {
			type_arguments.push_back(to_variant(argument_type));
		}
		descriptor["type_arguments"] = type_arguments;
	}
	return descriptor;
}

} // namespace ContainerTypeDescriptor
