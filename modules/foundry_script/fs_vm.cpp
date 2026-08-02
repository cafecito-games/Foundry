/**************************************************************************/
/*  fs_vm.cpp                                                             */
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

#include "foundry_script.h"
#include "fs_cache.h"
#include "fs_conformance_registry.h"
#include "fs_function.h"
#include "fs_lambda_callable.h"
#include "fs_script_test_guard.h"

#include "core/object/script_function_state.h"
#include "core/os/os.h"
#include "core/profiling/profiling.h"

#ifdef DEBUG_ENABLED

static bool _profile_count_as_native(const Object *p_base_obj, const StringName &p_methodname) {
	if (!p_base_obj) {
		return false;
	}
	StringName cname = p_base_obj->get_class_name();
	if ((p_methodname == "new" && cname == "FoundryScript") || p_methodname == "call") {
		return false;
	}
	return ClassDB::class_exists(cname) && ClassDB::has_method(cname, p_methodname, false);
}

static String _get_element_type(const ContainerType &p_type) {
	if (p_type.builtin_type == Variant::ARRAY && !p_type.element_types.is_empty()) {
		return vformat("Array[%s]", _get_element_type(p_type.element_types[0]));
	}
	if (p_type.builtin_type == Variant::DICTIONARY && !p_type.element_types.is_empty()) {
		const String key = p_type.element_types.size() > 0 ? _get_element_type(p_type.element_types[0]) : String("Variant");
		const String value = p_type.element_types.size() > 1 ? _get_element_type(p_type.element_types[1]) : String("Variant");
		return vformat("Dictionary[%s, %s]", key, value);
	}
	if (p_type.is_type_handle) {
		// A class-handle slot must not be reported as its represented instance type: `Array[Node]`
		// for an `Array[Type[Node]]` describes the wrong expectation to the reader.
		ContainerType represented_type = p_type;
		represented_type.is_type_handle = false;
		return vformat("Type[%s]", _get_element_type(represented_type));
	}
	if (p_type.script.is_valid() && p_type.script->is_valid()) {
		return FoundryScript::debug_get_script_name(p_type.script);
	}
	if (p_type.class_name != StringName()) {
		return p_type.class_name.operator String();
	}
	return Variant::get_type_name(p_type.builtin_type);
}

static String _get_var_type(const Variant *p_var) {
	String basestr;

	if (p_var->get_type() == Variant::OBJECT) {
		bool was_freed;
		Object *bobj = p_var->get_validated_object_with_check(was_freed);
		if (!bobj) {
			if (was_freed) {
				basestr = "previously freed";
			} else {
				basestr = "null instance";
			}
		} else {
			if (bobj->is_class_ptr(FSNativeClass::get_class_ptr_static())) {
				basestr = Object::cast_to<FSNativeClass>(bobj)->get_name();
			} else if (FSSpecializedClassHandle *specialized_handle =
							   Object::cast_to<FSSpecializedClassHandle>(bobj)) {
				basestr = specialized_handle->get_type_name();
			} else {
				basestr = bobj->get_class();
				if (bobj->get_script_instance()) {
					basestr += " (" + FoundryScript::debug_get_script_name(bobj->get_script_instance()->get_script()) + ")";
				}
			}
		}

	} else {
		if (p_var->get_type() == Variant::ARRAY) {
			basestr = "Array";
			const Array *p_array = VariantInternal::get_array(p_var);
			if (p_array->is_typed()) {
				basestr += "[" + _get_element_type(p_array->get_element_type()) + "]";
			}
		} else if (p_var->get_type() == Variant::DICTIONARY) {
			basestr = "Dictionary";
			const Dictionary *p_dictionary = VariantInternal::get_dictionary(p_var);
			if (p_dictionary->is_typed()) {
				basestr += "[" + _get_element_type(p_dictionary->get_key_type()) +
						", " + _get_element_type(p_dictionary->get_value_type()) + "]";
			}
		} else {
			basestr = Variant::get_type_name(p_var->get_type());
		}
	}

	return basestr;
}

void FSFunction::_profile_native_call(uint64_t p_t_taken, const String &p_func_name, const String &p_instance_class_name) {
	HashMap<String, Profile::NativeProfile>::Iterator inner_prof = profile.native_calls.find(p_func_name);
	if (inner_prof) {
		inner_prof->value.call_count += 1;
	} else {
		String sig = vformat("%s::0::%s%s%s", get_script()->get_script_path(), p_instance_class_name, p_instance_class_name.is_empty() ? "" : ".", p_func_name);
		inner_prof = profile.native_calls.insert(p_func_name, Profile::NativeProfile{ 1, 0, sig });
	}
	inner_prof->value.total_time += p_t_taken;
}

#endif // DEBUG_ENABLED

static FSDataType _make_native_type_handle_type(FSNativeClass *p_native_class) {
	FSDataType type;
	type.kind = FSDataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_native_class->get_name();
	type.is_type_handle = true;
	return type;
}

static FSDataType _make_script_type_handle_type(Script *p_script) {
	FSDataType type;
	type.kind = Object::cast_to<FoundryScript>(p_script) != nullptr ? FSDataType::FOUNDRY_SCRIPT : FSDataType::SCRIPT;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_script->get_instance_base_type();
	type.script_type = p_script;
	type.script_type_ref.reference_ptr(p_script);
	type.is_type_handle = true;
	type.is_script_trait = p_script->is_trait_type();
	type.script_trait = p_script->get_trait_type_name();
	return type;
}

static bool _type_handle_test_matches(const FSDataType &p_expected_type, const Variant &p_value, bool &r_was_freed) {
	r_was_freed = false;
	if (p_value.get_type() == Variant::NIL) {
		return false;
	}
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object_with_check(r_was_freed);
		if (r_was_freed || object == nullptr) {
			return false;
		}
	}
	return p_expected_type.is_type(p_value);
}

static bool _is_container_type_descriptor(const Variant &p_type_info) {
	if (p_type_info.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary descriptor = p_type_info;
	return descriptor.has("builtin_type");
}

static ContainerType _container_type_from_descriptor(const Variant &p_descriptor) {
	Dictionary descriptor = p_descriptor;
	ContainerType type;
	type.builtin_type = Variant::Type(int(descriptor.get("builtin_type", Variant::NIL)));
	type.class_name = descriptor.get("native_type", StringName());
	Ref<Script> script = descriptor.get("script_type", Variant());
	type.script = script;
	// A `Type[T]` node tests class handles rather than instances at every nesting depth, so the flag
	// is read back per node instead of only at the descriptor root.
	type.is_type_handle = descriptor.get("is_type_handle", false);

	Array element_types = descriptor.get("element_types", Array());
	for (int i = 0; i < element_types.size(); i++) {
		type.element_types.push_back(_container_type_from_descriptor(element_types[i]));
	}

	Array type_arguments = descriptor.get("type_arguments", Array());
	for (int i = 0; i < type_arguments.size(); i++) {
		type.type_arguments.push_back(_container_type_from_descriptor(type_arguments[i]));
	}
	return type;
}

// Rebuilds the tuple shape a type test was compiled against. Only tuple descriptors carry the
// `is_tuple` marker, so every other node reads back through the shared container-type path.
static FSDataType _data_type_from_tuple_descriptor(const Variant &p_descriptor) {
	const Dictionary descriptor = p_descriptor;
	FSDataType type;
	if (!descriptor.get("is_tuple", false)) {
		const ContainerType container_type = _container_type_from_descriptor(descriptor);
		type = descriptor.get("is_type_handle", false)
				? FSDataType::from_type_handle_container_type(container_type)
				: FSDataType::from_container_type(container_type);
		type.is_nullable = descriptor.get("is_nullable", false);
		return type;
	}

	type.kind = FSDataType::TUPLE;
	type.builtin_type = Variant::ARRAY;
	type.is_nullable = descriptor.get("is_nullable", false);
	const Array element_types = descriptor.get("element_types", Array());
	for (int i = 0; i < element_types.size(); i++) {
		type.container_element_types.push_back(_data_type_from_tuple_descriptor(element_types[i]));
	}
	return type;
}

static ContainerType _container_type_from_type_info(const Variant &p_type_info, Variant::Type p_builtin_type, const StringName &p_native_type) {
	if (_is_container_type_descriptor(p_type_info)) {
		return _container_type_from_descriptor(p_type_info);
	}
	ContainerType type;
	type.builtin_type = p_builtin_type;
	type.class_name = p_native_type;
	Ref<Script> script = p_type_info;
	type.script = script;
	return type;
}

// Returns the fully wrapped `Type[...]` display name for a class-handle-typed slot, ready to use
// in a diagnostic without any further wrapping by the caller: `ContainerType::get_type_name()`
// already renders the `Type[...]` wrapper once `is_type_handle` is threaded through the conversion.
[[maybe_unused]] static String _get_type_handle_type_name(const FSDataType &p_expected_type, Script *p_base_type) {
	if (!p_expected_type.type_arguments.is_empty()) {
		return p_expected_type.to_container_type().get_type_name();
	}
	return "Type[" + FoundryScript::debug_get_script_name(Ref<Script>(p_base_type)) + "]";
}

static Script *_script_type_from_type_info(const Variant &p_type_info, FSDataType *r_type_handle = nullptr) {
	if (_is_container_type_descriptor(p_type_info)) {
		const ContainerType type = _container_type_from_descriptor(p_type_info);
		if (r_type_handle != nullptr) {
			*r_type_handle = FSDataType::from_type_handle_container_type(type);
		}
		return type.script.ptr();
	}

	Script *script = Object::cast_to<Script>(p_type_info.operator Object *());
	if (r_type_handle != nullptr && script != nullptr) {
		*r_type_handle = _make_script_type_handle_type(script);
	}
	return script;
}

static FSSpecializedClassHandle *_specialized_handle_from_variant(const Variant *p_value) {
	if (p_value->get_type() != Variant::OBJECT) {
		return nullptr;
	}

	Object *object = p_value->get_validated_object();
	if (object == nullptr) {
		return nullptr;
	}

	return Object::cast_to<FSSpecializedClassHandle>(object);
}

static FSSpecializedClassHandle *_specialized_handle_assignable_to_native_script(const Variant *p_value,
		const StringName &p_native_type) {
	FSSpecializedClassHandle *specialized_handle = _specialized_handle_from_variant(p_value);
	if (specialized_handle == nullptr || !specialized_handle->is_assignable_to_native_type(p_native_type)) {
		return nullptr;
	}
	return specialized_handle;
}

static bool _native_container_type_accepts_specialized_handle_erasure(const ContainerType &p_expected_type) {
	return p_expected_type.builtin_type == Variant::OBJECT && p_expected_type.script.is_null() &&
			p_expected_type.type_arguments.is_empty();
}

static bool _container_type_accepts_specialized_handle_erasure(const ContainerType &p_expected_type) {
	if (_native_container_type_accepts_specialized_handle_erasure(p_expected_type)) {
		return true;
	}

	if (p_expected_type.builtin_type == Variant::ARRAY) {
		return !p_expected_type.element_types.is_empty() &&
				_container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[0]);
	}

	if (p_expected_type.builtin_type == Variant::DICTIONARY && !p_expected_type.element_types.is_empty()) {
		if (_container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[0])) {
			return true;
		}
		return p_expected_type.element_types.size() > 1 &&
				_container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[1]);
	}

	return false;
}

static bool _erase_specialized_handle_for_native_container_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (!_native_container_type_accepts_specialized_handle_erasure(p_expected_type)) {
		return false;
	}

	FSSpecializedClassHandle *specialized_handle = _specialized_handle_from_variant(&r_value);
	if (specialized_handle == nullptr || !specialized_handle->is_assignable_to_native_type(p_expected_type.class_name)) {
		return false;
	}

	r_value = specialized_handle->get_specialized_script();
	return true;
}

static bool _erase_specialized_handles_for_container_type(const ContainerType &p_expected_type, Variant &r_value);

static bool _erase_specialized_handles_for_container_array_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (p_expected_type.builtin_type != Variant::ARRAY || p_expected_type.element_types.is_empty() ||
			r_value.get_type() != Variant::ARRAY) {
		return false;
	}

	const ContainerType &element_type = p_expected_type.element_types[0];
	const Array source = r_value;
	Array erased;
	erased.resize(source.size());

	bool changed = false;
	for (int i = 0; i < source.size(); i++) {
		Variant value = source[i];
		changed = _erase_specialized_handles_for_container_type(element_type, value) || changed;
		erased[i] = value;
	}

	if (!changed) {
		return false;
	}

	r_value = erased;
	return true;
}

static bool _erase_specialized_handles_for_container_dictionary_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (p_expected_type.builtin_type != Variant::DICTIONARY || p_expected_type.element_types.is_empty() ||
			r_value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	const ContainerType &key_type = p_expected_type.element_types[0];
	const ContainerType value_type = p_expected_type.element_types.size() > 1 ? p_expected_type.element_types[1] : ContainerType();
	const Dictionary source = r_value;
	Dictionary erased;
	erased.reserve(source.size());

	bool changed = false;
	for (const KeyValue<Variant, Variant> &E : source) {
		Variant key = E.key;
		Variant value = E.value;
		changed = _erase_specialized_handles_for_container_type(key_type, key) || changed;
		changed = _erase_specialized_handles_for_container_type(value_type, value) || changed;
		erased[key] = value;
	}

	if (!changed) {
		return false;
	}

	r_value = erased;
	return true;
}

static bool _erase_specialized_handles_for_container_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (_erase_specialized_handle_for_native_container_type(p_expected_type, r_value)) {
		return true;
	}
	if (_erase_specialized_handles_for_container_array_type(p_expected_type, r_value)) {
		return true;
	}
	return _erase_specialized_handles_for_container_dictionary_type(p_expected_type, r_value);
}

static bool _erase_specialized_handles_for_native_array_elements(const ContainerType &p_element_type, Variant &r_value) {
	if (_erase_specialized_handles_for_container_type(p_element_type, r_value)) {
		return true;
	}

	if (r_value.get_type() != Variant::ARRAY) {
		return false;
	}

	const Array source = r_value;
	Array erased;
	erased.resize(source.size());

	bool changed = false;
	for (int i = 0; i < source.size(); i++) {
		Variant value = source[i];
		changed = _erase_specialized_handles_for_container_type(p_element_type, value) || changed;
		erased[i] = value;
	}

	if (!changed) {
		return false;
	}

	r_value = erased;
	return true;
}

