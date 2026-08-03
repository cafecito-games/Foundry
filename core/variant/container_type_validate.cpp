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
#include "core/object/class_handle.h"
#include "core/object/object.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant_internal.h"

// Compares a value's projected type arguments against an expected specialization. A projected argument
// that is known and differs from the expected one is a definite invariance violation; an unknown
// argument (an unspecialized-leaf open parameter, or a step of the chain that could not be resolved)
// carries no evidence and is left to gradual acceptance. Evidence is recursive, so a partially known
// argument still enforces the parts of itself that are known. `p_expected` is the expected element
// specialization (always fully known here).
static bool _projected_type_arguments_conflict(const Vector<ContainerType> &p_expected, const Vector<ProjectedContainerType> &p_projected) {
	for (int i = 0; i < p_projected.size() && i < p_expected.size(); i++) {
		if (p_projected[i].conflicts_with_expected(p_expected[i])) {
			return true;
		}
	}
	return false;
}

// Node identity without descendants: everything a projection knows about a single level of a type.
static bool _shallow_container_identity_equals(const ContainerType &p_left, const ContainerType &p_right) {
	return p_left.builtin_type == p_right.builtin_type &&
			p_left.numeric_type == p_right.numeric_type &&
			p_left.class_name == p_right.class_name &&
			p_left.script == p_right.script &&
			p_left.is_type_handle == p_right.is_type_handle;
}

bool ContainerType::operator==(const ContainerType &p_type) const {
	return builtin_type == p_type.builtin_type &&
			numeric_type == p_type.numeric_type &&
			class_name == p_type.class_name &&
			script == p_type.script &&
			element_types == p_type.element_types &&
			type_arguments == p_type.type_arguments &&
			is_type_handle == p_type.is_type_handle;
}

bool ContainerType::operator!=(const ContainerType &p_type) const {
	return !(*this == p_type);
}

String ContainerType::get_type_name() const {
	// The handle wrapper is applied last so a specialized handle renders as `Type[Box[int]]` rather
	// than `Type[Box][int]`.
	ContainerType value_type = *this;
	value_type.is_type_handle = false;
	String name = ContainerTypeValidate(value_type).get_type_name();
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
	if (is_type_handle) {
		name = "Type[" + name + "]";
	}
	return name;
}

ContainerTypeValidate::ContainerTypeValidate(const ContainerType &p_type) {
	type = p_type.builtin_type;
	numeric_type = p_type.numeric_type;
	class_name = p_type.class_name;
	script = p_type.script;
	for (const ContainerType &element_type : p_type.element_types) {
		element_types.push_back(ContainerTypeValidate(element_type));
	}
	type_arguments = p_type.type_arguments;
	is_type_handle = p_type.is_type_handle;
}

ContainerType ContainerTypeValidate::get_container_type() const {
	ContainerType result;
	result.builtin_type = type;
	result.numeric_type = numeric_type;
	result.class_name = class_name;
	result.script = script;
	for (const ContainerTypeValidate &element_type : element_types) {
		result.element_types.push_back(element_type.get_container_type());
	}
	result.type_arguments = type_arguments;
	result.is_type_handle = is_type_handle;
	return result;
}

String ContainerTypeValidate::get_type_name() const {
	String name = _get_value_type_name();
	if (!is_type_handle) {
		return name;
	}
	// A handle names the specialization it represents, so a nested `Type[Box[int]]` element does not
	// degrade to `Type[Box]`.
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
	return "Type[" + name + "]";
}

