/**************************************************************************/
/*  fs_function.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "fs_function.h"

#include "foundry_script.h"
#include "fs_conformance_registry.h"

bool FSDataType::_script_conforms_to_trait(const Ref<Script> &p_base, const StringName &p_trait) {
	if (p_base.is_null() || p_trait == StringName()) {
		return false;
	}
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	Ref<Script> script = p_base;
	while (script.is_valid()) {
		// The registry keys a target by its FQCN, global class name, and script path; try each alias.
		const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(script.ptr());
		if (foundry_script != nullptr && registry->has_conformance(foundry_script->get_fully_qualified_name(), p_trait)) {
			return true;
		}
		const StringName global_name = script->get_global_name();
		if (global_name != StringName() && registry->has_conformance(String(global_name), p_trait)) {
			return true;
		}
		const String script_path = script->get_path();
		if (!script_path.is_empty() && registry->has_conformance(script_path, p_trait)) {
			return true;
		}
		script = script->get_base_script();
	}
	return false;
}

static FSDataType _gdtype_from_container_type(const ContainerType &p_container_type, bool p_is_type_handle) {
	FSDataType type;
	type.is_type_handle = p_is_type_handle;

	if (p_container_type.script.is_valid()) {
		type.kind = Object::cast_to<FoundryScript>(p_container_type.script.ptr()) != nullptr ? FSDataType::FOUNDRY_SCRIPT : FSDataType::SCRIPT;
		type.builtin_type = Variant::OBJECT;
		type.native_type = p_container_type.script->get_instance_base_type();
		type.script_type_ref = p_container_type.script;
		type.script_type = type.script_type_ref.ptr();
		type.is_script_trait = p_container_type.script->is_trait_type();
		type.script_trait = p_container_type.script->get_trait_type_name();
	} else if (p_container_type.builtin_type == Variant::OBJECT) {
		type.kind = FSDataType::NATIVE;
		type.builtin_type = Variant::OBJECT;
		type.native_type = p_container_type.class_name != StringName() ? p_container_type.class_name : Object::get_class_static();
	} else if (p_container_type.builtin_type != Variant::NIL) {
		type.kind = FSDataType::BUILTIN;
		type.builtin_type = p_container_type.builtin_type;
	} else {
		type.kind = FSDataType::VARIANT;
	}

	for (const ContainerType &element_type : p_container_type.element_types) {
		type.container_element_types.push_back(_gdtype_from_container_type(element_type, false));
	}
	for (const ContainerType &argument_type : p_container_type.type_arguments) {
		type.type_arguments.push_back(_gdtype_from_container_type(argument_type, false));
	}

	return type;
}

static bool _type_arguments_match_script(const FSDataType &p_expected_type,
		const Ref<FoundryScript> &p_source_script, const Vector<ContainerType> &p_source_type_arguments,
		bool p_require_bound_arguments = false) {
	if (p_expected_type.type_arguments.is_empty()) {
		return true;
	}
	if (p_source_script.is_null() || p_expected_type.script_type == nullptr) {
		return false;
	}

	Ref<Script> expected_script;
	expected_script.reference_ptr(p_expected_type.script_type);

	Vector<ContainerType> expected_type_arguments;
	for (const FSDataType &argument_type : p_expected_type.type_arguments) {
		expected_type_arguments.push_back(argument_type.to_container_type());
	}

	Vector<ContainerType> projected_type_arguments;
	Vector<bool> projected_argument_bound;
	if (!p_source_script->project_type_arguments_onto_base(expected_script, p_source_type_arguments,
				projected_type_arguments, projected_argument_bound)) {
		if (p_source_script.ptr() != p_expected_type.script_type) {
			return false;
		}
		projected_type_arguments = p_source_type_arguments;
		projected_argument_bound.resize(projected_type_arguments.size());
		for (int i = 0; i < projected_argument_bound.size(); i++) {
			projected_argument_bound.write[i] = true;
		}
	}

	if (projected_type_arguments.size() != expected_type_arguments.size()) {
		return false;
	}
	for (int i = 0; i < expected_type_arguments.size(); i++) {
		if (i >= projected_argument_bound.size() || !projected_argument_bound[i]) {
			if (p_require_bound_arguments) {
				return false;
			}
			continue;
		}
		if (projected_type_arguments[i] != expected_type_arguments[i]) {
			return false;
		}
	}
	return true;
}

bool FSDataType::is_type_handle_type(const Variant &p_variant) const {
	if (p_variant.get_type() == Variant::NIL) {
		return true;
	}
	if (p_variant.get_type() != Variant::OBJECT) {
		return false;
	}

	bool was_freed = false;
	Object *object = p_variant.get_validated_object_with_check(was_freed);
	if (object == nullptr) {
		return !was_freed;
	}

	FSNativeClass *native_class = Object::cast_to<FSNativeClass>(object);
	if (native_class != nullptr) {
		return kind == NATIVE && ClassDB::is_parent_class(native_class->get_name(), native_type);
	}

	FSSpecializedClassHandle *specialized_handle = Object::cast_to<FSSpecializedClassHandle>(object);
	if (specialized_handle != nullptr) {
		const Ref<FoundryScript> handle_script = specialized_handle->get_specialized_script();
		if (handle_script.is_null()) {
			return false;
		}
		if (kind == NATIVE) {
			const StringName script_native = handle_script->get_instance_base_type();
			return script_native != StringName() && ClassDB::is_parent_class(script_native, native_type);
		}
		if (kind == SCRIPT || kind == FOUNDRY_SCRIPT) {
			if (is_script_trait) {
				return handle_script->has_script_trait(script_trait) &&
						_type_arguments_match_script(*this, handle_script, specialized_handle->get_type_arguments());
			}
			Script *script = handle_script.ptr();
			while (script != nullptr) {
				if (script == script_type) {
					return _type_arguments_match_script(*this, handle_script, specialized_handle->get_type_arguments());
				}
				script = script->get_base_script().ptr();
			}
		}
		return false;
	}

	Script *script = Object::cast_to<Script>(object);
	if (script == nullptr) {
		return false;
	}

	if (kind == NATIVE) {
		const StringName script_native = script->get_instance_base_type();
		return script_native != StringName() && ClassDB::is_parent_class(script_native, native_type);
	}

	if (kind == SCRIPT || kind == FOUNDRY_SCRIPT) {
		if (is_script_trait) {
			if (!script->has_script_trait(script_trait)) {
				return false;
			}
			if (type_arguments.is_empty()) {
				return true;
			}
			FoundryScript *foundry_script = Object::cast_to<FoundryScript>(script);
			if (foundry_script == nullptr) {
				return false;
			}
			Ref<FoundryScript> source_script;
			source_script.reference_ptr(foundry_script);
			return _type_arguments_match_script(*this, source_script, Vector<ContainerType>(), true);
		}
		FoundryScript *foundry_script = Object::cast_to<FoundryScript>(script);
		while (script != nullptr) {
			if (script == script_type) {
				if (type_arguments.is_empty()) {
					return true;
				}
				if (foundry_script == nullptr) {
					return false;
				}
				Ref<FoundryScript> source_script;
				source_script.reference_ptr(foundry_script);
				return _type_arguments_match_script(*this, source_script, Vector<ContainerType>(), true);
			}
			script = script->get_base_script().ptr();
		}
	}

	return false;
}

FSDataType FSDataType::from_type_handle_container_type(const ContainerType &p_container_type) {
	return _gdtype_from_container_type(p_container_type, true);
}

Variant FSFunction::get_constant(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, constants.size(), "<errconst>");
	return constants[p_idx];
}

StringName FSFunction::get_global_name(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, global_names.size(), "<errgname>");
	return global_names[p_idx];
}

struct _GDFKC {
	int order = 0;
	List<int> pos;
};

struct _GDFKCS {
	int order = 0;
	StringName id;
	int pos = 0;

	bool operator<(const _GDFKCS &p_r) const {
		return order < p_r.order;
	}
};

void FSFunction::debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const {
	int oc = 0;
	HashMap<StringName, _GDFKC> sdmap;
	for (const StackDebug &sd : stack_debug) {
		if (sd.line >= p_line) {
			break;
		}

		if (sd.added) {
			if (!sdmap.has(sd.identifier)) {
				_GDFKC d;
				d.order = oc++;
				d.pos.push_back(sd.pos);
				sdmap[sd.identifier] = d;

			} else {
				sdmap[sd.identifier].pos.push_back(sd.pos);
			}
		} else {
			ERR_CONTINUE(!sdmap.has(sd.identifier));

			sdmap[sd.identifier].pos.pop_back();
			if (sdmap[sd.identifier].pos.is_empty()) {
				sdmap.erase(sd.identifier);
			}
		}
	}

	List<_GDFKCS> stackpositions;
	for (const KeyValue<StringName, _GDFKC> &E : sdmap) {
		_GDFKCS spp;
		spp.id = E.key;
		spp.order = E.value.order;
		spp.pos = E.value.pos.back()->get();
		stackpositions.push_back(spp);
	}

	stackpositions.sort();

	for (_GDFKCS &E : stackpositions) {
		Pair<StringName, int> p;
		p.first = E.id;
		p.second = E.pos;
		r_stackvars->push_back(p);
	}
}

FSFunction::FSFunction() {
	name = "<anonymous>";
#ifdef DEBUG_ENABLED
	{
		MutexLock lock(FSLanguage::get_singleton()->mutex);
		FSLanguage::get_singleton()->function_list.add(&function_list);
	}
#endif
}

FSFunction::~FSFunction() {
	get_script()->member_functions.erase(name);

	for (int i = 0; i < lambdas.size(); i++) {
		memdelete(lambdas[i]);
	}

	for (int i = 0; i < argument_types.size(); i++) {
		argument_types.write[i].script_type_ref = Ref<Script>();
	}
	return_type.script_type_ref = Ref<Script>();

#ifdef DEBUG_ENABLED
	MutexLock lock(FSLanguage::get_singleton()->mutex);
	FSLanguage::get_singleton()->function_list.remove(&function_list);
#endif
}

/////////////////////

Variant FSFunctionState::_signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	Variant arg;
	r_error.error = Callable::CallError::CALL_OK;

	if (p_argcount == 0) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	} else if (p_argcount == 1) {
		//noooneee
	} else if (p_argcount == 2) {
		arg = *p_args[0];
	} else {
		Array extra_args;
		for (int i = 0; i < p_argcount - 1; i++) {
			extra_args.push_back(*p_args[i]);
		}
		arg = extra_args;
	}

	Ref<FSFunctionState> self = *p_args[p_argcount - 1];

	if (self.is_null()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_argcount - 1;
		r_error.expected = Variant::OBJECT;
		return Variant();
	}

	return resume(arg);
}

bool FSFunctionState::is_valid(bool p_extended_check) const {
	if (function == nullptr) {
		return false;
	}

	if (p_extended_check) {
		MutexLock lock(FSLanguage::get_singleton()->mutex);

		// Script gone?
		if (!scripts_list.in_list()) {
			return false;
		}
		// Class instance gone? (if not static function)
		if (state.instance && !instances_list.in_list()) {
			return false;
		}
	}

	return true;
}

Variant FSFunctionState::resume(const Variant &p_arg) {
	ERR_FAIL_NULL_V(function, Variant());
	{
		MutexLock lock(FSLanguage::singleton->mutex);

		if (!scripts_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but script is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		if (state.instance && !instances_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but class instance is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		// Do these now to avoid locking again after the call
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}

	state.result = p_arg;
	Callable::CallError err;
	Variant ret = function->call(nullptr, nullptr, 0, err, &state);

	bool completed = true;

	// If the return value is a FSFunctionState reference,
	// then the function did await again after resuming.
	if (ret.is_ref_counted()) {
		FSFunctionState *gdfs = Object::cast_to<FSFunctionState>(ret);
		if (gdfs && gdfs->function == function) {
			completed = false;
			// Keep the first state alive via reference.
			gdfs->first_state = first_state.is_valid() ? first_state : Ref<FSFunctionState>(this);
		}
	}

	function = nullptr; //cleaned up;
	state.result = Variant();

	if (completed) {
		_clear_stack();
	}

	return ret;
}

void FSFunctionState::_clear_stack() {
	if (state.stack_size) {
		Variant *stack = (Variant *)state.stack.ptr();
		// First `FSFunction::FIXED_ADDRESSES_MAX` stack addresses are special
		// and not copied to the state, so we skip them here.
		for (int i = FSFunction::FIXED_ADDRESSES_MAX; i < state.stack_size; i++) {
			stack[i].~Variant();
		}
		state.stack_size = 0;
	}
}

void FSFunctionState::_clear_connections() {
	List<Object::Connection> conns;
	get_signals_connected_to_this(&conns);

	for (Object::Connection &c : conns) {
		c.signal.disconnect(c.callable);
	}
}

void FSFunctionState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("resume", "arg"), &FSFunctionState::resume, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("is_valid", "extended_check"), &FSFunctionState::is_valid, DEFVAL(false));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_signal_callback", &FSFunctionState::_signal_callback, MethodInfo("_signal_callback"));

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

FSFunctionState::FSFunctionState() :
		scripts_list(this),
		instances_list(this) {
}

FSFunctionState::~FSFunctionState() {
	{
		MutexLock lock(FSLanguage::singleton->mutex);
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}
}