static bool _erase_specialized_handles_for_native_dictionary_entries(const ContainerType &p_key_type,
		const ContainerType &p_value_type, Variant &r_value) {
	if (r_value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	const Dictionary source = r_value;
	Dictionary erased;
	erased.reserve(source.size());

	bool changed = false;
	for (const KeyValue<Variant, Variant> &E : source) {
		Variant key = E.key;
		Variant value = E.value;
		changed = _erase_specialized_handles_for_container_type(p_key_type, key) || changed;
		changed = _erase_specialized_handles_for_container_type(p_value_type, value) || changed;
		erased[key] = value;
	}

	if (!changed) {
		return false;
	}

	r_value = erased;
	return true;
}

static bool _erase_specialized_handles_for_typed_array_argument(Variant *p_base, Variant &r_value) {
	if (p_base->get_type() != Variant::ARRAY) {
		return false;
	}

	Array *array = VariantInternal::get_array(p_base);
	if (!array->is_typed()) {
		return false;
	}

	return _erase_specialized_handles_for_native_array_elements(array->get_element_type(), r_value);
}

static bool _erase_specialized_handles_for_typed_dictionary_arguments(Variant *p_base, Variant &r_key, Variant &r_value) {
	if (p_base->get_type() != Variant::DICTIONARY) {
		return false;
	}

	Dictionary *dictionary = VariantInternal::get_dictionary(p_base);
	if (!dictionary->is_typed()) {
		return false;
	}

	const ContainerType key_type = dictionary->get_key_type();
	const ContainerType value_type = dictionary->get_value_type();
	bool changed = _erase_specialized_handles_for_container_type(key_type, r_key);
	changed = _erase_specialized_handles_for_container_type(value_type, r_value) || changed;
	return changed;
}

static bool _erase_specialized_handles_for_typed_container_set(Variant *p_base, Variant &r_key, Variant &r_value) {
	if (p_base->get_type() == Variant::ARRAY) {
		return _erase_specialized_handles_for_typed_array_argument(p_base, r_value);
	}

	return _erase_specialized_handles_for_typed_dictionary_arguments(p_base, r_key, r_value);
}

static bool _erase_specialized_handles_for_dictionary_call_argument(const StringName &p_method, int p_arg_index,
		const ContainerType &p_key_type, const ContainerType &p_value_type, Variant &r_arg) {
	if (p_method == SNAME("set")) {
		if (p_arg_index == 0) {
			return _erase_specialized_handles_for_container_type(p_key_type, r_arg);
		}
		if (p_arg_index == 1) {
			return _erase_specialized_handles_for_container_type(p_value_type, r_arg);
		}
		return false;
	}

	if (p_method == SNAME("get_or_add")) {
		if (p_arg_index == 0) {
			return _erase_specialized_handles_for_container_type(p_key_type, r_arg);
		}
		if (p_arg_index == 1) {
			return _erase_specialized_handles_for_container_type(p_value_type, r_arg);
		}
		return false;
	}

	if (p_method == SNAME("has") || p_method == SNAME("erase") || p_method == SNAME("get")) {
		return p_arg_index == 0 && _erase_specialized_handles_for_container_type(p_key_type, r_arg);
	}

	if (p_method == SNAME("has_all")) {
		return p_arg_index == 0 && _erase_specialized_handles_for_native_array_elements(p_key_type, r_arg);
	}

	if (p_method == SNAME("find_key")) {
		return p_arg_index == 0 && _erase_specialized_handles_for_container_type(p_value_type, r_arg);
	}

	if (p_method == SNAME("assign") || p_method == SNAME("merge") || p_method == SNAME("merged")) {
		return p_arg_index == 0 && _erase_specialized_handles_for_native_dictionary_entries(p_key_type, p_value_type, r_arg);
	}

	return false;
}

static const Variant **_erase_specialized_handles_for_typed_container_call(Variant *p_base, const StringName &p_method,
		Variant **p_args, int p_argcount, Vector<Variant> &r_arg_storage, Vector<const Variant *> &r_argptr_storage) {
	if (p_argcount == 0 || (p_base->get_type() != Variant::ARRAY && p_base->get_type() != Variant::DICTIONARY)) {
		return (const Variant **)p_args;
	}

	const Variant::Type base_type = p_base->get_type();
	bool changed = false;

	if (base_type == Variant::ARRAY) {
		Array *array = VariantInternal::get_array(p_base);
		const ContainerType element_type = array->get_element_type();
		if (!array->is_typed() || !_container_type_accepts_specialized_handle_erasure(element_type)) {
			return (const Variant **)p_args;
		}

		r_arg_storage.resize(p_argcount);
		r_argptr_storage.resize(p_argcount);

		Variant *args = r_arg_storage.ptrw();
		const Variant **argptrs = r_argptr_storage.ptrw();
		for (int i = 0; i < p_argcount; i++) {
			args[i] = *p_args[i];
			changed = _erase_specialized_handles_for_native_array_elements(element_type, args[i]) || changed;
			argptrs[i] = &args[i];
		}
	} else {
		Dictionary *dictionary = VariantInternal::get_dictionary(p_base);
		const ContainerType key_type = dictionary->get_key_type();
		const ContainerType value_type = dictionary->get_value_type();
		const bool can_erase_key = _container_type_accepts_specialized_handle_erasure(key_type);
		const bool can_erase_value = _container_type_accepts_specialized_handle_erasure(value_type);
		if (!dictionary->is_typed() || (!can_erase_key && !can_erase_value)) {
			return (const Variant **)p_args;
		}

		r_arg_storage.resize(p_argcount);
		r_argptr_storage.resize(p_argcount);

		Variant *args = r_arg_storage.ptrw();
		const Variant **argptrs = r_argptr_storage.ptrw();
		for (int i = 0; i < p_argcount; i++) {
			args[i] = *p_args[i];
			changed = _erase_specialized_handles_for_dictionary_call_argument(p_method, i, key_type, value_type, args[i]) || changed;
			argptrs[i] = &args[i];
		}
	}

	return changed ? r_argptr_storage.ptrw() : (const Variant **)p_args;
}

Variant FSFunction::_get_default_variant_for_data_type(const FSDataType &p_data_type) {
	if (p_data_type.kind == FSDataType::BUILTIN) {
		if (p_data_type.builtin_type == Variant::ARRAY) {
			Array array;
			// Typed array.
			if (p_data_type.has_container_element_type(0)) {
				const FSDataType &element_type = p_data_type.get_container_element_type(0);
				array.set_typed(element_type.to_container_type());
			}

			return array;
		} else if (p_data_type.builtin_type == Variant::DICTIONARY) {
			Dictionary dict;
			// Typed dictionary.
			if (p_data_type.has_container_element_types()) {
				const FSDataType &key_type = p_data_type.get_container_element_type_or_variant(0);
				const FSDataType &value_type = p_data_type.get_container_element_type_or_variant(1);
				dict.set_typed(key_type.to_container_type(), value_type.to_container_type());
			}

			return dict;
		} else {
			Callable::CallError ce;
			Variant variant;
			Variant::construct(p_data_type.builtin_type, variant, nullptr, 0, ce);

			ERR_FAIL_COND_V(ce.error != Callable::CallError::CALL_OK, Variant());

			return variant;
		}
	}

	return Variant();
}

String FSFunction::_get_call_error(const String &p_where, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const {
	switch (p_err.error) {
		case Callable::CallError::CALL_OK:
			return String();
		case Callable::CallError::CALL_ERROR_INVALID_METHOD:
			if (p_ret.get_type() == Variant::STRING && !p_ret.operator String().is_empty()) {
				return "Invalid call " + p_where + ": " + p_ret.operator String();
			}
			return "Invalid call. Nonexistent " + p_where + ".";
		case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
			ERR_FAIL_INDEX_V_MSG(p_err.argument, p_argcount, "Bug: Invalid call error argument index.", "Bug: Invalid call error argument index.");
			ERR_FAIL_NULL_V_MSG(p_argptrs[p_err.argument], "Bug: Argument is null pointer.", "Bug: Argument is null pointer.");
			// Handle the Object to Object case separately as we don't have further class details.
#ifdef DEBUG_ENABLED
			if (p_err.expected == Variant::OBJECT && p_argptrs[p_err.argument]->get_type() == p_err.expected) {
				return "Invalid type in " + p_where + ". The Object-derived class of argument " + itos(p_err.argument + 1) + " (" + _get_var_type(p_argptrs[p_err.argument]) + ") is not a subclass of the expected argument class.";
			}
			if (p_err.expected == Variant::ARRAY && p_argptrs[p_err.argument]->get_type() == p_err.expected) {
				return "Invalid type in " + p_where + ". The array of argument " + itos(p_err.argument + 1) + " (" + _get_var_type(p_argptrs[p_err.argument]) + ") does not have the same element type as the expected typed array argument.";
			}
			if (p_err.expected == Variant::DICTIONARY && p_argptrs[p_err.argument]->get_type() == p_err.expected) {
				return "Invalid type in " + p_where + ". The dictionary of argument " + itos(p_err.argument + 1) + " (" + _get_var_type(p_argptrs[p_err.argument]) + ") does not have the same element type as the expected typed dictionary argument.";
			}
#endif // DEBUG_ENABLED
			return "Invalid type in " + p_where + ". Cannot convert argument " + itos(p_err.argument + 1) + " from " + Variant::get_type_name(p_argptrs[p_err.argument]->get_type()) + " to " + Variant::get_type_name(Variant::Type(p_err.expected)) + ".";
		case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
		case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
			return "Invalid call to " + p_where + ". Expected " + itos(p_err.expected) + " argument(s).";
		case Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL:
			return "Attempt to call " + p_where + " on a null instance.";
		case Callable::CallError::CALL_ERROR_METHOD_NOT_CONST:
			return "Attempt to call " + p_where + " on a const instance.";
	}
	return "Bug: Invalid call error code " + itos(p_err.error) + ".";
}

String FSFunction::_get_callable_call_error(const String &p_where, const Callable &p_callable, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const {
	Vector<Variant> binds;
	p_callable.get_bound_arguments_ref(binds);

	int args_unbound = p_callable.get_unbound_arguments_count();

	if (p_argcount - args_unbound < 0) {
		return "Callable unbinds " + itos(args_unbound) + " arguments, but called with " + itos(p_argcount);
	} else {
		Vector<const Variant *> argptrs;
		argptrs.resize(p_argcount - args_unbound + binds.size());
		for (int i = 0; i < p_argcount - args_unbound; i++) {
			argptrs.write[i] = p_argptrs[i];
		}
		for (int i = 0; i < binds.size(); i++) {
			argptrs.write[i + p_argcount - args_unbound] = &binds[i];
		}
		return _get_call_error(p_where, (const Variant **)argptrs.ptr(), argptrs.size(), p_ret, p_err);
	}
}

void (*type_init_function_table[])(Variant *) = {
	nullptr, // NIL (shouldn't be called).
	&VariantInitializer<bool>::init, // BOOL.
	&VariantInitializer<int64_t>::init, // INT.
	&VariantInitializer<double>::init, // FLOAT.
	&VariantInitializer<String>::init, // STRING.
	&VariantInitializer<Vector2>::init, // VECTOR2.
	&VariantInitializer<Vector2i>::init, // VECTOR2I.
	&VariantInitializer<Rect2>::init, // RECT2.
	&VariantInitializer<Rect2i>::init, // RECT2I.
	&VariantInitializer<Vector3>::init, // VECTOR3.
	&VariantInitializer<Vector3i>::init, // VECTOR3I.
	&VariantInitializer<Transform2D>::init, // TRANSFORM2D.
	&VariantInitializer<Vector4>::init, // VECTOR4.
	&VariantInitializer<Vector4i>::init, // VECTOR4I.
	&VariantInitializer<Plane>::init, // PLANE.
	&VariantInitializer<Quaternion>::init, // QUATERNION.
	&VariantInitializer<AABB>::init, // AABB.
	&VariantInitializer<Basis>::init, // BASIS.
	&VariantInitializer<Transform3D>::init, // TRANSFORM3D.
	&VariantInitializer<Projection>::init, // PROJECTION.
	&VariantInitializer<Color>::init, // COLOR.
	&VariantInitializer<StringName>::init, // STRING_NAME.
	&VariantInitializer<NodePath>::init, // NODE_PATH.
	&VariantInitializer<RID>::init, // RID.
	&VariantInitializer<Object *>::init, // OBJECT.
	&VariantInitializer<Callable>::init, // CALLABLE.
	&VariantInitializer<Signal>::init, // SIGNAL.
	&VariantInitializer<Dictionary>::init, // DICTIONARY.
	&VariantInitializer<Array>::init, // ARRAY.
	&VariantInitializer<PackedByteArray>::init, // PACKED_BYTE_ARRAY.
	&VariantInitializer<PackedInt32Array>::init, // PACKED_INT32_ARRAY.
	&VariantInitializer<PackedInt64Array>::init, // PACKED_INT64_ARRAY.
	&VariantInitializer<PackedFloat32Array>::init, // PACKED_FLOAT32_ARRAY.
	&VariantInitializer<PackedFloat64Array>::init, // PACKED_FLOAT64_ARRAY.
	&VariantInitializer<PackedStringArray>::init, // PACKED_STRING_ARRAY.
	&VariantInitializer<PackedVector2Array>::init, // PACKED_VECTOR2_ARRAY.
	&VariantInitializer<PackedVector3Array>::init, // PACKED_VECTOR3_ARRAY.
	&VariantInitializer<PackedColorArray>::init, // PACKED_COLOR_ARRAY.
	&VariantInitializer<PackedVector4Array>::init, // PACKED_VECTOR4_ARRAY.
	&VariantUIntInitializer::init, // UINT.
};

#if defined(__GNUC__) || defined(__clang__)
#define OPCODES_TABLE                                    \
	static const void *switch_table_ops[] = {            \
		&&OPCODE_OPERATOR,                               \
		&&OPCODE_OPERATOR_VALIDATED,                     \
		&&OPCODE_TYPE_TEST_BUILTIN,                      \
		&&OPCODE_TYPE_TEST_ARRAY,                        \
		&&OPCODE_TYPE_TEST_DICTIONARY,                   \
		&&OPCODE_TYPE_TEST_TUPLE,                        \
		&&OPCODE_TYPE_TEST_ENUM,                         \
		&&OPCODE_TYPE_TEST_ENUM_CASE,                    \
		&&OPCODE_TYPE_TEST_NATIVE,                       \
		&&OPCODE_TYPE_TEST_SCRIPT,                       \
		&&OPCODE_SET_KEYED,                              \
		&&OPCODE_SET_KEYED_VALIDATED,                    \
		&&OPCODE_SET_INDEXED_VALIDATED,                  \
		&&OPCODE_GET_KEYED,                              \
		&&OPCODE_GET_KEYED_VALIDATED,                    \
		&&OPCODE_GET_INDEXED_VALIDATED,                  \
		&&OPCODE_SET_NAMED,                              \
		&&OPCODE_SET_NAMED_VALIDATED,                    \
		&&OPCODE_GET_NAMED,                              \
		&&OPCODE_GET_NAMED_VALIDATED,                    \
		&&OPCODE_SET_MEMBER,                             \
		&&OPCODE_GET_MEMBER,                             \
		&&OPCODE_GET_TYPE_PARAMETER,                     \
		&&OPCODE_SET_STATIC_VARIABLE,                    \
		&&OPCODE_GET_STATIC_VARIABLE,                    \
		&&OPCODE_ASSIGN,                                 \
		&&OPCODE_ASSIGN_NULL,                            \
		&&OPCODE_ASSIGN_TRUE,                            \
		&&OPCODE_ASSIGN_FALSE,                           \
		&&OPCODE_ASSIGN_TYPED_BUILTIN,                   \
		&&OPCODE_ASSIGN_TYPED_ARRAY,                     \
		&&OPCODE_ASSIGN_TYPED_DICTIONARY,                \
		&&OPCODE_ASSIGN_TYPED_NATIVE,                    \
		&&OPCODE_ASSIGN_TYPED_SCRIPT,                    \
		&&OPCODE_ASSIGN_TYPED_PARAMETER,                 \
		&&OPCODE_ASSIGN_TYPED_ARRAY_CONVERT,             \
		&&OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT,        \
		&&OPCODE_CAST_TO_BUILTIN,                        \
		&&OPCODE_CAST_TO_NATIVE,                         \
		&&OPCODE_CAST_TO_SCRIPT,                         \
		&&OPCODE_CONSTRUCT,                              \
		&&OPCODE_CONSTRUCT_VALIDATED,                    \
		&&OPCODE_CONSTRUCT_ARRAY,                        \
		&&OPCODE_CONSTRUCT_TYPED_ARRAY,                  \
		&&OPCODE_CONSTRUCT_TUPLE,                        \
		&&OPCODE_CONSTRUCT_DICTIONARY,                   \
		&&OPCODE_CONSTRUCT_TYPED_DICTIONARY,             \
		&&OPCODE_CONSTRUCT_SPECIALIZED,                  \
		&&OPCODE_CALL,                                   \
		&&OPCODE_CALL_RETURN,                            \
		&&OPCODE_CALL_ASYNC,                             \
		&&OPCODE_CALL_ENUM,                              \
		&&OPCODE_CALL_ENUM_RETURN,                       \
		&&OPCODE_CALL_ENUM_ASYNC,                        \
		&&OPCODE_CALL_UTILITY,                           \
		&&OPCODE_CALL_UTILITY_VALIDATED,                 \
		&&OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY,            \
		&&OPCODE_CALL_BUILTIN_TYPE_VALIDATED,            \
		&&OPCODE_CALL_SELF_BASE,                         \
		&&OPCODE_CALL_METHOD_BIND,                       \
		&&OPCODE_CALL_METHOD_BIND_RET,                   \
		&&OPCODE_CALL_BUILTIN_STATIC,                    \
		&&OPCODE_CALL_NATIVE_STATIC,                     \
		&&OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN,    \
		&&OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN, \
		&&OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN,      \
		&&OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN,   \
		&&OPCODE_AWAIT,                                  \
		&&OPCODE_AWAIT_RESUME,                           \
		&&OPCODE_CREATE_LAMBDA,                          \
		&&OPCODE_CREATE_SELF_LAMBDA,                     \
		&&OPCODE_JUMP,                                   \
		&&OPCODE_JUMP_IF,                                \
		&&OPCODE_JUMP_IF_NOT,                            \
		&&OPCODE_JUMP_TO_DEF_ARGUMENT,                   \
		&&OPCODE_JUMP_IF_SHARED,                         \
		&&OPCODE_RETURN,                                 \
		&&OPCODE_RETURN_TYPED_BUILTIN,                   \
		&&OPCODE_RETURN_TYPED_ARRAY,                     \
		&&OPCODE_RETURN_TYPED_DICTIONARY,                \
		&&OPCODE_RETURN_TYPED_NATIVE,                    \
		&&OPCODE_RETURN_TYPED_SCRIPT,                    \
		&&OPCODE_ITERATE_BEGIN,                          \
		&&OPCODE_ITERATE_BEGIN_INT,                      \
		&&OPCODE_ITERATE_BEGIN_FLOAT,                    \
		&&OPCODE_ITERATE_BEGIN_VECTOR2,                  \
		&&OPCODE_ITERATE_BEGIN_VECTOR2I,                 \
		&&OPCODE_ITERATE_BEGIN_VECTOR3,                  \
		&&OPCODE_ITERATE_BEGIN_VECTOR3I,                 \
		&&OPCODE_ITERATE_BEGIN_STRING,                   \
		&&OPCODE_ITERATE_BEGIN_DICTIONARY,               \
		&&OPCODE_ITERATE_BEGIN_ARRAY,                    \
		&&OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY,        \
		&&OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY,       \
		&&OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY,       \
		&&OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY,     \
		&&OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY,     \
		&&OPCODE_ITERATE_BEGIN_PACKED_STRING_ARRAY,      \
		&&OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY,     \
		&&OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY,     \
		&&OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY,       \
		&&OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY,     \
		&&OPCODE_ITERATE_BEGIN_OBJECT,                   \
		&&OPCODE_ITERATE_BEGIN_RANGE,                    \
		&&OPCODE_ITERATE,                                \
		&&OPCODE_ITERATE_INT,                            \
		&&OPCODE_ITERATE_FLOAT,                          \
		&&OPCODE_ITERATE_VECTOR2,                        \
		&&OPCODE_ITERATE_VECTOR2I,                       \
		&&OPCODE_ITERATE_VECTOR3,                        \
		&&OPCODE_ITERATE_VECTOR3I,                       \
		&&OPCODE_ITERATE_STRING,                         \
		&&OPCODE_ITERATE_DICTIONARY,                     \
		&&OPCODE_ITERATE_ARRAY,                          \
		&&OPCODE_ITERATE_PACKED_BYTE_ARRAY,              \
		&&OPCODE_ITERATE_PACKED_INT32_ARRAY,             \
		&&OPCODE_ITERATE_PACKED_INT64_ARRAY,             \
		&&OPCODE_ITERATE_PACKED_FLOAT32_ARRAY,           \
		&&OPCODE_ITERATE_PACKED_FLOAT64_ARRAY,           \
		&&OPCODE_ITERATE_PACKED_STRING_ARRAY,            \
		&&OPCODE_ITERATE_PACKED_VECTOR2_ARRAY,           \
		&&OPCODE_ITERATE_PACKED_VECTOR3_ARRAY,           \
		&&OPCODE_ITERATE_PACKED_COLOR_ARRAY,             \
		&&OPCODE_ITERATE_PACKED_VECTOR4_ARRAY,           \
		&&OPCODE_ITERATE_OBJECT,                         \
		&&OPCODE_ITERATE_RANGE,                          \
		&&OPCODE_STORE_GLOBAL,                           \
		&&OPCODE_STORE_NAMED_GLOBAL,                     \
		&&OPCODE_TYPE_ADJUST_BOOL,                       \
		&&OPCODE_TYPE_ADJUST_INT,                        \
		&&OPCODE_TYPE_ADJUST_FLOAT,                      \
		&&OPCODE_TYPE_ADJUST_STRING,                     \
		&&OPCODE_TYPE_ADJUST_VECTOR2,                    \
		&&OPCODE_TYPE_ADJUST_VECTOR2I,                   \
		&&OPCODE_TYPE_ADJUST_RECT2,                      \
		&&OPCODE_TYPE_ADJUST_RECT2I,                     \
		&&OPCODE_TYPE_ADJUST_VECTOR3,                    \
		&&OPCODE_TYPE_ADJUST_VECTOR3I,                   \
		&&OPCODE_TYPE_ADJUST_TRANSFORM2D,                \
		&&OPCODE_TYPE_ADJUST_VECTOR4,                    \
		&&OPCODE_TYPE_ADJUST_VECTOR4I,                   \
		&&OPCODE_TYPE_ADJUST_PLANE,                      \
		&&OPCODE_TYPE_ADJUST_QUATERNION,                 \
		&&OPCODE_TYPE_ADJUST_AABB,                       \
		&&OPCODE_TYPE_ADJUST_BASIS,                      \
		&&OPCODE_TYPE_ADJUST_TRANSFORM3D,                \
		&&OPCODE_TYPE_ADJUST_PROJECTION,                 \
		&&OPCODE_TYPE_ADJUST_COLOR,                      \
		&&OPCODE_TYPE_ADJUST_STRING_NAME,                \
		&&OPCODE_TYPE_ADJUST_NODE_PATH,                  \
		&&OPCODE_TYPE_ADJUST_RID,                        \
		&&OPCODE_TYPE_ADJUST_OBJECT,                     \
		&&OPCODE_TYPE_ADJUST_CALLABLE,                   \
		&&OPCODE_TYPE_ADJUST_SIGNAL,                     \
		&&OPCODE_TYPE_ADJUST_DICTIONARY,                 \
		&&OPCODE_TYPE_ADJUST_ARRAY,                      \
		&&OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY,          \
		&&OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY,         \
		&&OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY,         \
		&&OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY,       \
		&&OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY,       \
		&&OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY,        \
		&&OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY,       \
		&&OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY,       \
		&&OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY,         \
		&&OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY,       \
		&&OPCODE_ASSERT,                                 \
		&&OPCODE_BREAKPOINT,                             \
		&&OPCODE_LINE,                                   \
		&&OPCODE_END                                     \
	};                                                   \
	static_assert(std_size(switch_table_ops) == (OPCODE_END + 1), "Opcodes in jump table aren't the same as opcodes in enum.");

#define OPCODE(m_op) \
	m_op:
#define OPCODE_WHILE(m_test)
#define OPCODES_END \
	OPSEXIT:
#define OPCODES_OUT \
	OPSOUT:
#define OPCODE_SWITCH(m_test) goto *switch_table_ops[m_test];

#ifdef DEBUG_ENABLED
#define DISPATCH_OPCODE          \
	last_opcode = _code_ptr[ip]; \
	goto *switch_table_ops[last_opcode]
#else // !DEBUG_ENABLED
#define DISPATCH_OPCODE goto *switch_table_ops[_code_ptr[ip]]
#endif // DEBUG_ENABLED

#define OPCODE_BREAK goto OPSEXIT
#define OPCODE_OUT goto OPSOUT
#else // !(defined(__GNUC__) || defined(__clang__))
#define OPCODES_TABLE
#define OPCODE(m_op) case m_op:
#define OPCODE_WHILE(m_test) while (m_test)
#define OPCODES_END
#define OPCODES_OUT
#define DISPATCH_OPCODE continue

#ifdef _MSC_VER
#define OPCODE_SWITCH(m_test)       \
	__assume(m_test <= OPCODE_END); \
	switch (m_test)
#else // !_MSC_VER
#define OPCODE_SWITCH(m_test) switch (m_test)
#endif // _MSC_VER

#define OPCODE_BREAK break
#define OPCODE_OUT break
#endif // defined(__GNUC__) || defined(__clang__)

// Helpers for VariantInternal methods in macros.
#define OP_GET_BOOL get_bool
#define OP_GET_INT get_int
#define OP_GET_FLOAT get_float
#define OP_GET_VECTOR2 get_vector2
#define OP_GET_VECTOR2I get_vector2i
#define OP_GET_VECTOR3 get_vector3
#define OP_GET_VECTOR3I get_vector3i
#define OP_GET_RECT2 get_rect2
#define OP_GET_VECTOR4 get_vector4
#define OP_GET_VECTOR4I get_vector4i
#define OP_GET_RECT2I get_rect2i
#define OP_GET_QUATERNION get_quaternion
#define OP_GET_COLOR get_color
#define OP_GET_STRING get_string
#define OP_GET_STRING_NAME get_string_name
#define OP_GET_NODE_PATH get_node_path
#define OP_GET_CALLABLE get_callable
#define OP_GET_SIGNAL get_signal
#define OP_GET_ARRAY get_array
#define OP_GET_DICTIONARY get_dictionary
#define OP_GET_PACKED_BYTE_ARRAY get_byte_array
#define OP_GET_PACKED_INT32_ARRAY get_int32_array
#define OP_GET_PACKED_INT64_ARRAY get_int64_array
#define OP_GET_PACKED_FLOAT32_ARRAY get_float32_array
#define OP_GET_PACKED_FLOAT64_ARRAY get_float64_array
#define OP_GET_PACKED_STRING_ARRAY get_string_array
#define OP_GET_PACKED_VECTOR2_ARRAY get_vector2_array
#define OP_GET_PACKED_VECTOR3_ARRAY get_vector3_array
#define OP_GET_PACKED_COLOR_ARRAY get_color_array
#define OP_GET_PACKED_VECTOR4_ARRAY get_vector4_array
#define OP_GET_TRANSFORM3D get_transform
#define OP_GET_TRANSFORM2D get_transform2d
#define OP_GET_PROJECTION get_projection
#define OP_GET_PLANE get_plane
#define OP_GET_AABB get_aabb
#define OP_GET_BASIS get_basis
#define OP_GET_RID get_rid

#define METHOD_CALL_ON_NULL_VALUE_ERROR(method_pointer) "Cannot call method '" + (method_pointer)->get_name() + "' on a null value."
#define METHOD_CALL_ON_FREED_INSTANCE_ERROR(method_pointer) "Cannot call method '" + (method_pointer)->get_name() + "' on a previously freed instance."

Variant FSFunction::call_witness(const Variant &p_self, const Variant **p_args, int p_argcount, Callable::CallError &r_err) {
	return call(nullptr, p_args, p_argcount, r_err, nullptr, &p_self);
}

Variant FSFunction::call(FSInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_err, CallState *p_state, const Variant *p_self_override, const FSStaticSelfContext *p_static_self) {
	FoundryProfileZoneScript(this, source, name, name, _initial_line);

	OPCODES_TABLE;

	if (!_code_ptr) {
		return _get_default_variant_for_data_type(return_type);
	}

	r_err.error = Callable::CallError::CALL_OK;

	static thread_local int call_depth = 0;
	if (unlikely(++call_depth > MAX_CALL_DEPTH)) {
		call_depth--;
#ifdef DEBUG_ENABLED
		String err_file;
		if (p_instance && ObjectDB::get_instance(p_instance->owner_id) != nullptr && p_instance->script->is_valid() && !p_instance->script->path.is_empty()) {
			err_file = p_instance->script->path;
		} else if (_script) {
			err_file = _script->path;
		}
		if (err_file.is_empty()) {
			err_file = "<built-in>";
		}
		String err_func = name;
		if (p_instance && ObjectDB::get_instance(p_instance->owner_id) != nullptr && p_instance->script->is_valid() && p_instance->script->local_name != StringName()) {
			err_func = p_instance->script->local_name.operator String() + "." + err_func;
		}
		int err_line = _initial_line;
		const char *err_text = "Stack overflow. Check for infinite recursion in your script.";
		_err_print_error(err_func.utf8().get_data(), err_file.utf8().get_data(), err_line, err_text, false, ERR_HANDLER_SCRIPT);
		FSLanguage::get_singleton()->debug_break(err_text, false);
#endif
		return _get_default_variant_for_data_type(return_type);
	}

	Variant retvalue;
	Variant *stack = nullptr;
	Variant **instruction_args = nullptr;
	int defarg = 0;

	uint32_t alloca_size = 0;
	FoundryScript *script;
	int ip = 0;
	int line = _initial_line;

	if (p_state) {
		// Use existing (supplied) state (awaited).
		stack = (Variant *)p_state->stack.ptr();
		instruction_args = (Variant **)&p_state->stack.ptr()[sizeof(Variant) * p_state->stack_size]; // `ptr()` to avoid bounds check.
		line = p_state->line;
		ip = p_state->ip;
		alloca_size = p_state->stack.size();
		script = p_state->script;
		p_instance = p_state->instance;
		defarg = p_state->defarg;
		if (p_state->has_self_override) {
			p_self_override = &p_state->self_override;
		}
		if (p_state->static_self.is_valid()) {
			// The suspended frame kept its receiver, so resumption cannot come back with an absent or
			// different specialization.
			p_static_self = &p_state->static_self;
		}

		// Responsibility for the stack is moved from `FSFunctionState` to this method. Reset
		// `stack_size` so `_clear_stack()` does not destroy the same slots again after this call
		// finishes (including the resumed-then-awaited-again path).
		p_state->stack_size = 0;

	} else {
		if (p_argcount != _argument_count) {
			if (p_argcount > _argument_count) {
				if (!is_vararg()) {
					r_err.error = Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
					r_err.expected = _argument_count;
					call_depth--;
					return _get_default_variant_for_data_type(return_type);
				}
			} else if (p_argcount < _argument_count - _default_arg_count) {
				r_err.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
				r_err.expected = _argument_count - _default_arg_count;
				call_depth--;
				return _get_default_variant_for_data_type(return_type);
			} else {
				defarg = _argument_count - p_argcount;
			}
		}

		alloca_size = sizeof(Variant *) * FIXED_ADDRESSES_MAX + sizeof(Variant *) * _instruction_args_size + sizeof(Variant) * _stack_size;

		uint8_t *aptr = (uint8_t *)alloca(alloca_size);
		stack = (Variant *)aptr;

		const int non_vararg_arg_count = MIN(p_argcount, _argument_count);
		for (int i = 0; i < non_vararg_arg_count; i++) {
			if (!argument_types[i].has_type()) {
				memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(*p_args[i]));
				continue;
			}
			if (!argument_types[i].is_type_handle && argument_types[i].kind == FSDataType::NATIVE) {
				FSSpecializedClassHandle *specialized_handle =
						_specialized_handle_assignable_to_native_script(p_args[i], argument_types[i].native_type);
				if (specialized_handle != nullptr) {
					memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(specialized_handle->get_specialized_script()));
					continue;
				}
			}
			// If types already match, don't call Variant::construct(). Constructors of some types
			// (e.g. packed arrays) do copies, whereas they pass by reference when inside a Variant.
			if (argument_types[i].is_type(*p_args[i], false)) {
				memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(*p_args[i]));
				continue;
			}
			if (!argument_types[i].is_type(*p_args[i], true)) {
				r_err.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
				r_err.argument = i;
				r_err.expected = argument_types[i].builtin_type;
				call_depth--;
				return _get_default_variant_for_data_type(return_type);
			}
			if (argument_types[i].kind == FSDataType::BUILTIN) {
				if (argument_types[i].builtin_type == Variant::DICTIONARY && argument_types[i].has_container_element_types()) {
					const FSDataType &arg_key_type = argument_types[i].get_container_element_type_or_variant(0);
					const FSDataType &arg_value_type = argument_types[i].get_container_element_type_or_variant(1);
					Dictionary dict(p_args[i]->operator Dictionary(), arg_key_type.to_container_type(), arg_value_type.to_container_type());
					memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(dict));
				} else if (argument_types[i].builtin_type == Variant::ARRAY && argument_types[i].has_container_element_type(0)) {
					const FSDataType &arg_type = argument_types[i].container_element_types[0];
					Array array(p_args[i]->operator Array(), arg_type.to_container_type());
					memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(array));
				} else {
					Variant variant;
					Variant::construct(argument_types[i].builtin_type, variant, &p_args[i], 1, r_err);
					if (unlikely(r_err.error)) {
						r_err.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
						r_err.argument = i;
						r_err.expected = argument_types[i].builtin_type;
						call_depth--;
						return _get_default_variant_for_data_type(return_type);
					}
					memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(variant));
				}
			} else {
				memnew_placement(&stack[i + FIXED_ADDRESSES_MAX], Variant(*p_args[i]));
			}
		}
		for (int i = non_vararg_arg_count + FIXED_ADDRESSES_MAX; i < _stack_size; i++) {
			memnew_placement(&stack[i], Variant);
		}

		if (is_vararg()) {
			Array vararg;
			stack[_vararg_index] = vararg;
			if (p_argcount > _argument_count) {
				vararg.resize(p_argcount - _argument_count);
				for (int i = 0; i < p_argcount - _argument_count; i++) {
					vararg[i] = *p_args[i + _argument_count];
				}
			}
		}

		if (_instruction_args_size) {
			instruction_args = (Variant **)&aptr[sizeof(Variant) * _stack_size];
		} else {
			instruction_args = nullptr;
		}

		for (const KeyValue<int, Variant::Type> &E : temporary_slots) {
			type_init_function_table[E.value](&stack[E.key]);
		}
	}

	if (p_instance) {
		memnew_placement(&stack[ADDR_STACK_SELF], Variant(p_instance->owner));
		script = p_instance->script.ptr();
	} else if (p_self_override != nullptr) {
		// Retroactive-conformance witness dispatched on a receiver with no FSInstance (native object or
		// builtin value): bind `self` to the receiver; the script context stays the witness's own script.
		memnew_placement(&stack[ADDR_STACK_SELF], Variant(*p_self_override));
		script = _script;
	} else {
		memnew_placement(&stack[ADDR_STACK_SELF], Variant);
		script = _script;
	}
	memnew_placement(&stack[ADDR_STACK_CLASS], Variant(script));
	memnew_placement(&stack[ADDR_STACK_NIL], Variant);

	// The receiver descriptor belongs to this frame alone. The guard restores the caller's descriptor
	// on every exit path, including the one that suspends this frame into an `FSFunctionState`.
	StaticSelfContextGuard static_self_guard(p_static_self);

	String err_text;

	FSLanguage::CallLevel call_level;
	FSLanguage::get_singleton()->enter_function(&call_level, p_instance, this, stack, &ip, &line);