String ContainerTypeValidate::_get_value_type_name() const {
	if (type == Variant::NIL) {
		return "Variant";
	}
	// A declared width names itself: `Variant::get_type_name()` only knows the carrier, so it cannot
	// tell `int` from `long`. Descriptors without a source spelling fall back to their diagnostic name.
	if (numeric_type != NumericType::NONE && numeric_type_is_carrier_consistent(numeric_type, type)) {
		return numeric_type_has_public_name(numeric_type) ? numeric_type_public_name(numeric_type) : numeric_type_name(numeric_type);
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

	// The carrier now matches, so an integer slot with a declared width can check the magnitude. A
	// `NONE` descriptor is the absence of a width constraint rather than an empty range, so it is not
	// consulted at all: an unannotated container keeps accepting everything its carrier can hold.
	if (numeric_type != NumericType::NONE && !numeric_type_contains(numeric_type, inout_variant)) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s the value %s into a %s of type '%s', which only accepts values from %s to %s.", String(p_operation), inout_variant.stringify(), where, get_type_name(), String::num_int64(numeric_type_minimum(numeric_type)), String::num_uint64(numeric_type_maximum(numeric_type))));
		}
		return false;
	}

	if (type == Variant::OBJECT) {
		if (is_type_handle) {
			return _internal_validate_class_handle(inout_variant, p_operation, p_output_errors);
		}
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
		Vector<ProjectedContainerType> projected_type_arguments;
		if (other_script->project_type_arguments_onto_base(script, reified_type_arguments, projected_type_arguments) &&
				_projected_type_arguments_conflict(type_arguments, projected_type_arguments)) {
			if (p_output_errors) {
				ContainerType expected;
				expected.builtin_type = type;
				expected.class_name = class_name;
				expected.script = script;
				expected.type_arguments = type_arguments;
				ContainerType actual = expected;
				actual.type_arguments.clear();
				for (const ProjectedContainerType &projected : projected_type_arguments) {
					actual.type_arguments.push_back(projected.to_container_type());
				}
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object specialized as '%s' into a %s of '%s'.", String(p_operation), actual.get_type_name(), String(where), expected.get_type_name()));
			}
			return false;
		}
	}

	return true;
}

// Compares the reified arguments a class handle carries against an expected specialization, after
// projecting them onto the expected base's parameters. `p_handle_carries_arguments` is false for a
// handle that cannot express a specialization at all (a bare script resource): such a handle offers no
// evidence for any argument, and an expected specialization is therefore unsatisfied rather than
// gradually accepted, which is the same answer a specialized handle with an explicitly unbound slot
// would give.
static bool _class_handle_type_arguments_match(const Vector<ContainerType> &p_expected_arguments,
		const Ref<Script> &p_expected_script, const Ref<Script> &p_handle_script,
		const Vector<ContainerType> &p_handle_arguments, bool p_handle_carries_arguments) {
	if (p_expected_arguments.is_empty()) {
		return true;
	}
	if (p_handle_script.is_null() || p_expected_script.is_null()) {
		return false;
	}

	Vector<ProjectedContainerType> projected_type_arguments;
	if (!p_handle_script->project_type_arguments_onto_base(p_expected_script, p_handle_arguments,
				projected_type_arguments)) {
		if (p_handle_script != p_expected_script) {
			return false;
		}
		for (const ContainerType &handle_argument : p_handle_arguments) {
			projected_type_arguments.push_back(ProjectedContainerType::exact(handle_argument));
		}
	}

	if (projected_type_arguments.size() != p_expected_arguments.size()) {
		return false;
	}
	for (int i = 0; i < p_expected_arguments.size(); i++) {
		if (!projected_type_arguments[i].is_known() && !p_handle_carries_arguments) {
			return false;
		}
		if (projected_type_arguments[i].conflicts_with_expected(p_expected_arguments[i])) {
			return false;
		}
	}
	return true;
}