#ifdef DEBUG_ENABLED
#define GD_ERR_BREAK(m_cond)                                                                                           \
	{                                                                                                                  \
		if (unlikely(m_cond)) {                                                                                        \
			_err_print_error(FUNCTION_STR, __FILE__, __LINE__, "Condition ' " _STR(m_cond) " ' is true. Breaking..:"); \
			OPCODE_BREAK;                                                                                              \
		}                                                                                                              \
	}

#define CHECK_SPACE(m_space) \
	GD_ERR_BREAK((ip + m_space) > _code_size)

#define GET_VARIANT_PTR(m_v, m_code_ofs)                                                            \
	Variant *m_v;                                                                                   \
	{                                                                                               \
		int address = _code_ptr[ip + 1 + (m_code_ofs)];                                             \
		int address_type = (address & ADDR_TYPE_MASK) >> ADDR_BITS;                                 \
		if (unlikely(address_type < 0 || address_type >= ADDR_TYPE_MAX)) {                          \
			err_text = "Bad address type.";                                                         \
			OPCODE_BREAK;                                                                           \
		}                                                                                           \
		int address_index = address & ADDR_MASK;                                                    \
		if (unlikely(address_index < 0 || address_index >= variant_address_limits[address_type])) { \
			if (address_type == ADDR_TYPE_MEMBER && !p_instance) {                                  \
				err_text = "Cannot access member without instance.";                                \
			} else {                                                                                \
				err_text = "Bad address index.";                                                    \
			}                                                                                       \
			OPCODE_BREAK;                                                                           \
		}                                                                                           \
		m_v = &variant_addresses[address_type][address_index];                                      \
		if (unlikely(!m_v))                                                                         \
			OPCODE_BREAK;                                                                           \
	}

#else // !DEBUG_ENABLED
#define GD_ERR_BREAK(m_cond)
#define CHECK_SPACE(m_space)

#define GET_VARIANT_PTR(m_v, m_code_ofs)                                                        \
	Variant *m_v;                                                                               \
	{                                                                                           \
		int address = _code_ptr[ip + 1 + (m_code_ofs)];                                         \
		m_v = &variant_addresses[(address & ADDR_TYPE_MASK) >> ADDR_BITS][address & ADDR_MASK]; \
		if (unlikely(!m_v))                                                                     \
			OPCODE_BREAK;                                                                       \
	}

#endif // DEBUG_ENABLED

#define LOAD_INSTRUCTION_ARGS                   \
	int instr_arg_count = _code_ptr[ip + 1];    \
	for (int i = 0; i < instr_arg_count; i++) { \
		GET_VARIANT_PTR(v, i + 1);              \
		instruction_args[i] = v;                \
	}                                           \
	ip += 1; // Offset to skip instruction argcount.

#define GET_INSTRUCTION_ARG(m_v, m_idx) \
	Variant *m_v = instruction_args[m_idx]

#ifdef DEBUG_ENABLED
	uint64_t function_start_time = 0;
	uint64_t function_call_time = 0;

	if (FSLanguage::get_singleton()->profiling) {
		function_start_time = OS::get_singleton()->get_ticks_usec();
		function_call_time = 0;
		profile.call_count.increment();
		profile.frame_call_count.increment();
	}
	bool exit_ok = false;
	int variant_address_limits[ADDR_TYPE_MAX] = { _stack_size, _constant_count, p_instance ? (int)p_instance->members.size() : 0 };
#endif

	bool awaited = false;
	Variant *variant_addresses[ADDR_TYPE_MAX] = { stack, _constants_ptr, p_instance ? p_instance->members.ptrw() : nullptr };

#ifdef DEBUG_ENABLED
	OPCODE_WHILE(ip < _code_size) {
		int last_opcode = _code_ptr[ip];
#else
	OPCODE_WHILE(true) {
#endif

		OPCODE_SWITCH(_code_ptr[ip]) {
			OPCODE(OPCODE_OPERATOR) {
				constexpr int _pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(*_code_ptr);
				CHECK_SPACE(7 + _pointer_size);

				bool valid;
				Variant::Operator op = (Variant::Operator)_code_ptr[ip + 4];
				GD_ERR_BREAK(op >= Variant::OP_MAX);

				GET_VARIANT_PTR(a, 0);
				GET_VARIANT_PTR(b, 1);
				GET_VARIANT_PTR(dst, 2);
				// Compute signatures (types of operands) so it can be optimized when matching.
				uint32_t op_signature = _code_ptr[ip + 5];
				uint32_t actual_signature = (a->get_type() << 8) | (b->get_type());

#ifdef DEBUG_ENABLED
				if (op == Variant::OP_DIVIDE || op == Variant::OP_MODULE) {
					// Don't optimize division and modulo since there's not check for division by zero with validated calls.
					op_signature = 0xFFFF;
					_code_ptr[ip + 5] = op_signature;
				}
#endif

				// Check if this is the first run. If so, store the current signature for the optimized path.
				if (unlikely(op_signature == 0)) {
					static Mutex initializer_mutex;
					initializer_mutex.lock();
					Variant::Type a_type = (Variant::Type)((actual_signature >> 8) & 0xFF);
					Variant::Type b_type = (Variant::Type)(actual_signature & 0xFF);

					Variant::ValidatedOperatorEvaluator op_func = Variant::get_validated_operator_evaluator(op, a_type, b_type);

					if (unlikely(!op_func)) {
						err_text = "Invalid operands '" + Variant::get_type_name(a->get_type()) + "' and '" + Variant::get_type_name(b->get_type()) + "' in operator '" + Variant::get_operator_name(op) + "'.";
						initializer_mutex.unlock();
						OPCODE_BREAK;
					} else {
						Variant::Type ret_type = Variant::get_operator_return_type(op, a_type, b_type);
						VariantInternal::initialize(dst, ret_type);
						op_func(a, b, dst);

						// Check again in case another thread already set it.
						if (_code_ptr[ip + 5] == 0) {
							_code_ptr[ip + 5] = actual_signature;
							_code_ptr[ip + 6] = static_cast<int>(ret_type);
							Variant::ValidatedOperatorEvaluator *tmp = reinterpret_cast<Variant::ValidatedOperatorEvaluator *>(&_code_ptr[ip + 7]);
							*tmp = op_func;
						}
					}
					initializer_mutex.unlock();
				} else if (likely(op_signature == actual_signature)) {
					// Re-resolve the validated evaluator instead of reading the cached function
					// pointer from bytecode so concurrent initialization cannot observe a torn write.
					Variant::Type a_type = (Variant::Type)((actual_signature >> 8) & 0xFF);
					Variant::Type b_type = (Variant::Type)(actual_signature & 0xFF);
					Variant::ValidatedOperatorEvaluator op_func = Variant::get_validated_operator_evaluator(op, a_type, b_type);
					if (unlikely(!op_func)) {
						err_text = "Invalid operands '" + Variant::get_type_name(a->get_type()) + "' and '" + Variant::get_type_name(b->get_type()) + "' in operator '" + Variant::get_operator_name(op) + "'.";
						OPCODE_BREAK;
					}
					Variant::Type ret_type = Variant::get_operator_return_type(op, a_type, b_type);
					VariantInternal::initialize(dst, ret_type);
					op_func(a, b, dst);
				} else {
					// If the signature doesn't match, we have to use the slow path.
					Variant ret;
					Variant::evaluate(op, *a, *b, ret, valid);
					if (!valid) {
						if (ret.get_type() == Variant::STRING) {
							err_text = ret;
							err_text += " in operator '" + Variant::get_operator_name(op) + "'.";
						} else {
							err_text = "Invalid operands '" + Variant::get_type_name(a->get_type()) + "' and '" + Variant::get_type_name(b->get_type()) + "' in operator '" + Variant::get_operator_name(op) + "'.";
						}
						OPCODE_BREAK;
					}
					*dst = ret;
				}
				ip += 7 + _pointer_size;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_OPERATOR_VALIDATED) {
				CHECK_SPACE(5);

				int operator_idx = _code_ptr[ip + 4];
				GD_ERR_BREAK(operator_idx < 0 || operator_idx >= _operator_funcs_count);
				Variant::ValidatedOperatorEvaluator operator_func = _operator_funcs_ptr[operator_idx];

				GET_VARIANT_PTR(a, 0);
				GET_VARIANT_PTR(b, 1);
				GET_VARIANT_PTR(dst, 2);

				operator_func(a, b, dst);

				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_BUILTIN) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				int builtin_type_operand = _code_ptr[ip + 3];
				const bool is_nullable = builtin_type_operand & FSFunction::NULLABLE_TYPE_OPERAND_FLAG;
				Variant::Type builtin_type = (Variant::Type)(builtin_type_operand & ~FSFunction::NULLABLE_TYPE_OPERAND_FLAG);
				GD_ERR_BREAK(builtin_type < 0 || builtin_type >= Variant::VARIANT_MAX);

				*dst = value->get_type() == builtin_type || (is_nullable && value->get_type() == Variant::NIL);
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_ARRAY) {
				CHECK_SPACE(6);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				GET_VARIANT_PTR(type_info, 2);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 4];
				int native_type_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const ContainerType expected_type = _container_type_from_type_info(*type_info, builtin_type, native_type);

				bool result = false;
				if (value->get_type() == Variant::ARRAY) {
					Array *array = VariantInternal::get_array(value);
					result = array->get_element_type() == expected_type;
				}

				*dst = result;
				ip += 6;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_DICTIONARY) {
				CHECK_SPACE(9);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				GET_VARIANT_PTR(key_type_info, 2);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 5];
				int key_native_type_idx = _code_ptr[ip + 6];
				GD_ERR_BREAK(key_native_type_idx < 0 || key_native_type_idx >= _global_names_count);
				const StringName key_native_type = _global_names_ptr[key_native_type_idx];
				const ContainerType expected_key_type = _container_type_from_type_info(*key_type_info, key_builtin_type, key_native_type);

				GET_VARIANT_PTR(value_type_info, 3);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 7];
				int value_native_type_idx = _code_ptr[ip + 8];
				GD_ERR_BREAK(value_native_type_idx < 0 || value_native_type_idx >= _global_names_count);
				const StringName value_native_type = _global_names_ptr[value_native_type_idx];
				const ContainerType expected_value_type = _container_type_from_type_info(*value_type_info, value_builtin_type, value_native_type);

				bool result = false;
				if (value->get_type() == Variant::DICTIONARY) {
					Dictionary *dictionary = VariantInternal::get_dictionary(value);
					result = dictionary->get_key_type() == expected_key_type && dictionary->get_value_type() == expected_value_type;
				}

				*dst = result;
				ip += 9;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_TUPLE) {
				CHECK_SPACE(5);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				GET_VARIANT_PTR(type_info, 2);
				const int arity = _code_ptr[ip + 4];

				bool result = false;
				if (value->get_type() == Variant::ARRAY) {
					// The arity check is the cheap rejection; only a candidate of the right shape pays
					// for rebuilding the element types and testing them one by one.
					result = VariantInternal::get_array(value)->size() == arity &&
							_data_type_from_tuple_descriptor(*type_info).is_type(*value);
				} else if (value->get_type() == Variant::NIL) {
					// A nullable tuple type accepts null; the flag travels on the descriptor.
					result = _data_type_from_tuple_descriptor(*type_info).is_nullable;
				}

				*dst = result;
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_ENUM) {
				CHECK_SPACE(5);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				GET_VARIANT_PTR(values, 2);
				const bool is_tagged_union = _code_ptr[ip + 4];

				// An enum is a named subset of its backing representation, so `is` is a membership
				// test: an int-backed enum accepts a declared int, a tagged union accepts a
				// `[tag, payload...]` Array whose tag is declared.
				bool result = false;
				if (likely(values->get_type() == Variant::PACKED_INT64_ARRAY)) {
					const PackedInt64Array *declared_values = VariantInternal::get_int64_array(values);
					if (is_tagged_union) {
						if (value->get_type() == Variant::ARRAY) {
							const Array *array = VariantInternal::get_array(value);
							if (array->size() >= 1) {
								const Variant &tag = (*array)[0];
								result = tag.get_type() == Variant::INT && declared_values->has((int64_t)tag);
							}
						}
					} else if (value->get_type() == Variant::INT) {
						result = declared_values->has(*VariantInternal::get_int(value));
					}
				}

				*dst = result;
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_ENUM_CASE) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);
				ip += instr_arg_count;

				const int tag = _code_ptr[ip + 1];
				const int bind_count = _code_ptr[ip + 2];

				GET_INSTRUCTION_ARG(value, bind_count);
				GET_INSTRUCTION_ARG(dst, bind_count + 1);

				// Every case value is `[tag, payload...]`, so the shape check is the exact arity plus
				// the tag, and the payload binds are the remaining elements in declaration order.
				bool result = false;
				if (value->get_type() == Variant::ARRAY) {
					const Array *array = VariantInternal::get_array(value);
					if (array->size() == bind_count + 1) {
						const Variant &value_tag = (*array)[0];
						result = value_tag.get_type() == Variant::INT && (int64_t)value_tag == (int64_t)tag;
					}
					if (result) {
						for (int i = 0; i < bind_count; i++) {
							GET_INSTRUCTION_ARG(bind, i);
							*bind = (*array)[i + 1];
						}
					}
				}

				*dst = result;
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_NATIVE) {
				CHECK_SPACE(5);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				int native_type_idx = _code_ptr[ip + 3];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const bool is_type_handle = _code_ptr[ip + 4];

				if (is_type_handle) {
					FSDataType expected_type;
					expected_type.kind = FSDataType::NATIVE;
					expected_type.builtin_type = Variant::OBJECT;
					expected_type.native_type = native_type;
					expected_type.is_type_handle = true;
					bool was_freed = false;
					*dst = _type_handle_test_matches(expected_type, *value, was_freed);
					if (was_freed) {
						err_text = "Left operand of 'is' is a previously freed instance.";
						OPCODE_BREAK;
					}
				} else {
					bool was_freed = false;
					Object *object = value->get_validated_object_with_check(was_freed);
					if (was_freed) {
						err_text = "Left operand of 'is' is a previously freed instance.";
						OPCODE_BREAK;
					}

					*dst = _specialized_handle_assignable_to_native_script(value, native_type) != nullptr ||
							(object && ClassDB::is_parent_class(object->get_class_name(), native_type));
				}
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_TYPE_TEST_SCRIPT) {
				CHECK_SPACE(5);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				GET_VARIANT_PTR(type, 2);
				const bool is_type_handle = _code_ptr[ip + 4];
				bool result = false;

				if (is_type_handle) {
					bool was_freed = false;
					{
						FSDataType expected_handle_type;
						[[maybe_unused]] Script *script_type = _script_type_from_type_info(*type, &expected_handle_type);
						GD_ERR_BREAK(!script_type);
						result = _type_handle_test_matches(expected_handle_type, *value, was_freed);
					}
					if (was_freed) {
						err_text = "Left operand of 'is' is a previously freed instance.";
						OPCODE_BREAK;
					}
				} else {
					Script *script_type = _script_type_from_type_info(*type);
					GD_ERR_BREAK(!script_type);
					FoundryScript *fs_type = Object::cast_to<FoundryScript>(script_type);
					const bool is_trait_type = fs_type != nullptr && fs_type->is_trait_type();

					bool was_freed = false;
					Object *object = value->get_validated_object_with_check(was_freed);
					if (was_freed) {
						err_text = "Left operand of 'is' is a previously freed instance.";
						OPCODE_BREAK;
					}

					if (is_trait_type) {
						// Trait-typed values are Object-backed: a trait has no native class of its own, so
						// membership is a nominal trait-set lookup over the flattened implementer rather than
						// a native/script inheritance walk.
						const StringName trait_name = fs_type->get_trait_type_name();
						if (object) {
							ScriptInstance *script_instance = object->get_script_instance();
							if (script_instance != nullptr) {
								Ref<Script> script_ref = script_instance->get_script();
								result = script_ref.is_valid() && script_ref->has_script_trait(trait_name);
							}
							if (!result) {
								// A native object (no Foundry Script instance), or a scripted object whose engine
								// base class was retroactively conformed, satisfies the trait via the registry.
								result = FSConformanceRegistry::get_singleton()->native_class_conforms(object->get_class_name(), trait_name, true);
							}
						} else if (value->get_type() != Variant::NIL && value->get_type() != Variant::OBJECT) {
							result = FSConformanceRegistry::get_singleton()->builtin_type_conforms(value->get_type(), trait_name, true);
						}
					} else if (object && object->get_script_instance()) {
						Ref<Script> script_ref = object->get_script_instance()->get_script();
						Script *script_ptr = script_ref.ptr();
						while (script_ptr) {
							if (script_ptr == script_type) {
								result = true;
								break;
							}
							script_ptr = script_ptr->get_base_script().ptr();
						}
					}
				}

				*dst = result;
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_KEYED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(index, 1);
				GET_VARIANT_PTR(value, 2);

				Variant erased_index = *index;
				Variant erased_value = *value;
				const bool erased = _erase_specialized_handles_for_typed_container_set(dst, erased_index, erased_value);
				const Variant *index_arg = erased ? &erased_index : index;
				const Variant *value_arg = erased ? &erased_value : value;

				bool valid;
#ifdef DEBUG_ENABLED
				Variant::VariantSetError err_code;
				dst->set(*index_arg, *value_arg, &valid, &err_code);
#else
				dst->set(*index_arg, *value_arg, &valid);
#endif
#ifdef DEBUG_ENABLED
				if (!valid) {
					if (dst->is_read_only()) {
						err_text = "Invalid assignment on read-only value (on base: '" + _get_var_type(dst) + "').";
					} else {
						Object *obj = dst->get_validated_object();
						String v = index_arg->operator String();
						bool read_only_property = false;
						if (obj) {
							read_only_property = ClassDB::has_property(obj->get_class_name(), v) && (ClassDB::get_property_setter(obj->get_class_name(), v) == StringName());
						}
						if (read_only_property) {
							err_text = vformat(R"(Cannot set value into property "%s" (on base "%s") because it is read-only.)", v, _get_var_type(dst));
						} else {
							if (!v.is_empty()) {
								v = "'" + v + "'";
							} else {
								v = "of type '" + _get_var_type(index_arg) + "'";
							}
							err_text = "Invalid assignment of property or key " + v + " with value of type '" + _get_var_type(value_arg) + "' on a base object of type '" + _get_var_type(dst) + "'.";
							if (err_code == Variant::VariantSetError::SET_INDEXED_ERR) {
								err_text = "Invalid assignment of index " + v + " (on base: '" + _get_var_type(dst) + "') with value of type '" + _get_var_type(value_arg) + "'.";
							}
						}
					}
					OPCODE_BREAK;
				}
#endif
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_KEYED_VALIDATED) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(index, 1);
				GET_VARIANT_PTR(value, 2);

				Variant erased_index = *index;
				Variant erased_value = *value;
				const bool erased = _erase_specialized_handles_for_typed_container_set(dst, erased_index, erased_value);
				const Variant *index_arg = erased ? &erased_index : index;
				const Variant *value_arg = erased ? &erased_value : value;

				int index_setter = _code_ptr[ip + 4];
				GD_ERR_BREAK(index_setter < 0 || index_setter >= _keyed_setters_count);
				const Variant::ValidatedKeyedSetter setter = _keyed_setters_ptr[index_setter];

				bool valid;
				setter(dst, index_arg, value_arg, &valid);