// Validates a value against a `Type[T]` slot: the value must denote the class T (or a subtype of it),
// not be an instance of it. Values that denote a class are `ClassHandle` implementations contributed by
// a scripting language, and bare `Script` resources, which denote the class they define.
bool ContainerTypeValidate::_internal_validate_class_handle(const Variant &p_variant, const char *p_operation, bool p_output_errors) const {
	// Null is permissive here for the same reason it is on the instance path: rejecting null in a
	// non-nullable slot is a static concern, not a runtime one.
	if (p_variant.get_type() == Variant::NIL) {
		return true;
	}
	if (p_variant.get_type() != Variant::OBJECT) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s a value of type '%s' into a %s of type '%s', which requires a class handle.", String(p_operation), Variant::get_type_name(p_variant.get_type()), where, get_type_name()));
		}
		return false;
	}

	bool was_freed = false;
	Object *object = p_variant.get_validated_object_with_check(was_freed);
	if (object == nullptr) {
		if (was_freed) {
			if (p_output_errors) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s an invalid (previously freed?) class handle into a %s of type '%s'.", String(p_operation), where, get_type_name()));
			}
			return false;
		}
		return true;
	}

	StringName handle_native_class;
	Ref<Script> handle_script;
	Vector<ContainerType> handle_type_arguments;
	bool handle_carries_arguments = false;

	if (ClassHandle *class_handle = Object::cast_to<ClassHandle>(object)) {
		handle_native_class = class_handle->get_represented_native_class();
		handle_script = class_handle->get_represented_script();
		class_handle->get_represented_type_arguments(handle_type_arguments);
		handle_carries_arguments = true;
	} else if (Script *script_value = Object::cast_to<Script>(object)) {
		// A script resource denotes the class it defines, always unspecialized.
		handle_script = Ref<Script>(script_value);
	}

	if (handle_native_class == StringName() && handle_script.is_null()) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object of type '%s' into a %s of type '%s', which requires a class handle.", String(p_operation), object->get_class(), where, get_type_name()));
		}
		return false;
	}

	ContainerType represented;
	represented.builtin_type = Variant::OBJECT;
	represented.class_name = handle_native_class != StringName() ? handle_native_class : handle_script->get_instance_base_type();
	represented.script = handle_script;
	represented.type_arguments = handle_type_arguments;

	if (script.is_null()) {
		// The slot expects a native engine class, so any handle whose represented instances inherit it
		// satisfies it, scripted or not.
		const StringName &represented_native_class = represented.class_name;
		if (represented_native_class == StringName() ||
				(class_name != StringName() && !ClassDB::is_parent_class(represented_native_class, class_name))) {
			if (p_output_errors) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a class handle for '%s' into a %s of type '%s', whose represented type is not compatible.", String(p_operation), represented.get_type_name(), where, get_type_name()));
			}
			return false;
		}
		return true;
	}

	// The slot expects a script type. A handle for a native engine class denotes no script, so it can
	// never satisfy one — including a trait-typed slot, where retroactive conformance is a property of
	// scripts and reaches here through `Script::has_script_trait()`.
	bool represents_expected_type = false;
	if (handle_script.is_valid()) {
		if (script->is_trait_type()) {
			represents_expected_type = handle_script->has_script_trait(script->get_trait_type_name());
		} else {
			Script *walker = handle_script.ptr();
			while (walker != nullptr) {
				if (walker == script.ptr()) {
					represents_expected_type = true;
					break;
				}
				walker = walker->get_base_script().ptr();
			}
		}
	}
	if (!represents_expected_type) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s a class handle for '%s' into a %s of type '%s', whose represented type is not compatible.", String(p_operation), represented.get_type_name(), where, get_type_name()));
		}
		return false;
	}

	if (!_class_handle_type_arguments_match(type_arguments, script, handle_script, handle_type_arguments, handle_carries_arguments)) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s a class handle specialized as '%s' into a %s of type '%s'.", String(p_operation), represented.get_type_name(), where, get_type_name()));
		}
		return false;
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

	// A container of class handles and a container of instances hold disjoint value sets, so neither can
	// be referenced as the other.
	if (is_type_handle != p_type.is_type_handle) {
		return false;
	}

	// Referencing skips per-value validation, so it must not be more permissive than it. A slot with no
	// width constraint accepts every value its carrier holds and can therefore alias any width on that
	// carrier; two different declared widths describe values the other would reject, so `int` and `long`
	// containers stay invariant.
	if (!numeric_type_can_alias(numeric_type, p_type.numeric_type)) {
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
		if (is_type_handle) {
			// Referencing skips per-element validation, so it must not be more permissive than the
			// per-value rule. A source element type that leaves an expected argument unbound describes
			// handles the destination would reject one by one, so it cannot be referenced.
			return _class_handle_type_arguments_match(type_arguments, script, p_type.script, p_type.type_arguments, !p_type.type_arguments.is_empty());
		}
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
			Vector<ProjectedContainerType> projected_type_arguments;
			if (p_type.script->project_type_arguments_onto_base(script, p_type.type_arguments, projected_type_arguments) &&
					_projected_type_arguments_conflict(type_arguments, projected_type_arguments)) {
				return false;
			}
		}
	}

	return true;
}