#ifdef DEBUG_ENABLED
				if (!valid) {
					if (dst->is_read_only()) {
						err_text = "Invalid assignment on read-only value (on base: '" + _get_var_type(dst) + "').";
					} else {
						String v = index_arg->operator String();
						if (!v.is_empty()) {
							v = "'" + v + "'";
						} else {
							v = "of type '" + _get_var_type(index_arg) + "'";
						}
						err_text = "Invalid assignment of property or key " + v + " with value of type '" + _get_var_type(value_arg) + "' on a base object of type '" + _get_var_type(dst) + "'.";
					}
					OPCODE_BREAK;
				}
#endif
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_INDEXED_VALIDATED) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(index, 1);
				GET_VARIANT_PTR(value, 2);

				Variant erased_value = *value;
				const bool erased = _erase_specialized_handles_for_typed_array_argument(dst, erased_value);
				const Variant *value_arg = erased ? &erased_value : value;

				int index_setter = _code_ptr[ip + 4];
				GD_ERR_BREAK(index_setter < 0 || index_setter >= _indexed_setters_count);
				const Variant::ValidatedIndexedSetter setter = _indexed_setters_ptr[index_setter];

				int64_t int_index = *VariantInternal::get_int(index);

				bool oob;
				setter(dst, int_index, value_arg, &oob);

#ifdef DEBUG_ENABLED
				if (oob) {
					if (dst->is_read_only()) {
						err_text = "Invalid assignment on read-only value (on base: '" + _get_var_type(dst) + "').";
					} else {
						String v = index->operator String();
						if (!v.is_empty()) {
							v = "'" + v + "'";
						} else {
							v = "of type '" + _get_var_type(index) + "'";
						}
						err_text = "Out of bounds set index " + v + " (on base: '" + _get_var_type(dst) + "')";
					}
					OPCODE_BREAK;
				}
#endif
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_KEYED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(index, 1);
				GET_VARIANT_PTR(dst, 2);

				bool valid;
#ifdef DEBUG_ENABLED
				// Allow better error message in cases where src and dst are the same stack position.
				Variant::VariantGetError err_code;
				Variant ret = src->get(*index, &valid, &err_code);
#else
				*dst = src->get(*index, &valid);

#endif
#ifdef DEBUG_ENABLED
				if (!valid) {
					String v = index->operator String();
					if (!v.is_empty()) {
						v = "'" + v + "'";
					} else {
						v = "of type '" + _get_var_type(index) + "'";
					}
					err_text = "Invalid access to property or key " + v + " on a base object of type '" + _get_var_type(src) + "'.";
					if (err_code == Variant::VariantGetError::GET_INDEXED_ERR) {
						err_text = "Invalid access of index " + v + " on a base object of type: '" + _get_var_type(src) + "'.";
					}
					OPCODE_BREAK;
				}
				*dst = ret;
#endif
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_KEYED_VALIDATED) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(key, 1);
				GET_VARIANT_PTR(dst, 2);

				int index_getter = _code_ptr[ip + 4];
				GD_ERR_BREAK(index_getter < 0 || index_getter >= _keyed_getters_count);
				const Variant::ValidatedKeyedGetter getter = _keyed_getters_ptr[index_getter];

				bool valid;
#ifdef DEBUG_ENABLED
				// Allow better error message in cases where src and dst are the same stack position.
				Variant ret;
				getter(src, key, &ret, &valid);
#else
				getter(src, key, dst, &valid);
#endif
#ifdef DEBUG_ENABLED
				if (!valid) {
					String v = key->operator String();
					if (!v.is_empty()) {
						v = "'" + v + "'";
					} else {
						v = "of type '" + _get_var_type(key) + "'";
					}
					err_text = "Invalid access to property or key " + v + " on a base object of type '" + _get_var_type(src) + "'.";
					OPCODE_BREAK;
				}
				*dst = ret;
#endif
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_INDEXED_VALIDATED) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(index, 1);
				GET_VARIANT_PTR(dst, 2);

				int index_getter = _code_ptr[ip + 4];
				GD_ERR_BREAK(index_getter < 0 || index_getter >= _indexed_getters_count);
				const Variant::ValidatedIndexedGetter getter = _indexed_getters_ptr[index_getter];

				int64_t int_index = *VariantInternal::get_int(index);

				bool oob;
				getter(src, int_index, dst, &oob);

#ifdef DEBUG_ENABLED
				if (oob) {
					String v = index->operator String();
					if (!v.is_empty()) {
						v = "'" + v + "'";
					} else {
						v = "of type '" + _get_var_type(index) + "'";
					}
					err_text = "Out of bounds get index " + v + " (on base: '" + _get_var_type(src) + "')";
					OPCODE_BREAK;
				}
#endif
				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_NAMED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				int indexname = _code_ptr[ip + 3];

				GD_ERR_BREAK(indexname < 0 || indexname >= _global_names_count);
				const StringName *index = &_global_names_ptr[indexname];

				bool valid;
				dst->set_named(*index, *value, valid);

#ifdef DEBUG_ENABLED
				if (!valid) {
					if (dst->is_read_only()) {
						err_text = "Invalid assignment on read-only value (on base: '" + _get_var_type(dst) + "').";
					} else {
						Object *obj = dst->get_validated_object();
						bool read_only_property = false;
						if (obj) {
							read_only_property = ClassDB::has_property(obj->get_class_name(), *index) && (ClassDB::get_property_setter(obj->get_class_name(), *index) == StringName());
						}
						if (read_only_property) {
							err_text = vformat(R"(Cannot set value into property "%s" (on base "%s") because it is read-only.)", String(*index), _get_var_type(dst));
						} else {
							err_text = "Invalid assignment of property or key '" + String(*index) + "' with value of type '" + _get_var_type(value) + "' on a base object of type '" + _get_var_type(dst) + "'.";
						}
					}
					OPCODE_BREAK;
				}
#endif
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_NAMED_VALIDATED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(value, 1);

				int index_setter = _code_ptr[ip + 3];
				GD_ERR_BREAK(index_setter < 0 || index_setter >= _setters_count);
				const Variant::ValidatedSetter setter = _setters_ptr[index_setter];

				setter(dst, value);
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_NAMED) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(dst, 1);

				int indexname = _code_ptr[ip + 3];

				GD_ERR_BREAK(indexname < 0 || indexname >= _global_names_count);
				const StringName *index = &_global_names_ptr[indexname];

				bool valid;
#ifdef DEBUG_ENABLED
				//allow better error message in cases where src and dst are the same stack position
				Variant ret = src->get_named(*index, valid);

#else
				*dst = src->get_named(*index, valid);
#endif
#ifdef DEBUG_ENABLED
				if (!valid) {
					err_text = "Invalid access to property or key '" + index->operator String() + "' on a base object of type '" + _get_var_type(src) + "'.";
					OPCODE_BREAK;
				}
				*dst = ret;
#endif
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_NAMED_VALIDATED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(dst, 1);

				int index_getter = _code_ptr[ip + 3];
				GD_ERR_BREAK(index_getter < 0 || index_getter >= _getters_count);
				const Variant::ValidatedGetter getter = _getters_ptr[index_getter];

				getter(src, dst);
				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_MEMBER) {
				CHECK_SPACE(3);
				GET_VARIANT_PTR(src, 0);
				int indexname = _code_ptr[ip + 2];
				GD_ERR_BREAK(indexname < 0 || indexname >= _global_names_count);
				const StringName *index = &_global_names_ptr[indexname];

				bool valid;
#ifndef DEBUG_ENABLED
				ClassDB::set_property(p_instance->owner, *index, *src, &valid);
#else
				bool ok = ClassDB::set_property(p_instance->owner, *index, *src, &valid);
				if (!ok) {
					err_text = "Internal error setting property: " + String(*index);
					OPCODE_BREAK;
				} else if (!valid) {
					err_text = "Error setting property '" + String(*index) + "' with value of type " + Variant::get_type_name(src->get_type()) + ".";
					OPCODE_BREAK;
				}
#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_MEMBER) {
				CHECK_SPACE(3);
				GET_VARIANT_PTR(dst, 0);
				int indexname = _code_ptr[ip + 2];
				GD_ERR_BREAK(indexname < 0 || indexname >= _global_names_count);
				const StringName *index = &_global_names_ptr[indexname];
#ifndef DEBUG_ENABLED
				ClassDB::get_property(p_instance->owner, *index, *dst);
#else
				bool ok = ClassDB::get_property(p_instance->owner, *index, *dst);
				if (!ok) {
					err_text = "Internal error getting property: " + String(*index);
					OPCODE_BREAK;
				}
#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_TYPE_PARAMETER) {
				CHECK_SPACE(3);
				GET_VARIANT_PTR(dst, 0);
				int type_parameter_index = _code_ptr[ip + 2];

				// Materialize the script bound to the enclosing class's i-th type parameter
				// from this instance's reified type arguments (e.g. `Greeter` in
				// `Mock[Greeter].new()`). Used to forward a type parameter into
				// `create_proxy[T]`. The result is the bound `Script` (null when the
				// argument is a builtin or unbound), which the proxy constructor validates.
				if (unlikely(p_instance == nullptr)) {
					err_text = "Cannot resolve a type parameter without an instance.";
					OPCODE_BREAK;
				}
				// The index is a type-parameter ordinal in `_script`, the class that declares this
				// function. The leaf script holds a per-ancestor table mapping each ancestor's parameter
				// to how it resolves for this instance: FIXED to a concrete script by an `extends Base[X]`
				// specialization in the chain, or OPEN against the instance's own reified arguments. This
				// resolves `T` correctly both for a directly-specialized instance (`Mock[Greeter]`) and a
				// derived one whose base was specialized (`Mock extends Base[Greeter]`).
				const Vector<ContainerType> &reified = p_instance->get_type_arguments();
				*dst = Variant();
				if (p_instance->script.is_valid()) {
					const HashMap<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> &table = p_instance->script->type_parameter_bindings_by_ancestor;
					const Vector<FoundryScript::TypeArgumentBinding> *entry = table.getptr(_script);
					if (entry != nullptr && type_parameter_index >= 0 && type_parameter_index < entry->size()) {
						const FoundryScript::TypeArgumentBinding &binding = (*entry)[type_parameter_index];
						if (binding.kind == FoundryScript::TypeArgumentBinding::FIXED) {
							*dst = binding.fixed.to_container_type().script;
						} else if (binding.kind == FoundryScript::TypeArgumentBinding::OPEN &&
								binding.leaf_ordinal >= 0 && binding.leaf_ordinal < reified.size()) {
							*dst = reified[binding.leaf_ordinal].script;
						}
					}
				}
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_SET_STATIC_VARIABLE) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(value, 0);

				GET_VARIANT_PTR(_class, 1);
				FoundryScript *foundry_script = Object::cast_to<FoundryScript>(_class->operator Object *());
				GD_ERR_BREAK(!foundry_script);

				int index = _code_ptr[ip + 3];
				GD_ERR_BREAK(index < 0 || index >= foundry_script->static_variables.size());

				foundry_script->static_variables.write[index] = *value;

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_GET_STATIC_VARIABLE) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(target, 0);

				GET_VARIANT_PTR(_class, 1);
				FoundryScript *foundry_script = Object::cast_to<FoundryScript>(_class->operator Object *());
				GD_ERR_BREAK(!foundry_script);

				int index = _code_ptr[ip + 3];
				GD_ERR_BREAK(index < 0 || index >= foundry_script->static_variables.size());

				*target = foundry_script->static_variables[index];

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN) {
				CHECK_SPACE(3);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				*dst = *src;

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_NULL) {
				CHECK_SPACE(2);
				GET_VARIANT_PTR(dst, 0);

				*dst = Variant();

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TRUE) {
				CHECK_SPACE(2);
				GET_VARIANT_PTR(dst, 0);

				*dst = true;

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_FALSE) {
				CHECK_SPACE(2);
				GET_VARIANT_PTR(dst, 0);

				*dst = false;

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_BUILTIN) {
				CHECK_SPACE(4);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				int var_type_operand = _code_ptr[ip + 3];
				const bool is_nullable = var_type_operand & FSFunction::NULLABLE_TYPE_OPERAND_FLAG;
				Variant::Type var_type = (Variant::Type)(var_type_operand & ~FSFunction::NULLABLE_TYPE_OPERAND_FLAG);
				GD_ERR_BREAK(var_type < 0 || var_type >= Variant::VARIANT_MAX);

				if (is_nullable && src->get_type() == Variant::NIL) {
					// A nullable target stores null as-is instead of converting it to the underlying type.
					*dst = *src;
				} else if (src->get_type() != var_type) {
#ifdef DEBUG_ENABLED
					if (Variant::can_convert_strict(src->get_type(), var_type)) {
#endif // DEBUG_ENABLED
						Callable::CallError ce;
						Variant::construct(var_type, *dst, const_cast<const Variant **>(&src), 1, ce);
					} else {
#ifdef DEBUG_ENABLED
						err_text = "Trying to assign value of type '" + Variant::get_type_name(src->get_type()) +
								"' to a variable of type '" + Variant::get_type_name(var_type) + "'.";
						OPCODE_BREAK;
					}
				} else {
#endif // DEBUG_ENABLED
					*dst = *src;
				}

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_ARRAY) {
				CHECK_SPACE(6);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(type_info, 2);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 4];
				int native_type_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const ContainerType expected_type = _container_type_from_type_info(*type_info, builtin_type, native_type);

				if (src->get_type() != Variant::ARRAY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign a value of type "%s" to a variable of type "Array[%s]".)",
							_get_var_type(src), _get_element_type(expected_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				Array *array = VariantInternal::get_array(src);

				if (array->get_element_type() != expected_type) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign an array of type "%s" to a variable of type "Array[%s]".)",
							_get_var_type(src), _get_element_type(expected_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				*dst = *src;

				ip += 6;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_DICTIONARY) {
				CHECK_SPACE(9);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(key_type_info, 2);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 5];
				int key_native_type_idx = _code_ptr[ip + 6];
				GD_ERR_BREAK(key_native_type_idx < 0 || key_native_type_idx >= _global_names_count);
				const StringName key_native_type = _global_names_ptr[key_native_type_idx];
				const ContainerType expected_key_type = _container_type_from_type_info(*key_type_info, key_builtin_type, key_native_type);

				GET_VARIANT_PTR(value_type_info, 3);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 7];
				int value_native_type_idx = _code_ptr[ip + 8];
				GD_ERR_BREAK(value_native_type_idx < 0 || value_native_type_idx >= _global_names_count);
				const StringName value_native_type = _global_names_ptr[value_native_type_idx];
				const ContainerType expected_value_type = _container_type_from_type_info(*value_type_info, value_builtin_type, value_native_type);

				if (src->get_type() != Variant::DICTIONARY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign a value of type "%s" to a variable of type "Dictionary[%s, %s]".)",
							_get_var_type(src), _get_element_type(expected_key_type),
							_get_element_type(expected_value_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				Dictionary *dictionary = VariantInternal::get_dictionary(src);

				if (dictionary->get_key_type() != expected_key_type || dictionary->get_value_type() != expected_value_type) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign a dictionary of type "%s" to a variable of type "Dictionary[%s, %s]".)",
							_get_var_type(src), _get_element_type(expected_key_type),
							_get_element_type(expected_value_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				*dst = *src;

				ip += 9;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_NATIVE) {
				CHECK_SPACE(5);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(type, 2);
				FSNativeClass *nc = Object::cast_to<FSNativeClass>(type->operator Object *());
				GD_ERR_BREAK(!nc);
				const bool is_type_handle = _code_ptr[ip + 4];
				FSSpecializedClassHandle *specialized_handle = !is_type_handle ? _specialized_handle_assignable_to_native_script(src, nc->get_name()) : nullptr;

#ifdef DEBUG_ENABLED
				if (is_type_handle) {
					if (!_make_native_type_handle_type(nc).is_type(*src)) {
						err_text = "Trying to assign value of type '" + _get_var_type(src) +
								"' to a variable of type 'Type[" + String(nc->get_name()) + "]'.";
						OPCODE_BREAK;
					}
				} else if (src->get_type() != Variant::OBJECT && src->get_type() != Variant::NIL) {
					err_text = "Trying to assign value of type '" + Variant::get_type_name(src->get_type()) +
							"' to a variable of type '" + nc->get_name() + "'.";
					OPCODE_BREAK;
				} else if (src->get_type() == Variant::OBJECT) {
					bool was_freed = false;
					Object *src_obj = src->get_validated_object_with_check(was_freed);
					if (!src_obj && was_freed) {
						err_text = "Trying to assign invalid previously freed instance.";
						OPCODE_BREAK;
					}

					if (src_obj && specialized_handle == nullptr &&
							!ClassDB::is_parent_class(src_obj->get_class_name(), nc->get_name())) {
						err_text = "Trying to assign value of type '" + src_obj->get_class_name() +
								"' to a variable of type '" + nc->get_name() + "'.";
						OPCODE_BREAK;
					}
				}
#endif // DEBUG_ENABLED
				if (specialized_handle != nullptr) {
					*dst = specialized_handle->get_specialized_script();
				} else {
					*dst = *src;
				}

				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_SCRIPT) {
				CHECK_SPACE(5);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(type, 2);
				FSDataType expected_handle_type;
				Script *base_type = _script_type_from_type_info(*type, &expected_handle_type);

				GD_ERR_BREAK(!base_type);
				FoundryScript *fs_base_type = Object::cast_to<FoundryScript>(base_type);
				[[maybe_unused]] const bool is_trait_type = fs_base_type != nullptr && fs_base_type->is_trait_type();
				[[maybe_unused]] const bool is_type_handle = _code_ptr[ip + 4];

#ifdef DEBUG_ENABLED
				if (is_type_handle) {
					if (!expected_handle_type.is_type(*src)) {
						err_text = "Trying to assign value of type '" + _get_var_type(src) +
								"' to a variable of type '" + _get_type_handle_type_name(expected_handle_type, base_type) + "'.";
						OPCODE_BREAK;
					}
				} else if (is_trait_type && src->get_type() != Variant::OBJECT && src->get_type() != Variant::NIL) {
					const StringName trait_name = fs_base_type->get_trait_type_name();
					if (!FSConformanceRegistry::get_singleton()->builtin_type_conforms(src->get_type(), trait_name, true)) {
						err_text = "Trying to assign value of type '" + Variant::get_type_name(src->get_type()) +
								"' to a variable of type '" + base_type->get_path().get_file() + "'.";
						OPCODE_BREAK;
					}
				} else if (src->get_type() != Variant::OBJECT && src->get_type() != Variant::NIL) {
					err_text = "Trying to assign a non-object value to a variable of type '" + base_type->get_path().get_file() + "'.";
					OPCODE_BREAK;
				} else if (src->get_type() == Variant::OBJECT) {
					bool was_freed = false;
					Object *val_obj = src->get_validated_object_with_check(was_freed);
					if (!val_obj && was_freed) {
						err_text = "Trying to assign invalid previously freed instance.";
						OPCODE_BREAK;
					}

					if (val_obj) { // src is not null
						ScriptInstance *scr_inst = val_obj->get_script_instance();
						bool valid = false;

						if (is_trait_type) {
							// A native object satisfies a trait-typed slot when its engine class was
							// retroactively conformed, so a Foundry Script instance is not required here.
							const StringName trait_name = fs_base_type->get_trait_type_name();
							if (scr_inst != nullptr) {
								Ref<Script> src_script = scr_inst->get_script();
								valid = src_script.is_valid() && src_script->has_script_trait(trait_name);
							}
							if (!valid) {
								valid = FSConformanceRegistry::get_singleton()->native_class_conforms(val_obj->get_class_name(), trait_name, true);
							}
						} else if (scr_inst != nullptr) {
							Script *src_type = scr_inst->get_script().ptr();
							while (src_type) {
								if (src_type == base_type) {
									valid = true;
									break;
								}
								src_type = src_type->get_base_script().ptr();
							}
						}

						if (!valid) {
							err_text = "Trying to assign value of type '" + val_obj->get_class_name() +
									"' to a variable of type '" + base_type->get_path().get_file() + "'.";
							OPCODE_BREAK;
						}
					}
				}
#endif // DEBUG_ENABLED

				*dst = *src;

				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_PARAMETER) {
				CHECK_SPACE(4);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				int member_index = _code_ptr[ip + 3];

				// A `T`-typed member is erased to a plain Variant slot at compile time, so a direct member
				// store (e.g. `value = v` inside `class Box[T]`) bypasses the `set()` validation used for
				// external writes. Resolve the binding the leaf script precomputed for this member slot:
				// FIXED to a concrete argument by an `extends Base[int]` specialization, or OPEN against the
				// argument reified onto this instance. An OPEN member on an instance without explicit
				// arguments carries no binding, leaving the slot effectively untyped.
				bool has_expected_type = false;
				bool expected_is_type_handle = false;
				ContainerType expected_type;
				if (p_instance != nullptr && p_instance->script.is_valid()) {
					const Vector<FoundryScript::TypeArgumentBinding> &bindings = p_instance->script->member_type_argument_bindings;
					if (member_index >= 0 && member_index < bindings.size()) {
						const FoundryScript::TypeArgumentBinding &binding = bindings[member_index];
						if (binding.kind == FoundryScript::TypeArgumentBinding::FIXED) {
							expected_type = binding.fixed.to_container_type();
							has_expected_type = true;
							expected_is_type_handle = binding.is_type_handle;
						} else if (binding.kind == FoundryScript::TypeArgumentBinding::OPEN &&
								binding.leaf_ordinal >= 0 && binding.leaf_ordinal < p_instance->type_arguments.size()) {
							expected_type = p_instance->type_arguments[binding.leaf_ordinal];
							has_expected_type = true;
							expected_is_type_handle = binding.is_type_handle;
						}
					}
				}

				if (has_expected_type) {
					Variant value = *src;
					if (expected_is_type_handle) {
						const FSDataType expected_handle_type = FSDataType::from_type_handle_container_type(expected_type);
						if (!expected_handle_type.is_type(value)) {
#ifdef DEBUG_ENABLED
							// `expected_type` is the reified argument's own (non-handle) descriptor; the slot's
							// handle-ness lives only in `expected_is_type_handle`, so the wrapper is added here
							// rather than expected from `expected_type.get_type_name()` itself.
							err_text = vformat(R"(Trying to assign a value of type "%s" to a member of type "Type[%s]".)",
									_get_var_type(src), expected_type.get_type_name());
#endif // DEBUG_ENABLED
							OPCODE_BREAK;
						}
					} else {
						_erase_specialized_handles_for_container_type(expected_type, value);
						ContainerTypeValidate validator(expected_type);
						validator.where = "member";
						if (!validator.validate(value, "assign")) {
#ifdef DEBUG_ENABLED
							err_text = vformat(R"(Trying to assign a value of type "%s" to a member of type "%s".)",
									_get_var_type(src), _get_element_type(expected_type));
#endif // DEBUG_ENABLED
							OPCODE_BREAK;
						}
					}
					*dst = value;
				} else {
					*dst = *src;
				}

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_ARRAY_CONVERT) {
				CHECK_SPACE(6);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(type_info, 2);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 4];
				int native_type_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const ContainerType expected_type = _container_type_from_type_info(*type_info, builtin_type, native_type);

				if (src->get_type() != Variant::ARRAY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign a value of type "%s" to a variable of type "Array[%s]".)",
							_get_var_type(src), _get_element_type(expected_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				// A generic method returning `Array[T]` yields an untyped array at runtime (the element is
				// erased). Retype it into the concrete typed array the call site expects, converting each
				// element. Only emitted where the analyzer flagged an erased-container return, so it never
				// relaxes ordinary strict typed-array assignment.
				*dst = Array(*VariantInternal::get_array(src), expected_type);

				ip += 6;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT) {
				CHECK_SPACE(9);
				GET_VARIANT_PTR(dst, 0);
				GET_VARIANT_PTR(src, 1);

				GET_VARIANT_PTR(key_type_info, 2);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 5];
				int key_native_type_idx = _code_ptr[ip + 6];
				GD_ERR_BREAK(key_native_type_idx < 0 || key_native_type_idx >= _global_names_count);
				const StringName key_native_type = _global_names_ptr[key_native_type_idx];
				const ContainerType expected_key_type = _container_type_from_type_info(*key_type_info, key_builtin_type, key_native_type);

				GET_VARIANT_PTR(value_type_info, 3);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 7];
				int value_native_type_idx = _code_ptr[ip + 8];
				GD_ERR_BREAK(value_native_type_idx < 0 || value_native_type_idx >= _global_names_count);
				const StringName value_native_type = _global_names_ptr[value_native_type_idx];
				const ContainerType expected_value_type = _container_type_from_type_info(*value_type_info, value_builtin_type, value_native_type);

				if (src->get_type() != Variant::DICTIONARY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to assign a value of type "%s" to a variable of type "Dictionary[%s, %s]".)",
							_get_var_type(src), _get_element_type(expected_key_type),
							_get_element_type(expected_value_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				// A generic method returning `Dictionary[K, V]` yields an untyped dictionary at runtime (the
				// key/value types are erased). Retype it into the concrete typed dictionary the call site
				// expects, converting each entry. Only emitted where the analyzer flagged an erased-container
				// return, so it never relaxes ordinary strict typed-dictionary assignment.
				*dst = Dictionary(*VariantInternal::get_dictionary(src), expected_key_type, expected_value_type);

				ip += 9;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CAST_TO_BUILTIN) {
				CHECK_SPACE(4);
				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(dst, 1);
				int to_type_operand = _code_ptr[ip + 3];
				const bool is_nullable = to_type_operand & FSFunction::NULLABLE_TYPE_OPERAND_FLAG;
				Variant::Type to_type = (Variant::Type)(to_type_operand & ~FSFunction::NULLABLE_TYPE_OPERAND_FLAG);

				GD_ERR_BREAK(to_type < 0 || to_type >= Variant::VARIANT_MAX);

#ifdef DEBUG_ENABLED
				if (src->operator Object *() && !src->get_validated_object()) {
					err_text = "Trying to cast a freed object.";
					OPCODE_BREAK;
				}
#endif

				if (is_nullable && src->get_type() == Variant::NIL) {
					// Casting null to a nullable builtin yields null instead of the type's default value.
					*dst = *src;
					ip += 4;
					DISPATCH_OPCODE;
				}

				Callable::CallError err;
				Variant::construct(to_type, *dst, (const Variant **)&src, 1, err);

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					err_text = "Invalid cast: could not convert value to '" + Variant::get_type_name(to_type) + "'.";
					OPCODE_BREAK;
				}
#endif

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CAST_TO_NATIVE) {
				CHECK_SPACE(5);
				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(dst, 1);
				GET_VARIANT_PTR(to_type, 2);

				FSNativeClass *nc = Object::cast_to<FSNativeClass>(to_type->operator Object *());
				GD_ERR_BREAK(!nc);
				const bool is_type_handle = _code_ptr[ip + 4];

#ifdef DEBUG_ENABLED
				if (src->operator Object *() && !src->get_validated_object()) {
					err_text = "Trying to cast a freed object.";
					OPCODE_BREAK;
				}
				if (src->get_type() != Variant::OBJECT && src->get_type() != Variant::NIL) {
					err_text = "Invalid cast: can't convert a non-object value to an object type.";
					OPCODE_BREAK;
				}
#endif
				Object *src_obj = src->operator Object *();
				FSSpecializedClassHandle *specialized_handle = !is_type_handle ? _specialized_handle_assignable_to_native_script(src, nc->get_name()) : nullptr;

				if (is_type_handle) {
					if (_make_native_type_handle_type(nc).is_type(*src)) {
						*dst = *src;
					} else {
						*dst = Variant();
					}
				} else if (specialized_handle != nullptr) {
					*dst = specialized_handle->get_specialized_script();
				} else if (src_obj && !ClassDB::is_parent_class(src_obj->get_class_name(), nc->get_name())) {
					*dst = Variant(); // invalid cast, assign NULL
				} else {
					*dst = *src;
				}

				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CAST_TO_SCRIPT) {
				CHECK_SPACE(5);
				GET_VARIANT_PTR(src, 0);
				GET_VARIANT_PTR(dst, 1);
				GET_VARIANT_PTR(to_type, 2);

				FSDataType expected_handle_type;
				Script *base_type = _script_type_from_type_info(*to_type, &expected_handle_type);

				GD_ERR_BREAK(!base_type);
				FoundryScript *fs_base_type = Object::cast_to<FoundryScript>(base_type);
				const bool is_trait_type = fs_base_type != nullptr && fs_base_type->is_trait_type();
				const bool is_type_handle = _code_ptr[ip + 4];

#ifdef DEBUG_ENABLED
				if (src->operator Object *() && !src->get_validated_object()) {
					err_text = "Trying to cast a freed object.";
					OPCODE_BREAK;
				}
				if (src->get_type() != Variant::OBJECT && src->get_type() != Variant::NIL) {
					if (!is_trait_type || !FSConformanceRegistry::get_singleton()->builtin_type_conforms(src->get_type(), fs_base_type->get_trait_type_name(), true)) {
						err_text = "Trying to assign a non-object value to a variable of type '" + base_type->get_path().get_file() + "'.";
						OPCODE_BREAK;
					}
				}
#endif

				bool valid = false;

				if (is_type_handle) {
					valid = expected_handle_type.is_type(*src);
				} else if (is_trait_type) {
					const StringName trait_name = fs_base_type->get_trait_type_name();
					if (src->get_type() == Variant::OBJECT && src->operator Object *() != nullptr) {
						Object *src_obj = src->operator Object *();
						ScriptInstance *scr_inst = src_obj->get_script_instance();
						if (scr_inst) {
							Ref<Script> src_script = scr_inst->get_script();
							valid = src_script.is_valid() && src_script->has_script_trait(trait_name);
						}
						if (!valid) {
							// A native object (no Foundry Script instance), or a scripted object whose engine
							// base class was retroactively conformed, casts successfully via the registry.
							valid = FSConformanceRegistry::get_singleton()->native_class_conforms(src_obj->get_class_name(), trait_name, true);
						}
					} else if (src->get_type() != Variant::NIL) {
						valid = FSConformanceRegistry::get_singleton()->builtin_type_conforms(src->get_type(), trait_name, true);
					}
				} else if (src->get_type() != Variant::NIL && src->operator Object *() != nullptr) {
					Object *src_obj = src->operator Object *();
					ScriptInstance *scr_inst = src_obj->get_script_instance();
					if (scr_inst) {
						Ref<Script> src_script = scr_inst->get_script();
						Script *src_type = src_script.ptr();
						while (src_type) {
							if (src_type == base_type) {
								valid = true;
								break;
							}
							src_type = src_type->get_base_script().ptr();
						}
					}
				}

				if (valid) {
					*dst = *src; // Valid cast, copy the source object
				} else {
					*dst = Variant(); // invalid cast, assign NULL
				}

				ip += 5;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];

				Variant::Type t = Variant::Type(_code_ptr[ip + 2]);

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				Callable::CallError err;
				Variant::construct(t, *dst, (const Variant **)argptrs, argc, err);

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					err_text = _get_call_error("'" + Variant::get_type_name(t) + "' constructor", (const Variant **)argptrs, argc, *dst, err);
					OPCODE_BREAK;
				}
#endif

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_VALIDATED) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];

				int constructor_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(constructor_idx < 0 || constructor_idx >= _constructors_count);
				Variant::ValidatedConstructor constructor = _constructors_ptr[constructor_idx];

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				constructor(dst, (const Variant **)argptrs);

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_ARRAY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(1 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				Array array;
				array.resize(argc);

				for (int i = 0; i < argc; i++) {
					array[i] = *(instruction_args[i]);
				}

				GET_INSTRUCTION_ARG(dst, argc);
				*dst = Variant(); // Clear potential previous typed array.

				*dst = array;

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_TYPED_ARRAY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];

				GET_INSTRUCTION_ARG(type_info, argc + 1);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 2];
				int native_type_idx = _code_ptr[ip + 3];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const ContainerType element_type = _container_type_from_type_info(*type_info, builtin_type, native_type);

				Array array;
				array.set_typed(element_type);
				array.resize(argc);
				for (int i = 0; i < argc; i++) {
					Variant value = *(instruction_args[i]);
					_erase_specialized_handles_for_container_type(element_type, value);
					// Use .set instead of operator[] to handle type conversion / validation.
					array.set(i, value);
				}

				GET_INSTRUCTION_ARG(dst, argc);
				*dst = Variant(); // Clear potential previous typed array.

				*dst = array;

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_TUPLE) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(1 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				Array tuple;
				tuple.resize(argc);

				for (int i = 0; i < argc; i++) {
					tuple[i] = *(instruction_args[i]);
				}
				// Tuples are immutable values: the read-only Array is the runtime backstop that keeps a
				// tuple that escaped into a Variant from being mutated, and makes it a valid Dictionary key.
				// The freeze is deliberately shallow. Elements are stored by value, and a nested tuple is
				// itself built by this opcode, so a tuple of scalars and tuples is fully immutable. An
				// element that is a shared reference (Array, Dictionary, object) keeps its own mutability:
				// freezing it would mutate a container the caller still owns. Such an element makes the
				// tuple's content hash unstable exactly like using that container as a Dictionary key
				// directly does, which is the pre-existing engine contract for reference elements.
				tuple.make_read_only();

				GET_INSTRUCTION_ARG(dst, argc);
				*dst = Variant(); // Clear potential previous typed array.

				*dst = tuple;

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_DICTIONARY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				Dictionary dict;
				dict.reserve(argc);
				for (int i = 0; i < argc; i++) {
					GET_INSTRUCTION_ARG(k, i * 2 + 0);
					GET_INSTRUCTION_ARG(v, i * 2 + 1);
					dict[*k] = *v;
				}

				GET_INSTRUCTION_ARG(dst, argc * 2);

				*dst = Variant(); // Clear potential previous typed dictionary.

				*dst = dict;

				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CONSTRUCT_TYPED_DICTIONARY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(6 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];

				GET_INSTRUCTION_ARG(key_type_info, argc * 2 + 1);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 2];
				int key_native_type_idx = _code_ptr[ip + 3];
				GD_ERR_BREAK(key_native_type_idx < 0 || key_native_type_idx >= _global_names_count);
				const StringName key_native_type = _global_names_ptr[key_native_type_idx];
				const ContainerType key_type = _container_type_from_type_info(*key_type_info, key_builtin_type, key_native_type);

				GET_INSTRUCTION_ARG(value_type_info, argc * 2 + 2);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 4];
				int value_native_type_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(value_native_type_idx < 0 || value_native_type_idx >= _global_names_count);
				const StringName value_native_type = _global_names_ptr[value_native_type_idx];
				const ContainerType value_type = _container_type_from_type_info(*value_type_info, value_builtin_type, value_native_type);

				Dictionary dict;
				dict.set_typed(key_type, value_type);
				dict.reserve(argc);
				for (int i = 0; i < argc; i++) {
					GET_INSTRUCTION_ARG(k, i * 2 + 0);
					GET_INSTRUCTION_ARG(v, i * 2 + 1);
					Variant key = *k;
					Variant value = *v;
					_erase_specialized_handles_for_container_type(key_type, key);
					_erase_specialized_handles_for_container_type(value_type, value);
					// Use .set instead of operator[] to handle type conversion / validation.
					dict.set(key, value);
				}

				GET_INSTRUCTION_ARG(dst, argc * 2);

				*dst = Variant(); // Clear potential previous typed dictionary.

				*dst = dict;

				ip += 6;
			}
			DISPATCH_OPCODE;
			OPCODE(OPCODE_CONSTRUCT_SPECIALIZED) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);
				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);
				int type_argument_count = _code_ptr[ip + 2];
				GD_ERR_BREAK(type_argument_count < 0);

				// Instruction args: [ctor args..., type-argument descriptors..., base script, expected base script, target].
				GET_INSTRUCTION_ARG(base, argc + type_argument_count);
				GET_INSTRUCTION_ARG(expected_base, argc + type_argument_count + 1);
				GET_INSTRUCTION_ARG(dst, argc + type_argument_count + 2);

				Ref<FSSpecializedClassHandle> specialized_handle;
				Ref<FoundryScript> foundry_script = *base;
				if (foundry_script.is_null()) {
					specialized_handle = *base;
					if (specialized_handle.is_valid()) {
						foundry_script = specialized_handle->get_specialized_script();
					}
				}
				if (foundry_script.is_null()) {
					err_text = "Cannot instantiate a specialized type whose base is not a FoundryScript.";
					OPCODE_BREAK;
				}
				Ref<FoundryScript> expected_foundry_script = *expected_base;

				Vector<ContainerType> type_arguments;
				if (specialized_handle.is_valid()) {
					type_arguments = specialized_handle->get_type_arguments();
				} else if (expected_foundry_script.is_null() || foundry_script == expected_foundry_script) {
					for (int i = 0; i < type_argument_count; i++) {
						GET_INSTRUCTION_ARG(type_info, argc + i);
						type_arguments.push_back(_container_type_from_type_info(*type_info, Variant::NIL, StringName()));
					}
				}

				Variant **argptrs = instruction_args;
				Callable::CallError err;
				Variant result = foundry_script->_new_specialized((const Variant **)argptrs, argc, type_arguments, err);

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					err_text = _get_call_error("constructor 'new'", (const Variant **)argptrs, argc, result, err);
					OPCODE_BREAK;
				}