bool ContainerTypeValidate::operator==(const ContainerTypeValidate &p_type) const {
	return type == p_type.type &&
			numeric_type == p_type.numeric_type &&
			class_name == p_type.class_name &&
			script == p_type.script &&
			element_types == p_type.element_types &&
			type_arguments == p_type.type_arguments &&
			is_type_handle == p_type.is_type_handle;
}

bool ContainerTypeValidate::operator!=(const ContainerTypeValidate &p_type) const {
	return !(*this == p_type);
}

ProjectedContainerType ProjectedContainerType::_exact(const ContainerType &p_type, int p_depth) {
	ProjectedContainerType projected;
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		// Only this subtree loses its evidence; every shallower node keeps validating.
		return projected;
	}

	projected.state = EXACT;
	projected.outer = p_type;
	projected.outer.element_types.clear();
	projected.outer.type_arguments.clear();
	for (const ContainerType &element_type : p_type.element_types) {
		projected.element_types.push_back(_exact(element_type, p_depth + 1));
	}
	for (const ContainerType &type_argument : p_type.type_arguments) {
		projected.type_arguments.push_back(_exact(type_argument, p_depth + 1));
	}
	return projected;
}

ProjectedContainerType ProjectedContainerType::exact(const ContainerType &p_type) {
	return _exact(p_type, 0);
}

ContainerType ProjectedContainerType::to_container_type() const {
	ContainerType type = outer;
	type.element_types.clear();
	type.type_arguments.clear();
	if (state == UNKNOWN) {
		return ContainerType();
	}
	for (const ProjectedContainerType &element_type : element_types) {
		type.element_types.push_back(element_type.to_container_type());
	}
	for (const ProjectedContainerType &type_argument : type_arguments) {
		type.type_arguments.push_back(type_argument.to_container_type());
	}
	return type;
}

String ProjectedContainerType::get_type_name() const {
	return to_container_type().get_type_name();
}

bool ProjectedContainerType::conflicts_with_expected(const ContainerType &p_expected) const {
	if (state == UNKNOWN) {
		return false;
	}
	if (!_shallow_container_identity_equals(outer, p_expected)) {
		return true;
	}
	// A known node knows its own arity, so a differing one is a real conflict; this makes a fully
	// `EXACT` comparison identical to comparing the materialized types with `!=`.
	if (element_types.size() != p_expected.element_types.size() ||
			type_arguments.size() != p_expected.type_arguments.size()) {
		return true;
	}
	for (int i = 0; i < element_types.size(); i++) {
		if (element_types[i].conflicts_with_expected(p_expected.element_types[i])) {
			return true;
		}
	}
	for (int i = 0; i < type_arguments.size(); i++) {
		if (type_arguments[i].conflicts_with_expected(p_expected.type_arguments[i])) {
			return true;
		}
	}
	return false;
}