#endif

				*dst = result;

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_ASYNC)
			OPCODE(OPCODE_CALL_RETURN)
			OPCODE(OPCODE_CALL) {
				bool call_ret = (_code_ptr[ip]) != OPCODE_CALL;
#ifdef DEBUG_ENABLED
				bool call_async = (_code_ptr[ip]) == OPCODE_CALL_ASYNC;
#endif
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				int methodname_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(methodname_idx < 0 || methodname_idx >= _global_names_count);
				const StringName *methodname = &_global_names_ptr[methodname_idx];

				FoundryProfileZoneScriptSystemCall(methodname, source, name, *methodname, line);

				GET_INSTRUCTION_ARG(base, argc);
				Vector<Variant> erased_arg_storage;
				Vector<const Variant *> erased_argptr_storage;
				const Variant **argptrs = _erase_specialized_handles_for_typed_container_call(
						base, *methodname, instruction_args, argc, erased_arg_storage, erased_argptr_storage);

#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;

				if (FSLanguage::get_singleton()->profiling) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
				Variant::Type base_type = base->get_type();
				Object *base_obj = base->get_validated_object();
				StringName base_class = base_obj ? base_obj->get_class_name() : StringName();
#endif

				Variant temp_ret;
				Callable::CallError err;
				Variant *call_ret_dst = nullptr;
				if (call_ret) {
					GET_INSTRUCTION_ARG(ret, argc + 1);
					call_ret_dst = ret;
				}
				// An unqualified call inside a static frame targets the class slot, which holds the class
				// that declares the running function. Dispatching through the script directly lets the
				// nested frame keep the receiver the outer call began on, instead of resetting it to the
				// declaring class. Every other base names its own receiver and goes through `callp`.
				FoundryScript *inherited_delegation_target = nullptr;
				if (unlikely(p_static_self != nullptr && base == &stack[ADDR_STACK_CLASS])) {
					inherited_delegation_target = Object::cast_to<FoundryScript>(base->get_validated_object());
				}
				if (unlikely(inherited_delegation_target != nullptr)) {
					temp_ret = inherited_delegation_target->call_static_with_context(*methodname, argptrs, argc, err, *p_static_self);
				} else {
					base->callp(*methodname, argptrs, argc, temp_ret, err);
				}

				// Retroactive-conformance witness fallback. A trait method called on a receiver that does not
				// implement the method natively misses `Variant::callp`; consult the conformance registry
				// and, when a witness is registered, dispatch it with the receiver as `self`. Foundry Script
				// instances reach their witnesses through `FSInstance::callp` for class targets; native
				// objects and builtin values use this fallback (runs in both debug and release builds).
				if (err.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
					FSFunction *witness = nullptr;
					if (base->get_type() == Variant::OBJECT) {
						Object *witness_obj = base->get_validated_object();
						if (witness_obj != nullptr) {
							witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(witness_obj->get_class_name(), *methodname);
						}
					} else if (base->get_type() != Variant::NIL) {
						witness = FSConformanceRegistry::get_singleton()->find_builtin_witness_function(base->get_type(), *methodname);
					}
					if (witness != nullptr) {
						err.error = Callable::CallError::CALL_OK;
						temp_ret = witness->call_witness(*base, argptrs, argc, err);
					}
				}

				if (call_ret) {
					*call_ret_dst = temp_ret;
#ifdef DEBUG_ENABLED
					if (call_ret_dst->get_type() == Variant::NIL) {
						if (base_type == Variant::OBJECT) {
							if (base_obj) {
								MethodBind *method = ClassDB::get_method(base_class, *methodname);
								if (*methodname == CoreStringName(free_) || (method && !method->has_return())) {
									err_text = R"(Trying to get a return value of a method that returns "void")";
									OPCODE_BREAK;
								}
							}
						} else if (Variant::has_builtin_method(base_type, *methodname) && !Variant::has_builtin_method_return_value(base_type, *methodname)) {
							err_text = R"(Trying to get a return value of a method that returns "void")";
							OPCODE_BREAK;
						}
					}

					if (!call_async && call_ret_dst->get_type() == Variant::OBJECT) {
						// Check if getting a function state without await.
						bool was_freed = false;
						Object *obj = call_ret_dst->get_validated_object_with_check(was_freed);

						if (obj && obj->is_class_ptr(ScriptFunctionState::get_class_ptr_static())) {
							err_text = R"(Trying to call an async function without "await".)";
							OPCODE_BREAK;
						}
					}
#endif
				}
#ifdef DEBUG_ENABLED

				if (FSLanguage::get_singleton()->profiling) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					if (FSLanguage::get_singleton()->profile_native_calls && _profile_count_as_native(base_obj, *methodname)) {
						_profile_native_call(t_taken, *methodname, base_class);
					}
					function_call_time += t_taken;
				}

				if (err.error != Callable::CallError::CALL_OK) {
					String methodstr = *methodname;
					String basestr = _get_var_type(base);
					bool is_callable = false;

					if (methodstr == "call") {
						if (argc >= 1 && base->get_type() != Variant::CALLABLE) {
							methodstr = String(*argptrs[0]) + " (via call)";
							if (err.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
								err.argument += 1;
							}
						} else {
							methodstr = base->operator String() + " (Callable)";
							is_callable = true;
						}
					} else if (methodstr == "free") {
						if (err.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
							if (base->is_ref_counted()) {
								err_text = "Attempted to free a RefCounted object.";
								OPCODE_BREAK;
							} else if (base->get_type() == Variant::OBJECT) {
								err_text = "Attempted to free a locked object (calling or emitting).";
								OPCODE_BREAK;
							}
						}
					} else if (methodstr == "call_recursive" && basestr == "TreeItem") {
						if (argc >= 1) {
							methodstr = String(*argptrs[0]) + " (via TreeItem.call_recursive)";
							if (err.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
								err.argument += 1;
							}
						}
					}

					if (is_callable) {
						err_text = _get_callable_call_error(vformat("function '%s'", methodstr), *base, argptrs, argc, temp_ret, err);
					} else {
						err_text = _get_call_error(vformat("function '%s' in base '%s'", methodstr, basestr), argptrs, argc, temp_ret, err);
					}
					OPCODE_BREAK;
				}
#endif // DEBUG_ENABLED

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_ENUM_ASYNC)
			OPCODE(OPCODE_CALL_ENUM_RETURN)
			OPCODE(OPCODE_CALL_ENUM) {
				const bool call_ret = (_code_ptr[ip]) != OPCODE_CALL_ENUM;
#ifdef DEBUG_ENABLED
				const bool call_async = (_code_ptr[ip]) == OPCODE_CALL_ENUM_ASYNC;
#endif
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(7 + instr_arg_count);

				ip += instr_arg_count;

				const int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				const int owner_path_idx = _code_ptr[ip + 2];
				const int owner_class_idx = _code_ptr[ip + 3];
				const int enum_type_idx = _code_ptr[ip + 4];
				const int function_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(owner_path_idx < 0 || owner_path_idx >= _global_names_count);
				GD_ERR_BREAK(owner_class_idx < 0 || owner_class_idx >= _global_names_count);
				GD_ERR_BREAK(enum_type_idx < 0 || enum_type_idx >= _global_names_count);
				GD_ERR_BREAK(function_idx < 0 || function_idx >= _global_names_count);

				const String owner_path = String(_global_names_ptr[owner_path_idx]);
				const StringName owner_class = _global_names_ptr[owner_class_idx];
				const StringName enum_type = _global_names_ptr[enum_type_idx];
				const StringName function_name = _global_names_ptr[function_idx];
				const int call_kind = _code_ptr[ip + 6];
				GD_ERR_BREAK(call_kind != 0 && call_kind != 1);
				const bool is_static = call_kind == 1;

				const String call_identity = vformat("%s enum function \"%s.%s()\" owned by class \"%s\" in script \"%s\"",
						is_static ? "static" : "instance", enum_type, function_name, owner_class, owner_path);

				if (owner_path.is_empty() || owner_class == StringName() ||
						enum_type == StringName() || function_name == StringName()) {
					err_text = "Cannot resolve " + call_identity + ": its compiled owner identity is incomplete.";
					OPCODE_BREAK;
				}

				GET_INSTRUCTION_ARG(base, argc);
				// An int-backed enum's receiver is its integer value; a tagged union's receiver is the
				// read-only `[tag, payload...]` Array its cases erase to. Both are bound as `self`.
				if (!is_static && base->get_type() != Variant::INT && base->get_type() != Variant::ARRAY) {
					err_text = vformat("Cannot call %s: the receiver is %s instead of an enum value.",
							call_identity, Variant::get_type_name(base->get_type()));
					OPCODE_BREAK;
				}

				FoundryScript *current_root = script != nullptr ? script->get_root_script() : nullptr;
				Ref<FoundryScript> loaded_owner_root;
				FoundryScript *owner_root = nullptr;
				if (current_root != nullptr &&
						FoundryScript::is_canonically_equal_paths(owner_path, current_root->get_script_path())) {
					owner_root = current_root;
				} else {
					Error load_error = OK;
					loaded_owner_root = FSCache::get_full_script(owner_path, load_error,
							script != nullptr ? script->get_script_path() : String());
					if (load_error != OK || loaded_owner_root.is_null()) {
						err_text = vformat("Cannot resolve %s: the owner script could not be loaded (error %d).",
								call_identity, int(load_error));
						OPCODE_BREAK;
					}
					owner_root = loaded_owner_root.ptr();
				}

				FoundryScript *owner_script = owner_root->find_class(String(owner_class));
				if (owner_script == nullptr) {
					err_text = "Cannot resolve " + call_identity + ": the owner class was not found.";
					OPCODE_BREAK;
				}

				FSFunction *enum_function = owner_script->get_enum_function(enum_type, function_name, is_static);
				if (enum_function == nullptr) {
					err_text = "Cannot resolve " + call_identity + ": the compiled function was not found.";
					OPCODE_BREAK;
				}

				// Enum dispatch supplies no `FSStaticSelfContext`: an enum has no ancestors and no
				// subtypes, so `Self` inside an enum function's signature is resolved eagerly to the
				// exact enum type at analysis time rather than left for late runtime binding (see
				// `FSAnalyzer::enum_self_type` and the `FSParser::DataType::ENUM` case in
				// `_gdtype_from_datatype`, which always compiles to a plain `FSDataType::BUILTIN` and
				// never sets `is_self_type`). This check turns a future regression of that invariant
				// into a loud, diagnosable error instead of silently calling with a missing receiver.
				if (is_static && enum_function->has_self_referencing_signature()) {
					err_text = "Cannot resolve " + call_identity +
							": its signature references \"Self\", but enum dispatch has no static receiver to resolve it against.";
					OPCODE_BREAK;
				}

				const Variant **argptrs = (const Variant **)instruction_args;
				Callable::CallError err;
				Variant result = is_static
						? enum_function->call(nullptr, argptrs, argc, err)
						: enum_function->call_witness(*base, argptrs, argc, err);
				if (err.error != Callable::CallError::CALL_OK) {
					err_text = _get_call_error(call_identity, argptrs, argc, result, err);
					OPCODE_BREAK;
				}

				if (call_ret) {
					GET_INSTRUCTION_ARG(ret, argc + 1);
					*ret = result;
#ifdef DEBUG_ENABLED
					if (!call_async && ret->get_type() == Variant::OBJECT) {
						bool was_freed = false;
						Object *result_object = ret->get_validated_object_with_check(was_freed);
						if (result_object != nullptr && result_object->is_class_ptr(ScriptFunctionState::get_class_ptr_static())) {
							err_text = R"(Trying to call an async function without "await".)";
							OPCODE_BREAK;
						}
					}
#endif
				}

				ip += 7;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_METHOD_BIND)
			OPCODE(OPCODE_CALL_METHOD_BIND_RET) {
				bool call_ret = (_code_ptr[ip]) == OPCODE_CALL_METHOD_BIND_RET;
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);
				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 2]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				GET_INSTRUCTION_ARG(base, argc);

#ifdef DEBUG_ENABLED
				bool freed = false;
				Object *base_obj = base->get_validated_object_with_check(freed);
				if (freed) {
					err_text = METHOD_CALL_ON_FREED_INSTANCE_ERROR(method);
					OPCODE_BREAK;
				} else if (!base_obj) {
					err_text = METHOD_CALL_ON_NULL_VALUE_ERROR(method);
					OPCODE_BREAK;
				}
#else
				Object *base_obj = base->operator Object *();
#endif
				Variant **argptrs = instruction_args;

#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				Variant temp_ret;
				Callable::CallError err;
				if (call_ret) {
					GET_INSTRUCTION_ARG(ret, argc + 1);
					temp_ret = method->call(base_obj, (const Variant **)argptrs, argc, err);
					*ret = temp_ret;
				} else {
					temp_ret = method->call(base_obj, (const Variant **)argptrs, argc, err);
				}

#ifdef DEBUG_ENABLED

				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}

				if (err.error != Callable::CallError::CALL_OK) {
					String methodstr = method->get_name();
					String basestr = _get_var_type(base);

					if (methodstr == "call") {
						if (argc >= 1) {
							methodstr = String(*argptrs[0]) + " (via call)";
							if (err.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
								err.argument += 1;
							}
						}
					} else if (methodstr == "free") {
						if (err.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
							if (base->is_ref_counted()) {
								err_text = "Attempted to free a RefCounted object.";
								OPCODE_BREAK;
							} else if (base->get_type() == Variant::OBJECT) {
								err_text = "Attempted to free a locked object (calling or emitting).";
								OPCODE_BREAK;
							}
						}
					}
					err_text = _get_call_error("function '" + methodstr + "' in base '" + basestr + "'", (const Variant **)argptrs, argc, temp_ret, err);
					OPCODE_BREAK;
				}
#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_BUILTIN_STATIC) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(4 + instr_arg_count);

				ip += instr_arg_count;

				GD_ERR_BREAK(_code_ptr[ip + 1] < 0 || _code_ptr[ip + 1] >= Variant::VARIANT_MAX);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 1];

				int methodname_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(methodname_idx < 0 || methodname_idx >= _global_names_count);
				const StringName *methodname = &_global_names_ptr[methodname_idx];

				FoundryProfileZoneScriptSystemCall(methodname, source, name, *methodname, line);

				int argc = _code_ptr[ip + 3];
				GD_ERR_BREAK(argc < 0);

				GET_INSTRUCTION_ARG(ret, argc);

				const Variant **argptrs = const_cast<const Variant **>(instruction_args);

				Callable::CallError err;
				Variant::call_static(builtin_type, *methodname, argptrs, argc, *ret, err);

				// Retroactive-conformance fallback. A `static` witness supplied by an external
				// `extend <BuiltinType> uses Trait: ...` is not part of the builtin's own static surface,
				// so it misses `Variant::call_static`; consult the conformance registry and dispatch it
				// with no instance (runs in both debug and release builds).
				if (err.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
					FSFunction *witness = FSConformanceRegistry::get_singleton()->find_builtin_witness_function(builtin_type, *methodname);
					if (witness != nullptr && witness->is_static()) {
						err.error = Callable::CallError::CALL_OK;
						const FSStaticSelfContext witness_static_self = FSStaticSelfContext::for_builtin_type(builtin_type);
						*ret = witness->call(nullptr, argptrs, argc, err, nullptr, nullptr, &witness_static_self);
					}
				}

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					err_text = _get_call_error("static function '" + methodname->operator String() + "' in type '" + Variant::get_type_name(builtin_type) + "'", argptrs, argc, *ret, err);
					OPCODE_BREAK;
				}
#endif

				ip += 4;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_NATIVE_STATIC) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				GD_ERR_BREAK(_code_ptr[ip + 1] < 0 || _code_ptr[ip + 1] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 1]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				int argc = _code_ptr[ip + 2];
				GD_ERR_BREAK(argc < 0);

				GET_INSTRUCTION_ARG(ret, argc);

				const Variant **argptrs = const_cast<const Variant **>(instruction_args);

#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				Callable::CallError err;
				*ret = method->call(nullptr, argptrs, argc, err);

#ifdef DEBUG_ENABLED
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}
#endif

				if (err.error != Callable::CallError::CALL_OK) {
					err_text = _get_call_error("static function '" + method->get_name().operator String() + "' in type '" + method->get_instance_class().operator String() + "'", argptrs, argc, *ret, err);
					OPCODE_BREAK;
				}

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 2]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				Variant **argptrs = instruction_args;

#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				GET_INSTRUCTION_ARG(ret, argc);
				method->validated_call(nullptr, (const Variant **)argptrs, ret);

#ifdef DEBUG_ENABLED
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}
#endif

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 2]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				Variant **argptrs = instruction_args;
#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				GET_INSTRUCTION_ARG(ret, argc);
				VariantInternal::initialize(ret, Variant::NIL);
				method->validated_call(nullptr, (const Variant **)argptrs, nullptr);

#ifdef DEBUG_ENABLED
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}
#endif

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 2]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				GET_INSTRUCTION_ARG(base, argc);

#ifdef DEBUG_ENABLED
				bool freed = false;
				Object *base_obj = base->get_validated_object_with_check(freed);
				if (freed) {
					err_text = METHOD_CALL_ON_FREED_INSTANCE_ERROR(method);
					OPCODE_BREAK;
				} else if (!base_obj) {
					err_text = METHOD_CALL_ON_NULL_VALUE_ERROR(method);
					OPCODE_BREAK;
				}
#else
				Object *base_obj = *VariantInternal::get_object(base);
#endif

				Variant **argptrs = instruction_args;

#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				GET_INSTRUCTION_ARG(ret, argc + 1);
				method->validated_call(base_obj, (const Variant **)argptrs, ret);

#ifdef DEBUG_ENABLED
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}
#endif

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _methods_count);
				MethodBind *method = _methods_ptr[_code_ptr[ip + 2]];

				FoundryProfileZoneScriptSystemCall(method, source, name, method->get_name(), line);

				GET_INSTRUCTION_ARG(base, argc);
#ifdef DEBUG_ENABLED
				bool freed = false;
				Object *base_obj = base->get_validated_object_with_check(freed);
				if (freed) {
					err_text = METHOD_CALL_ON_FREED_INSTANCE_ERROR(method);
					OPCODE_BREAK;
				} else if (!base_obj) {
					err_text = METHOD_CALL_ON_NULL_VALUE_ERROR(method);
					OPCODE_BREAK;
				}
#else
				Object *base_obj = *VariantInternal::get_object(base);
#endif
				Variant **argptrs = instruction_args;
#ifdef DEBUG_ENABLED
				uint64_t call_time = 0;
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					call_time = OS::get_singleton()->get_ticks_usec();
				}
#endif

				GET_INSTRUCTION_ARG(ret, argc + 1);
				VariantInternal::initialize(ret, Variant::NIL);
				method->validated_call(base_obj, (const Variant **)argptrs, nullptr);

#ifdef DEBUG_ENABLED
				if (FSLanguage::get_singleton()->profiling && FSLanguage::get_singleton()->profile_native_calls) {
					uint64_t t_taken = OS::get_singleton()->get_ticks_usec() - call_time;
					_profile_native_call(t_taken, method->get_name(), method->get_instance_class());
					function_call_time += t_taken;
				}
#endif

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_BUILTIN_TYPE_VALIDATED) {
				LOAD_INSTRUCTION_ARGS

				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GET_INSTRUCTION_ARG(base, argc);

				int method_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(method_idx < 0 || method_idx >= _builtin_methods_count);
				Variant::ValidatedBuiltInMethod method = _builtin_methods_ptr[method_idx];
				const StringName method_name = method_idx < builtin_method_names.size() ? builtin_method_names[method_idx] : StringName();
				Vector<Variant> erased_arg_storage;
				Vector<const Variant *> erased_argptr_storage;
				const Variant **argptrs = _erase_specialized_handles_for_typed_container_call(
						base, method_name, instruction_args, argc, erased_arg_storage, erased_argptr_storage);

				GET_INSTRUCTION_ARG(ret, argc + 1);
				method(base, argptrs, argc, ret);

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_UTILITY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _global_names_count);
				StringName function = _global_names_ptr[_code_ptr[ip + 2]];

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				Callable::CallError err;
				Variant::call_utility_function(function, dst, (const Variant **)argptrs, argc, err);

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					String methodstr = function;
					if (dst->get_type() == Variant::STRING && !dst->operator String().is_empty()) {
						// Call provided error string.
						err_text = vformat(R"*(Error calling utility function "%s()": %s)*", methodstr, *dst);
					} else {
						err_text = _get_call_error(vformat(R"*(utility function "%s()")*", methodstr), (const Variant **)argptrs, argc, *dst, err);
					}
					OPCODE_BREAK;
				}
#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_UTILITY_VALIDATED) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _utilities_count);
				Variant::ValidatedUtilityFunction function = _utilities_ptr[_code_ptr[ip + 2]];

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				function(dst, (const Variant **)argptrs, argc);

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_FOUNDRY_SCRIPT_UTILITY) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				GD_ERR_BREAK(_code_ptr[ip + 2] < 0 || _code_ptr[ip + 2] >= _gds_utilities_count);
				FSUtilityFunctions::FunctionPtr function = _gds_utilities_ptr[_code_ptr[ip + 2]];

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				Callable::CallError err;
				function(dst, (const Variant **)argptrs, argc, err);

#ifdef DEBUG_ENABLED
				if (err.error != Callable::CallError::CALL_OK) {
					String methodstr = gds_utilities_names[_code_ptr[ip + 2]];
					if (dst->get_type() == Variant::STRING && !dst->operator String().is_empty()) {
						// Call provided error string.
						err_text = vformat(R"*(Error calling FoundryScript utility function "%s()": %s)*", methodstr, *dst);
					} else {
						err_text = _get_call_error(vformat(R"*(FoundryScript utility function "%s()")*", methodstr), (const Variant **)argptrs, argc, *dst, err);
					}
					OPCODE_BREAK;
				}
#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CALL_SELF_BASE) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(3 + instr_arg_count);

				ip += instr_arg_count;

				int argc = _code_ptr[ip + 1];
				GD_ERR_BREAK(argc < 0);

				int self_fun = _code_ptr[ip + 2];
#ifdef DEBUG_ENABLED
				if (self_fun < 0 || self_fun >= _global_names_count) {
					err_text = "compiler bug, function name not found";
					OPCODE_BREAK;
				}