bool ProjectedContainerType::conflicts_with(const ProjectedContainerType &p_other) const {
	if (state == UNKNOWN || p_other.state == UNKNOWN) {
		return false;
	}
	if (!_shallow_container_identity_equals(outer, p_other.outer)) {
		return true;
	}
	// An empty child vector on either side is an unspecialized node rather than an arity claim, so it
	// contributes no evidence; two non-empty vectors of different length describe different shapes.
	if (!element_types.is_empty() && !p_other.element_types.is_empty()) {
		if (element_types.size() != p_other.element_types.size()) {
			return true;
		}
		for (int i = 0; i < element_types.size(); i++) {
			if (element_types[i].conflicts_with(p_other.element_types[i])) {
				return true;
			}
		}
	}
	if (!type_arguments.is_empty() && !p_other.type_arguments.is_empty()) {
		if (type_arguments.size() != p_other.type_arguments.size()) {
			return true;
		}
		for (int i = 0; i < type_arguments.size(); i++) {
			if (type_arguments[i].conflicts_with(p_other.type_arguments[i])) {
				return true;
			}
		}
	}
	return false;
}

bool ProjectedContainerType::is_witnessed_by(const ProjectedContainerType &p_other) const {
	if (state == UNKNOWN) {
		return true;
	}
	if (p_other.state == UNKNOWN || !_shallow_container_identity_equals(outer, p_other.outer)) {
		return false;
	}
	if (!element_types.is_empty()) {
		if (element_types.size() != p_other.element_types.size()) {
			return false;
		}
		for (int i = 0; i < element_types.size(); i++) {
			if (!element_types[i].is_witnessed_by(p_other.element_types[i])) {
				return false;
			}
		}
	}
	if (!type_arguments.is_empty()) {
		if (type_arguments.size() != p_other.type_arguments.size()) {
			return false;
		}
		for (int i = 0; i < type_arguments.size(); i++) {
			if (!type_arguments[i].is_witnessed_by(p_other.type_arguments[i])) {
				return false;
			}
		}
	}
	return true;
}

bool ProjectedContainerType::validate_value(Variant &r_value, const char *p_where, const char *p_operation) const {
	if (state == UNKNOWN) {
		return true;
	}
	if (state == EXACT) {
		ContainerTypeValidate validator(to_container_type());
		validator.where = p_where;
		return validator.validate(r_value, p_operation);
	}

	// The node itself is known but at least one descendant is not, so the fully-known validators cannot
	// be handed the whole shape: they compare arguments and element types invariantly and would reject a
	// value whose unknown slot simply cannot be recovered here. Validate the known node on its own —
	// wrong class, script, trait, carrier, width, nullability, or instance-versus-handle shape still
	// reject — and then enforce every descendant that IS known separately.
	ContainerType shallow = outer;
	shallow.element_types.clear();
	shallow.type_arguments.clear();
	ContainerTypeValidate validator(shallow);
	validator.where = p_where;
	if (!validator.validate(r_value, p_operation)) {
		return false;
	}
	return _validate_known_descendants(r_value, p_where, p_operation);
}

bool ProjectedContainerType::_validate_known_descendants(Variant &p_value, const char *p_where, const char *p_operation) const {
	bool has_known_argument = false;
	for (const ProjectedContainerType &type_argument : type_arguments) {
		if (type_argument.is_known()) {
			has_known_argument = true;
			break;
		}
	}

	if (outer.builtin_type == Variant::OBJECT) {
		if (!has_known_argument) {
			return true;
		}

		bool was_freed = false;
		Object *object = p_value.get_validated_object_with_check(was_freed);
		if (object == nullptr) {
			// A null or already-rejected value is the shallow validator's concern, not this one's.
			return true;
		}

		Ref<Script> value_script;
		Vector<ContainerType> value_type_arguments;
		bool value_carries_arguments = false;
		if (outer.is_type_handle) {
			if (ClassHandle *class_handle = Object::cast_to<ClassHandle>(object)) {
				value_script = class_handle->get_represented_script();
				class_handle->get_represented_type_arguments(value_type_arguments);
				value_carries_arguments = true;
			} else if (Script *script_value = Object::cast_to<Script>(object)) {
				// A bare script resource denotes its class unspecialized, so it offers no argument
				// evidence for a slot that demands one.
				value_script = Ref<Script>(script_value);
			}
		} else {
			ScriptInstance *instance = object->get_script_instance();
			if (instance != nullptr) {
				value_script = instance->get_script();
				instance->get_reified_type_arguments(value_type_arguments);
				value_carries_arguments = true;
			}
		}

		Vector<ProjectedContainerType> value_projection;
		if (value_script.is_null() ||
				!value_script->project_type_arguments_onto_base(outer.script, value_type_arguments, value_projection)) {
			if (outer.is_type_handle && !value_carries_arguments) {
				// Matches the fully-known class-handle rule: a handle that cannot express a specialization
				// at all does not satisfy a slot that requires one.
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a class handle for an unspecialized '%s' into a %s of type '%s'.", String(p_operation), String(outer.script.is_valid() ? outer.script->get_class_name() : outer.class_name), String(p_where), get_type_name()));
			}
			return true;
		}

		for (int i = 0; i < type_arguments.size() && i < value_projection.size(); i++) {
			if (outer.is_type_handle && type_arguments[i].is_known() && !value_projection[i].is_known() &&
					!value_carries_arguments) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a class handle for an unspecialized '%s' into a %s of type '%s'.", String(p_operation), String(outer.script.is_valid() ? outer.script->get_class_name() : outer.class_name), String(p_where), get_type_name()));
			}
			if (type_arguments[i].conflicts_with(value_projection[i])) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a value specialized as '%s' into a %s of '%s'.", String(p_operation), value_projection[i].get_type_name(), String(p_where), get_type_name()));
			}
		}
		return true;
	}

	// A typed Array/Dictionary declares its element types, so the known ones are compared against that
	// metadata. An untyped one has none, so the known evidence is enforced value by value instead, the
	// same way the fully-known validator walks an untyped container it cannot reference wholesale. The
	// converted container the fully-known path would build cannot be produced here: an unknown element
	// slot has no type to bake into it.
	if (outer.builtin_type == Variant::ARRAY && !element_types.is_empty() && p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		if (array.is_typed()) {
			const ProjectedContainerType source_element = ProjectedContainerType::exact(array.get_element_type());
			if (element_types[0].conflicts_with(source_element)) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s an array of '%s' into a %s of '%s'.", String(p_operation), array.get_element_type().get_type_name(), String(p_where), get_type_name()));
			}
			if (element_types[0].is_witnessed_by(source_element)) {
				return true;
			}
			// The metadata left part of the known evidence unstated (a raw `Array[Pair]` source against an
			// expected `Pair[int, ?]` element), so it proved nothing about that part. Fall through to the
			// per-value check, which is what the fully-known validator does with a source it cannot
			// reference wholesale.
		}
		if (!element_types[0].is_known()) {
			return true;
		}
		Array validated;
		validated.resize(array.size());
		bool converted = false;
		for (int i = 0; i < array.size(); i++) {
			Variant element = array[i];
			const Variant original = element;
			if (!element_types[0].validate_value(element, p_where, p_operation)) {
				return false;
			}
			converted = converted || !element.identity_compare(original);
			validated[i] = element;
		}
		if (converted && !array.is_typed()) {
			// A descendant validator converted an element (a nested untyped array becoming typed, a
			// specialized handle erasing to its script). The outer container cannot be rebuilt with the
			// declared shape — an unknown slot has no type to bake in — but its elements must still be
			// stored in the form validation accepted them in. A typed source keeps its own element type,
			// which replacing it with this untyped copy would throw away.
			p_value = validated;
		}
		return true;
	}
	if (outer.builtin_type == Variant::DICTIONARY && !element_types.is_empty() && p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		const ProjectedContainerType expected_value_type = element_types.size() > 1 ? element_types[1] : ProjectedContainerType();
		if (dictionary.is_typed()) {
			const ProjectedContainerType source_key = ProjectedContainerType::exact(dictionary.get_key_type());
			const ProjectedContainerType source_value = ProjectedContainerType::exact(dictionary.get_value_type());
			if (element_types[0].conflicts_with(source_key)) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a dictionary keyed by '%s' into a %s of '%s'.", String(p_operation), dictionary.get_key_type().get_type_name(), String(p_where), get_type_name()));
			}
			if (expected_value_type.conflicts_with(source_value)) {
				ERR_FAIL_V_MSG(false, vformat("Attempted to %s a dictionary of '%s' into a %s of '%s'.", String(p_operation), dictionary.get_value_type().get_type_name(), String(p_where), get_type_name()));
			}
			if (element_types[0].is_witnessed_by(source_key) && expected_value_type.is_witnessed_by(source_value)) {
				return true;
			}
		}
		if (!element_types[0].is_known() && !expected_value_type.is_known()) {
			return true;
		}
		Dictionary validated;
		validated.reserve(dictionary.size());
		bool converted = false;
		for (const KeyValue<Variant, Variant> &entry : dictionary) {
			Variant key = entry.key;
			if (!element_types[0].validate_value(key, p_where, p_operation)) {
				return false;
			}
			converted = converted || !key.identity_compare(entry.key);
			Variant value = entry.value;
			if (!expected_value_type.validate_value(value, p_where, p_operation)) {
				return false;
			}
			converted = converted || !value.identity_compare(entry.value);
			validated[key] = value;
		}
		if (converted && !dictionary.is_typed()) {
			p_value = validated;
		}
		return true;
	}

	return true;
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

	if (descriptor.has("numeric_type")) {
		const Variant numeric_type_value = descriptor["numeric_type"];
		if (numeric_type_value.get_type() != Variant::INT) {
			return _fail(r_error, R"(Container type descriptor "numeric_type" must be an int.)");
		}
		const int64_t numeric_type_id = numeric_type_value;
		if (numeric_type_id < 0 || numeric_type_id >= int64_t(NumericType::MAX)) {
			return _fail(r_error, vformat("Container type descriptor has invalid numeric type id %d.", numeric_type_id));
		}
		type.numeric_type = NumericType(numeric_type_id);
		if (!numeric_type_is_carrier_consistent(type.numeric_type, type.builtin_type)) {
			return _fail(r_error, vformat("Container type descriptor numeric type '%s' does not match type '%s'.", numeric_type_name(type.numeric_type), Variant::get_type_name(type.builtin_type)));
		}
	}

	if (descriptor.has("is_type_handle")) {
		const Variant is_type_handle_value = descriptor["is_type_handle"];
		if (is_type_handle_value.get_type() != Variant::BOOL) {
			return _fail(r_error, R"(Container type descriptor "is_type_handle" must be a bool.)");
		}
		type.is_type_handle = is_type_handle_value;
		if (type.is_type_handle && type.builtin_type != Variant::OBJECT) {
			return _fail(r_error, R"(Container type descriptor "is_type_handle" is only valid for Object types.)");
		}
	}

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
	// Only emitted when a width is declared, so a carrier-only descriptor is spelled exactly as it was
	// before numeric descriptors existed and an absent key decodes back to an unconstrained slot.
	if (p_type.numeric_type != NumericType::NONE) {
		descriptor["numeric_type"] = int64_t(p_type.numeric_type);
	}
	// Only emitted when set, so an instance-typed descriptor is spelled exactly as it was before class
	// handles existed and an absent key decodes back to an instance type.
	if (p_type.is_type_handle) {
		descriptor["is_type_handle"] = true;
	}
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