#endif
				const StringName *methodname = &_global_names_ptr[self_fun];

				FoundryProfileZoneScriptSystemCall(methodname, source, name, *methodname, line);

				Variant **argptrs = instruction_args;

				GET_INSTRUCTION_ARG(dst, argc);

				const FoundryScript *gds = _script;

				HashMap<StringName, FSFunction *>::ConstIterator E;
				while (gds->base.ptr()) {
					gds = gds->base.ptr();
					E = gds->member_functions.find(*methodname);
					if (E) {
						break;
					}
				}

				Callable::CallError err;

				if (E) {
					// A `super` delegation continues the call the outer receiver began; it must not reset
					// the receiver to the base class that declares the delegated implementation.
					*dst = E->value->call(p_instance, (const Variant **)argptrs, argc, err, nullptr, nullptr, p_static_self);
				} else if (gds->native.ptr()) {
					if (*methodname != FSLanguage::get_singleton()->strings._init) {
						MethodBind *mb = ClassDB::get_method(gds->native->get_name(), *methodname);
						if (!mb) {
							err.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
						} else {
							*dst = mb->call(p_instance->owner, (const Variant **)argptrs, argc, err);
						}
					} else {
						err.error = Callable::CallError::CALL_OK;
					}
				} else {
					if (*methodname != FSLanguage::get_singleton()->strings._init) {
						err.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
					} else {
						err.error = Callable::CallError::CALL_OK;
					}
				}

				if (err.error != Callable::CallError::CALL_OK) {
					String methodstr = *methodname;
					err_text = _get_call_error("function '" + methodstr + "'", (const Variant **)argptrs, argc, *dst, err);

					OPCODE_BREAK;
				}

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_AWAIT) {
				CHECK_SPACE(2);

				// Do the one-shot connect.
				GET_VARIANT_PTR(argobj, 0);

				Signal sig;
				bool is_signal = true;

				{
					Variant result = *argobj;

					if (argobj->get_type() == Variant::OBJECT) {
						bool was_freed = false;
						Object *obj = argobj->get_validated_object_with_check(was_freed);

						if (was_freed) {
							err_text = "Trying to await on a freed object.";
							OPCODE_BREAK;
						}

						// Is this even possible to be null at this point?
						if (obj) {
							if (obj->is_class_ptr(ScriptFunctionState::get_class_ptr_static())) {
								result = Signal(obj, SNAME("completed"));
							}
						}
					}

					if (result.get_type() != Variant::SIGNAL) {
						// Not async, return immediately using the target from OPCODE_AWAIT_RESUME.
						GET_VARIANT_PTR(target, 2);
						*target = result;
						ip += 4; // Skip OPCODE_AWAIT_RESUME and its data.
						is_signal = false;
					} else {
						sig = result;
					}
				}

				if (is_signal) {
					Ref<FSFunctionState> gdfs = memnew(FSFunctionState);
					gdfs->function = this;

					gdfs->state.stack.resize(alloca_size);

					// First `FIXED_ADDRESSES_MAX` stack addresses are special, so we just skip them here.
					for (int i = FIXED_ADDRESSES_MAX; i < _stack_size; i++) {
						memnew_placement(&gdfs->state.stack.write[sizeof(Variant) * i], Variant(stack[i]));
					}
					gdfs->state.stack_size = _stack_size;
					gdfs->state.has_self_override = p_self_override != nullptr;
					if (p_self_override != nullptr) {
						gdfs->state.self_override = *p_self_override;
					}
					// The borrowed descriptor dies with the caller's frame, so the suspended state owns
					// a copy of it until this call resumes.
					if (p_static_self != nullptr) {
						gdfs->state.static_self = *p_static_self;
					}
					gdfs->state.ip = ip + 2;
					gdfs->state.line = line;
					gdfs->state.script = _script;
					{
						MutexLock lock(FSLanguage::get_singleton()->mutex);
						_script->pending_func_states.add(&gdfs->scripts_list);
						if (p_instance) {
							gdfs->state.instance = p_instance;
							p_instance->pending_func_states.add(&gdfs->instances_list);
						} else {
							gdfs->state.instance = nullptr;
						}
					}
#ifdef DEBUG_ENABLED
					gdfs->state.function_name = name;
					gdfs->state.script_path = _script->get_script_path();
#endif
					gdfs->state.defarg = defarg;
					gdfs->function = this;

					if (p_state) {
						// Pass down the signal from the first state.
						gdfs->state.completed = p_state->completed;
					} else {
						gdfs->state.completed = Signal(gdfs.ptr(), SNAME("completed"));
					}

					retvalue = gdfs;

					Error err = sig.connect(Callable(gdfs.ptr(), "_signal_callback").bind(retvalue), Object::CONNECT_ONE_SHOT);
					if (err != OK) {
						err_text = "Error connecting to signal: " + sig.get_name() + " during await.";
						OPCODE_BREAK;
					}

					awaited = true;

#ifdef DEBUG_ENABLED
					exit_ok = true;
#endif
					OPCODE_BREAK;
				}
			}
			DISPATCH_OPCODE; // Needed for synchronous calls (when result is immediately available).

			OPCODE(OPCODE_AWAIT_RESUME) {
				CHECK_SPACE(2);
#ifdef DEBUG_ENABLED
				if (!p_state) {
					err_text = ("Invalid Resume (bug?)");
					OPCODE_BREAK;
				}
#endif
				{
					String guard_message;
					FSScriptTestGuard::UnwindReason guard_reason = FSScriptTestGuard::UNWIND_NONE;
					if (FSScriptTestGuard::checkpoint(guard_message, guard_reason)) {
						FSScriptTestGuard::mark_unwind(guard_reason, guard_message);
						err_text = guard_message;
						OPCODE_BREAK;
					}
				}
				GET_VARIANT_PTR(result, 0);
				*result = p_state->result;
				ip += 2;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CREATE_LAMBDA) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);

				ip += instr_arg_count;

				int captures_count = _code_ptr[ip + 1];
				GD_ERR_BREAK(captures_count < 0);

				int lambda_index = _code_ptr[ip + 2];
				GD_ERR_BREAK(lambda_index < 0 || lambda_index >= _lambdas_count);
				FSFunction *lambda = _lambdas_ptr[lambda_index];

				Vector<Variant> captures;
				captures.resize(captures_count);
				for (int i = 0; i < captures_count; i++) {
					GET_INSTRUCTION_ARG(arg, i);
					captures.write[i] = *arg;
				}

				FSLambdaCallable *callable = memnew(FSLambdaCallable(Ref<FoundryScript>(script), lambda, captures));

				GET_INSTRUCTION_ARG(result, captures_count);
				*result = Callable(callable);

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_CREATE_SELF_LAMBDA) {
				LOAD_INSTRUCTION_ARGS
				CHECK_SPACE(2 + instr_arg_count);

				GD_ERR_BREAK(p_instance == nullptr);

				ip += instr_arg_count;

				int captures_count = _code_ptr[ip + 1];
				GD_ERR_BREAK(captures_count < 0);

				int lambda_index = _code_ptr[ip + 2];
				GD_ERR_BREAK(lambda_index < 0 || lambda_index >= _lambdas_count);
				FSFunction *lambda = _lambdas_ptr[lambda_index];

				Vector<Variant> captures;
				captures.resize(captures_count);
				for (int i = 0; i < captures_count; i++) {
					GET_INSTRUCTION_ARG(arg, i);
					captures.write[i] = *arg;
				}

				FSLambdaSelfCallable *callable;
				if (Object::cast_to<RefCounted>(p_instance->owner)) {
					callable = memnew(FSLambdaSelfCallable(Ref<RefCounted>(Object::cast_to<RefCounted>(p_instance->owner)), lambda, captures));
				} else {
					callable = memnew(FSLambdaSelfCallable(p_instance->owner, lambda, captures));
				}

				GET_INSTRUCTION_ARG(result, captures_count);
				*result = Callable(callable);

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_JUMP) {
				CHECK_SPACE(2);
				int to = _code_ptr[ip + 1];

				GD_ERR_BREAK(to < 0 || to > _code_size);
				ip = to;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_JUMP_IF) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(test, 0);

				bool result = test->booleanize();

				if (result) {
					int to = _code_ptr[ip + 2];
					GD_ERR_BREAK(to < 0 || to > _code_size);
					ip = to;
				} else {
					ip += 3;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_JUMP_IF_NOT) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(test, 0);

				bool result = test->booleanize();

				if (!result) {
					int to = _code_ptr[ip + 2];
					GD_ERR_BREAK(to < 0 || to > _code_size);
					ip = to;
				} else {
					ip += 3;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_JUMP_TO_DEF_ARGUMENT) {
				CHECK_SPACE(2);
				ip = _default_arg_ptr[defarg];
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_JUMP_IF_SHARED) {
				CHECK_SPACE(3);

				GET_VARIANT_PTR(val, 0);

				if (val->is_shared()) {
					int to = _code_ptr[ip + 2];
					GD_ERR_BREAK(to < 0 || to > _code_size);
					ip = to;
				} else {
					ip += 3;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_RETURN) {
				CHECK_SPACE(2);
				GET_VARIANT_PTR(r, 0);
				retvalue = *r;
#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_RETURN_TYPED_BUILTIN) {
				CHECK_SPACE(3);
				GET_VARIANT_PTR(r, 0);

				int ret_type_operand = _code_ptr[ip + 2];
				const bool is_nullable = ret_type_operand & FSFunction::NULLABLE_TYPE_OPERAND_FLAG;
				Variant::Type ret_type = (Variant::Type)(ret_type_operand & ~FSFunction::NULLABLE_TYPE_OPERAND_FLAG);
				GD_ERR_BREAK(ret_type < 0 || ret_type >= Variant::VARIANT_MAX);

				if (is_nullable && r->get_type() == Variant::NIL) {
					// A nullable return type returns null as-is instead of converting it to the underlying type.
					retvalue = *r;
				} else if (r->get_type() != ret_type) {
					if (Variant::can_convert_strict(r->get_type(), ret_type)) {
						Callable::CallError ce;
						Variant::construct(ret_type, retvalue, const_cast<const Variant **>(&r), 1, ce);
					} else {
						err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
								Variant::get_type_name(r->get_type()), Variant::get_type_name(ret_type));
						OPCODE_BREAK;
					}
				} else {
					retvalue = *r;
				}
#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif // DEBUG_ENABLED
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_RETURN_TYPED_ARRAY) {
				CHECK_SPACE(5);
				GET_VARIANT_PTR(r, 0);

				GET_VARIANT_PTR(type_info, 1);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 3];
				int native_type_idx = _code_ptr[ip + 4];
				GD_ERR_BREAK(native_type_idx < 0 || native_type_idx >= _global_names_count);
				const StringName native_type = _global_names_ptr[native_type_idx];
				const ContainerType expected_type = _container_type_from_type_info(*type_info, builtin_type, native_type);

				if (r->get_type() != Variant::ARRAY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "Array[%s]".)",
							Variant::get_type_name(r->get_type()), _get_element_type(expected_type));
#endif
					OPCODE_BREAK;
				}

				Array *array = VariantInternal::get_array(r);

				if (array->get_element_type() != expected_type) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to return an array of type "%s" where expected return type is "Array[%s]".)",
							_get_var_type(r), _get_element_type(expected_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				retvalue = *array;

#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif // DEBUG_ENABLED
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_RETURN_TYPED_DICTIONARY) {
				CHECK_SPACE(8);
				GET_VARIANT_PTR(r, 0);

				GET_VARIANT_PTR(key_type_info, 1);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 4];
				int key_native_type_idx = _code_ptr[ip + 5];
				GD_ERR_BREAK(key_native_type_idx < 0 || key_native_type_idx >= _global_names_count);
				const StringName key_native_type = _global_names_ptr[key_native_type_idx];
				const ContainerType expected_key_type = _container_type_from_type_info(*key_type_info, key_builtin_type, key_native_type);

				GET_VARIANT_PTR(value_type_info, 2);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 6];
				int value_native_type_idx = _code_ptr[ip + 7];
				GD_ERR_BREAK(value_native_type_idx < 0 || value_native_type_idx >= _global_names_count);
				const StringName value_native_type = _global_names_ptr[value_native_type_idx];
				const ContainerType expected_value_type = _container_type_from_type_info(*value_type_info, value_builtin_type, value_native_type);

				if (r->get_type() != Variant::DICTIONARY) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to return a value of type "%s" where expected return type is "Dictionary[%s, %s]".)",
							_get_var_type(r), _get_element_type(expected_key_type),
							_get_element_type(expected_value_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				Dictionary *dictionary = VariantInternal::get_dictionary(r);

				if (dictionary->get_key_type() != expected_key_type || dictionary->get_value_type() != expected_value_type) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to return a dictionary of type "%s" where expected return type is "Dictionary[%s, %s]".)",
							_get_var_type(r), _get_element_type(expected_key_type),
							_get_element_type(expected_value_type));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				}

				retvalue = *dictionary;

#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif // DEBUG_ENABLED
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_RETURN_TYPED_NATIVE) {
				CHECK_SPACE(4);
				GET_VARIANT_PTR(r, 0);

				GET_VARIANT_PTR(type, 1);
				FSNativeClass *nc = Object::cast_to<FSNativeClass>(type->operator Object *());
				GD_ERR_BREAK(!nc);
				const bool is_type_handle = _code_ptr[ip + 3];
				FSSpecializedClassHandle *specialized_handle = !is_type_handle ? _specialized_handle_assignable_to_native_script(r, nc->get_name()) : nullptr;

				if (is_type_handle) {
					if (!_make_native_type_handle_type(nc).is_type(*r)) {
#ifdef DEBUG_ENABLED
						err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "Type[%s]".)",
								_get_var_type(r), nc->get_name());
#endif // DEBUG_ENABLED
						OPCODE_BREAK;
					}
				} else if (r->get_type() != Variant::OBJECT && r->get_type() != Variant::NIL) {
					err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
							Variant::get_type_name(r->get_type()), nc->get_name());
					OPCODE_BREAK;
				} else {
#ifdef DEBUG_ENABLED
					bool freed = false;
					Object *ret_obj = r->get_validated_object_with_check(freed);

					if (freed) {
						err_text = "Trying to return a previously freed instance.";
						OPCODE_BREAK;
					}
#else
					Object *ret_obj = r->operator Object *();
#endif // DEBUG_ENABLED
					if (ret_obj && specialized_handle == nullptr &&
							!ClassDB::is_parent_class(ret_obj->get_class_name(), nc->get_name())) {
#ifdef DEBUG_ENABLED
						err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
								ret_obj->get_class_name(), nc->get_name());
#endif // DEBUG_ENABLED
						OPCODE_BREAK;
					}
				}
				if (specialized_handle != nullptr) {
					retvalue = specialized_handle->get_specialized_script();
				} else {
					retvalue = *r;
				}

#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif // DEBUG_ENABLED
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_RETURN_TYPED_SCRIPT) {
				CHECK_SPACE(4);
				GET_VARIANT_PTR(r, 0);

				GET_VARIANT_PTR(type, 1);
				FSDataType expected_handle_type;
				Script *base_type = _script_type_from_type_info(*type, &expected_handle_type);
				GD_ERR_BREAK(!base_type);
				const bool is_type_handle = _code_ptr[ip + 3];

				if (is_type_handle) {
					if (!expected_handle_type.is_type(*r)) {
#ifdef DEBUG_ENABLED
						err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
								_get_var_type(r), _get_type_handle_type_name(expected_handle_type, base_type));
#endif // DEBUG_ENABLED
						OPCODE_BREAK;
					}
				} else if (r->get_type() != Variant::OBJECT && r->get_type() != Variant::NIL) {
#ifdef DEBUG_ENABLED
					err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
							Variant::get_type_name(r->get_type()), FoundryScript::debug_get_script_name(Ref<Script>(base_type)));
#endif // DEBUG_ENABLED
					OPCODE_BREAK;
				} else {
#ifdef DEBUG_ENABLED
					bool freed = false;
					Object *ret_obj = r->get_validated_object_with_check(freed);

					if (freed) {
						err_text = "Trying to return a previously freed instance.";
						OPCODE_BREAK;
					}
#else
					Object *ret_obj = r->operator Object *();
#endif // DEBUG_ENABLED

					if (ret_obj) {
						ScriptInstance *ret_inst = ret_obj->get_script_instance();
						if (!ret_inst) {
#ifdef DEBUG_ENABLED
							err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
									ret_obj->get_class_name(), FoundryScript::debug_get_script_name(Ref<FoundryScript>(base_type)));
#endif // DEBUG_ENABLED
							OPCODE_BREAK;
						}

						Script *ret_type = ret_obj->get_script_instance()->get_script().ptr();
						bool valid = false;

						while (ret_type) {
							if (ret_type == base_type) {
								valid = true;
								break;
							}
							ret_type = ret_type->get_base_script().ptr();
						}

						if (!valid) {
#ifdef DEBUG_ENABLED
							err_text = vformat(R"(Trying to return value of type "%s" from a function whose return type is "%s".)",
									FoundryScript::debug_get_script_name(ret_obj->get_script_instance()->get_script()), FoundryScript::debug_get_script_name(Ref<FoundryScript>(base_type)));
#endif // DEBUG_ENABLED
							OPCODE_BREAK;
						}
					}
				}
				retvalue = *r;

#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif // DEBUG_ENABLED
				OPCODE_BREAK;
			}

			OPCODE(OPCODE_ITERATE_BEGIN) {
				CHECK_SPACE(8); // Space for this and a regular iterate.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				*counter = Variant();

				bool valid;
				if (!container->iter_init(*counter, valid)) {
#ifdef DEBUG_ENABLED
					if (!valid) {
						err_text = "Unable to iterate on object of type '" + Variant::get_type_name(container->get_type()) + "'.";
						OPCODE_BREAK;
					}
#endif
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);

					*iterator = container->iter_get(*counter, valid);
#ifdef DEBUG_ENABLED
					if (!valid) {
						err_text = "Unable to obtain iterator object of type '" + Variant::get_type_name(container->get_type()) + "'.";
						OPCODE_BREAK;
					}
#endif
					ip += 5; // Skip regular iterate which is always next.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_INT) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				int64_t size = *VariantInternal::get_int(container);

				VariantInternal::initialize(counter, Variant::INT);
				*VariantInternal::get_int(counter) = 0;

				if (size > 0) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::INT);
					*VariantInternal::get_int(iterator) = 0;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_FLOAT) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				double size = *VariantInternal::get_float(container);

				VariantInternal::initialize(counter, Variant::FLOAT);
				*VariantInternal::get_float(counter) = 0.0;

				if (size > 0) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::FLOAT);
					*VariantInternal::get_float(iterator) = 0;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_VECTOR2) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Vector2 *bounds = VariantInternal::get_vector2(container);

				VariantInternal::initialize(counter, Variant::FLOAT);
				*VariantInternal::get_float(counter) = bounds->x;

				if (bounds->x < bounds->y) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::FLOAT);
					*VariantInternal::get_float(iterator) = bounds->x;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_VECTOR2I) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Vector2i *bounds = VariantInternal::get_vector2i(container);

				VariantInternal::initialize(counter, Variant::FLOAT);
				*VariantInternal::get_int(counter) = bounds->x;

				if (bounds->x < bounds->y) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::INT);
					*VariantInternal::get_int(iterator) = bounds->x;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_VECTOR3) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Vector3 *bounds = VariantInternal::get_vector3(container);
				double from = bounds->x;
				double to = bounds->y;
				double step = bounds->z;

				VariantInternal::initialize(counter, Variant::FLOAT);
				*VariantInternal::get_float(counter) = from;

				bool do_continue = from == to ? false : (from < to ? step > 0 : step < 0);

				if (do_continue) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::FLOAT);
					*VariantInternal::get_float(iterator) = from;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_VECTOR3I) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Vector3i *bounds = VariantInternal::get_vector3i(container);
				int64_t from = bounds->x;
				int64_t to = bounds->y;
				int64_t step = bounds->z;

				VariantInternal::initialize(counter, Variant::INT);
				*VariantInternal::get_int(counter) = from;

				bool do_continue = from == to ? false : (from < to ? step > 0 : step < 0);

				if (do_continue) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::INT);
					*VariantInternal::get_int(iterator) = from;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_STRING) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				String *str = VariantInternal::get_string(container);

				VariantInternal::initialize(counter, Variant::INT);
				*VariantInternal::get_int(counter) = 0;

				if (!str->is_empty()) {
					GET_VARIANT_PTR(iterator, 2);
					VariantInternal::initialize(iterator, Variant::STRING);
					*VariantInternal::get_string(iterator) = str->substr(0, 1);

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_DICTIONARY) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Dictionary *dict = VariantInternal::get_dictionary(container);
				const Variant *next = dict->next(nullptr);

				if (!dict->is_empty()) {
					GET_VARIANT_PTR(iterator, 2);
					*counter = *next;
					*iterator = *next;

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_ARRAY) {
				CHECK_SPACE(8); // Check space for iterate instruction too.

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				Array *array = VariantInternal::get_array(container);

				VariantInternal::initialize(counter, Variant::INT);
				*VariantInternal::get_int(counter) = 0;

				if (!array->is_empty()) {
					GET_VARIANT_PTR(iterator, 2);
					*iterator = array->get(0);

					// Skip regular iterate.
					ip += 5;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

#define OPCODE_ITERATE_BEGIN_PACKED_ARRAY(m_var_type, m_elem_type, m_get_func, m_var_ret_type, m_ret_type, m_ret_get_func) \
	OPCODE(OPCODE_ITERATE_BEGIN_PACKED_##m_var_type##_ARRAY) {                                                             \
		CHECK_SPACE(8);                                                                                                    \
		GET_VARIANT_PTR(counter, 0);                                                                                       \
		GET_VARIANT_PTR(container, 1);                                                                                     \
		Vector<m_elem_type> *array = VariantInternal::m_get_func(container);                                               \
		VariantInternal::initialize(counter, Variant::INT);                                                                \
		*VariantInternal::get_int(counter) = 0;                                                                            \
		if (!array->is_empty()) {                                                                                          \
			GET_VARIANT_PTR(iterator, 2);                                                                                  \
			VariantInternal::initialize(iterator, Variant::m_var_ret_type);                                                \
			m_ret_type *it = VariantInternal::m_ret_get_func(iterator);                                                    \
			*it = array->get(0);                                                                                           \
			ip += 5;                                                                                                       \
		} else {                                                                                                           \
			int jumpto = _code_ptr[ip + 4];                                                                                \
			GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);                                                               \
			ip = jumpto;                                                                                                   \
		}                                                                                                                  \
	}                                                                                                                      \
	DISPATCH_OPCODE

			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(BYTE, uint8_t, get_byte_array, INT, int64_t, get_int);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(INT32, int32_t, get_int32_array, INT, int64_t, get_int);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(INT64, int64_t, get_int64_array, INT, int64_t, get_int);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(FLOAT32, float, get_float32_array, FLOAT, double, get_float);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(FLOAT64, double, get_float64_array, FLOAT, double, get_float);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(STRING, String, get_string_array, STRING, String, get_string);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(VECTOR2, Vector2, get_vector2_array, VECTOR2, Vector2, get_vector2);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(VECTOR3, Vector3, get_vector3_array, VECTOR3, Vector3, get_vector3);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(COLOR, Color, get_color_array, COLOR, Color, get_color);
			OPCODE_ITERATE_BEGIN_PACKED_ARRAY(VECTOR4, Vector4, get_vector4_array, VECTOR4, Vector4, get_vector4);

			OPCODE(OPCODE_ITERATE_BEGIN_OBJECT) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

#ifdef DEBUG_ENABLED
				bool freed = false;
				Object *obj = container->get_validated_object_with_check(freed);
				if (freed) {
					err_text = "Trying to iterate on a previously freed object.";
					OPCODE_BREAK;
				} else if (!obj) {
					err_text = "Trying to iterate on a null value.";
					OPCODE_BREAK;
				}
#else
				Object *obj = *VariantInternal::get_object(container);
#endif

				*counter = Variant();
				Array ref = { *counter };
				Variant vref;
				VariantInternal::initialize(&vref, Variant::ARRAY);
				*VariantInternal::get_array(&vref) = ref;

				const Variant *args[] = { &vref };

				Callable::CallError ce;
				Variant has_next = obj->callp(CoreStringName(_iter_init), args, 1, ce);

#ifdef DEBUG_ENABLED
				if (ref.size() != 1 || ce.error != Callable::CallError::CALL_OK) {
					err_text = vformat(R"(There was an error calling "_iter_next" on iterator object of type %s.)", *container);
					OPCODE_BREAK;
				}
#endif
				if (!has_next.booleanize()) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					*counter = ref[0];

					GET_VARIANT_PTR(iterator, 2);
					*iterator = obj->callp(CoreStringName(_iter_get), (const Variant **)&counter, 1, ce);
#ifdef DEBUG_ENABLED
					if (ce.error != Callable::CallError::CALL_OK) {
						err_text = vformat(R"(There was an error calling "_iter_get" on iterator object of type %s.)", *container);
						OPCODE_BREAK;
					}
#endif

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_BEGIN_RANGE) {
				CHECK_SPACE(6);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(from_ptr, 1);
				GET_VARIANT_PTR(to_ptr, 2);
				GET_VARIANT_PTR(step_ptr, 3);

				int64_t from = *VariantInternal::get_int(from_ptr);
				int64_t to = *VariantInternal::get_int(to_ptr);
				int64_t step = *VariantInternal::get_int(step_ptr);

				VariantInternal::initialize(counter, Variant::INT);
				*VariantInternal::get_int(counter) = from;

				bool do_continue = from == to ? false : (from < to ? step > 0 : step < 0);

				if (do_continue) {
					GET_VARIANT_PTR(iterator, 4);
					VariantInternal::initialize(iterator, Variant::INT);
					*VariantInternal::get_int(iterator) = from;

					// Skip regular iterate.
					ip += 7;
				} else {
					// Jump to end of loop.
					int jumpto = _code_ptr[ip + 6];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				bool valid;
				if (!container->iter_next(*counter, valid)) {
#ifdef DEBUG_ENABLED
					if (!valid) {
						err_text = "Unable to iterate on object of type '" + Variant::get_type_name(container->get_type()) + "' (type changed since first iteration?).";
						OPCODE_BREAK;
					}
#endif
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);

					*iterator = container->iter_get(*counter, valid);
#ifdef DEBUG_ENABLED
					if (!valid) {
						err_text = "Unable to obtain iterator object of type '" + Variant::get_type_name(container->get_type()) + "' (but was obtained on first iteration?).";
						OPCODE_BREAK;
					}
#endif
					ip += 5; //loop again
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_INT) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				int64_t size = *VariantInternal::get_int(container);
				int64_t *count = VariantInternal::get_int(counter);

				(*count)++;

				if (*count >= size) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_int(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_FLOAT) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				double size = *VariantInternal::get_float(container);
				double *count = VariantInternal::get_float(counter);

				(*count)++;

				if (*count >= size) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_float(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_VECTOR2) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Vector2 *bounds = VariantInternal::get_vector2((const Variant *)container);
				double *count = VariantInternal::get_float(counter);

				(*count)++;

				if (*count >= bounds->y) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_float(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_VECTOR2I) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Vector2i *bounds = VariantInternal::get_vector2i((const Variant *)container);
				int64_t *count = VariantInternal::get_int(counter);

				(*count)++;

				if (*count >= bounds->y) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_int(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_VECTOR3) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Vector3 *bounds = VariantInternal::get_vector3((const Variant *)container);
				double *count = VariantInternal::get_float(counter);

				*count += bounds->z;

				if ((bounds->z < 0 && *count <= bounds->y) || (bounds->z > 0 && *count >= bounds->y)) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_float(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_VECTOR3I) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Vector3i *bounds = VariantInternal::get_vector3i((const Variant *)container);
				int64_t *count = VariantInternal::get_int(counter);

				*count += bounds->z;

				if ((bounds->z < 0 && *count <= bounds->y) || (bounds->z > 0 && *count >= bounds->y)) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_int(iterator) = *count;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_STRING) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const String *str = VariantInternal::get_string((const Variant *)container);
				int64_t *idx = VariantInternal::get_int(counter);
				(*idx)++;

				if (*idx >= str->length()) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*VariantInternal::get_string(iterator) = str->substr(*idx, 1);

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_DICTIONARY) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Dictionary *dict = VariantInternal::get_dictionary((const Variant *)container);
				const Variant *next = dict->next(counter);

				if (!next) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*counter = *next;
					*iterator = *next;

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_ARRAY) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

				const Array *array = VariantInternal::get_array((const Variant *)container);
				int64_t *idx = VariantInternal::get_int(counter);
				(*idx)++;

				if (*idx >= array->size()) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 2);
					*iterator = array->get(*idx);

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

#define OPCODE_ITERATE_PACKED_ARRAY(m_var_type, m_elem_type, m_get_func, m_ret_get_func)            \
	OPCODE(OPCODE_ITERATE_PACKED_##m_var_type##_ARRAY) {                                            \
		CHECK_SPACE(4);                                                                             \
		GET_VARIANT_PTR(counter, 0);                                                                \
		GET_VARIANT_PTR(container, 1);                                                              \
		const Vector<m_elem_type> *array = VariantInternal::m_get_func((const Variant *)container); \
		int64_t *idx = VariantInternal::get_int(counter);                                           \
		(*idx)++;                                                                                   \
		if (*idx >= array->size()) {                                                                \
			int jumpto = _code_ptr[ip + 4];                                                         \
			GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);                                        \
			ip = jumpto;                                                                            \
		} else {                                                                                    \
			GET_VARIANT_PTR(iterator, 2);                                                           \
			*VariantInternal::m_ret_get_func(iterator) = array->get(*idx);                          \
			ip += 5;                                                                                \
		}                                                                                           \
	}                                                                                               \
	DISPATCH_OPCODE

			OPCODE_ITERATE_PACKED_ARRAY(BYTE, uint8_t, get_byte_array, get_int);
			OPCODE_ITERATE_PACKED_ARRAY(INT32, int32_t, get_int32_array, get_int);
			OPCODE_ITERATE_PACKED_ARRAY(INT64, int64_t, get_int64_array, get_int);
			OPCODE_ITERATE_PACKED_ARRAY(FLOAT32, float, get_float32_array, get_float);
			OPCODE_ITERATE_PACKED_ARRAY(FLOAT64, double, get_float64_array, get_float);
			OPCODE_ITERATE_PACKED_ARRAY(STRING, String, get_string_array, get_string);
			OPCODE_ITERATE_PACKED_ARRAY(VECTOR2, Vector2, get_vector2_array, get_vector2);
			OPCODE_ITERATE_PACKED_ARRAY(VECTOR3, Vector3, get_vector3_array, get_vector3);
			OPCODE_ITERATE_PACKED_ARRAY(COLOR, Color, get_color_array, get_color);
			OPCODE_ITERATE_PACKED_ARRAY(VECTOR4, Vector4, get_vector4_array, get_vector4);

			OPCODE(OPCODE_ITERATE_OBJECT) {
				CHECK_SPACE(4);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(container, 1);

#ifdef DEBUG_ENABLED
				bool freed = false;
				Object *obj = container->get_validated_object_with_check(freed);
				if (freed) {
					err_text = "Trying to iterate on a previously freed object.";
					OPCODE_BREAK;
				} else if (!obj) {
					err_text = "Trying to iterate on a null value.";
					OPCODE_BREAK;
				}
#else
				Object *obj = *VariantInternal::get_object(container);
#endif

				Array ref = { *counter };
				Variant vref;
				VariantInternal::initialize(&vref, Variant::ARRAY);
				*VariantInternal::get_array(&vref) = ref;

				const Variant *args[] = { &vref };

				Callable::CallError ce;
				Variant has_next = obj->callp(CoreStringName(_iter_next), args, 1, ce);

#ifdef DEBUG_ENABLED
				if (ref.size() != 1 || ce.error != Callable::CallError::CALL_OK) {
					err_text = vformat(R"(There was an error calling "_iter_next" on iterator object of type %s.)", *container);
					OPCODE_BREAK;
				}
#endif
				if (!has_next.booleanize()) {
					int jumpto = _code_ptr[ip + 4];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					*counter = ref[0];

					GET_VARIANT_PTR(iterator, 2);
					*iterator = obj->callp(CoreStringName(_iter_get), (const Variant **)&counter, 1, ce);
#ifdef DEBUG_ENABLED
					if (ce.error != Callable::CallError::CALL_OK) {
						err_text = vformat(R"(There was an error calling "_iter_get" on iterator object of type %s.)", *container);
						OPCODE_BREAK;
					}
#endif

					ip += 5; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_ITERATE_RANGE) {
				CHECK_SPACE(5);

				GET_VARIANT_PTR(counter, 0);
				GET_VARIANT_PTR(to_ptr, 1);
				GET_VARIANT_PTR(step_ptr, 2);

				int64_t to = *VariantInternal::get_int(to_ptr);
				int64_t step = *VariantInternal::get_int(step_ptr);

				int64_t *count = VariantInternal::get_int(counter);

				*count += step;

				if ((step < 0 && *count <= to) || (step > 0 && *count >= to)) {
					int jumpto = _code_ptr[ip + 5];
					GD_ERR_BREAK(jumpto < 0 || jumpto > _code_size);
					ip = jumpto;
				} else {
					GET_VARIANT_PTR(iterator, 3);
					*VariantInternal::get_int(iterator) = *count;

					ip += 6; // Loop again.
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_STORE_GLOBAL) {
				CHECK_SPACE(3);
				int global_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(global_idx < 0 || global_idx >= FSLanguage::get_singleton()->get_global_array_size());

				GET_VARIANT_PTR(dst, 0);
				*dst = FSLanguage::get_singleton()->get_global_array()[global_idx];

				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_STORE_NAMED_GLOBAL) {
				CHECK_SPACE(3);
				int globalname_idx = _code_ptr[ip + 2];
				GD_ERR_BREAK(globalname_idx < 0 || globalname_idx >= _global_names_count);
				const StringName *globalname = &_global_names_ptr[globalname_idx];
				GD_ERR_BREAK(!FSLanguage::get_singleton()->get_named_globals_map().has(*globalname));

				GET_VARIANT_PTR(dst, 0);
				*dst = FSLanguage::get_singleton()->get_named_globals_map()[*globalname];

				ip += 3;
			}
			DISPATCH_OPCODE;

#define OPCODE_TYPE_ADJUST(m_v_type, m_c_type)    \
	OPCODE(OPCODE_TYPE_ADJUST_##m_v_type) {       \
		CHECK_SPACE(2);                           \
		GET_VARIANT_PTR(arg, 0);                  \
		VariantTypeAdjust<m_c_type>::adjust(arg); \
		ip += 2;                                  \
	}                                             \
	DISPATCH_OPCODE

			OPCODE_TYPE_ADJUST(BOOL, bool);
			OPCODE_TYPE_ADJUST(INT, int64_t);
			OPCODE_TYPE_ADJUST(FLOAT, double);
			OPCODE_TYPE_ADJUST(STRING, String);
			OPCODE_TYPE_ADJUST(VECTOR2, Vector2);
			OPCODE_TYPE_ADJUST(VECTOR2I, Vector2i);
			OPCODE_TYPE_ADJUST(RECT2, Rect2);
			OPCODE_TYPE_ADJUST(RECT2I, Rect2i);
			OPCODE_TYPE_ADJUST(VECTOR3, Vector3);
			OPCODE_TYPE_ADJUST(VECTOR3I, Vector3i);
			OPCODE_TYPE_ADJUST(TRANSFORM2D, Transform2D);
			OPCODE_TYPE_ADJUST(VECTOR4, Vector4);
			OPCODE_TYPE_ADJUST(VECTOR4I, Vector4i);
			OPCODE_TYPE_ADJUST(PLANE, Plane);
			OPCODE_TYPE_ADJUST(QUATERNION, Quaternion);
			OPCODE_TYPE_ADJUST(AABB, AABB);
			OPCODE_TYPE_ADJUST(BASIS, Basis);
			OPCODE_TYPE_ADJUST(TRANSFORM3D, Transform3D);
			OPCODE_TYPE_ADJUST(PROJECTION, Projection);
			OPCODE_TYPE_ADJUST(COLOR, Color);
			OPCODE_TYPE_ADJUST(STRING_NAME, StringName);
			OPCODE_TYPE_ADJUST(NODE_PATH, NodePath);
			OPCODE_TYPE_ADJUST(RID, RID);
			OPCODE_TYPE_ADJUST(OBJECT, Object *);
			OPCODE_TYPE_ADJUST(CALLABLE, Callable);
			OPCODE_TYPE_ADJUST(SIGNAL, Signal);
			OPCODE_TYPE_ADJUST(DICTIONARY, Dictionary);
			OPCODE_TYPE_ADJUST(ARRAY, Array);
			OPCODE_TYPE_ADJUST(PACKED_BYTE_ARRAY, PackedByteArray);
			OPCODE_TYPE_ADJUST(PACKED_INT32_ARRAY, PackedInt32Array);
			OPCODE_TYPE_ADJUST(PACKED_INT64_ARRAY, PackedInt64Array);
			OPCODE_TYPE_ADJUST(PACKED_FLOAT32_ARRAY, PackedFloat32Array);
			OPCODE_TYPE_ADJUST(PACKED_FLOAT64_ARRAY, PackedFloat64Array);
			OPCODE_TYPE_ADJUST(PACKED_STRING_ARRAY, PackedStringArray);
			OPCODE_TYPE_ADJUST(PACKED_VECTOR2_ARRAY, PackedVector2Array);
			OPCODE_TYPE_ADJUST(PACKED_VECTOR3_ARRAY, PackedVector3Array);
			OPCODE_TYPE_ADJUST(PACKED_COLOR_ARRAY, PackedColorArray);
			OPCODE_TYPE_ADJUST(PACKED_VECTOR4_ARRAY, PackedVector4Array);

			OPCODE(OPCODE_ASSERT) {
				CHECK_SPACE(3);

#ifdef DEBUG_ENABLED
				GET_VARIANT_PTR(test, 0);
				bool result = test->booleanize();

				if (!result) {
					String message_str;
					if (_code_ptr[ip + 2] != 0) {
						GET_VARIANT_PTR(message, 1);
						Variant message_var = *message;
						if (message->get_type() != Variant::NIL) {
							message_str = message_var;
						}
					}
					if (message_str.is_empty()) {
						err_text = "Assertion failed.";
					} else {
						err_text = "Assertion failed: " + message_str;
					}
					OPCODE_BREAK;
				}

#endif
				ip += 3;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_BREAKPOINT) {
#ifdef DEBUG_ENABLED
				if (EngineDebugger::is_active()) {
					FSLanguage::get_singleton()->debug_break("Breakpoint Statement", true);
				}
#endif
				ip += 1;
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_LINE) {
				CHECK_SPACE(2);

				line = _code_ptr[ip + 1];
				ip += 2;

				if (FSScriptTestGuard::is_active()) {
					String guard_message;
					FSScriptTestGuard::UnwindReason guard_reason = FSScriptTestGuard::UNWIND_NONE;
					if (FSScriptTestGuard::checkpoint(guard_message, guard_reason)) {
						FSScriptTestGuard::mark_unwind(guard_reason, guard_message);
						err_text = guard_message;
						OPCODE_BREAK;
					}
				}

				if (EngineDebugger::is_active()) {
					// line
					bool do_break = false;

					if (unlikely(EngineDebugger::get_script_debugger()->get_lines_left() > 0)) {
						if (EngineDebugger::get_script_debugger()->get_depth() <= 0) {
							EngineDebugger::get_script_debugger()->set_lines_left(EngineDebugger::get_script_debugger()->get_lines_left() - 1);
						}
						if (EngineDebugger::get_script_debugger()->get_lines_left() <= 0) {
							do_break = true;
						}
					}

					if (EngineDebugger::get_script_debugger()->is_breakpoint(line, source)) {
						do_break = true;
					}

					if (unlikely(do_break)) {
						FSLanguage::get_singleton()->debug_break("Breakpoint", true);
					}

					EngineDebugger::get_singleton()->line_poll();
				}
			}
			DISPATCH_OPCODE;

			OPCODE(OPCODE_END) {
#ifdef DEBUG_ENABLED
				exit_ok = true;
#endif
				OPCODE_BREAK;
			}

#if 0 // Enable for debugging.
			default: {
				err_text = "Illegal opcode " + itos(_code_ptr[ip]) + " at address " + itos(ip);
				OPCODE_BREAK;
			}
#endif
		}

		OPCODES_END
#ifdef DEBUG_ENABLED
		if (exit_ok) {
			OPCODE_OUT;
		}
		if (err_text.is_empty() && !exit_ok) {
			err_text = "Internal script error! Opcode: " + itos(last_opcode) + " (please report).";
		}
#endif
		if (!err_text.is_empty()) {
			String err_file;
			bool instance_valid_with_script = p_instance && ObjectDB::get_instance(p_instance->owner_id) != nullptr && p_instance->script->is_valid();
			if (instance_valid_with_script && !get_script()->path.is_empty()) {
				err_file = get_script()->path;
			} else if (script) {
				err_file = script->path;
			}
			if (err_file.is_empty()) {
				err_file = "<built-in>";
			}
			String err_func = name;
			if (instance_valid_with_script && p_instance->script->local_name != StringName()) {
				err_func = p_instance->script->local_name.operator String() + "." + err_func;
			}
			int err_line = line;

			bool suppress_report = false;
			FSScriptTestGuard::notify_script_error(err_text, suppress_report);
			if (!suppress_report) {
				_err_print_error(err_func.utf8().get_data(), err_file.utf8().get_data(), err_line, err_text.utf8().get_data(), false, ERR_HANDLER_SCRIPT);
			}
#ifdef DEBUG_ENABLED
			if (!suppress_report) {
				FSLanguage::get_singleton()->debug_break(err_text, false);
			}
#endif
			retvalue = _get_default_variant_for_data_type(return_type);
		}

		OPCODE_OUT;
	}

	OPCODES_OUT
#ifdef DEBUG_ENABLED
	if (FSLanguage::get_singleton()->profiling) {
		uint64_t time_taken = OS::get_singleton()->get_ticks_usec() - function_start_time;
		profile.total_time.add(time_taken);
		profile.self_time.add(time_taken - function_call_time);
		profile.frame_total_time.add(time_taken);
		profile.frame_self_time.add(time_taken - function_call_time);
		if (Thread::get_caller_id() == Thread::get_main_id()) {
			FSLanguage::get_singleton()->script_frame_time += time_taken - function_call_time;
		}
	}
#endif

	if (p_state && !awaited) {
		// This means we have finished executing a resumed function and it was not awaited again.
		// Signal the next function-state to resume.
		const Variant *args[1] = { &retvalue };
		p_state->completed.emit(args, 1);
	}

	// Exit function only after executing the remaining function states to preserve async call stack.
	// This ensures the call stack can be properly shown when using `await`, showing what resumed the function.
	FSLanguage::get_singleton()->exit_function();

	// Always free reserved addresses, since they are never copied.
	for (int i = 0; i < FIXED_ADDRESSES_MAX; i++) {
		stack[i].~Variant();
	}

	// Free stack, except reserved addresses.
	for (int i = FIXED_ADDRESSES_MAX; i < _stack_size; i++) {
		stack[i].~Variant();
	}

	call_depth--;

	return retvalue;
}
