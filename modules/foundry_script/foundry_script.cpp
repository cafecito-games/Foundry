/**************************************************************************/
/*  foundry_script.cpp                                                    */
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

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
#include "fs_analyzer.h"
#include "fs_compiler.h"
#include "fs_warning.h"
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
#include "fs_bytecode_loader.h"
#include "fs_cache.h"
#include "fs_class_handle_callable.h"
#include "fs_conformance_registry.h"
#include "fs_no_frontend.h"
#include "fs_parser.h"
#include "fs_project_scripts.h"
#include "fs_reflection.h"
#include "fs_rpc_callable.h"
#include "fs_script_test_guard.h"
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
#include "fs_tokenizer_buffer.h"
#endif // FOUNDRY_SCRIPT_NO_FRONTEND

#ifdef TOOLS_ENABLED
#include "editor/fs_docgen.h"
#endif

#if defined(TOOLS_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
#include "language_server/fs_language_protocol.h"
#endif

#ifdef TESTS_ENABLED
#include "tests/fs_benchmark_runner.h"
#include "tests/fs_test_runner.h"
#endif

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/core_constants.h"
#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/variant/container_type_validate.h"

#include "scene/resources/packed_scene.h"
#include "scene/scene_string_names.h"

#ifdef TOOLS_ENABLED
#include "core/extension/foundry_extension_manager.h"
#include "editor/file_system/editor_paths.h"
#endif

///////////////////////////

FSNativeClass::FSNativeClass(const StringName &p_name) {
	name = p_name;
}

bool FSNativeClass::_get(const StringName &p_name, Variant &r_ret) const {
	bool ok;
	int64_t v = ClassDB::get_integer_constant(name, p_name, &ok);

	if (ok) {
		r_ret = v;
		return true;
	}

	MethodBind *method = ClassDB::get_method(name, p_name);
	if (method && method->is_static()) {
		// Native static method.
		r_ret = Callable(this, p_name);
		return true;
	}

	return false;
}

void FSNativeClass::_bind_methods() {
	ClassDB::bind_method(D_METHOD("new"), &FSNativeClass::_new);
}

void FSTypeParameter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_parameter_name"), &FSTypeParameter::get_parameter_name);
	ClassDB::bind_method(D_METHOD("get_index"), &FSTypeParameter::get_index);
	ClassDB::bind_method(D_METHOD("get_scope"), &FSTypeParameter::get_scope);
	ClassDB::bind_method(D_METHOD("is_bounded"), &FSTypeParameter::is_bounded);
	ClassDB::bind_method(D_METHOD("get_bound"), &FSTypeParameter::get_bound);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_parameter_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "index", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_index");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "scope", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_scope");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_bound", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "is_bounded");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "bound", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_bound");
}

void FSSpecializedClassHandle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_equals", "other"), &FSSpecializedClassHandle::_equals);
	ClassDB::bind_method(D_METHOD("_hash_code"), &FSSpecializedClassHandle::_hash_code);
}

Ref<Script> FSSpecializedClassHandle::get_represented_script() const {
	return script;
}

String FSSpecializedClassHandle::get_type_name() const {
	if (script.is_null()) {
		return "FoundryScript";
	}
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = script->get_instance_base_type();
	type.script = script;
	type.type_arguments = type_arguments;
	return type.get_type_name();
}

bool FSSpecializedClassHandle::is_assignable_to_native_type(const StringName &p_native_type) const {
	return script.is_valid() && (p_native_type == StringName() || ClassDB::is_parent_class(script->get_class_name(), p_native_type));
}

bool FSSpecializedClassHandle::_equals(const Variant &p_other) const {
	Ref<FSSpecializedClassHandle> other = p_other;
	return other.is_valid() && script == other->script && type_arguments == other->type_arguments;
}

int64_t FSSpecializedClassHandle::_hash_code() const {
	uint32_t hash = hash_murmur3_one_64(reinterpret_cast<uint64_t>(script.ptr()));
	for (const ContainerType &argument_type : type_arguments) {
		hash = hash_murmur3_one_32(argument_type.get_type_name().hash(), hash);
	}
	return hash_fmix32(hash);
}

bool FSSpecializedClassHandle::_get(const StringName &p_name, Variant &r_ret) const {
	if (script.is_null()) {
		return false;
	}
	if (script->resolves_to_static_function(p_name)) {
		// The receiver of an extracted static callable is this specialization, not the unspecialized
		// script: invoking it later has to construct `Crate[int]` exactly as calling through this handle
		// directly would. The callable owns the handle because nothing else does.
		r_ret = Callable(memnew(FSClassHandleCallable(Ref<ClassHandle>(const_cast<FSSpecializedClassHandle *>(this)), p_name)));
		return true;
	}
	return script->_get(p_name, r_ret);
}

Variant FSSpecializedClassHandle::callp(const StringName &p_method, const Variant **p_args, int p_argcount,
		Callable::CallError &r_error) {
	if (p_method == SNAME("new")) {
		if (script.is_null()) {
			r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
			return Variant();
		}
		return script->_new_specialized(p_args, p_argcount, type_arguments, r_error);
	}
	if (p_method == CoreStringName(_equals) || p_method == CoreStringName(_hash_code)) {
		return RefCounted::callp(p_method, p_args, p_argcount, r_error);
	}
	if (script.is_valid()) {
		// The receiver is the specialization, not the bare script, so the concrete type arguments stay
		// with the call.
		Variant ret = script->call_static_with_context(p_method, p_args, p_argcount, r_error,
				FSStaticSelfContext::for_specialized_script(script, type_arguments));
		if (r_error.error != Callable::CallError::CALL_ERROR_INVALID_METHOD) {
			return ret;
		}
	}
	return RefCounted::callp(p_method, p_args, p_argcount, r_error);
}

Ref<FSSpecializedClassHandle> FSSpecializedClassHandle::create(const Ref<FoundryScript> &p_script,
		const Vector<ContainerType> &p_type_arguments) {
	Ref<FSSpecializedClassHandle> handle;
	handle.instantiate();
	handle->script = p_script;
	handle->type_arguments = p_type_arguments;
	return handle;
}

static FSSpecializedClassHandle *_specialized_class_handle_from_variant(const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return nullptr;
	}

	Object *object = p_value.get_validated_object();
	if (object == nullptr) {
		return nullptr;
	}

	return Object::cast_to<FSSpecializedClassHandle>(object);
}

static bool _native_container_type_accepts_specialized_handle_erasure(const ContainerType &p_expected_type) {
	// A class-handle slot (`Type[Node]`) expects the handle itself, and core's class-handle rule reads a
	// specialized handle's own reified arguments. Erasing it to the bare script would throw that away.
	return p_expected_type.builtin_type == Variant::OBJECT && p_expected_type.script.is_null() &&
			!p_expected_type.is_type_handle && p_expected_type.type_arguments.is_empty();
}

bool FoundryScript::container_type_accepts_specialized_handle_erasure(const ContainerType &p_expected_type) {
	if (_native_container_type_accepts_specialized_handle_erasure(p_expected_type)) {
		return true;
	}

	if (p_expected_type.builtin_type == Variant::ARRAY) {
		return !p_expected_type.element_types.is_empty() &&
				container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[0]);
	}

	if (p_expected_type.builtin_type == Variant::DICTIONARY && !p_expected_type.element_types.is_empty()) {
		if (container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[0])) {
			return true;
		}
		return p_expected_type.element_types.size() > 1 &&
				container_type_accepts_specialized_handle_erasure(p_expected_type.element_types[1]);
	}

	return false;
}

static bool _erase_specialized_class_handle_for_native_container_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (!_native_container_type_accepts_specialized_handle_erasure(p_expected_type)) {
		return false;
	}

	FSSpecializedClassHandle *specialized_handle = _specialized_class_handle_from_variant(r_value);
	if (specialized_handle == nullptr || !specialized_handle->is_assignable_to_native_type(p_expected_type.class_name)) {
		return false;
	}

	r_value = specialized_handle->get_specialized_script();
	return true;
}

static bool _erase_specialized_class_handles_for_array_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (p_expected_type.builtin_type != Variant::ARRAY || p_expected_type.element_types.is_empty() ||
			r_value.get_type() != Variant::ARRAY) {
		return false;
	}

	const ContainerType &element_type = p_expected_type.element_types[0];
	const Array source = r_value;

	Vector<Variant> values;
	values.resize(source.size());
	bool changed = false;
	for (int i = 0; i < source.size(); i++) {
		Variant value = source[i];
		changed = FoundryScript::erase_specialized_class_handles_for_container_type(element_type, value) || changed;
		values.write[i] = value;
	}

	if (!changed) {
		return false;
	}

	Array erased;
	// Erasing a handle replaces it with the bare script it specializes, which the source's own element
	// type may no longer accept. Keep the source's typing whenever it still holds, so a write does not
	// silently downgrade a typed container to an untyped one.
	if (source.is_typed()) {
		const ContainerTypeValidate element_validator(source.get_element_type());
		bool keeps_element_type = true;
		for (const Variant &value : values) {
			if (!element_validator.test_validate(value)) {
				keeps_element_type = false;
				break;
			}
		}
		if (keeps_element_type) {
			erased.set_typed(source.get_element_type());
		}
	}
	erased.resize(values.size());
	for (int i = 0; i < values.size(); i++) {
		erased[i] = values[i];
	}

	r_value = erased;
	return true;
}

static bool _erase_specialized_class_handles_for_dictionary_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (p_expected_type.builtin_type != Variant::DICTIONARY || p_expected_type.element_types.is_empty() ||
			r_value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	const ContainerType &key_type = p_expected_type.element_types[0];
	const ContainerType value_type = p_expected_type.element_types.size() > 1 ? p_expected_type.element_types[1] : ContainerType();
	const Dictionary source = r_value;

	Vector<Pair<Variant, Variant>> entries;
	entries.resize(source.size());
	int entry_index = 0;
	bool changed = false;
	for (const KeyValue<Variant, Variant> &E : source) {
		Variant key = E.key;
		Variant value = E.value;
		changed = FoundryScript::erase_specialized_class_handles_for_container_type(key_type, key) || changed;
		changed = FoundryScript::erase_specialized_class_handles_for_container_type(value_type, value) || changed;
		entries.write[entry_index++] = Pair<Variant, Variant>(key, value);
	}

	if (!changed) {
		return false;
	}

	Dictionary erased;
	// Same reasoning as the array case: keep the source's key and value types when erasure did not
	// invalidate them.
	if (source.is_typed()) {
		const ContainerTypeValidate key_validator(source.get_key_type());
		const ContainerTypeValidate value_validator(source.get_value_type());
		bool keeps_entry_types = true;
		for (const Pair<Variant, Variant> &entry : entries) {
			if (!key_validator.test_validate(entry.first) || !value_validator.test_validate(entry.second)) {
				keeps_entry_types = false;
				break;
			}
		}
		if (keeps_entry_types) {
			erased.set_typed(source.get_key_type(), source.get_value_type());
		}
	}
	erased.reserve(entries.size());
	for (const Pair<Variant, Variant> &entry : entries) {
		erased[entry.first] = entry.second;
	}

	r_value = erased;
	return true;
}

bool FoundryScript::erase_specialized_class_handles_for_container_type(const ContainerType &p_expected_type, Variant &r_value) {
	if (_erase_specialized_class_handle_for_native_container_type(p_expected_type, r_value)) {
		return true;
	}
	if (_erase_specialized_class_handles_for_array_type(p_expected_type, r_value)) {
		return true;
	}
	return _erase_specialized_class_handles_for_dictionary_type(p_expected_type, r_value);
}

static bool _erase_specialized_class_handle_for_native_data_type(const FSDataType &p_expected_type, Variant &r_value) {
	if (p_expected_type.kind != FSDataType::NATIVE || p_expected_type.builtin_type != Variant::OBJECT) {
		return false;
	}

	FSSpecializedClassHandle *specialized_handle = _specialized_class_handle_from_variant(r_value);
	if (specialized_handle == nullptr || !specialized_handle->is_assignable_to_native_type(p_expected_type.native_type)) {
		return false;
	}

	r_value = specialized_handle->get_specialized_script();
	return true;
}

// Resolves the surviving `TYPE_PARAMETER` nodes of a baked binding type against the reified arguments
// of the receiver, producing recursive evidence. A node that names a class parameter the receiver
// supplied becomes that argument; one that names a parameter nothing supplied, or that was permanently
// unresolved by a raw `extends` step (`type_parameter_index == -1`), becomes `UNKNOWN` and leaves only
// that subtree gradual.
static ProjectedContainerType _project_binding_data_type(const FSDataType &p_type, const Vector<ContainerType> &p_leaf_type_arguments, int p_depth) {
	ProjectedContainerType projected;
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		return projected;
	}

	if (p_type.is_nullable) {
		// Core container types cannot express "this type or null", which is why `to_container_type()`
		// erases a nullable type outright. There is no descriptor that would accept the nulls the slot
		// admits, so the subtree carries no runtime evidence; the analyzer still enforces it statically.
		return projected;
	}

	if (p_type.kind == FSDataType::TYPE_PARAMETER) {
		if (p_type.type_parameter_scope != FSDataType::TYPE_PARAMETER_CLASS ||
				p_type.type_parameter_index < 0 || p_type.type_parameter_index >= p_leaf_type_arguments.size()) {
			return projected;
		}
		ProjectedContainerType resolved = ProjectedContainerType::exact(p_leaf_type_arguments[p_type.type_parameter_index]);
		if (p_type.is_type_handle) {
			// `Type[T]` reifies as a class handle for the argument, not an instance of it. A non-object
			// argument has no handle form to describe, so that subtree keeps no evidence.
			if (resolved.outer.builtin_type != Variant::OBJECT) {
				return projected;
			}
			resolved.outer.is_type_handle = true;
		}
		return resolved;
	}

	projected.state = ProjectedContainerType::EXACT;
	projected.outer = p_type.to_container_type();
	projected.outer.element_types.clear();
	projected.outer.type_arguments.clear();
	for (const FSDataType &element_type : p_type.container_element_types) {
		projected.element_types.push_back(_project_binding_data_type(element_type, p_leaf_type_arguments, p_depth + 1));
	}
	for (const FSDataType &type_argument : p_type.type_arguments) {
		projected.type_arguments.push_back(_project_binding_data_type(type_argument, p_leaf_type_arguments, p_depth + 1));
	}

	for (const ProjectedContainerType &child : projected.element_types) {
		if (!child.is_known() || child.state == ProjectedContainerType::PARTIAL) {
			projected.state = ProjectedContainerType::PARTIAL;
			break;
		}
	}
	if (projected.state == ProjectedContainerType::EXACT) {
		for (const ProjectedContainerType &child : projected.type_arguments) {
			if (!child.is_known() || child.state == ProjectedContainerType::PARTIAL) {
				projected.state = ProjectedContainerType::PARTIAL;
				break;
			}
		}
	}
	return projected;
}

ProjectedContainerType FoundryScript::project_type_argument_binding(const FoundryScript::TypeArgumentBinding &p_binding, const Vector<ContainerType> &p_leaf_type_arguments) {
	switch (p_binding.kind) {
		case FoundryScript::TypeArgumentBinding::NONE:
			return ProjectedContainerType();
		case FoundryScript::TypeArgumentBinding::FIXED:
			return _project_binding_data_type(p_binding.fixed, p_leaf_type_arguments, 0);
		case FoundryScript::TypeArgumentBinding::OPEN: {
			if (p_binding.leaf_ordinal < 0 || p_binding.leaf_ordinal >= p_leaf_type_arguments.size()) {
				// An OPEN binding with no reified argument (e.g. an unspecialized generic class) leaves the
				// slot effectively untyped.
				return ProjectedContainerType();
			}
			// The leaf was specialized at this ordinal, so the argument (even an explicit `Variant`) is
			// definite evidence.
			return ProjectedContainerType::exact(p_leaf_type_arguments[p_binding.leaf_ordinal]);
		}
	}
	return ProjectedContainerType();
}

// Applies a member slot's optional `Type[...]` wrapper to already-resolved evidence and validates the
// write. Every member-write path funnels through here so the dynamic `set()` path, the direct VM
// member-store opcode, and the inherited-static backstop cannot disagree.
static bool _validate_write_against_projected_type(ProjectedContainerType p_expected, bool p_is_type_handle, Variant &r_value, String *r_expected_type_name) {
	ProjectedContainerType expected = p_expected;
	if (!expected.is_known()) {
		return true;
	}

	if (p_is_type_handle) {
		// The binding resolves the represented value type; a `Type[T]` member wraps a handle layer around
		// it. A value type that is not object-shaped denotes no class at all, so only null satisfies the
		// slot, matching the descriptor-level class-handle rule.
		if (expected.outer.builtin_type != Variant::OBJECT) {
			if (r_value.get_type() == Variant::NIL) {
				return true;
			}
			if (r_expected_type_name != nullptr) {
				*r_expected_type_name = vformat("Type[%s]", expected.get_type_name());
			}
			return false;
		}
		expected.outer.is_type_handle = true;
	}

	FoundryScript::erase_specialized_class_handles_for_container_type(expected.to_container_type(), r_value);
	if (!expected.validate_value(r_value, "member", "assign")) {
		if (r_expected_type_name != nullptr) {
			*r_expected_type_name = expected.get_type_name();
		}
		return false;
	}
	return true;
}

bool FoundryScript::validate_type_argument_binding_write(const FoundryScript::TypeArgumentBinding &p_binding, const Vector<ContainerType> &p_leaf_type_arguments, Variant &r_value, String *r_expected_type_name) {
	return _validate_write_against_projected_type(project_type_argument_binding(p_binding, p_leaf_type_arguments),
			p_binding.is_type_handle, r_value, r_expected_type_name);
}

bool FoundryScript::_validate_static_member_write(FoundryScript *p_receiver, FoundryScript *p_declaring_script, const FoundryScript::TypeArgumentBinding &p_binding, const Vector<ContainerType> &p_leaf_type_arguments, Variant &r_value) {
	if (p_binding.kind == FoundryScript::TypeArgumentBinding::NONE) {
		return true;
	}
	if (p_binding.kind == FoundryScript::TypeArgumentBinding::FIXED || p_declaring_script == p_receiver) {
		// A FIXED binding is self-sufficient (already a concrete argument), and an OPEN binding declared
		// directly on the receiver already indexes the receiver's own parameter list.
		return validate_type_argument_binding_write(p_binding, p_leaf_type_arguments, r_value);
	}

	// The static member is inherited from a generic ancestor that was never copied into the receiver
	// (unlike instance members, static members stay on the class that declares them), so the binding's
	// ordinal indexes `p_declaring_script`'s own type parameters, not the receiver's. Project it through
	// the receiver's per-ancestor specialization table (`extends Base[int]` in the chain, or the
	// receiver's own reified arguments) before validating.
	//
	// The declaring script's storage slot is shared by every subclass, so two subclasses that fix the
	// ancestor's parameter differently (`IntBox extends Box[int]`, `StringBox extends Box[String]`) are
	// still writing the same physical slot: this only makes a dynamic write at least as strict as the
	// already-compiled static path (which has the identical cross-subclass sharing hazard with zero
	// runtime validation at all), not a stronger guarantee that the slot's current value matches every
	// subclass's declared type at every moment.
	Vector<ProjectedContainerType> projected;
	if (!p_receiver->project_type_arguments_onto_base(p_declaring_script, p_leaf_type_arguments, projected)) {
		return true;
	}
	if (p_binding.leaf_ordinal < 0 || p_binding.leaf_ordinal >= projected.size()) {
		return true;
	}

	// The projected slot carries the same recursive evidence a resolved instance binding does, so a
	// composite argument fixed partway up the chain (`extends Box[Pair[int, U]]`) still enforces its
	// known parts here instead of degrading the whole slot to untyped.
	return _validate_write_against_projected_type(projected[p_binding.leaf_ordinal], p_binding.is_type_handle, r_value, nullptr);
}

Ref<FSAnnotation> FSAnnotation::from_usage(const FoundryScript::AnnotationUsage &p_usage) {
	Ref<FSAnnotation> descriptor;
	descriptor.instantiate();
	descriptor->name = p_usage.name;
	descriptor->qualified_name = p_usage.qualified_name;
	// Deep copy so the descriptor owns an independent snapshot and never aliases compiled metadata.
	descriptor->args = p_usage.args.duplicate(true);
	descriptor->kwargs = p_usage.kwargs.duplicate(true);
	descriptor->builtin = p_usage.is_builtin;
	return descriptor;
}

void FSAnnotation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_annotation_name"), &FSAnnotation::get_annotation_name);
	ClassDB::bind_method(D_METHOD("get_qualified_name"), &FSAnnotation::get_qualified_name);
	ClassDB::bind_method(D_METHOD("get_arguments"), &FSAnnotation::get_arguments);
	ClassDB::bind_method(D_METHOD("get_named_arguments"), &FSAnnotation::get_named_arguments);
	ClassDB::bind_method(D_METHOD("is_builtin"), &FSAnnotation::is_builtin);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_annotation_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "qualified_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_qualified_name");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "args", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_arguments");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "kwargs", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_named_arguments");
	// Property name differs from the getter (`is_builtin`) so the bound method does not collide with a
	// same-named property, matching Godot's bool-property convention (e.g. `visible` / `is_visible`).
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "builtin", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "is_builtin");
}

Ref<FSMethodDescriptor> FSMethodDescriptor::create(const MethodInfo &p_method_info, const TypedArray<FSAnnotation> &p_annotations, bool p_fs_member, const HashMap<StringName, TypedArray<FSAnnotation>> &p_parameter_annotations) {
	Ref<FSMethodDescriptor> descriptor;
	descriptor.instantiate();
	descriptor->method_info = p_method_info;
	descriptor->annotations = p_annotations;
	descriptor->parameter_annotations = p_parameter_annotations;
	descriptor->fs_member = p_fs_member;
	return descriptor;
}

TypedArray<Dictionary> FSMethodDescriptor::get_arguments() const {
	TypedArray<Dictionary> result;
	for (const PropertyInfo &argument : method_info.arguments) {
		Dictionary arg_dict(argument);
		if (fs_member) {
			const TypedArray<FSAnnotation> *parameter_annotations_for_arg = parameter_annotations.getptr(argument.name);
			if (parameter_annotations_for_arg != nullptr) {
				arg_dict["annotations"] = parameter_annotations_for_arg->duplicate();
			}
		}
		result.push_back(arg_dict);
	}
	return result;
}

Dictionary FSMethodDescriptor::get_return_value() const {
	return Dictionary(method_info.return_val);
}

Array FSMethodDescriptor::get_default_arguments() const {
	Array result;
	for (const Variant &default_argument : method_info.default_arguments) {
		result.push_back(default_argument);
	}
	// Deep copy so an Array/Dictionary default cannot be mutated through the returned snapshot and
	// alias this read-only descriptor's stored metadata.
	return result.duplicate(true);
}

Dictionary FSMethodDescriptor::to_dictionary() const {
	Dictionary descriptor(method_info);
	// Dictionary(MethodInfo) shallow-copies default_arguments, so an Array/Dictionary default would
	// alias this descriptor's stored metadata. Replace it with the deep-copied snapshot to keep the
	// returned Dictionary read-only.
	descriptor["default_args"] = get_default_arguments();
	// Mirror the loosely-keyed `get_methods()` descriptor: FoundryScript methods carry an `annotations` key
	// (empty when the method has none); native methods omit it to preserve the historical shape.
	// Duplicate so the returned Dictionary never aliases this read-only descriptor's stored array.
	if (fs_member) {
		descriptor["annotations"] = annotations.duplicate();
	}
	return descriptor;
}

void FSMethodDescriptor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_method_name"), &FSMethodDescriptor::get_method_name);
	ClassDB::bind_method(D_METHOD("get_arguments"), &FSMethodDescriptor::get_arguments);
	ClassDB::bind_method(D_METHOD("get_return_value"), &FSMethodDescriptor::get_return_value);
	ClassDB::bind_method(D_METHOD("get_default_arguments"), &FSMethodDescriptor::get_default_arguments);
	ClassDB::bind_method(D_METHOD("get_flags"), &FSMethodDescriptor::get_flags);
	ClassDB::bind_method(D_METHOD("get_annotations"), &FSMethodDescriptor::get_annotations);
	ClassDB::bind_method(D_METHOD("to_dictionary"), &FSMethodDescriptor::to_dictionary);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_method_name");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "args", PROPERTY_HINT_ARRAY_TYPE, "Dictionary", PROPERTY_USAGE_READ_ONLY), "", "get_arguments");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "return_value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_return_value");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "default_args", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_default_arguments");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flags", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_flags");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "annotations", PROPERTY_HINT_ARRAY_TYPE, "FSAnnotation", PROPERTY_USAGE_READ_ONLY), "", "get_annotations");
}

Ref<FSPropertyDescriptor> FSPropertyDescriptor::create(const PropertyInfo &p_property_info, const TypedArray<FSAnnotation> &p_annotations, bool p_fs_member) {
	Ref<FSPropertyDescriptor> descriptor;
	descriptor.instantiate();
	descriptor->property_info = p_property_info;
	descriptor->annotations = p_annotations;
	descriptor->fs_member = p_fs_member;
	return descriptor;
}

Dictionary FSPropertyDescriptor::to_dictionary() const {
	Dictionary descriptor(property_info);
	// Mirror the loosely-keyed `get_properties()` descriptor: FoundryScript variables carry an `annotations`
	// key (empty when the variable has none); native properties omit it to preserve the historical shape.
	// Duplicate so the returned Dictionary never aliases this read-only descriptor's stored array.
	if (fs_member) {
		descriptor["annotations"] = annotations.duplicate();
	}
	return descriptor;
}

void FSPropertyDescriptor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_property_name"), &FSPropertyDescriptor::get_property_name);
	ClassDB::bind_method(D_METHOD("get_property_type"), &FSPropertyDescriptor::get_property_type);
	ClassDB::bind_method(D_METHOD("get_property_class_name"), &FSPropertyDescriptor::get_property_class_name);
	ClassDB::bind_method(D_METHOD("get_property_hint"), &FSPropertyDescriptor::get_property_hint);
	ClassDB::bind_method(D_METHOD("get_property_hint_string"), &FSPropertyDescriptor::get_property_hint_string);
	ClassDB::bind_method(D_METHOD("get_property_usage"), &FSPropertyDescriptor::get_property_usage);
	ClassDB::bind_method(D_METHOD("get_annotations"), &FSPropertyDescriptor::get_annotations);
	ClassDB::bind_method(D_METHOD("to_dictionary"), &FSPropertyDescriptor::to_dictionary);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_type");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "class_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_class_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hint", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_hint");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "hint_string", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_hint_string");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "usage", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_property_usage");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "annotations", PROPERTY_HINT_ARRAY_TYPE, "FSAnnotation", PROPERTY_USAGE_READ_ONLY), "", "get_annotations");
}

Variant FSNativeClass::_new() {
	Object *o = instantiate();
	ERR_FAIL_NULL_V_MSG(o, Variant(), "Class type: '" + String(name) + "' is not instantiable.");

	RefCounted *rc = Object::cast_to<RefCounted>(o);
	if (rc) {
		return Ref<RefCounted>(rc);
	} else {
		return o;
	}
}

Object *FSNativeClass::instantiate() {
	return ClassDB::instantiate_no_placeholders(name);
}

Variant FSNativeClass::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_method == SNAME("new")) {
		// Constructor.
		return Object::callp(p_method, p_args, p_argcount, r_error);
	}
	MethodBind *method = ClassDB::get_method(name, p_method);
	if (method && method->is_static()) {
		// Native static method.
		return method->call(nullptr, p_args, p_argcount, r_error);
	}

	// Retroactive-conformance fallback. A `static` witness supplied by an external
	// `extend <EngineClass> uses Trait: ...` has no `MethodBind` and is reachable only through the class,
	// so it is dispatched here with no instance. The engine inheritance chain is walked, so a witness
	// declared on a base class answers for a subclass as well.
	FSFunction *witness = FSConformanceRegistry::get_singleton()->find_native_witness_function(name, p_method);
	if (witness != nullptr && witness->is_static()) {
		// The witness may be declared on a native ancestor; the frame is still told the exact class
		// handle the call was made through.
		const FSStaticSelfContext static_self = FSStaticSelfContext::for_native_class(name);
		return witness->call(nullptr, p_args, p_argcount, r_error, nullptr, nullptr, &static_self);
	}

	r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}

FSFunction *FoundryScript::_super_constructor(FoundryScript *p_script) {
	if (likely(p_script->valid) && p_script->initializer) {
		return p_script->initializer;
	} else {
		FoundryScript *base_src = p_script->base.ptr();
		if (base_src != nullptr) {
			return _super_constructor(base_src);
		} else {
			return nullptr;
		}
	}
}

void FoundryScript::_super_implicit_constructor(FoundryScript *p_script, FSInstance *p_instance, Callable::CallError &r_error) {
	FoundryScript *base_src = p_script->base.ptr();
	if (base_src != nullptr) {
		_super_implicit_constructor(base_src, p_instance, r_error);
		if (r_error.error != Callable::CallError::CALL_OK) {
			return;
		}
	}
	if (unlikely(p_script->implicit_initializer == nullptr)) {
		// Compilation always produces an `@implicit_new()` function, so a missing one means this
		// script was cleared (e.g. language shutdown while a stale Ref/ResourceCache entry kept it
		// alive) or never compiled. A cleared script is also marked invalid, so a valid script
		// missing its initializer is an engine bug.
		DEV_ASSERT(!p_script->valid);
		// Propagate the failure through `r_error` so `_create_instance` tears the instance down
		// instead of returning a half-constructed instance whose member defaults never ran.
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		ERR_FAIL_MSG(vformat("Cannot construct an instance of script \"%s\": missing compiled implicit initializer (the script was cleared or failed to compile).", p_script->get_script_path()));
	}
	if (likely(p_script->valid)) {
		p_script->implicit_initializer->call(p_instance, nullptr, 0, r_error);
	} else {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	}
}

FSInstance *FoundryScript::_create_instance(const Variant **p_args, int p_argcount, Object *p_owner, Callable::CallError &r_error, const Vector<ContainerType> *p_type_arguments) {
	/* STEP 1, CREATE */

	FSInstance *instance = memnew(FSInstance);
	instance->members.resize(member_indices.size());
	instance->script = Ref<FoundryScript>(this);
	instance->owner = p_owner;
	instance->owner_id = p_owner->get_instance_id();
	if (p_type_arguments != nullptr) {
		instance->type_arguments = *p_type_arguments;
	}
#ifdef DEBUG_ENABLED
	//needed for hot reloading
	for (const KeyValue<StringName, MemberInfo> &E : member_indices) {
		instance->member_indices_cache[E.key] = E.value.index;
	}
#endif
	instance->owner->set_script_instance(instance);

	/* STEP 2, INITIALIZE AND CONSTRUCT */
	{
		MutexLock lock(FSLanguage::singleton->mutex);
		instances.insert(instance->owner);
	}

	_super_implicit_constructor(this, instance, r_error);
	if (r_error.error != Callable::CallError::CALL_OK) {
		String error_text = Variant::get_call_error_text(instance->owner, "@implicit_new", nullptr, 0, r_error);
		instance->script = Ref<FoundryScript>();
		instance->owner->set_script_instance(nullptr);
		{
			MutexLock lock(FSLanguage::singleton->mutex);
			instances.erase(p_owner);
		}
		ERR_FAIL_V_MSG(nullptr, "Error constructing a FSInstance: " + error_text);
	}

	if (p_argcount < 0) {
		return instance;
	}

	FSFunction *applicable_initializer = _super_constructor(this);
	if (applicable_initializer != nullptr) {
		applicable_initializer->call(instance, p_args, p_argcount, r_error);
		if (r_error.error != Callable::CallError::CALL_OK) {
			String error_text = Variant::get_call_error_text(instance->owner, "_init", p_args, p_argcount, r_error);
			instance->script = Ref<FoundryScript>();
			instance->owner->set_script_instance(nullptr);
			{
				MutexLock lock(FSLanguage::singleton->mutex);
				instances.erase(p_owner);
			}
			ERR_FAIL_V_MSG(nullptr, "Error constructing a FSInstance: " + error_text);
		}
	}
	//@TODO make thread safe
	return instance;
}

Variant FoundryScript::_new(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return _new_specialized(p_args, p_argcount, Vector<ContainerType>(), r_error);
}

Variant FoundryScript::_new_specialized(const Variant **p_args, int p_argcount, const Vector<ContainerType> &p_type_arguments, Callable::CallError &r_error) {
	/* STEP 1, CREATE */

	if (!valid) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	r_error.error = Callable::CallError::CALL_OK;
	Ref<RefCounted> ref;
	Object *owner = nullptr;

	FoundryScript *_baseptr = this;
	while (_baseptr->base.ptr()) {
		_baseptr = _baseptr->base.ptr();
	}

	ERR_FAIL_COND_V(_baseptr->native.is_null(), Variant());
	if (_baseptr->native.ptr()) {
		owner = _baseptr->native->instantiate();
	} else {
		owner = memnew(RefCounted); //by default, no base means use reference
	}
	ERR_FAIL_NULL_V_MSG(owner, Variant(), "Can't inherit from a virtual class.");

	RefCounted *r = Object::cast_to<RefCounted>(owner);
	if (r) {
		ref = Ref<RefCounted>(r);
	}

	const Vector<ContainerType> *type_arguments = p_type_arguments.is_empty() ? nullptr : &p_type_arguments;
	FSInstance *instance = _create_instance(p_args, p_argcount, owner, r_error, type_arguments);
	if (!instance) {
		if (ref.is_null()) {
			memdelete(owner); //no owner, sorry
		}
		return Variant();
	}

	if (ref.is_valid()) {
		return ref;
	} else {
		return owner;
	}
}

bool FoundryScript::can_instantiate() const {
#ifdef TOOLS_ENABLED
	return valid && (tool || ScriptServer::is_scripting_enabled()) && !Engine::get_singleton()->is_recovery_mode_hint();
#else
	return valid;
#endif
}

Ref<Script> FoundryScript::get_base_script() const {
	return base;
}

StringName FoundryScript::get_global_name() const {
	return global_name;
}

StringName FoundryScript::get_instance_base_type() const {
	if (native.is_valid()) {
		return native->get_name();
	}
	if (base.is_valid() && base->is_valid()) {
		return base->get_instance_base_type();
	}
	return StringName();
}

struct _FSMemberSort {
	int index = 0;
	StringName name;
	_FORCE_INLINE_ bool operator<(const _FSMemberSort &p_member) const { return index < p_member.index; }
};

#ifdef TOOLS_ENABLED

void FoundryScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {
	placeholders.erase(p_placeholder);
}

#endif

void FoundryScript::_get_script_method_list(List<MethodInfo> *r_list, bool p_include_base) const {
	const FoundryScript *current = this;
	while (current) {
		for (const KeyValue<StringName, FSFunction *> &E : current->member_functions) {
			r_list->push_back(E.value->get_method_info());
		}

		if (!p_include_base) {
			return;
		}

		current = current->base.ptr();
	}
}

void FoundryScript::get_script_method_list(List<MethodInfo> *r_list) const {
	_get_script_method_list(r_list, true);
}

void FoundryScript::_get_script_property_list(List<PropertyInfo> *r_list, bool p_include_base) const {
	const FoundryScript *sptr = this;
	List<PropertyInfo> props;

	while (sptr) {
		Vector<_FSMemberSort> msort;
		for (const KeyValue<StringName, MemberInfo> &E : sptr->member_indices) {
			if (!sptr->members.has(E.key)) {
				continue; // Skip base class members.
			}
			_FSMemberSort ms;
			ms.index = E.value.index;
			ms.name = E.key;
			msort.push_back(ms);
		}

		msort.sort();
		msort.reverse();
		for (int i = 0; i < msort.size(); i++) {
			props.push_front(sptr->member_indices[msort[i].name].property_info);
		}

#ifdef TOOLS_ENABLED
		r_list->push_back(sptr->get_class_category());
#endif // TOOLS_ENABLED

		for (const PropertyInfo &E : props) {
			r_list->push_back(E);
		}

		if (!p_include_base) {
			break;
		}

		props.clear();
		sptr = sptr->base.ptr();
	}
}

void FoundryScript::get_script_property_list(List<PropertyInfo> *r_list) const {
	_get_script_property_list(r_list, true);
}

bool FoundryScript::has_method(const StringName &p_method) const {
	return member_functions.has(p_method);
}

bool FoundryScript::has_static_method(const StringName &p_method) const {
	return member_functions.has(p_method) && member_functions[p_method]->is_static();
}

int FoundryScript::get_script_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	HashMap<StringName, FSFunction *>::ConstIterator E = member_functions.find(p_method);
	if (!E) {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return 0;
	}

	if (r_is_valid) {
		*r_is_valid = true;
	}
	return E->value->get_argument_count();
}

MethodInfo FoundryScript::get_method_info(const StringName &p_method) const {
	HashMap<StringName, FSFunction *>::ConstIterator E = member_functions.find(p_method);
	if (!E) {
		return MethodInfo();
	}

	return E->value->get_method_info();
}

bool FoundryScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
#ifdef TOOLS_ENABLED

	HashMap<StringName, Variant>::ConstIterator E = member_default_values_cache.find(p_property);
	if (E) {
		r_value = E->value;
		return true;
	}

	if (base_cache.is_valid()) {
		return base_cache->get_property_default_value(p_property, r_value);
	}
#endif
	return false;
}

ScriptInstance *FoundryScript::instance_create(Object *p_this) {
	ERR_FAIL_COND_V_MSG(!valid, nullptr, "Script is invalid!");

	FoundryScript *top = this;
	while (top->base.ptr()) {
		top = top->base.ptr();
	}

	if (top->native.is_valid()) {
		if (!ClassDB::is_parent_class(p_this->get_class_name(), top->native->get_name())) {
			if (EngineDebugger::is_active()) {
				FSLanguage::get_singleton()->debug_break_parse(_get_debug_path(), 1, "Script inherits from native type '" + String(top->native->get_name()) + "', so it can't be assigned to an object of type: '" + p_this->get_class() + "'");
			}
			ERR_FAIL_V_MSG(nullptr, "Script inherits from native type '" + String(top->native->get_name()) + "', so it can't be assigned to an object of type '" + p_this->get_class() + "'" + ".");
		}
	}

	Callable::CallError unchecked_error;
	return _create_instance(nullptr, 0, p_this, unchecked_error);
}

PlaceHolderScriptInstance *FoundryScript::placeholder_instance_create(Object *p_this) {
#ifdef TOOLS_ENABLED
	PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(FSLanguage::get_singleton(), Ref<Script>(this), p_this));
	placeholders.insert(si);
	_update_exports(nullptr, false, si);
	return si;
#else
	return nullptr;
#endif
}

bool FoundryScript::instance_has(const Object *p_this) const {
	MutexLock lock(FSLanguage::singleton->mutex);

	return instances.has((Object *)p_this);
}

bool FoundryScript::has_source_code() const {
	return !source.is_empty();
}

String FoundryScript::get_source_code() const {
	return source;
}

void FoundryScript::set_source_code(const String &p_code) {
	if (source == p_code) {
		return;
	}
	source = p_code;
#ifdef TOOLS_ENABLED
	source_changed_cache = true;
#endif
}

#ifdef TOOLS_ENABLED
void FoundryScript::_update_exports_values(HashMap<StringName, Variant> &values, List<PropertyInfo> &propnames) {
	for (const KeyValue<StringName, Variant> &E : member_default_values_cache) {
		values[E.key] = E.value;
	}

	for (const PropertyInfo &E : members_cache) {
		propnames.push_back(E);
	}

	if (base_cache.is_valid()) {
		base_cache->_update_exports_values(values, propnames);
	}
}

void FoundryScript::_ensure_documentation() {
	if (docs_generated) {
		return;
	}
	// Docs bubble up to and are stored on the top-level script (see _add_doc).
	if (_owner != nullptr) {
		return;
	}
	// Mark generated up-front so re-entrant _add_doc calls (and recursion) don't loop.
	docs_generated = true;

	if (source.is_empty() && binary_tokens.is_empty()) {
		return;
	}

	// Doc generation needs an analyzed parse tree, plus the inner-class FoundryScript
	// objects created by make_scripts() during the last compile (which persist on `this`).
	FSParser parser;
	Error err;
	if (!binary_tokens.is_empty()) {
		err = parser.parse_binary(binary_tokens, path);
	} else {
		err = parser.parse(source, path, false);
	}
	if (err != OK) {
		return;
	}
	FSAnalyzer analyzer(&parser);
	if (analyzer.analyze() != OK) {
		return;
	}
	FSDocGen::generate_docs(this, parser.get_tree());
}

void FoundryScript::_add_doc(const DocData::ClassDoc &p_doc) {
	doc_class_name = p_doc.name;
	if (_owner) { // Only the top-level class stores doc info.
		_owner->_add_doc(p_doc);
	} else { // Remove old docs, add new.
		for (int i = 0; i < docs.size(); i++) {
			if (docs[i].name == p_doc.name) {
				docs.remove_at(i);
				break;
			}
		}
		docs.append(p_doc);
	}
}

void FoundryScript::_clear_doc() {
	doc_class_name = StringName();
	doc = DocData::ClassDoc();
	docs.clear();
	// Allow docs to be regenerated on the next request if the script is still valid.
	docs_generated = false;
}

String FoundryScript::get_class_icon_path() const {
	return simplified_icon_path;
}
#endif

bool FoundryScript::_update_exports(bool *r_err, bool p_recursive_call, PlaceHolderScriptInstance *p_instance_to_update, bool p_base_exports_changed, FSParser *p_reload_parser, FSAnalyzer *p_reload_analyzer) {
#ifdef TOOLS_ENABLED

	static Vector<FoundryScript *> base_caches;
	if (!p_recursive_call) {
		base_caches.clear();
	}
	base_caches.append(this);

	bool changed = p_base_exports_changed;

	if (source_changed_cache) {
		source_changed_cache = false;
		changed = true;

		// Reuse an already parsed and analyzed tree when the caller (reload()) provides one for
		// the current source, avoiding a redundant parse + analyze of the same script.
		FSParser local_parser;
		FSAnalyzer local_analyzer(&local_parser);
		FSParser *parser_ptr = p_reload_parser;
		FSAnalyzer *analyzer_ptr = p_reload_analyzer;
		bool analyzed_ok = true;
		if (parser_ptr == nullptr || analyzer_ptr == nullptr) {
			parser_ptr = &local_parser;
			analyzer_ptr = &local_analyzer;
			Error err = local_parser.parse(source, path, false);
			analyzed_ok = (err == OK && local_analyzer.analyze() == OK);
		}

		if (analyzed_ok) {
			const FSParser::ClassNode *c = parser_ptr->get_tree();
			FSAnalyzer &analyzer = *analyzer_ptr;

			if (base_cache.is_valid()) {
				base_cache->inheriters_cache.erase(get_instance_id());
				base_cache = Ref<FoundryScript>();
			}

			FSParser::DataType base_type = c->base_type;
			if (base_type.kind == FSParser::DataType::CLASS) {
				Error base_err = OK;
				Ref<FoundryScript> bf = FSCache::get_full_script(base_type.script_path, base_err, path);
				if (base_err == OK) {
					bf = Ref<FoundryScript>(bf->find_class(base_type.class_type->fqcn));
					if (bf.is_valid()) {
						base_cache = bf;
						bf->inheriters_cache.insert(get_instance_id());
					}
				}
			}

			members_cache.clear();
			member_default_values_cache.clear();
			_signals.clear();

			members_cache.push_back(get_class_category());

			for (int i = 0; i < c->members.size(); i++) {
				const FSParser::ClassNode::Member &member = c->members[i];

				switch (member.type) {
					case FSParser::ClassNode::Member::VARIABLE: {
						if (!member.variable->exported) {
							continue;
						}

						members_cache.push_back(member.variable->export_info);
						Variant default_value = analyzer.make_variable_default_value(member.variable);
						member_default_values_cache[member.variable->identifier->name] = default_value;
					} break;
					case FSParser::ClassNode::Member::SIGNAL: {
						_signals[member.signal->identifier->name] = member.signal->method_info;
					} break;
					case FSParser::ClassNode::Member::GROUP: {
						members_cache.push_back(member.annotation->export_info);
					} break;
					default:
						break; // Nothing.
				}
			}
		} else {
			placeholder_fallback_enabled = true;
			return false;
		}
	} else if (placeholder_fallback_enabled) {
		return false;
	}

	placeholder_fallback_enabled = false;

	if (base_cache.is_valid() && base_cache->is_valid()) {
		for (int i = 0; i < base_caches.size(); i++) {
			if (base_caches[i] == base_cache.ptr()) {
				if (r_err) {
					*r_err = true;
				}
				valid = false; // to show error in the editor
				base_cache->valid = false;
				base_cache->inheriters_cache.clear(); // to prevent future stackoverflows
				base_cache.unref();
				base.unref();
				ERR_FAIL_V_MSG(false, "Cyclic inheritance in script class.");
			}
		}
		if (base_cache->_update_exports(r_err, true)) {
			if (r_err && *r_err) {
				return false;
			}
			changed = true;
		}
	}

	if ((changed || p_instance_to_update) && placeholders.size()) { //hm :(

		// update placeholders if any
		HashMap<StringName, Variant> values;
		List<PropertyInfo> propnames;
		_update_exports_values(values, propnames);

		if (changed) {
			for (PlaceHolderScriptInstance *E : placeholders) {
				E->update(propnames, values);
			}
		} else {
			p_instance_to_update->update(propnames, values);
		}
	}

	return changed;

#else
	return false;
#endif
}

void FoundryScript::update_exports() {
#ifdef TOOLS_ENABLED
	_update_exports_down(false);
#endif
}

#ifdef TOOLS_ENABLED
void FoundryScript::_update_exports_down(bool p_base_exports_changed, FSParser *p_reload_parser, FSAnalyzer *p_reload_analyzer) {
	bool cyclic_error = false;
	// The provided parser/analyzer only describe this script, so they are used for this call and
	// never forwarded to inheriters (which have their own source and are re-parsed as before).
	bool changed = _update_exports(&cyclic_error, false, nullptr, p_base_exports_changed, p_reload_parser, p_reload_analyzer);

	if (cyclic_error) {
		return;
	}

	HashSet<ObjectID> copy = inheriters_cache; //might get modified

	for (const ObjectID &E : copy) {
		Object *id = ObjectDB::get_instance(E);
		FoundryScript *s = Object::cast_to<FoundryScript>(id);

		if (!s) {
			continue;
		}
		s->_update_exports_down(p_base_exports_changed || changed);
	}
}
#endif

String FoundryScript::_get_debug_path() const {
	if (is_built_in() && !get_name().is_empty()) {
		return vformat("%s(%s)", get_name(), get_script_path());
	} else {
		return get_script_path();
	}
}

Error FoundryScript::_static_init() {
	if (likely(valid) && static_initializer) {
		Callable::CallError call_err;
		const FSStaticSelfContext static_self = FSStaticSelfContext::for_script(Ref<Script>(this));
		static_initializer->call(nullptr, nullptr, 0, call_err, nullptr, nullptr, &static_self);
		if (call_err.error != Callable::CallError::CALL_OK) {
			return ERR_CANT_CREATE;
		}
	}
	Error err = OK;
	for (KeyValue<StringName, Ref<FoundryScript>> &inner : subclasses) {
		err = inner.value->_static_init();
		if (err) {
			break;
		}
	}
	return err;
}

void FoundryScript::_static_default_init() {
	for (const KeyValue<StringName, MemberInfo> &E : static_variables_indices) {
		const FSDataType &type = E.value.data_type;
		// Only initialize builtin types, which are not expected to be `null`.
		if (type.kind != FSDataType::BUILTIN) {
			continue;
		}
		if (type.builtin_type == Variant::ARRAY && type.has_container_element_type(0)) {
			const FSDataType element_type = type.get_container_element_type(0);
			Array default_value;
			default_value.set_typed(element_type.to_container_type());
			static_variables.write[E.value.index] = default_value;
		} else if (type.builtin_type == Variant::DICTIONARY && type.has_container_element_types()) {
			const FSDataType key_type = type.get_container_element_type_or_variant(0);
			const FSDataType value_type = type.get_container_element_type_or_variant(1);
			Dictionary default_value;
			default_value.set_typed(key_type.to_container_type(), value_type.to_container_type());
			static_variables.write[E.value.index] = default_value;
		} else {
			Variant default_value;
			Callable::CallError err;
			Variant::construct(type.builtin_type, default_value, nullptr, 0, err);
			static_variables.write[E.value.index] = default_value;
		}
	}
}

#ifdef TOOLS_ENABLED

void FoundryScript::_save_old_static_data() {
	old_static_variables_indices = static_variables_indices;
	old_static_variables = static_variables;
	for (KeyValue<StringName, Ref<FoundryScript>> &inner : subclasses) {
		inner.value->_save_old_static_data();
	}
}

void FoundryScript::_restore_old_static_data() {
	for (KeyValue<StringName, MemberInfo> &E : old_static_variables_indices) {
		if (static_variables_indices.has(E.key)) {
			static_variables.write[static_variables_indices[E.key].index] = old_static_variables[E.value.index];
		}
	}
	old_static_variables_indices.clear();
	old_static_variables.clear();
	for (KeyValue<StringName, Ref<FoundryScript>> &inner : subclasses) {
		inner.value->_restore_old_static_data();
	}
}

#endif

Error FoundryScript::reload(bool p_keep_state) {
	// Also the cycle terminator for bytecode-backed (`.fsb`) links: returning OK (not an error) lets
	// FSCache::get_full_script publish this script's invalid-but-error-free shell, which
	// FSBytecodeCacheResolver deliberately accepts so mutually preloading scripts can link.
	if (reloading) {
		return OK;
	}
	reloading = true;

	bool has_instances;
	{
		MutexLock lock(FSLanguage::singleton->mutex);

		has_instances = instances.size();
	}

	// Check condition but reset flag before early return
	if (!p_keep_state && has_instances) {
		reloading = false; // Reset flag before returning

		ERR_FAIL_V_MSG(ERR_ALREADY_IN_USE, "Cannot reload script while instances exist.");
	}

	String basedir = path;

	if (basedir.is_empty()) {
		basedir = get_path();
	}

	if (!basedir.is_empty()) {
		basedir = basedir.get_base_dir();
	}

	// Loading a template, don't parse.
#ifdef TOOLS_ENABLED
	if (EditorPaths::get_singleton() && basedir.begins_with(EditorPaths::get_singleton()->get_project_script_templates_dir())) {
		reloading = false;
		return OK;
	}
#endif

	// Bytecode-backed scripts carry no source to parse; they re-link from the compiled binary on
	// disk instead. An already linked script stays as-is: exported binaries are immutable, so
	// there is nothing newer to pick up.
	if (compiled_binary) {
		Error link_error = OK;
		if (!valid) {
			link_error = _reload_from_compiled_binary();
		}
		reloading = false;
		return link_error;
	}

#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	{
		reloading = false;
		const String script_path = path.is_empty() ? get_path() : path;
		_err_print_error("FoundryScript::reload", script_path.is_empty() ? "built-in" : (const char *)script_path.utf8().get_data(), 0,
				fs_no_frontend_error_message(script_path).utf8().get_data(), false, ERR_HANDLER_SCRIPT);
		return ERR_UNAVAILABLE;
	}
#else
	{
		String source_path = path;
		if (source_path.is_empty()) {
			source_path = get_path();
		}
		if (!source_path.is_empty()) {
			if (FSCache::get_cached_script(source_path).is_null()) {
				MutexLock lock(FSCache::singleton->mutex);
				FSCache::singleton->shallow_fs_cache[source_path] = Ref<FoundryScript>(this);
			}
			if (FSCache::has_parser(source_path)) {
				Error err = OK;
				Ref<FSParserRef> parser_ref = FSCache::get_parser(source_path, FSParserRef::EMPTY, err);
				if (parser_ref.is_valid()) {
					uint32_t source_hash;
					if (!binary_tokens.is_empty()) {
						source_hash = hash_djb2_buffer(binary_tokens.ptr(), binary_tokens.size());
					} else {
						source_hash = source.hash();
					}
					if (parser_ref->get_source_hash() != source_hash) {
#ifdef TOOLS_ENABLED
						FSLanguage::get_singleton()->notify_disk_source_changed(source_path);
#else
						FSCache::remove_parser(source_path);
#endif
					}
				}
			}
		}
	}

	bool can_run = ScriptServer::is_scripting_enabled() || is_tool();

#ifdef TOOLS_ENABLED
	if (p_keep_state && can_run && is_valid()) {
		_save_old_static_data();
	}
#endif

	valid = false;
	FSParser parser;
	Error err;
	if (!binary_tokens.is_empty()) {
		err = parser.parse_binary(binary_tokens, path);
	} else {
		err = parser.parse(source, path, false);
	}
	if (err) {
		if (EngineDebugger::is_active()) {
			FSLanguage::get_singleton()->debug_break_parse(_get_debug_path(), parser.get_errors().front()->get().line, "Parser Error: " + parser.get_errors().front()->get().message);
		}
		// TODO: Show all error messages.
		_err_print_error("FoundryScript::reload", path.is_empty() ? "built-in" : (const char *)path.utf8().get_data(), parser.get_errors().front()->get().line, ("Parse Error: " + parser.get_errors().front()->get().message).utf8().get_data(), false, ERR_HANDLER_SCRIPT);
		reloading = false;
		return ERR_PARSE_ERROR;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	if (err) {
		if (EngineDebugger::is_active()) {
			FSLanguage::get_singleton()->debug_break_parse(_get_debug_path(), parser.get_errors().front()->get().line, "Parser Error: " + parser.get_errors().front()->get().message);
		}

		const List<FSParser::ParserError>::Element *e = parser.get_errors().front();
		while (e != nullptr) {
			_err_print_error("FoundryScript::reload", path.is_empty() ? "built-in" : (const char *)path.utf8().get_data(), e->get().line, ("Parse Error: " + e->get().message).utf8().get_data(), false, ERR_HANDLER_SCRIPT);
			e = e->next();
		}
		reloading = false;
		return ERR_PARSE_ERROR;
	}

	can_run = ScriptServer::is_scripting_enabled() || parser.is_tool();

	FSCompiler compiler;
	err = compiler.compile(&parser, this, p_keep_state);

	if (err) {
		// TODO: Provide the script function as the first argument.
		_err_print_error("FoundryScript::reload", path.is_empty() ? "built-in" : (const char *)path.utf8().get_data(), compiler.get_error_line(), ("Compile Error: " + compiler.get_error()).utf8().get_data(), false, ERR_HANDLER_SCRIPT);
		if (can_run) {
			if (EngineDebugger::is_active()) {
				FSLanguage::get_singleton()->debug_break_parse(_get_debug_path(), compiler.get_error_line(), "Parser Error: " + compiler.get_error());
			}
			reloading = false;
			return ERR_COMPILATION_FAILED;
		} else {
			reloading = false;
			return err;
		}
	}

#ifdef TOOLS_ENABLED
	// Documentation is only consumed by editor-side surfaces (help viewer, script editor,
	// inspector tooltips, --doctool). It is never needed to load or instantiate a scene, so
	// it is generated lazily on the first get_documentation() request instead of on every
	// reload. This keeps scene/resource opening fast (doc generation was ~22% of cold script
	// load time). It still needs the inner class FoundryScript objects made by make_scripts()
	// within compiler.compile() above, which persist on the script.
	docs.clear();
	docs_generated = false;
#endif

#ifdef DEBUG_ENABLED
	for (const FSWarning &warning : parser.get_warnings()) {
		if (EngineDebugger::is_active()) {
			Vector<ScriptLanguage::StackInfo> si;
			// TODO: Provide the script function as the first argument.
			EngineDebugger::get_script_debugger()->send_error("FoundryScript::reload", get_script_path(), warning.start_line, warning.get_name(), warning.get_message(), false, ERR_HANDLER_WARNING, si);
		}
	}
#endif

	if (can_run) {
		err = _static_init();
		if (err) {
			return err;
		}
	}

#ifdef TOOLS_ENABLED
	if (can_run && p_keep_state) {
		_restore_old_static_data();
	}

	if (p_keep_state) {
		// Update the properties in the inspector. Reuse the parser/analyzer that just parsed and
		// analyzed this exact source above, so the export cache is populated without a redundant
		// third parse + second analyze of the same script.
		_update_exports_down(false, &parser, &analyzer);
	}
#endif

	reloading = false;
	return OK;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

void FoundryScript::_erase_function_lambda_info(FoundryScript *p_script, FSFunction *p_function) {
	if (p_script == nullptr || p_function == nullptr) {
		return;
	}
	p_script->lambda_info.erase(p_function);
	for (FSFunction *lambda : p_function->lambdas) {
		FoundryScript *lambda_script = Object::cast_to<FoundryScript>(lambda->get_script());
		_erase_function_lambda_info(lambda_script != nullptr ? lambda_script : p_script, lambda);
	}
}

void FoundryScript::_clear_partial_bytecode_link_state() {
	for (KeyValue<StringName, Ref<FoundryScript>> &subclass : subclasses) {
		subclass.value->_clear_partial_bytecode_link_state();
	}

	{
		MutexLock lock(func_ptrs_to_update_mutex);
		for (UpdatableFuncPtr *updatable : func_ptrs_to_update) {
			updatable->ptr = nullptr;
		}
	}

	RBSet<FSFunction *> functions_to_delete;

	if (!registered_conformance_source.is_empty()) {
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(registered_conformance_source);
		registered_conformance_source = String();
	}
	for (FSFunction *witness : witness_functions) {
		FoundryScript *target_script = Object::cast_to<FoundryScript>(witness->get_script());
		_erase_function_lambda_info(target_script, witness);
		functions_to_delete.insert(witness);
	}
	witness_functions.clear();
	witness_target_scripts.clear();
	namespace_conformance_scripts.clear();

	for (const KeyValue<StringName, FSFunction *> &entry : member_functions) {
		_erase_function_lambda_info(this, entry.value);
		functions_to_delete.insert(entry.value);
	}
	member_functions.clear();
	for (const KeyValue<StringName, EnumFunctionSet> &enum_entry : enum_functions) {
		for (const KeyValue<StringName, FSFunction *> &function : enum_entry.value.instance_functions) {
			_erase_function_lambda_info(this, function.value);
			functions_to_delete.insert(function.value);
		}
		for (const KeyValue<StringName, FSFunction *> &function : enum_entry.value.static_functions) {
			_erase_function_lambda_info(this, function.value);
			functions_to_delete.insert(function.value);
		}
	}
	enum_functions.clear();

	if (implicit_initializer != nullptr) {
		_erase_function_lambda_info(this, implicit_initializer);
		functions_to_delete.insert(implicit_initializer);
		implicit_initializer = nullptr;
	}
	if (implicit_ready != nullptr) {
		_erase_function_lambda_info(this, implicit_ready);
		functions_to_delete.insert(implicit_ready);
		implicit_ready = nullptr;
	}
	if (static_initializer != nullptr) {
		_erase_function_lambda_info(this, static_initializer);
		functions_to_delete.insert(static_initializer);
		static_initializer = nullptr;
	}
	initializer = nullptr;

	for (KeyValue<StringName, MemberInfo> &entry : member_indices) {
		entry.value.data_type.script_type_ref = Ref<Script>();
		entry.value.type_argument_binding.fixed.script_type_ref = Ref<Script>();
	}
	for (KeyValue<FoundryScript *, Vector<TypeArgumentBinding>> &entry : type_parameter_bindings_by_ancestor) {
		for (TypeArgumentBinding &binding : entry.value) {
			binding.fixed.script_type_ref = Ref<Script>();
		}
	}

	member_indices.clear();
	members.clear();
	member_type_argument_bindings.clear();
	static_variables.clear();
	static_variables_indices.clear();
	constants.clear();
	_signals.clear();
	script_trait_list.clear();
	abstract_trait_requirements.clear();
	type_parameters.clear();
	type_parameter_bindings_by_ancestor.clear();
	rpc_config.clear();
	class_annotations.clear();
	method_annotations.clear();
	variable_annotations.clear();
	signal_annotations.clear();
	constant_annotations.clear();
	method_parameter_annotations.clear();
	signal_parameter_annotations.clear();
	lambda_info.clear();

	for (FSFunction *function : functions_to_delete) {
		memdelete(function);
	}
}

Error FoundryScript::_reload_from_compiled_binary() {
	String binary_path = path;
	if (binary_path.is_empty()) {
		binary_path = get_path();
	}
	ERR_FAIL_COND_V_MSG(binary_path.is_empty(), ERR_FILE_NOT_FOUND,
			"Compiled Foundry Script binary has no path to re-link from.");

	const String remapped_path = ResourceLoader::path_remap(binary_path);
	Vector<uint8_t> buffer = FSCache::get_binary_tokens(remapped_path);
	if (buffer.is_empty()) {
		return ERR_FILE_CANT_READ;
	}

	// A prior link attempt that failed partway leaves body/witness state on this script and its inner
	// classes. Tear that residue down so the retry reads a clean shell, mirroring what the text reload
	// path does before recompiling.
	_clear_partial_bytecode_link_state();

	FSBytecodeCacheResolver resolver(binary_path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	const Error link_error = loader.load_full(buffer, Ref<FoundryScript>(this));
	if (link_error != OK) {
		return link_error;
	}
	// Publishing scripts with retained static data is the loader caller's job, mirroring what the
	// compiler does on the text path.
	if (loader.get_has_static_data() && !loader.get_annotated_static_unload()) {
		FSCache::add_static_script(Ref<FoundryScript>(this));
	}
	return OK;
}

ScriptLanguage *FoundryScript::get_language() const {
	return FSLanguage::get_singleton();
}

void FoundryScript::get_constants(HashMap<StringName, Variant> *p_constants) {
	if (p_constants) {
		for (const KeyValue<StringName, Variant> &E : constants) {
			(*p_constants)[E.key] = E.value;
		}
	}
}

void FoundryScript::get_members(HashSet<StringName> *p_members) {
	if (p_members) {
		for (const StringName &E : members) {
			p_members->insert(E);
		}
	}
}

const Variant FoundryScript::get_rpc_config() const {
	return rpc_config;
}

void FoundryScript::unload_static() const {
	FSCache::remove_script(fully_qualified_name);
}

Variant FoundryScript::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	// The call began on this script handle, even when lookup below walks a base script to find the
	// implementation.
	return call_static_with_context(p_method, p_args, p_argcount, r_error,
			FSStaticSelfContext::for_script(Ref<Script>(this)));
}

Variant FoundryScript::call_static_with_context(const StringName &p_method, const Variant **p_args, int p_argcount,
		Callable::CallError &r_error, const FSStaticSelfContext &p_static_self) {
	FoundryScript *top = this;
	while (top) {
		if (likely(top->valid)) {
			HashMap<StringName, FSFunction *>::Iterator E = top->member_functions.find(p_method);
			if (E) {
				ERR_FAIL_COND_V_MSG(!E->value->is_static(), Variant(), "Can't call non-static function '" + String(p_method) + "' in script.");

				return E->value->call(nullptr, p_args, p_argcount, r_error, nullptr, nullptr, &p_static_self);
			}
		}
		top = top->base.ptr();
	}

	//none found, regular

	const Variant result = Script::callp(p_method, p_args, p_argcount, r_error);
	if (r_error.error != Callable::CallError::CALL_ERROR_INVALID_METHOD) {
		return result;
	}

	// Retroactive-conformance fallback, last (which also keeps it off the hot path). A `static` witness
	// supplied by an external `extend Target uses Trait: ...` is not in any class's `member_functions`
	// because the declaring file does not own this class, so nothing above can find it. It is dispatched
	// with no instance; instance witnesses are reached through `FSInstance::callp` and skipped here.
	//
	// Last, and not before `Script::callp`, so that a witness never shadows a real method of the script
	// object — the analyzer resolves in this same order, and the two must agree on which function a call
	// means.
	//
	// The lookup is by target script rather than by the registry's string aliases: every class in a file,
	// inner classes included, registers the file's path, and a root class without `class_name` has that
	// same path as its FQCN, so an alias hit is neither unique to a class nor complete for it.
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	for (const FoundryScript *cursor = this; cursor != nullptr; cursor = cursor->base.ptr()) {
		FSFunction *witness = registry->find_witness_function_for_target(cursor, p_method);
		if (witness != nullptr && witness->is_static()) {
			r_error.error = Callable::CallError::CALL_OK;
			return witness->call(nullptr, p_args, p_argcount, r_error, nullptr, nullptr, &p_static_self);
		}
	}

	return result;
}

bool FoundryScript::resolves_to_static_function(const StringName &p_name) const {
	for (const FoundryScript *top = this; top != nullptr; top = top->base.ptr()) {
		if (top->constants.has(p_name) || top->static_variables_indices.has(p_name)) {
			return false;
		}
		if (likely(top->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator function_element = top->member_functions.find(p_name);
			if (function_element && function_element->value->is_static()) {
				return true;
			}
		}
		if (top->subclasses.has(p_name)) {
			return false;
		}
	}
	return false;
}

bool FoundryScript::_get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == FSLanguage::get_singleton()->strings._script_source) {
		r_ret = get_source_code();
		return true;
	}

	const FoundryScript *top = this;
	while (top) {
		{
			HashMap<StringName, Variant>::ConstIterator E = top->constants.find(p_name);
			if (E) {
				r_ret = E->value;
				return true;
			}
		}

		{
			HashMap<StringName, MemberInfo>::ConstIterator E = top->static_variables_indices.find(p_name);
			if (E) {
				if (likely(top->valid) && E->value.getter) {
					Callable::CallError ce;
					const Variant ret = const_cast<FoundryScript *>(this)->callp(E->value.getter, nullptr, 0, ce);
					r_ret = (ce.error == Callable::CallError::CALL_OK) ? ret : Variant();
					return true;
				}
				r_ret = top->static_variables[E->value.index];
				return true;
			}
		}

		if (likely(top->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = top->member_functions.find(p_name);
			if (E && E->value->is_static()) {
				// An extracted static callable is the pair of the selected function and the exact receiver
				// it was extracted from, so it binds the class the read began on rather than the ancestor
				// the implementation happens to be declared on. Dispatching it later then resolves `Self`
				// to the same class a direct call through this handle would have.
				FoundryScript *receiver = const_cast<FoundryScript *>(this);
				if (top->rpc_config.has(p_name)) {
					r_ret = Callable(memnew(FSRPCCallable(receiver, E->key)));
				} else {
					r_ret = Callable(receiver, E->key);
				}
				return true;
			}
		}

		{
			HashMap<StringName, Ref<FoundryScript>>::ConstIterator E = top->subclasses.find(p_name);
			if (E) {
				r_ret = E->value;
				return true;
			}
		}

		top = top->base.ptr();
	}

	return false;
}

bool FoundryScript::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == FSLanguage::get_singleton()->strings._script_source) {
		set_source_code(p_value);
		reload(true);
		return true;
	}

	FoundryScript *top = this;
	while (top) {
		HashMap<StringName, MemberInfo>::ConstIterator E = top->static_variables_indices.find(p_name);
		if (E) {
			const MemberInfo *member = &E->value;
			Variant value = p_value;
			// A static write through the bare class has no instance whose reified `type_arguments` an
			// OPEN binding declared directly on `this` could resolve against. When the member is
			// inherited from a generic ancestor (`top != this`), project its binding through `this`'s
			// specialization chain instead of treating it as unresolvable.
			if (!_validate_static_member_write(this, top, member->type_argument_binding, Vector<ContainerType>(), value)) {
				return false;
			}
			_erase_specialized_class_handle_for_native_data_type(member->data_type, value);
			if (!member->data_type.is_type(value)) {
				const Variant *args = &p_value;
				Callable::CallError err;
				Variant::construct(member->data_type.builtin_type, value, &args, 1, err);
				if (err.error != Callable::CallError::CALL_OK || !member->data_type.is_type(value)) {
					return false;
				}
			}
			if (likely(top->valid) && member->setter) {
				const Variant *args = &value;
				Callable::CallError err;
				callp(member->setter, &args, 1, err);
				return err.error == Callable::CallError::CALL_OK;
			} else {
				top->static_variables.write[member->index] = value;
				return true;
			}
		}

		top = top->base.ptr();
	}

	return false;
}

void FoundryScript::_get_property_list(List<PropertyInfo> *p_properties) const {
	p_properties->push_back(PropertyInfo(Variant::STRING, "script/source", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));

	List<const FoundryScript *> classes;
	const FoundryScript *top = this;
	while (top) {
		classes.push_back(top);
		top = top->base.ptr();
	}

	for (const List<const FoundryScript *>::Element *E = classes.back(); E; E = E->prev()) {
		Vector<_FSMemberSort> msort;
		for (const KeyValue<StringName, MemberInfo> &F : E->get()->static_variables_indices) {
			_FSMemberSort ms;
			ms.index = F.value.index;
			ms.name = F.key;
			msort.push_back(ms);
		}
		msort.sort();

		for (int i = 0; i < msort.size(); i++) {
			p_properties->push_back(E->get()->static_variables_indices[msort[i].name].property_info);
		}
	}
}

void FoundryScript::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &FoundryScript::_new, MethodInfo("new"));
	ClassDB::bind_method(D_METHOD("is_generic"), &FoundryScript::is_generic);
	ClassDB::bind_method(D_METHOD("get_type_parameter_list"), &FoundryScript::_get_type_parameter_list);
}

void FoundryScript::set_path_cache(const String &p_path) {
	if (is_root_script()) {
		Script::set_path_cache(p_path);
	}

	path = p_path;
	path_valid = true;

	for (KeyValue<StringName, Ref<FoundryScript>> &kv : subclasses) {
		kv.value->set_path_cache(p_path);
	}
}

void FoundryScript::set_path(const String &p_path, bool p_take_over) {
	if (is_root_script()) {
		Script::set_path(p_path, p_take_over);
	}

	String old_path = path;
	path = p_path;
	path_valid = true;
	FSCache::move_script(old_path, p_path);

	for (KeyValue<StringName, Ref<FoundryScript>> &kv : subclasses) {
		kv.value->set_path(p_path, p_take_over);
	}
}

String FoundryScript::get_script_path() const {
	if (!path_valid && !get_path().is_empty()) {
		return get_path();
	}
	return path;
}

Error FoundryScript::load_source_code(const String &p_path) {
	if (p_path.is_empty() || p_path.begins_with("foundryscript://") || ResourceLoader::get_resource_type(p_path.get_slice("::", 0)) == "PackedScene") {
		return OK;
	}

	Vector<uint8_t> sourcef;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (err) {
		const char *err_name;
		if (err < 0 || err >= ERR_MAX) {
			err_name = "(invalid error code)";
		} else {
			err_name = error_names[err];
		}
		ERR_FAIL_COND_V_MSG(err, err, "Attempt to open script '" + p_path + "' resulted in error '" + err_name + "'.");
	}

	uint64_t len = f->get_length();
	sourcef.resize(len + 1);
	uint8_t *w = sourcef.ptrw();
	uint64_t r = f->get_buffer(w, len);
	ERR_FAIL_COND_V(r != len, ERR_CANT_OPEN);
	w[len] = 0;

	String s;
	if (s.append_utf8((const char *)w, len) != OK) {
		ERR_FAIL_V_MSG(ERR_INVALID_DATA, "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}

	source = s;
	path = p_path;
	path_valid = true;
#ifdef TOOLS_ENABLED
	source_changed_cache = true;
	set_edited(false);
	set_last_modified_time(FileAccess::get_modified_time(path));
#endif // TOOLS_ENABLED
	return OK;
}

void FoundryScript::set_binary_tokens_source(const Vector<uint8_t> &p_binary_tokens) {
	binary_tokens = p_binary_tokens;
}

const Vector<uint8_t> &FoundryScript::get_binary_tokens_source() const {
	return binary_tokens;
}

Vector<uint8_t> FoundryScript::get_as_binary_tokens() const {
#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	ERR_PRINT("Foundry Script binary token export is unavailable in this build (foundry_script_frontend=no).");
	return Vector<uint8_t>();
#else
	FSTokenizerBuffer tokenizer;
	return tokenizer.parse_code_string(source, FSTokenizerBuffer::COMPRESS_NONE);
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

const HashMap<StringName, FSFunction *> &FoundryScript::debug_get_member_functions() const {
	return member_functions;
}

FSFunction *FoundryScript::get_enum_function(
		const StringName &p_enum_type, const StringName &p_function, bool p_static) const {
	const EnumFunctionSet *function_set = enum_functions.getptr(p_enum_type);
	if (function_set == nullptr) {
		return nullptr;
	}
	const HashMap<StringName, FSFunction *> &functions =
			p_static ? function_set->static_functions : function_set->instance_functions;
	FSFunction *const *function = functions.getptr(p_function);
	return function != nullptr ? *function : nullptr;
}

StringName FoundryScript::debug_get_member_by_index(int p_idx) const {
	for (const KeyValue<StringName, MemberInfo> &E : member_indices) {
		if (E.value.index == p_idx) {
			return E.key;
		}
	}

	return "<error>";
}

StringName FoundryScript::debug_get_static_var_by_index(int p_idx) const {
	for (const KeyValue<StringName, MemberInfo> &E : static_variables_indices) {
		if (E.value.index == p_idx) {
			return E.key;
		}
	}

	return "<error>";
}

Ref<FoundryScript> FoundryScript::get_base() const {
	return base;
}

bool FoundryScript::inherits_script(const Ref<Script> &p_script) const {
	Ref<FoundryScript> gd = p_script;
	if (gd.is_null()) {
		return false;
	}

	const FoundryScript *s = this;

	while (s) {
		if (s == p_script.ptr()) {
			return true;
		}
		s = s->base.ptr();
	}

	return false;
}

bool FoundryScript::project_type_arguments_onto_base(const Ref<Script> &p_base, const Vector<ContainerType> &p_leaf_type_arguments, Vector<ProjectedContainerType> &r_type_arguments) const {
	r_type_arguments.clear();

	const FoundryScript *base_script = Object::cast_to<FoundryScript>(p_base.ptr());
	if (base_script == nullptr) {
		return false;
	}

	// The leaf script holds a per-ancestor table mapping each ancestor's type parameter to how it
	// resolves for this leaf: FIXED to a concrete argument by an `extends Base[X]` specialization in
	// the chain, or OPEN against the leaf's own reified arguments (see
	// docs/superpowers/specs/2026-06-25-reified-type-argument-bindings-design.md). Projecting through it
	// yields the leaf's effective arguments for `p_base`'s parameters, so a subclass value validates
	// invariantly against an expected specialized base element type.
	const Vector<TypeArgumentBinding> *bindings = type_parameter_bindings_by_ancestor.getptr(const_cast<FoundryScript *>(base_script));
	if (bindings == nullptr) {
		return false;
	}

	Vector<ProjectedContainerType> projected;
	projected.resize(bindings->size());
	for (int i = 0; i < bindings->size(); i++) {
		// A temporary ContainerType is materialized here at validation time rather than persisted, so a
		// local-class argument is not held by a strong Ref in member metadata (avoiding reference cycles).
		// A `Type[...]` layer inside the binding travels with the evidence; a slot with no evidence at all
		// (an unspecialized leaf's open parameter, or a step of the chain that stayed unresolved) comes
		// back UNKNOWN and is skipped under gradual typing.
		projected.write[i] = project_type_argument_binding((*bindings)[i], p_leaf_type_arguments);
	}

	r_type_arguments = projected;
	return true;
}

FoundryScript *FoundryScript::find_class(const String &p_qualified_name) {
	String first = p_qualified_name.get_slice("::", 0);

	Vector<String> class_names;
	FoundryScript *result = nullptr;
	// Empty initial name means start here.
	if (first.is_empty() || first == global_name) {
		class_names = p_qualified_name.split("::");
		result = this;
	} else if (p_qualified_name.begins_with(get_root_script()->path)) {
		// Script path could have a class path separator("::") in it.
		class_names = p_qualified_name.trim_prefix(get_root_script()->path).split("::");
		result = get_root_script();
	} else if (HashMap<StringName, Ref<FoundryScript>>::Iterator E = subclasses.find(first)) {
		class_names = p_qualified_name.split("::");
		result = E->value.ptr();
	} else if (_owner != nullptr) {
		// Check parent scope.
		return _owner->find_class(p_qualified_name);
	}

	// Starts at index 1 because index 0 was handled above.
	for (int i = 1; result != nullptr && i < class_names.size(); i++) {
		if (HashMap<StringName, Ref<FoundryScript>>::Iterator E = result->subclasses.find(class_names[i])) {
			result = E->value.ptr();
		} else {
			// Couldn't find inner class.
			return nullptr;
		}
	}

	return result;
}

bool FoundryScript::has_class(const FoundryScript *p_script) {
	String fqn = p_script->fully_qualified_name;
	if (fully_qualified_name.is_empty() && fqn.get_slice("::", 0).is_empty()) {
		return p_script == this;
	} else if (fqn.begins_with(fully_qualified_name)) {
		return p_script == find_class(fqn.trim_prefix(fully_qualified_name));
	}
	return false;
}

FoundryScript *FoundryScript::get_root_script() {
	FoundryScript *result = this;
	while (result->_owner) {
		result = result->_owner;
	}
	return result;
}

bool FoundryScript::has_script_signal(const StringName &p_signal) const {
	if (_signals.has(p_signal)) {
		return true;
	}
	if (base.is_valid()) {
		return base->has_script_signal(p_signal);
	}
#ifdef TOOLS_ENABLED
	else if (base_cache.is_valid()) {
		return base_cache->has_script_signal(p_signal);
	}
#endif
	return false;
}

void FoundryScript::_get_script_signal_list(List<MethodInfo> *r_list, bool p_include_base) const {
	for (const KeyValue<StringName, MethodInfo> &E : _signals) {
		r_list->push_back(E.value);
	}

	if (!p_include_base) {
		return;
	}

	if (base.is_valid()) {
		base->get_script_signal_list(r_list);
	}
#ifdef TOOLS_ENABLED
	else if (base_cache.is_valid()) {
		base_cache->get_script_signal_list(r_list);
	}
#endif
}

void FoundryScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	_get_script_signal_list(r_signals, true);
}

void FoundryScript::_get_script_trait_list(List<StringName> *r_list, HashSet<StringName> &r_seen, bool p_include_base) const {
	for (const StringName &trait : script_trait_list) {
		if (r_seen.has(trait)) {
			continue;
		}
		r_seen.insert(trait);
		r_list->push_back(trait);
	}

	if (!p_include_base) {
		return;
	}

	if (base.is_valid()) {
		base->_get_script_trait_list(r_list, r_seen, true);
	}
#ifdef TOOLS_ENABLED
	else if (base_cache.is_valid()) {
		base_cache->_get_script_trait_list(r_list, r_seen, true);
	}
#endif
}

void FoundryScript::get_script_trait_list(List<StringName> *r_traits) const {
	HashSet<StringName> seen;
	_get_script_trait_list(r_traits, seen, true);
}

bool FoundryScript::_has_script_trait(const StringName &p_trait, bool p_include_runtime) const {
	// A trait conforms to its own identity. This is normally unobservable because
	// traits cannot be instantiated, but a dynamic proxy whose `get_script()` is the
	// trait itself must satisfy `proxy is ThatTrait`.
	if (_is_trait_type && trait_type_name == p_trait) {
		return true;
	}

	for (const StringName &trait : script_trait_list) {
		if (trait == p_trait) {
			return true;
		}
	}

	// A retroactive conformance (`extend This uses Trait: ...`) is recorded in the registry rather
	// than this script's own `script_trait_list`, so consult it by every identity alias the
	// registry keys a target by (FQCN / global class name / script path).
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (registry->has_conformance(get_fully_qualified_name(), p_trait, p_include_runtime)) {
		return true;
	}
	const StringName script_global_name = get_global_name();
	if (script_global_name != StringName() && registry->has_conformance(String(script_global_name), p_trait, p_include_runtime)) {
		return true;
	}
	// Only a root script is identified by its resource path: an inner class reports the same path as its
	// siblings and as the enclosing root class, so asking by path would answer with their conformances.
	if (is_root_script()) {
		const String script_path = get_script_path();
		if (!script_path.is_empty() && registry->has_conformance(script_path, p_trait, p_include_runtime)) {
			return true;
		}
	}

	if (base.is_valid()) {
		return base->_has_script_trait(p_trait, p_include_runtime);
	}
#ifdef TOOLS_ENABLED
	else if (base_cache.is_valid()) {
		return base_cache->_has_script_trait(p_trait, p_include_runtime);
	}
#endif
	return false;
}

bool FoundryScript::has_script_trait(const StringName &p_trait) const {
	return _has_script_trait(p_trait, true);
}

bool FoundryScript::has_script_trait_parse(const StringName &p_trait) const {
	return _has_script_trait(p_trait, false);
}

TypedArray<FSTypeParameter> FoundryScript::_get_type_parameter_list() const {
	TypedArray<FSTypeParameter> ret;
	for (const TypeParameter &parameter : type_parameters) {
		Ref<FSTypeParameter> entry;
		entry.instantiate();
		entry->name = parameter.name;
		entry->index = parameter.index;
		// Class-declared parameters are the only kind reachable from a compiled script; method type
		// parameters live on functions and are not reflected here.
		entry->scope = StringName("class");
		entry->_has_bound = parameter.has_bound;
		if (parameter.has_bound) {
			entry->bound = parameter.bound.operator Dictionary();
		}
		ret.push_back(entry);
	}
	return ret;
}

FoundryScript::FoundryScript() :
		script_list(this) {
	{
		MutexLock lock(FSLanguage::get_singleton()->mutex);

		FSLanguage::get_singleton()->script_list.add(&script_list);
	}

	// The scheme must be alphanumeric: String::simplify_path() only preserves the
	// "://" protocol separator when every character before it is alphanumeric, so an
	// underscore (foundry_script://) would be collapsed to a single slash and break
	// the in-memory script check in load_source_code().
	path = vformat("foundryscript://%d.fs", get_instance_id());
}

void FoundryScript::_save_orphaned_subclasses() {
	struct ClassRefWithName {
		ObjectID id;
		String fully_qualified_name;
	};
	Vector<ClassRefWithName> weak_subclasses;
	// collect subclasses ObjectID and name
	for (KeyValue<StringName, Ref<FoundryScript>> &E : subclasses) {
		E.value->_owner = nullptr; //bye, you are no longer owned cause I died
		ClassRefWithName subclass;
		subclass.id = E.value->get_instance_id();
		subclass.fully_qualified_name = E.value->fully_qualified_name;
		weak_subclasses.push_back(subclass);
	}

	// clear subclasses to allow unused subclasses to be deleted
	subclasses.clear();
	// subclasses are also held by constants, clear those as well
	constants.clear();

	// keep orphan subclass only for subclasses that are still in use
	for (int i = 0; i < weak_subclasses.size(); i++) {
		ClassRefWithName subclass = weak_subclasses[i];
		Object *obj = ObjectDB::get_instance(subclass.id);
		if (!obj) {
			continue;
		}
		// subclass is not released
		FSLanguage::get_singleton()->add_orphan_subclass(subclass.fully_qualified_name, subclass.id);
	}
}

String FoundryScript::debug_get_script_name(const Ref<Script> &p_script) {
	if (p_script.is_valid()) {
		Ref<FoundryScript> foundry_script = p_script;
		if (foundry_script.is_valid()) {
			if (foundry_script->get_local_name() != StringName()) {
				return foundry_script->get_local_name();
			}
			return foundry_script->get_fully_qualified_name().get_file();
		}

		if (p_script->get_global_name() != StringName()) {
			return p_script->get_global_name();
		} else if (!p_script->get_path().is_empty()) {
			return p_script->get_path().get_file();
		} else if (!p_script->get_name().is_empty()) {
			return p_script->get_name(); // Resource name.
		}
	}

	return "<unknown script>";
}

String FoundryScript::canonicalize_path(const String &p_path) {
	const String extension = p_path.get_extension();
	if (extension == "fsc" || extension == "fsb") {
		return p_path.get_basename() + ".fs";
	}
	return p_path;
}

FoundryScript::UpdatableFuncPtr::UpdatableFuncPtr(FSFunction *p_function) {
	if (p_function == nullptr) {
		return;
	}

	ptr = p_function;
	script = ptr->get_script();
	ERR_FAIL_NULL(script);

	MutexLock script_lock(script->func_ptrs_to_update_mutex);
	list_element = script->func_ptrs_to_update.push_back(this);
}

FoundryScript::UpdatableFuncPtr::~UpdatableFuncPtr() {
	ERR_FAIL_NULL(script);

	if (list_element) {
		MutexLock script_lock(script->func_ptrs_to_update_mutex);
		list_element->erase();
		list_element = nullptr;
	}
}

void FoundryScript::_recurse_replace_function_ptrs(const HashMap<FSFunction *, FSFunction *> &p_replacements) const {
	MutexLock lock(func_ptrs_to_update_mutex);
	for (UpdatableFuncPtr *updatable : func_ptrs_to_update) {
		HashMap<FSFunction *, FSFunction *>::ConstIterator replacement = p_replacements.find(updatable->ptr);
		if (replacement) {
			updatable->ptr = replacement->value;
		} else {
			// Probably a lambda from another reload, ignore.
			updatable->ptr = nullptr;
		}
	}

	for (HashMap<StringName, Ref<FoundryScript>>::ConstIterator subscript = subclasses.begin(); subscript; ++subscript) {
		subscript->value->_recurse_replace_function_ptrs(p_replacements);
	}
}

void FoundryScript::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	// Every compiled function is deleted below, so this script can no longer run. Mark it invalid
	// so `can_instantiate()`/`instance_create()`/`_new()` fail loudly instead of producing
	// half-constructed instances. This matters for scripts that outlive language shutdown through
	// a stale Ref or ResourceCache entry and get handed out again by a later cache-hit load.
	valid = false;

	RBSet<FSFunction *> functions_to_clear;

	{
		MutexLock lock(func_ptrs_to_update_mutex);
		for (UpdatableFuncPtr *updatable : func_ptrs_to_update) {
			updatable->ptr = nullptr;
		}
	}

	for (const KeyValue<StringName, FSFunction *> &E : member_functions) {
		functions_to_clear.insert(E.value);
	}
	member_functions.clear();
	for (const KeyValue<StringName, EnumFunctionSet> &enum_entry : enum_functions) {
		for (const KeyValue<StringName, FSFunction *> &function : enum_entry.value.instance_functions) {
			functions_to_clear.insert(function.value);
		}
		for (const KeyValue<StringName, FSFunction *> &function : enum_entry.value.static_functions) {
			functions_to_clear.insert(function.value);
		}
	}
	enum_functions.clear();

	// Drop borrowed pointers from the conformance registry before freeing the compiled witnesses, so a
	// concurrent or subsequent runtime dispatch never sees a dangling `FSFunction *`.
	if (!registered_conformance_source.is_empty()) {
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(registered_conformance_source);
		registered_conformance_source = String();
	}
	for (FSFunction *witness : witness_functions) {
		functions_to_clear.insert(witness);
	}
	witness_functions.clear();
	witness_target_scripts.clear();
	namespace_conformance_scripts.clear();

	for (KeyValue<StringName, MemberInfo> &E : member_indices) {
		E.value.data_type.script_type_ref = Ref<Script>();
		// A FIXED type-argument binding can hold a Ref<Script> to an external specialization argument;
		// release it to break cross-script reference cycles, mirroring the data_type handling above.
		E.value.type_argument_binding.fixed.script_type_ref = Ref<Script>();
	}
	for (KeyValue<FoundryScript *, Vector<TypeArgumentBinding>> &E : type_parameter_bindings_by_ancestor) {
		for (TypeArgumentBinding &binding : E.value) {
			binding.fixed.script_type_ref = Ref<Script>();
		}
	}

	member_indices.clear();
	member_type_argument_bindings.clear();
	type_parameter_bindings_by_ancestor.clear();
	static_variables.clear();
	static_variables_indices.clear();
	script_trait_list.clear();
	abstract_trait_requirements.clear();
	type_parameters.clear();
	class_annotations.clear();
	method_annotations.clear();
	variable_annotations.clear();
	signal_annotations.clear();
	constant_annotations.clear();
	method_parameter_annotations.clear();
	signal_parameter_annotations.clear();
	_is_trait_type = false;
	trait_type_name = StringName();

	if (implicit_initializer) {
		functions_to_clear.insert(implicit_initializer);
		implicit_initializer = nullptr;
	}

	if (implicit_ready) {
		functions_to_clear.insert(implicit_ready);
		implicit_ready = nullptr;
	}

	if (static_initializer) {
		functions_to_clear.insert(static_initializer);
		static_initializer = nullptr;
	}

	_save_orphaned_subclasses();

#ifdef TOOLS_ENABLED
	// Clearing inner class doc, script doc only cleared when the script source deleted.
	if (_owner) {
		_clear_doc();
	}
#endif

	// All dependencies have been accounted for
	for (FSFunction *E : functions_to_clear) {
		memdelete(E);
	}
	functions_to_clear.clear();
}

void FoundryScript::cancel_pending_functions(bool warn) {
	MutexLock lock(FSLanguage::get_singleton()->mutex);

	while (SelfList<FSFunctionState> *E = pending_func_states.first()) {
		// Order matters since clearing the stack may already cause
		// the FSFunctionState to be destroyed and thus removed from the list.
		pending_func_states.remove(E);
		FSFunctionState *state = E->self();
#ifdef DEBUG_ENABLED
		if (warn) {
			WARN_PRINT("Canceling suspended execution of \"" + state->get_readable_function() + "\" due to a script reload.");
		}
#endif
		ObjectID state_id = state->get_instance_id();
		state->_clear_connections();
		if (ObjectDB::get_instance(state_id)) {
			state->_clear_stack();
		}
	}
}

FoundryScript::~FoundryScript() {
	if (destructing) {
		return;
	}
	destructing = true;

	if (is_print_verbose_enabled()) {
		MutexLock lock(func_ptrs_to_update_mutex);
		if (!func_ptrs_to_update.is_empty()) {
			print_line(vformat("FoundryScript: %d orphaned lambdas becoming invalid at destruction of script '%s'.", func_ptrs_to_update.size(), fully_qualified_name));
		}
	}

	clear();

	cancel_pending_functions(false);

	{
		MutexLock lock(FSLanguage::get_singleton()->mutex);

		script_list.remove_from_list();
	}
}

//////////////////////////////
//         INSTANCE         //
//////////////////////////////

// Hidden storage property that persists an instance's reified generic type arguments (e.g. the
// `int` in `Box[int].new()`) across `.tres`/scene save-load and resource duplication. Member
// metadata and `is`/`as` resolution read these arguments dynamically, so restoring the vector is
// sufficient to make a reloaded instance behave like a freshly constructed one.
static const StringName &_foundry_script_type_arguments_property_name() {
	static const StringName name = StringName("__foundry_script_type_arguments__");
	return name;
}

// Serialize the reified arguments to an Array of container-type descriptors. The descriptor format
// is the engine-wide `ContainerTypeDescriptor` one (also used by the `godot.reflection` surface),
// so script-typed arguments persist as resource references that `.tres`/scene files round-trip.
static Array _serialize_type_arguments(const Vector<ContainerType> &p_type_arguments) {
	Array result;
	for (const ContainerType &type : p_type_arguments) {
		result.push_back(ContainerTypeDescriptor::to_variant(type));
	}
	return result;
}

static Vector<ContainerType> _deserialize_type_arguments(const Array &p_array) {
	Vector<ContainerType> result;
	for (int i = 0; i < p_array.size(); i++) {
		ContainerType type;
		String error;
		if (ContainerTypeDescriptor::from_variant(p_array[i], type, &error)) {
			result.push_back(type);
		} else {
			ERR_PRINT(vformat("Failed to restore reified generic type argument: %s", error));
		}
	}
	return result;
}

bool FSInstance::set(const StringName &p_name, const Variant &p_value) {
	// Handle the hidden storage property only when it does not shadow a real member, so a user
	// variable that happens to share the reserved name keeps its normal behavior.
	if (p_name == _foundry_script_type_arguments_property_name() && !script->member_indices.has(p_name)) {
		if (p_value.get_type() == Variant::ARRAY) {
			const Vector<ContainerType> restored = _deserialize_type_arguments(p_value);
			// Reject a payload whose arity does not match the class's type parameters (e.g. a
			// hand-edited or stale `.tres`); binding a mismatched vector would silently misalign the
			// per-member `leaf_ordinal` lookups. An empty vector is always valid (unspecialized).
			const int arity = script->get_type_parameters().size();
			if (!restored.is_empty() && restored.size() != arity) {
				ERR_PRINT(vformat("Ignoring serialized generic type arguments: expected %d argument(s) but got %d.", arity, restored.size()));
			} else {
				type_arguments = restored;
			}
		}
		return true;
	}
	{
		HashMap<StringName, FoundryScript::MemberInfo>::Iterator E = script->member_indices.find(p_name);
		if (E) {
			const FoundryScript::MemberInfo *member = &E->value;
			Variant value = p_value;
			if (member->type_argument_binding.kind != FoundryScript::TypeArgumentBinding::NONE) {
				// The member is typed as a class generic parameter, erased to a Variant slot. Validate the
				// write against the argument the binding resolves to: a concrete type fixed by an
				// `extends Base[int]` specialization in the chain (FIXED), or the argument reified onto this
				// instance (OPEN). An OPEN member on an instance created without explicit arguments carries
				// no binding, leaving the slot effectively untyped.
				if (!FoundryScript::validate_type_argument_binding_write(member->type_argument_binding, type_arguments, value)) {
					return false;
				}
			} else {
				_erase_specialized_class_handle_for_native_data_type(member->data_type, value);
				if (!member->data_type.is_type(value)) {
					const Variant *args = &p_value;
					Callable::CallError err;
					Variant::construct(member->data_type.builtin_type, value, &args, 1, err);
					if (err.error != Callable::CallError::CALL_OK || !member->data_type.is_type(value)) {
						return false;
					}
				}
			}
			if (likely(script->valid) && member->setter) {
				const Variant *args = &value;
				Callable::CallError err;
				callp(member->setter, &args, 1, err);
				return err.error == Callable::CallError::CALL_OK;
			} else {
				members.write[member->index] = value;
				return true;
			}
		}
	}

	FoundryScript *sptr = script.ptr();
	while (sptr) {
		{
			HashMap<StringName, FoundryScript::MemberInfo>::ConstIterator E = sptr->static_variables_indices.find(p_name);
			if (E) {
				const FoundryScript::MemberInfo *member = &E->value;
				Variant value = p_value;
				// A static member has no per-instance reification of its own. When the leaf script
				// declares the member directly (`sptr == script`), an OPEN binding resolves against the
				// instance's own `type_arguments` (e.g. a `Box[int].new()` instance's reified argument
				// applies to `Box`'s static members too). When it is inherited from a generic ancestor
				// (`sptr != script`), project the binding through the leaf's specialization chain instead.
				if (!FoundryScript::_validate_static_member_write(script.ptr(), sptr, member->type_argument_binding, type_arguments, value)) {
					return false;
				}
				_erase_specialized_class_handle_for_native_data_type(member->data_type, value);
				if (!member->data_type.is_type(value)) {
					const Variant *args = &p_value;
					Callable::CallError err;
					Variant::construct(member->data_type.builtin_type, value, &args, 1, err);
					if (err.error != Callable::CallError::CALL_OK || !member->data_type.is_type(value)) {
						return false;
					}
				}
				if (likely(sptr->valid) && member->setter) {
					const Variant *args = &value;
					Callable::CallError err;
					callp(member->setter, &args, 1, err);
					return err.error == Callable::CallError::CALL_OK;
				} else {
					sptr->static_variables.write[member->index] = value;
					return true;
				}
			}
		}

		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::Iterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._set);
			if (E) {
				Variant name = p_name;
				const Variant *args[2] = { &name, &p_value };

				Callable::CallError err;
				Variant ret = E->value->call(this, (const Variant **)args, 2, err);
				if (err.error == Callable::CallError::CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
					return true;
				}
			}
		}

		sptr = sptr->base.ptr();
	}

	return false;
}

bool FSInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == _foundry_script_type_arguments_property_name() && !script->member_indices.has(p_name)) {
		r_ret = _serialize_type_arguments(type_arguments);
		return true;
	}
	{
		HashMap<StringName, FoundryScript::MemberInfo>::ConstIterator E = script->member_indices.find(p_name);
		if (E) {
			if (likely(script->valid) && E->value.getter) {
				Callable::CallError err;
				const Variant ret = const_cast<FSInstance *>(this)->callp(E->value.getter, nullptr, 0, err);
				r_ret = (err.error == Callable::CallError::CALL_OK) ? ret : Variant();
				return true;
			}
			r_ret = members[E->value.index];
			return true;
		}
	}

	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		{
			HashMap<StringName, Variant>::ConstIterator E = sptr->constants.find(p_name);
			if (E) {
				r_ret = E->value;
				return true;
			}
		}

		{
			HashMap<StringName, FoundryScript::MemberInfo>::ConstIterator E = sptr->static_variables_indices.find(p_name);
			if (E) {
				if (likely(sptr->valid) && E->value.getter) {
					Callable::CallError ce;
					const Variant ret = const_cast<FoundryScript *>(sptr)->callp(E->value.getter, nullptr, 0, ce);
					r_ret = (ce.error == Callable::CallError::CALL_OK) ? ret : Variant();
					return true;
				}
				r_ret = sptr->static_variables[E->value.index];
				return true;
			}
		}

		{
			HashMap<StringName, MethodInfo>::ConstIterator E = sptr->_signals.find(p_name);
			if (E) {
				r_ret = Signal(owner, E->key);
				return true;
			}
		}

		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(p_name);
			if (E) {
				if (sptr->rpc_config.has(p_name)) {
					r_ret = Callable(memnew(FSRPCCallable(owner, E->key)));
				} else {
					r_ret = Callable(owner, E->key);
				}
				return true;
			}
		}

		{
			HashMap<StringName, Ref<FoundryScript>>::ConstIterator E = sptr->subclasses.find(p_name);
			if (E) {
				r_ret = E->value;
				return true;
			}
		}

		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._get);
			if (E) {
				Variant name = p_name;
				const Variant *args[1] = { &name };

				Callable::CallError err;
				Variant ret = E->value->call(const_cast<FSInstance *>(this), (const Variant **)args, 1, err);
				if (err.error == Callable::CallError::CALL_OK && ret.get_type() != Variant::NIL) {
					r_ret = ret;
					return true;
				}
			}
		}
		sptr = sptr->base.ptr();
	}

	return false;
}

Variant::Type FSInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	if (script->member_indices.has(p_name)) {
		if (r_is_valid) {
			*r_is_valid = true;
		}
		return script->member_indices[p_name].property_info.type;
	}

	if (r_is_valid) {
		*r_is_valid = false;
	}
	return Variant::NIL;
}

void FSInstance::validate_property(PropertyInfo &p_property) const {
	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._validate_property);
			if (E) {
				Variant property = (Dictionary)p_property;
				const Variant *args[1] = { &property };

				Callable::CallError err;
				Variant ret = E->value->call(const_cast<FSInstance *>(this), args, 1, err);
				if (err.error == Callable::CallError::CALL_OK) {
					p_property = PropertyInfo::from_dict(property);
					return;
				}
			}
		}
		sptr = sptr->base.ptr();
	}
}

void FSInstance::get_property_list(List<PropertyInfo> *p_properties) const {
	// exported members, not done yet!

	// Persist reified generic type arguments so a specialized instance (`Box[int].new()`) round-trips
	// through `.tres`/scene save-load and duplication. Only emitted when present, so non-generic or
	// unspecialized instances serialize unchanged.
	if (!type_arguments.is_empty() && !script->member_indices.has(_foundry_script_type_arguments_property_name())) {
		p_properties->push_back(PropertyInfo(Variant::ARRAY, _foundry_script_type_arguments_property_name(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_NO_EDITOR));
	}

	const FoundryScript *sptr = script.ptr();
	List<PropertyInfo> props;

	while (sptr) {
		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._get_property_list);
			if (E) {
				Callable::CallError err;
				Variant ret = E->value->call(const_cast<FSInstance *>(this), nullptr, 0, err);
				if (err.error == Callable::CallError::CALL_OK) {
					ERR_FAIL_COND_MSG(ret.get_type() != Variant::ARRAY, "Wrong type for _get_property_list, must be an array of dictionaries.");

					Array arr = ret;
					for (int i = 0; i < arr.size(); i++) {
						Dictionary d = arr[i];
						ERR_CONTINUE(!d.has("name"));
						ERR_CONTINUE(!d.has("type"));

						PropertyInfo pinfo;
						pinfo.name = d["name"];
						pinfo.type = Variant::Type(d["type"].operator int());
						if (d.has("hint")) {
							pinfo.hint = PropertyHint(d["hint"].operator int());
						}
						if (d.has("hint_string")) {
							pinfo.hint_string = d["hint_string"];
						}
						if (d.has("usage")) {
							pinfo.usage = d["usage"];
						}
						if (d.has("class_name")) {
							pinfo.class_name = d["class_name"];
						}

						ERR_CONTINUE(pinfo.name.is_empty() && (pinfo.usage & PROPERTY_USAGE_STORAGE));
						ERR_CONTINUE(pinfo.type < 0 || pinfo.type >= Variant::VARIANT_MAX);

						props.push_back(pinfo);
					}
				}
			}
		}

		//instance a fake script for editing the values

		Vector<_FSMemberSort> msort;
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &F : sptr->member_indices) {
			if (!sptr->members.has(F.key)) {
				continue; // Skip base class members.
			}
			_FSMemberSort ms;
			ms.index = F.value.index;
			ms.name = F.key;
			msort.push_back(ms);
		}

		msort.sort();
		msort.reverse();
		for (int i = 0; i < msort.size(); i++) {
			props.push_front(sptr->member_indices[msort[i].name].property_info);
		}

#ifdef TOOLS_ENABLED
		p_properties->push_back(sptr->get_class_category());
#endif // TOOLS_ENABLED

		for (PropertyInfo &prop : props) {
			validate_property(prop);
			p_properties->push_back(prop);
		}

		props.clear();

		sptr = sptr->base.ptr();
	}
}

bool FSInstance::property_can_revert(const StringName &p_name) const {
	Variant name = p_name;
	const Variant *args[1] = { &name };

	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._property_can_revert);
			if (E) {
				Callable::CallError err;
				Variant ret = E->value->call(const_cast<FSInstance *>(this), args, 1, err);
				if (err.error == Callable::CallError::CALL_OK && ret.get_type() == Variant::BOOL && ret.operator bool()) {
					return true;
				}
			}
		}
		sptr = sptr->base.ptr();
	}

	return false;
}

bool FSInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	Variant name = p_name;
	const Variant *args[1] = { &name };

	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(FSLanguage::get_singleton()->strings._property_get_revert);
			if (E) {
				Callable::CallError err;
				Variant ret = E->value->call(const_cast<FSInstance *>(this), args, 1, err);
				if (err.error == Callable::CallError::CALL_OK && ret.get_type() != Variant::NIL) {
					r_ret = ret;
					return true;
				}
			}
		}
		sptr = sptr->base.ptr();
	}

	return false;
}

void FSInstance::get_method_list(List<MethodInfo> *p_list) const {
	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		for (const KeyValue<StringName, FSFunction *> &E : sptr->member_functions) {
			p_list->push_back(E.value->get_method_info());
		}
		sptr = sptr->base.ptr();
	}
}

bool FSInstance::has_method(const StringName &p_method) const {
	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(p_method);
		if (E) {
			return true;
		}
		sptr = sptr->base.ptr();
	}

	return false;
}

int FSInstance::get_method_argument_count(const StringName &p_method, bool *r_is_valid) const {
	const FoundryScript *sptr = script.ptr();
	while (sptr) {
		HashMap<StringName, FSFunction *>::ConstIterator E = sptr->member_functions.find(p_method);
		if (E) {
			if (r_is_valid) {
				*r_is_valid = true;
			}
			return E->value->get_argument_count();
		}
		sptr = sptr->base.ptr();
	}

	if (r_is_valid) {
		*r_is_valid = false;
	}
	return 0;
}

void FSInstance::_call_implicit_ready_recursively(FoundryScript *p_script) {
	// Call base class first.
	if (p_script->base.ptr()) {
		_call_implicit_ready_recursively(p_script->base.ptr());
	}
	if (likely(p_script->valid) && p_script->implicit_ready) {
		Callable::CallError err;
		p_script->implicit_ready->call(this, nullptr, 0, err);
	}
}

Variant FSInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	FoundryScript *sptr = script.ptr();
	if (unlikely(p_method == SceneStringName(_ready))) {
		// Call implicit ready first, including for the super classes recursively.
		_call_implicit_ready_recursively(sptr);
	}
	while (sptr) {
		if (likely(sptr->valid)) {
			HashMap<StringName, FSFunction *>::Iterator E = sptr->member_functions.find(p_method);
			if (E) {
				return E->value->call(this, p_args, p_argcount, r_error);
			}
		}
		sptr = sptr->base.ptr();
	}

	// Retroactive-conformance fallback (only on a member-function miss, to keep the hot path fast). A
	// witness supplied by an external `extend Target uses Trait: ...` is not in any class's
	// `member_functions` because the declaring file does not own this class. Consult the conformance
	// registry by the instance's script identity aliases (FQCN / global class name / script path),
	// walking the base chain, and dispatch the compiled witness with `this` as `self`.
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	for (FoundryScript *cursor = script.ptr(); cursor != nullptr; cursor = cursor->base.ptr()) {
		FSFunction *witness = registry->find_witness_function(cursor->get_fully_qualified_name(), p_method);
		if (witness == nullptr) {
			const StringName global_name = cursor->get_global_name();
			if (global_name != StringName()) {
				witness = registry->find_witness_function(String(global_name), p_method);
			}
		}
		if (witness == nullptr && cursor->is_root_script()) {
			// See `_has_script_trait`: the resource path identifies a root script only, so an inner class
			// must not be answered with a witness declared for a class that merely shares its file.
			witness = registry->find_witness_function(cursor->get_script_path(), p_method);
		}
		if (witness != nullptr) {
			return witness->call(this, p_args, p_argcount, r_error);
		}
	}

	r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	return Variant();
}

void FSInstance::notification(int p_notification, bool p_reversed) {
	if (unlikely(!script->valid)) {
		return;
	}

	//notification is not virtual, it gets called at ALL levels just like in C.
	Variant value = p_notification;
	const Variant *args[1] = { &value };
	const StringName &notification_str = FSLanguage::get_singleton()->strings._notification;

	LocalVector<FoundryScript *> script_stack;
	uint32_t script_count = 0;
	for (FoundryScript *sptr = script.ptr(); sptr; sptr = sptr->base.ptr(), ++script_count) {
		script_stack.push_back(sptr);
	}

	const int start = p_reversed ? 0 : script_count - 1;
	const int end = p_reversed ? script_count : -1;
	const int step = p_reversed ? 1 : -1;

	for (int idx = start; idx != end; idx += step) {
		FoundryScript *sc = script_stack[idx];
		if (likely(sc->valid)) {
			HashMap<StringName, FSFunction *>::Iterator E = sc->member_functions.find(notification_str);
			if (E) {
				Callable::CallError err;
				E->value->call(this, args, 1, err);
				if (err.error != Callable::CallError::CALL_OK) {
					//print error about notification call
				}
			}
		}
	}
}

String FSInstance::to_string(bool *r_valid) {
	if (has_method(CoreStringName(_to_string))) {
		Callable::CallError ce;
		Variant ret = callp(CoreStringName(_to_string), nullptr, 0, ce);
		if (ce.error == Callable::CallError::CALL_OK) {
			if (ret.get_type() != Variant::STRING) {
				if (r_valid) {
					*r_valid = false;
				}
				ERR_FAIL_V_MSG(String(), "Wrong type for " + CoreStringName(_to_string) + ", must be a String.");
			}
			if (r_valid) {
				*r_valid = true;
			}
			return ret.operator String();
		}
	}
	if (r_valid) {
		*r_valid = false;
	}
	return String();
}

Ref<Script> FSInstance::get_script() const {
	return script;
}

ScriptLanguage *FSInstance::get_language() {
	return FSLanguage::get_singleton();
}

const Variant FSInstance::get_rpc_config() const {
	return script->get_rpc_config();
}

void FSInstance::reload_members() {
#ifdef DEBUG_ENABLED

	Vector<Variant> new_members;
	new_members.resize(script->member_indices.size());

	//pass the values to the new indices
	for (KeyValue<StringName, FoundryScript::MemberInfo> &E : script->member_indices) {
		if (member_indices_cache.has(E.key)) {
			Variant value = members[member_indices_cache[E.key]];
			new_members.write[E.value.index] = value;
		}
	}

	members.resize(new_members.size()); //resize

	//apply
	members = new_members;

	//pass the values to the new indices
	member_indices_cache.clear();
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &E : script->member_indices) {
		member_indices_cache[E.key] = E.value.index;
	}

#endif
}

FSInstance::~FSInstance() {
	MutexLock lock(FSLanguage::get_singleton()->mutex);

	while (SelfList<FSFunctionState> *E = pending_func_states.first()) {
		// Order matters since clearing the stack may already cause
		// the FSFunctionState to be destroyed and thus removed from the list.
		pending_func_states.remove(E);
		FSFunctionState *state = E->self();
		ObjectID state_id = state->get_instance_id();
		state->_clear_connections();
		if (ObjectDB::get_instance(state_id)) {
			state->_clear_stack();
		}
	}

	if (script.is_valid() && owner) {
		script->instances.erase(owner);
	}
}

/************* SCRIPT LANGUAGE **************/

FSLanguage *FSLanguage::singleton = nullptr;

String FSLanguage::get_name() const {
	return "FoundryScript";
}

/* LANGUAGE FUNCTIONS */

void FSLanguage::_add_global(const StringName &p_name, const Variant &p_value) {
	if (globals.has(p_name)) {
		//overwrite existing
		global_array.write[globals[p_name]] = p_value;
		return;
	}

	if (global_array_empty_indexes.size()) {
		int index = global_array_empty_indexes[global_array_empty_indexes.size() - 1];
		globals[p_name] = index;
		global_array.write[index] = p_value;
		global_array_empty_indexes.resize(global_array_empty_indexes.size() - 1);
	} else {
		globals[p_name] = global_array.size();
		global_array.push_back(p_value);
		_global_array = global_array.ptrw();
	}
}

void FSLanguage::_remove_global(const StringName &p_name) {
	if (!globals.has(p_name)) {
		return;
	}
	global_array_empty_indexes.push_back(globals[p_name]);
	global_array.write[globals[p_name]] = Variant::NIL;
	globals.erase(p_name);
}

void FSLanguage::add_global_constant(const StringName &p_variable, const Variant &p_value) {
	_add_global(p_variable, p_value);
}

void FSLanguage::add_named_global_constant(const StringName &p_name, const Variant &p_value) {
	named_globals[p_name] = p_value;
}

Variant FSLanguage::get_any_global_constant(const StringName &p_name) {
	if (named_globals.has(p_name)) {
		return named_globals[p_name];
	}
	if (globals.has(p_name)) {
		return _global_array[globals[p_name]];
	}
	ERR_FAIL_V_MSG(Variant(), vformat("Could not find any global constant with name: %s.", p_name));
}

Ref<FSReflection> FSLanguage::get_reflection_singleton() const {
	return reflection_singleton;
}

Ref<FSProjectScripts> FSLanguage::get_project_scripts_singleton() const {
	return project_scripts_singleton;
}

Ref<FSNamespace> FSLanguage::get_namespace_singleton() const {
	return namespace_singleton;
}

void FSLanguage::remove_named_global_constant(const StringName &p_name) {
	ERR_FAIL_COND(!named_globals.has(p_name));
	named_globals.erase(p_name);
}

// The reflection API is exposed as the `foundry` named global (see `init`/`finish`).
// `get_reserved_global_names` reports it so the editor rejects a project autoload that
// would shadow it; reflection wins by construction. Single source for the three sites.
static const char *FOUNDRY_SCRIPT_REFLECTION_NAMESPACE = "foundry";

void FSLanguage::init() {
	//populate global constants
	int gcc = CoreConstants::get_global_constant_count();
	for (int i = 0; i < gcc; i++) {
		_add_global(StringName(CoreConstants::get_global_constant_name(i)), CoreConstants::get_global_constant_value(i));
	}

	_add_global(StringName("PI"), Math::PI);
	_add_global(StringName("TAU"), Math::TAU);
	_add_global(StringName("INF"), Math::INF);
	_add_global(StringName("NAN"), Math::NaN);

	//populate native classes

	LocalVector<StringName> class_list;
	ClassDB::get_class_list(class_list);
	for (const StringName &class_name : class_list) {
		if (globals.has(class_name)) {
			continue;
		}
		Ref<FSNativeClass> nc = memnew(FSNativeClass(class_name));
		_add_global(class_name, nc);
	}

	//populate singletons

	List<Engine::Singleton> singletons;
	Engine::get_singleton()->get_singletons(&singletons);
	for (const Engine::Singleton &E : singletons) {
		_add_global(E.name, E.ptr);
	}

	// Expose the read-only reflection API as the `foundry.reflection` surface. A true
	// language namespace is not available, so `foundry` is a nested-singleton object
	// whose `reflection` and `project_scripts` members are the introspection surfaces.
	reflection_singleton.instantiate();
	project_scripts_singleton.instantiate();
	namespace_singleton.instantiate();
	namespace_singleton->set_reflection(reflection_singleton);
	namespace_singleton->set_project_scripts(project_scripts_singleton);
	add_named_global_constant(FOUNDRY_SCRIPT_REFLECTION_NAMESPACE, namespace_singleton);

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		FoundryExtensionManager::get_singleton()->connect("extension_loaded", callable_mp(this, &FSLanguage::_extension_loaded));
		FoundryExtensionManager::get_singleton()->connect("extension_unloading", callable_mp(this, &FSLanguage::_extension_unloading));
	}
#endif // TOOLS_ENABLED

#ifdef DEBUG_ENABLED
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	FSParser::update_project_settings();
	if (!ProjectSettings::get_singleton()->is_connected("settings_changed", callable_mp_static(&FSParser::update_project_settings))) {
		ProjectSettings::get_singleton()->connect("settings_changed", callable_mp_static(&FSParser::update_project_settings));
	}
	// Seed the strict-settings baseline and react to later changes by re-analyzing the live session.
	FSParser::invalidate_analysis_on_strict_settings_change();
	if (!ProjectSettings::get_singleton()->is_connected("settings_changed", callable_mp_static(&FSLanguage::_on_settings_changed))) {
		ProjectSettings::get_singleton()->connect("settings_changed", callable_mp_static(&FSLanguage::_on_settings_changed));
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
#endif // DEBUG_ENABLED

#ifdef TESTS_ENABLED
	// `--foundry_script-generate-tests` is handled as a `--test` command (see
	// `register_types.cpp`) so the process shuts down cleanly afterwards.
	FSTests::FSBenchmarkRunner::handle_cmdline();
#endif // TESTS_ENABLED
}

#ifdef TOOLS_ENABLED
void FSLanguage::_extension_loaded(const Ref<FoundryExtension> &p_extension) {
	List<StringName> class_list;
	ClassDB::get_extension_class_list(p_extension, &class_list);
	for (const StringName &n : class_list) {
		if (globals.has(n)) {
			continue;
		}
		Ref<FSNativeClass> nc = memnew(FSNativeClass(n));
		_add_global(n, nc);
	}
}

void FSLanguage::_extension_unloading(const Ref<FoundryExtension> &p_extension) {
	List<StringName> class_list;
	ClassDB::get_extension_class_list(p_extension, &class_list);
	for (const StringName &n : class_list) {
		_remove_global(n);
	}
}
#endif

String FSLanguage::get_type() const {
	return "FoundryScript";
}

String FSLanguage::get_extension() const {
	return "fs";
}

void FSLanguage::finish() {
	if (finishing) {
		return;
	}
	finishing = true;

	clear_global_annotations();
	clear_conformance_files();

	// Clear the cache before parsing the script_list
	FSCache::clear();

	// Clear dependencies between scripts, to ensure cyclic references are broken
	// (to avoid leaks at exit).
	//
	// Take a strong reference to every listed script before breaking anything: releasing one
	// script's references can destroy other scripts (destroying a root script releases its
	// subclasses), and a destroyed script detaches itself from this intrusive list, nulling its
	// links. Walking the list while that happens can land the iterator on a detached node,
	// silently ending the sweep early and leaking uncleared scripts (with their static-variable
	// state and ResourceCache entries) across a finish()/init() cycle. With every script pinned,
	// the list stays intact for the whole sweep, and the pinned references are released together
	// afterwards.
	LocalVector<Ref<FoundryScript>> scripts_to_clear;
	for (SelfList<FoundryScript> *s = script_list.first(); s; s = s->next()) {
		scripts_to_clear.push_back(Ref<FoundryScript>(s->self()));
	}
	for (Ref<FoundryScript> &scr : scripts_to_clear) {
		if (scr.is_valid()) {
			const auto clear_function_script_type_refs = [](FSFunction *p_function) {
				for (int i = 0; i < p_function->argument_types.size(); i++) {
					p_function->argument_types.write[i].script_type_ref = Ref<Script>();
				}
				p_function->return_type.script_type_ref = Ref<Script>();
			};
			for (KeyValue<StringName, FSFunction *> &E : scr->member_functions) {
				clear_function_script_type_refs(E.value);
			}
			for (KeyValue<StringName, FoundryScript::EnumFunctionSet> &enum_entry : scr->enum_functions) {
				for (KeyValue<StringName, FSFunction *> &function : enum_entry.value.instance_functions) {
					clear_function_script_type_refs(function.value);
				}
				for (KeyValue<StringName, FSFunction *> &function : enum_entry.value.static_functions) {
					clear_function_script_type_refs(function.value);
				}
			}
			for (KeyValue<StringName, FoundryScript::MemberInfo> &E : scr->member_indices) {
				E.value.data_type.script_type_ref = Ref<Script>();
				// A FIXED type-argument binding can hold a Ref<Script> to an external specialization
				// argument; release it here to break cross-script cycles, mirroring data_type above.
				E.value.type_argument_binding.fixed.script_type_ref = Ref<Script>();
			}
			for (KeyValue<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> &E : scr->type_parameter_bindings_by_ancestor) {
				for (FoundryScript::TypeArgumentBinding &binding : E.value) {
					binding.fixed.script_type_ref = Ref<Script>();
				}
			}
			scr->member_type_argument_bindings.clear();
			scr->type_parameter_bindings_by_ancestor.clear();

			// Clear backup for scripts that could slip out of the cyclic reference
			// check
			scr->clear();
			if (!scr->get_path().is_empty()) {
				// Drop cleared scripts from ResourceCache so a later cache-hit load cannot
				// resurrect a half-torn-down script after finish()/init() cycles.
				scr->set_path("");
			}
		}
	}
	scripts_to_clear.clear();
	script_list.clear();
	function_list.clear();

	// Every declaring script's `clear()` above already dropped its own runtime witnesses; clear the
	// whole registry as a final safety net so no borrowed pointer outlives language shutdown.
	FSConformanceRegistry::get_singleton()->clear();

	// Tear down the reflection singletons exposed via the `godot` global. Only
	// remove the named global if it still points to our singleton: a project
	// autoload could have overwritten the `foundry` entry, and we must not clobber it.
	if (namespace_singleton.is_valid() && named_globals.has(FOUNDRY_SCRIPT_REFLECTION_NAMESPACE)) {
		const Object *registered = named_globals[FOUNDRY_SCRIPT_REFLECTION_NAMESPACE].get_validated_object();
		if (registered == namespace_singleton.ptr()) {
			remove_named_global_constant(FOUNDRY_SCRIPT_REFLECTION_NAMESPACE);
		}
	}
	namespace_singleton.unref();
	project_scripts_singleton.unref();
	reflection_singleton.unref();

#ifdef DEV_ENABLED
	{
		List<Ref<Resource>> cached_resources;
		ResourceCache::get_cached_resources(&cached_resources);
		for (const Ref<Resource> &res : cached_resources) {
			const Ref<FoundryScript> script = res;
			if (script.is_valid()) {
				ERR_FAIL_MSG(vformat("FoundryScript '%s' still registered in ResourceCache after FSLanguage::finish().", script->get_path()));
			}
		}
	}
#endif // DEV_ENABLED

	finishing = false;
}

void FSLanguage::profiling_start() {
#ifdef DEBUG_ENABLED
	MutexLock lock(mutex);

	SelfList<FSFunction> *elem = function_list.first();
	while (elem) {
		elem->self()->profile.call_count.set(0);
		elem->self()->profile.self_time.set(0);
		elem->self()->profile.total_time.set(0);
		elem->self()->profile.frame_call_count.set(0);
		elem->self()->profile.frame_self_time.set(0);
		elem->self()->profile.frame_total_time.set(0);
		elem->self()->profile.last_frame_call_count = 0;
		elem->self()->profile.last_frame_self_time = 0;
		elem->self()->profile.last_frame_total_time = 0;
		elem->self()->profile.native_calls.clear();
		elem->self()->profile.last_native_calls.clear();
		elem = elem->next();
	}

	profiling = true;
#endif
}

void FSLanguage::profiling_set_save_native_calls(bool p_enable) {
#ifdef DEBUG_ENABLED
	MutexLock lock(mutex);
	profile_native_calls = p_enable;
#endif
}

void FSLanguage::profiling_stop() {
#ifdef DEBUG_ENABLED
	MutexLock lock(mutex);

	profiling = false;
#endif
}

int FSLanguage::profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) {
	int current = 0;
#ifdef DEBUG_ENABLED

	MutexLock lock(mutex);

	profiling_collate_native_call_data(true);
	SelfList<FSFunction> *elem = function_list.first();
	while (elem) {
		if (current >= p_info_max) {
			break;
		}
		int last_non_internal = current;
		p_info_arr[current].call_count = elem->self()->profile.call_count.get();
		p_info_arr[current].self_time = elem->self()->profile.self_time.get();
		p_info_arr[current].total_time = elem->self()->profile.total_time.get();
		p_info_arr[current].signature = elem->self()->profile.signature;
		current++;

		int nat_time = 0;
		HashMap<String, FSFunction::Profile::NativeProfile>::ConstIterator nat_calls = elem->self()->profile.native_calls.begin();
		while (nat_calls) {
			p_info_arr[current].call_count = nat_calls->value.call_count;
			p_info_arr[current].total_time = nat_calls->value.total_time;
			p_info_arr[current].self_time = nat_calls->value.total_time;
			p_info_arr[current].signature = nat_calls->value.signature;
			nat_time += nat_calls->value.total_time;
			current++;
			++nat_calls;
		}
		p_info_arr[last_non_internal].internal_time = nat_time;
		elem = elem->next();
	}
#endif

	return current;
}

int FSLanguage::profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) {
	int current = 0;

#ifdef DEBUG_ENABLED
	MutexLock lock(mutex);

	profiling_collate_native_call_data(false);
	SelfList<FSFunction> *elem = function_list.first();
	while (elem) {
		if (current >= p_info_max) {
			break;
		}
		if (elem->self()->profile.last_frame_call_count > 0) {
			int last_non_internal = current;
			p_info_arr[current].call_count = elem->self()->profile.last_frame_call_count;
			p_info_arr[current].self_time = elem->self()->profile.last_frame_self_time;
			p_info_arr[current].total_time = elem->self()->profile.last_frame_total_time;
			p_info_arr[current].signature = elem->self()->profile.signature;
			current++;

			int nat_time = 0;
			HashMap<String, FSFunction::Profile::NativeProfile>::ConstIterator nat_calls = elem->self()->profile.last_native_calls.begin();
			while (nat_calls) {
				p_info_arr[current].call_count = nat_calls->value.call_count;
				p_info_arr[current].total_time = nat_calls->value.total_time;
				p_info_arr[current].self_time = nat_calls->value.total_time;
				p_info_arr[current].internal_time = nat_calls->value.total_time;
				p_info_arr[current].signature = nat_calls->value.signature;
				nat_time += nat_calls->value.total_time;
				current++;
				++nat_calls;
			}
			p_info_arr[last_non_internal].internal_time = nat_time;
		}
		elem = elem->next();
	}
#endif

	return current;
}

void FSLanguage::profiling_collate_native_call_data(bool p_accumulated) {
#ifdef DEBUG_ENABLED
	// The same native call can be called from multiple functions, so join them together here.
	// Only use the name of the function (ie signature.split[2]).
	HashMap<String, FSFunction::Profile::NativeProfile *> seen_nat_calls;
	SelfList<FSFunction> *elem = function_list.first();
	while (elem) {
		HashMap<String, FSFunction::Profile::NativeProfile> *nat_calls = p_accumulated ? &elem->self()->profile.native_calls : &elem->self()->profile.last_native_calls;
		HashMap<String, FSFunction::Profile::NativeProfile>::Iterator it = nat_calls->begin();

		while (it != nat_calls->end()) {
			Vector<String> sig = it->value.signature.split("::");
			HashMap<String, FSFunction::Profile::NativeProfile *>::ConstIterator already_found = seen_nat_calls.find(sig[2]);
			if (already_found) {
				already_found->value->total_time += it->value.total_time;
				already_found->value->call_count += it->value.call_count;
				elem->self()->profile.last_native_calls.remove(it);
			} else {
				seen_nat_calls.insert(sig[2], &it->value);
			}
			++it;
		}
		elem = elem->next();
	}
#endif
}

struct FSDepSort {
	//must support sorting so inheritance works properly (parent must be reloaded first)
	bool operator()(const Ref<FoundryScript> &A, const Ref<FoundryScript> &B) const {
		if (A == B) {
			return false; //shouldn't happen but..
		}
		const FoundryScript *I = B->get_base().ptr();
		while (I) {
			if (I == A.ptr()) {
				// A is a base of B
				return true;
			}

			I = I->get_base().ptr();
		}

		return false; //not a base
	}
};

#ifdef DEBUG_ENABLED
void FSLanguage::_on_settings_changed() {
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	if (FSParser::invalidate_analysis_on_strict_settings_change()) {
		// A strict flag flipped: the cache's stale parser/script artifacts were just dropped, so the
		// next analysis of any script reads the new flags. We deliberately do NOT reload script
		// resources from disk here (e.g. reload_all_scripts(), whose path replaces in-memory source
		// with the on-disk version): that would clobber unsaved edits in open script buffers. The
		// editor re-validates open buffers from their own in-memory source on the next validation
		// pass, which now resolves against the invalidated cache and reports under the new flags.
		print_verbose("FoundryScript: Strict analysis settings changed; invalidated analysis cache.");

#if defined(TOOLS_ENABLED) && !defined(FOUNDRY_SCRIPT_NO_LSP)
		// The language server caches its own parsers per open document and would otherwise keep
		// publishing diagnostics from the old strict mode until an edit/reopen. Re-parse the open
		// documents from their in-memory buffers so the LSP re-publishes under the new flags too.
		FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
		if (protocol != nullptr && protocol->is_initialized()) {
			protocol->reparse_open_scripts();
		}
#endif
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}
#endif // DEBUG_ENABLED

void FSLanguage::reload_all_scripts() {
#ifdef DEBUG_ENABLED
	print_verbose("FoundryScript: Reloading all scripts");
	Array scripts;
	{
		MutexLock lock(mutex);

		SelfList<FoundryScript> *elem = script_list.first();
		while (elem) {
			if (elem->self()->get_path().is_resource_file()) {
				print_verbose("FoundryScript: Found: " + elem->self()->get_path());
				scripts.push_back(Ref<FoundryScript>(elem->self())); //cast to foundry_script to avoid being erased by accident
			}
			elem = elem->next();
		}

#ifdef TOOLS_ENABLED
		if (Engine::get_singleton()->is_editor_hint()) {
			// Reload all pointers to existing singletons so that tool scripts can work with the reloaded extensions.
			List<Engine::Singleton> singletons;
			Engine::get_singleton()->get_singletons(&singletons);
			for (const Engine::Singleton &E : singletons) {
				if (globals.has(E.name)) {
					_add_global(E.name, E.ptr);
				}
			}
		}
#endif // TOOLS_ENABLED
	}

	reload_scripts(scripts, true);
#endif // DEBUG_ENABLED
}

void FSLanguage::reload_scripts(const Array &p_scripts, bool p_soft_reload) {
#ifdef DEBUG_ENABLED

	List<Ref<FoundryScript>> scripts;
	{
		MutexLock lock(mutex);

		SelfList<FoundryScript> *elem = script_list.first();
		while (elem) {
			// Scripts will reload all subclasses, so only reload root scripts.
			if (elem->self()->is_root_script() && !elem->self()->get_path().is_empty()) {
				scripts.push_back(Ref<FoundryScript>(elem->self())); //cast to foundry_script to avoid being erased by accident
			}
			elem = elem->next();
		}
	}

	//when someone asks you why dynamically typed languages are easier to write....

	HashMap<Ref<FoundryScript>, HashMap<ObjectID, List<Pair<StringName, Variant>>>> to_reload;

	//as scripts are going to be reloaded, must proceed without locking here

	scripts.sort_custom<FSDepSort>(); //update in inheritance dependency order

	for (Ref<FoundryScript> &scr : scripts) {
		bool reload = p_scripts.has(scr) || to_reload.has(scr->get_base());

		if (!reload) {
			continue;
		}

		to_reload.insert(scr, HashMap<ObjectID, List<Pair<StringName, Variant>>>());

		if (!p_soft_reload) {
			//save state and remove script from instances
			HashMap<ObjectID, List<Pair<StringName, Variant>>> &map = to_reload[scr];

			while (scr->instances.front()) {
				Object *obj = scr->instances.front()->get();
				//save instance info
				List<Pair<StringName, Variant>> state;
				if (obj->get_script_instance()) {
					obj->get_script_instance()->get_property_state(state);
					map[obj->get_instance_id()] = state;
					obj->set_script(Variant());
				}
			}

			//same thing for placeholders
#ifdef TOOLS_ENABLED

			while (scr->placeholders.size()) {
				Object *obj = (*scr->placeholders.begin())->get_owner();

				//save instance info
				if (obj->get_script_instance()) {
					map.insert(obj->get_instance_id(), List<Pair<StringName, Variant>>());
					List<Pair<StringName, Variant>> &state = map[obj->get_instance_id()];
					obj->get_script_instance()->get_property_state(state);
					obj->set_script(Variant());
				} else {
					// no instance found. Let's remove it so we don't loop forever
					scr->placeholders.erase(*scr->placeholders.begin());
				}
			}

#endif // TOOLS_ENABLED

			for (const KeyValue<ObjectID, List<Pair<StringName, Variant>>> &F : scr->pending_reload_state) {
				map[F.key] = F.value; //pending to reload, use this one instead
			}
		}
	}

	for (KeyValue<Ref<FoundryScript>, HashMap<ObjectID, List<Pair<StringName, Variant>>>> &E : to_reload) {
		Ref<FoundryScript> scr = E.key;
		print_verbose("FoundryScript: Reloading: " + scr->get_path());
		if (scr->is_built_in()) {
			// TODO: It would be nice to do it more efficiently than loading the whole scene again.
			Ref<PackedScene> scene = ResourceLoader::load(scr->get_path().get_slice("::", 0), "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
			ERR_CONTINUE(scene.is_null());

			Ref<SceneState> state = scene->get_state();
			Ref<FoundryScript> fresh = state->get_sub_resource(scr->get_path());
			ERR_CONTINUE(fresh.is_null());

			scr->set_source_code(fresh->get_source_code());
		} else if (!scr->is_compiled_binary()) {
#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
			print_verbose("FoundryScript: live reload of source scripts is unavailable in this template build.");
			continue;
#else
			// Bytecode-backed scripts have no source; reload() re-links them from the binary.
			scr->load_source_code(scr->get_path());
#endif
		}
		scr->reload(p_soft_reload);

		//restore state if saved
		for (KeyValue<ObjectID, List<Pair<StringName, Variant>>> &F : E.value) {
			List<Pair<StringName, Variant>> &saved_state = F.value;

			Object *obj = ObjectDB::get_instance(F.key);
			if (!obj) {
				continue;
			}

			if (!p_soft_reload) {
				//clear it just in case (may be a pending reload state)
				obj->set_script(Variant());
			}
			obj->set_script(scr);

			ScriptInstance *script_inst = obj->get_script_instance();

			if (!script_inst) {
				//failed, save reload state for next time if not saved
				if (!scr->pending_reload_state.has(obj->get_instance_id())) {
					scr->pending_reload_state[obj->get_instance_id()] = saved_state;
				}
				continue;
			}

			if (script_inst->is_placeholder() && scr->is_placeholder_fallback_enabled()) {
				PlaceHolderScriptInstance *placeholder = static_cast<PlaceHolderScriptInstance *>(script_inst);
				for (List<Pair<StringName, Variant>>::Element *G = saved_state.front(); G; G = G->next()) {
					placeholder->property_set_fallback(G->get().first, G->get().second);
				}
			} else {
				for (List<Pair<StringName, Variant>>::Element *G = saved_state.front(); G; G = G->next()) {
					script_inst->set(G->get().first, G->get().second);
				}
			}

			scr->pending_reload_state.erase(obj->get_instance_id()); //as it reloaded, remove pending state
		}

		//if instance states were saved, set them!
	}

#endif // DEBUG_ENABLED
}

void FSLanguage::reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	Array scripts = { p_script };
	reload_scripts(scripts, p_soft_reload);
}

void FSLanguage::frame() {
	FSScriptTestGuard::poll_timeouts();
#ifdef DEBUG_ENABLED
	if (profiling) {
		MutexLock lock(mutex);

		SelfList<FSFunction> *elem = function_list.first();
		while (elem) {
			elem->self()->profile.last_frame_call_count = elem->self()->profile.frame_call_count.get();
			elem->self()->profile.last_frame_self_time = elem->self()->profile.frame_self_time.get();
			elem->self()->profile.last_frame_total_time = elem->self()->profile.frame_total_time.get();
			elem->self()->profile.last_native_calls = elem->self()->profile.native_calls;
			elem->self()->profile.frame_call_count.set(0);
			elem->self()->profile.frame_self_time.set(0);
			elem->self()->profile.frame_total_time.set(0);
			elem->self()->profile.native_calls.clear();
			elem = elem->next();
		}
	}

#endif
}

/* EDITOR FUNCTIONS */
Vector<String> FSLanguage::get_reserved_words() const {
	// Please keep alphabetical order within categories.
	static const Vector<String> ret = {
		// Control flow.
		"break",
		"continue",
		"elif",
		"else",
		"for",
		"if",
		"match",
		"pass",
		"return",
		"when",
		"while",
		// Declarations.
		"abstract",
		"class",
		"class_name",
		"const",
		"enum",
		"enum_name",
		"extends",
		"final",
		"func",
		"import",
		"namespace",
		"signal",
		"static",
		// Do not add `async` here: it is contextual and remains a valid identifier outside function modifiers.
		"trait",
		"trait_name",
		"tuple",
		"tuple_name",
		"var",
		// Other keywords.
		"await",
		"breakpoint",
		"self",
		"super",
		"yield", // Reserved for potential future use.
		// Operators.
		"and",
		"as",
		"in",
		"is",
		"not",
		"or",
		// Special values (tokenizer treats them as literals, not as tokens).
		"false",
		"null",
		"true",
		// Constants.
		"INF",
		"NAN",
		"PI",
		"TAU",
		// Functions (highlighter uses global function color instead).
		"assert",
		"preload",
		// Types (highlighter uses type color instead).
		"void",
	};

	return ret;
}

Vector<String> FSLanguage::get_reserved_global_names() const {
	// `godot` is registered as a named global constant exposing `godot.reflection`
	// (see `init`). Reserve it so a project autoload cannot silently shadow it.
	//
	// The reservation is scoped to editor/tools builds, where the compiler can resolve
	// named globals (see the TOOLS_ENABLED guard in FSCompiler). In an exported
	// non-tools runtime the reflection namespace is not compiler-visible anyway, so
	// reserving the name would only strip a project autoload of its sole binding.
#ifdef TOOLS_ENABLED
	static const Vector<String> ret = { FOUNDRY_SCRIPT_REFLECTION_NAMESPACE };
	return ret;
#else
	return Vector<String>();
#endif
}

bool FSLanguage::is_reserved_global_name(const StringName &p_name) const {
#ifdef TOOLS_ENABLED
	return p_name == StringName(FOUNDRY_SCRIPT_REFLECTION_NAMESPACE);
#else
	return false;
#endif
}

bool FSLanguage::is_control_flow_keyword(const String &p_keyword) const {
	// Please keep alphabetical order.
	return p_keyword == "break" ||
			p_keyword == "continue" ||
			p_keyword == "elif" ||
			p_keyword == "else" ||
			p_keyword == "for" ||
			p_keyword == "if" ||
			p_keyword == "match" ||
			p_keyword == "pass" ||
			p_keyword == "return" ||
			p_keyword == "when" ||
			p_keyword == "while";
}

bool FSLanguage::handles_global_class_type(const String &p_type) const {
	return p_type == "FoundryScript";
}

String FSLanguage::get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool, bool *r_is_trait, bool *r_is_enum) const {
	LocalVector<String> r_vec;
	return _get_global_class_name(p_path, r_base_type, r_icon_path, r_is_abstract, r_is_tool, r_is_trait, r_is_enum, r_vec);
}

String FSLanguage::_get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool, bool *r_is_trait, bool *r_is_enum, LocalVector<String> &r_visited) const {
#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	return String();
#else
	if (r_visited.has(p_path)) {
		return String();
	}

	r_visited.push_back(p_path);

	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (err) {
		return String();
	}

	String source = f->get_as_utf8_string();

	FSParser parser;
	err = parser.parse(source, p_path, false, false);

	const FSParser::ClassNode *c = parser.get_tree();
	if (!c) {
		return String(); // No class parsed.
	}

	/* **WARNING**
	 *
	 * This function is written with the goal to be *extremely* error tolerant, as such
	 * it should meet the following requirements:
	 *
	 * - It must not rely on the analyzer (in fact, the analyzer must not be used here),
	 *   because at the time global classes are parsed, the dependencies may not be present
	 *   yet, hence the function will fail (which is unintended).
	 * - It must not fail even if the parsing fails, because even if the file is broken,
	 *   it should attempt its best to retrieve the inheritance metadata.
	 *
	 * Before changing this function, please ask the current maintainer of EditorFileSystem.
	 */

	if (r_base_type && (c->is_enum_file || c->is_tuple_file)) {
		// An `enum_name` or `tuple_name` file declares a type, not a script: it has no base class.
		*r_base_type = String();
	} else if (r_base_type) {
		const FSParser::ClassNode *subclass = c;
		String path = p_path;
		FSParser subparser;
		while (subclass) {
			if (subclass->extends_used) {
				if (!subclass->extends_path.is_empty()) {
					if (subclass->extends.is_empty()) {
						// We only care about the referenced class_name.
						_ALLOW_DISCARD_ _get_global_class_name(subclass->extends_path, r_base_type, nullptr, nullptr, nullptr, nullptr, nullptr, r_visited);
						subclass = nullptr;
						break;
					} else {
						Vector<FSParser::IdentifierNode *> extend_classes = subclass->extends;

						Ref<FileAccess> subfile = FileAccess::open(subclass->extends_path, FileAccess::READ);
						if (subfile.is_null()) {
							break;
						}
						String subsource = subfile->get_as_utf8_string();

						if (subsource.is_empty()) {
							break;
						}
						String subpath = subclass->extends_path;
						if (subpath.is_relative_path()) {
							subpath = path.get_base_dir().path_join(subpath).simplify_path();
						}

						if (OK != subparser.parse(subsource, subpath, false)) {
							break;
						}
						path = subpath;
						subclass = subparser.get_tree();

						while (extend_classes.size() > 0) {
							bool found = false;
							for (int i = 0; i < subclass->members.size(); i++) {
								if (subclass->members[i].type != FSParser::ClassNode::Member::CLASS) {
									continue;
								}

								const FSParser::ClassNode *inner_class = subclass->members[i].m_class;
								if (inner_class->identifier->name == extend_classes[0]->name) {
									extend_classes.remove_at(0);
									found = true;
									subclass = inner_class;
									break;
								}
							}
							if (!found) {
								subclass = nullptr;
								break;
							}
						}
					}
				} else if (subclass->extends.size() == 1) {
					*r_base_type = subclass->extends[0]->name;
					subclass = nullptr;
				} else {
					break;
				}
			} else {
				*r_base_type = "RefCounted";
				subclass = nullptr;
			}
		}
	}
	if (r_icon_path) {
		*r_icon_path = c->simplified_icon_path;
	}
	if (r_is_abstract) {
		*r_is_abstract = c->is_abstract;
	}
	if (r_is_tool) {
		*r_is_tool = parser.is_tool();
	}
	if (r_is_trait) {
		*r_is_trait = c->is_trait;
	}
	if (r_is_enum) {
		*r_is_enum = c->is_enum_file;
	}
	if (c->identifier == nullptr) {
		return String();
	}
	return c->qualified_global_name.is_empty() ? String(c->identifier->name) : c->qualified_global_name;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
// Parses `p_path` into `r_parser` for the cross-file declaration indexes, and reports the root class
// on success.
//
// Like `get_global_class_name`, this must not rely on the analyzer: these declarations are indexed
// before dependencies are guaranteed to be resolvable. Unlike class-name extraction, a file that
// fails to parse is not indexed: a broken source cannot be a reliable declaration library, and
// indexing partial declarations would surface spurious duplicate-identity collisions. Only the
// syntactic parse is consulted (the analyzer is not run), so a file with valid declarations but
// unrelated analyzer errors is still indexed. The full body must be parsed because annotation and
// conformance declarations are root body declarations; the class-name fast path (which skips the
// body) would never see them.
static const FSParser::ClassNode *_parse_indexed_declarations(const String &p_path, FSParser &r_parser) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &err);
	if (err) {
		return nullptr;
	}
	if (r_parser.parse(file->get_as_utf8_string(), p_path, false, true) != OK) {
		return nullptr;
	}
	return r_parser.get_tree();
}

static void _collect_global_annotations(const FSParser::ClassNode *p_root, List<StringName> *r_annotations) {
	for (const FSParser::AnnotationDeclarationNode *declaration : p_root->annotation_declarations) {
		if (declaration->identifier == nullptr || declaration->qualified_name.is_empty()) {
			continue;
		}
		r_annotations->push_back(StringName(declaration->qualified_name));
	}
}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND

void FSLanguage::get_global_annotations(const String &p_path, List<StringName> *r_annotations) const {
	ERR_FAIL_NULL(r_annotations);

#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	return;
#else
	FSParser parser;
	const FSParser::ClassNode *root = _parse_indexed_declarations(p_path, parser);
	if (root == nullptr) {
		return;
	}
	_collect_global_annotations(root, r_annotations);
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

bool FSLanguage::get_declared_conformance_namespace(const String &p_path, String &r_namespace) const {
#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	return false;
#else
	FSParser parser;
	const FSParser::ClassNode *root = _parse_indexed_declarations(p_path, parser);
	if (root == nullptr || root->conformances.is_empty()) {
		return false;
	}
	r_namespace = root->namespace_name;
	return true;
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

void FSLanguage::replace_global_annotations(const String &p_path, const List<StringName> &p_annotations) {
	// Drop every entry currently registered for this path and register the new set under a single
	// lock, so a concurrent refresh of the same path (LSP reparse vs. editor scan with the threaded
	// language server) cannot interleave the remove and add steps and strand an older parse's data.
	// Which of two concurrent refreshes wins is decided one level up, by the generation tokens
	// `commit_declaration_index_refresh` checks: this lock only keeps a single refresh atomic.
	MutexLock lock(annotation_index_mutex);

	List<StringName> emptied;
	for (KeyValue<StringName, Vector<String>> &entry : global_annotations) {
		entry.value.erase(p_path);
		if (entry.value.is_empty()) {
			emptied.push_back(entry.key);
		}
	}
	for (const StringName &name : emptied) {
		global_annotations.erase(name);
	}

	for (const StringName &qualified_name : p_annotations) {
		Vector<String> &paths = global_annotations[qualified_name];
		if (!paths.has(p_path)) {
			paths.push_back(p_path);
		}
	}
}

uint64_t FSLanguage::_claim_declaration_index_generation(const String &p_path) {
	const uint64_t token = ++declaration_index_generation_counter;
	declaration_index_generations[p_path] = token;
	return token;
}

uint64_t FSLanguage::claim_declaration_index_refresh(const String &p_path) {
	MutexLock lock(declaration_index_generation_mutex);
	return _claim_declaration_index_generation(p_path);
}

void FSLanguage::claim_declaration_index_rename_refresh(const String &p_search_path, const String &p_target_path, uint64_t &r_search_token, uint64_t &r_target_token) {
	MutexLock lock(declaration_index_generation_mutex);
	r_target_token = _claim_declaration_index_generation(p_target_path);
	r_search_token = _claim_declaration_index_generation(p_search_path);
}

bool FSLanguage::_is_declaration_index_token_current(const String &p_path, uint64_t p_token) const {
	if (p_token <= declaration_index_generation_floor) {
		return false;
	}
	const uint64_t *claimed = declaration_index_generations.getptr(p_path);
	return claimed != nullptr && *claimed == p_token;
}

void FSLanguage::invalidate_declaration_index_claims(const String &p_path) {
	MutexLock lock(declaration_index_generation_mutex);
	// Stamping a token nobody holds is what invalidates the outstanding claims: no in-flight refresh
	// can match it, and the next refresh claims a newer one and commits normally.
	declaration_index_generations[p_path] = ++declaration_index_generation_counter;
}

void FSLanguage::invalidate_all_declaration_index_claims() {
	MutexLock lock(declaration_index_generation_mutex);
	// A floor rather than per-path stamps: a full clear must not have to enumerate paths, and
	// emptying the generation map would let every stale claim through instead of rejecting it.
	declaration_index_generation_floor = ++declaration_index_generation_counter;
}

bool FSLanguage::commit_declaration_index_refresh(const String &p_search_path, uint64_t p_search_token, const String &p_target_path, uint64_t p_target_token, const List<StringName> &p_annotations, bool p_declares_conformances, const String &p_conformance_namespace) {
	const bool moved = p_search_path != p_target_path;
	// Side effects on other subsystems are collected here and run after every lock is released.
	List<String> cleared_conformance_files;
	// Only a committed mutation changes a namespace's conformance file set, so the deltas are
	// gathered inside the generation-guarded block and notified outside it.
	List<String> changed_conformance_namespaces;

	bool committed = false;
	{
		MutexLock generation_lock(declaration_index_generation_mutex);
		// Each path is guarded by its own token. A superseded path is left to the refresh that
		// superseded it: that refresh is either the last claimant, which always commits, or is itself
		// superseded by an even newer one, so every superseded path is published by somebody. What
		// must not happen is dropping a side nobody else can publish — only this refresh knows the
		// file moved away from the search path, so that removal is committed whenever the search
		// path's own token is still current, even if the target side was superseded.
		const bool target_current = _is_declaration_index_token_current(p_target_path, p_target_token);
		const bool search_current = moved && _is_declaration_index_token_current(p_search_path, p_search_token);

		String removed_namespace;
		if (search_current) {
			// The file moved: the old path's entries go away.
			remove_global_annotations_by_path(p_search_path);
			if (_erase_conformance_file(p_search_path, &removed_namespace)) {
				cleared_conformance_files.push_back(p_search_path);
				changed_conformance_namespaces.push_back(removed_namespace);
			}
		}

		if (target_current) {
			replace_global_annotations(p_target_path, p_annotations);
			if (p_declares_conformances) {
				_index_conformance_file(p_target_path, p_conformance_namespace, &changed_conformance_namespaces);
			} else if (_erase_conformance_file(p_target_path, &removed_namespace)) {
				cleared_conformance_files.push_back(p_target_path);
				changed_conformance_namespaces.push_back(removed_namespace);
			}
		}

		committed = target_current || search_current;
	}

	for (const String &path : cleared_conformance_files) {
		FSConformanceRegistry::get_singleton()->clear_file(path);
	}
	_notify_conformance_namespaces_changed(changed_conformance_namespaces);
	return committed;
}

void FSLanguage::update_global_declaration_index(const String &p_search_path, const String &p_target_path) {
	// Claim before reading the file, so the refresh that commits is guaranteed to have read the file
	// no earlier than any refresh it supersedes: the loser's content is never newer than the winner's
	// at the moment the loser claimed. This is per-trigger eventual consistency, not a serialization
	// of the disk reads themselves — an edit landing after the winner's read is picked up by the
	// refresh that edit's own scan/notify trigger starts. A rename claims both paths because it
	// publishes a removal at one and an addition at the other, both claimed together.
	uint64_t target_token = 0;
	uint64_t search_token = 0;
	if (p_search_path == p_target_path) {
		target_token = claim_declaration_index_refresh(p_target_path);
		search_token = target_token;
	} else {
		claim_declaration_index_rename_refresh(p_search_path, p_target_path, search_token, target_token);
	}

	// A path that no longer exists or fails to parse yields no declarations, so a removed/renamed
	// file collapses to an empty replacement, dropping its stale entries.
	List<StringName> annotations;
	String conformance_namespace;
	bool declares_conformances = false;

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	// One parse feeds every index built from this file's syntactic declarations; the editor
	// file-system scan calls this for every `.fs` file in the project.
	FSParser parser;
	const FSParser::ClassNode *root = _parse_indexed_declarations(p_target_path, parser);
	if (root != nullptr) {
		_collect_global_annotations(root, &annotations);
		declares_conformances = !root->conformances.is_empty();
		conformance_namespace = root->namespace_name;
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND

	commit_declaration_index_refresh(p_search_path, search_token, p_target_path, target_token, annotations, declares_conformances, conformance_namespace);
}

void FSLanguage::get_indexed_conformances(Array &r_conformances) const {
	MutexLock lock(conformance_index_mutex);
	for (const KeyValue<String, String> &entry : conformance_namespace_by_file) {
		Dictionary conformance;
		conformance["path"] = entry.key;
		conformance["namespace"] = entry.value;
		r_conformances.push_back(conformance);
	}
}

void FSLanguage::add_indexed_conformance(const String &p_path, const String &p_namespace) {
	add_conformance_file(p_path, p_namespace);
}

void FSLanguage::clear_indexed_conformances() {
	clear_conformance_files();
}

void FSLanguage::prune_missing_indexed_conformances() {
	List<String> missing_paths;
	{
		MutexLock lock(conformance_index_mutex);
		for (const KeyValue<String, String> &entry : conformance_namespace_by_file) {
			if (!FileAccess::exists(entry.key)) {
				missing_paths.push_back(entry.key);
			}
		}
	}
	for (const String &path : missing_paths) {
		// Supersede any refresh already in flight for the path so it cannot re-add what the sweep is
		// about to drop, then drop it.
		invalidate_declaration_index_claims(path);
		remove_conformance_file(path);
	}
}

void FSLanguage::clear_global_declaration_index_under(const String &p_root_prefix) {
	if (p_root_prefix.is_empty()) {
		return;
	}

	// Supersede every in-flight claim for a path under the prefix before dropping anything: a refresh
	// that read the file before the sweep must not commit afterwards and resurrect an entry the sweep
	// is removing. A refresh that claims after this point is newer than the sweep and commits
	// normally, which is why the claims are enumerated first and the removals happen second.
	HashSet<String> dropped_paths;
	{
		MutexLock lock(annotation_index_mutex);
		for (const KeyValue<StringName, Vector<String>> &entry : global_annotations) {
			for (const String &path : entry.value) {
				if (path.begins_with(p_root_prefix)) {
					dropped_paths.insert(path);
				}
			}
		}
	}

	List<String> dropped_conformance_paths;
	{
		MutexLock lock(conformance_index_mutex);
		for (const KeyValue<String, String> &entry : conformance_namespace_by_file) {
			if (entry.key.begins_with(p_root_prefix)) {
				dropped_conformance_paths.push_back(entry.key);
			}
		}
	}
	for (const String &path : dropped_conformance_paths) {
		dropped_paths.insert(path);
	}

	for (const String &path : dropped_paths) {
		invalidate_declaration_index_claims(path);
	}

	{
		MutexLock lock(annotation_index_mutex);
		List<StringName> emptied;
		for (KeyValue<StringName, Vector<String>> &entry : global_annotations) {
			for (int i = entry.value.size() - 1; i >= 0; i--) {
				if (entry.value[i].begins_with(p_root_prefix)) {
					entry.value.remove_at(i);
				}
			}
			if (entry.value.is_empty()) {
				emptied.push_back(entry.key);
			}
		}
		for (const StringName &name : emptied) {
			global_annotations.erase(name);
		}
	}

	for (const String &path : dropped_conformance_paths) {
		remove_conformance_file(path);
	}
}

void FSLanguage::_index_conformance_file(const String &p_path, const String &p_namespace, List<String> *r_changed_namespaces) {
	MutexLock lock(conformance_index_mutex);

	const String *previous_namespace = conformance_namespace_by_file.getptr(p_path);
	if (previous_namespace != nullptr) {
		if (*previous_namespace == p_namespace) {
			// Re-indexing an unchanged file leaves every namespace's file set exactly as it was.
			return;
		}
		Vector<String> *previous_files = conformance_files_by_namespace.getptr(*previous_namespace);
		if (previous_files != nullptr) {
			previous_files->erase(p_path);
			if (previous_files->is_empty()) {
				conformance_files_by_namespace.erase(*previous_namespace);
			}
		}
		if (r_changed_namespaces != nullptr) {
			r_changed_namespaces->push_back(*previous_namespace);
		}
	}

	conformance_namespace_by_file[p_path] = p_namespace;
	Vector<String> &files = conformance_files_by_namespace[p_namespace];
	if (!files.has(p_path)) {
		files.push_back(p_path);
	}
	if (r_changed_namespaces != nullptr) {
		r_changed_namespaces->push_back(p_namespace);
	}
}

void FSLanguage::add_conformance_file(const String &p_path, const String &p_namespace) {
	// Scoped so this mutator matches `remove_conformance_file`: the notification below runs with no
	// index lock held.
	List<String> changed_namespaces;
	_index_conformance_file(p_path, p_namespace, &changed_namespaces);
	_notify_conformance_namespaces_changed(changed_namespaces);
}

bool FSLanguage::_erase_conformance_file(const String &p_path, String *r_removed_namespace) {
	MutexLock lock(conformance_index_mutex);

	const String *declared_namespace = conformance_namespace_by_file.getptr(p_path);
	if (declared_namespace == nullptr) {
		return false;
	}
	if (r_removed_namespace != nullptr) {
		*r_removed_namespace = *declared_namespace;
	}
	Vector<String> *files = conformance_files_by_namespace.getptr(*declared_namespace);
	if (files != nullptr) {
		files->erase(p_path);
		if (files->is_empty()) {
			conformance_files_by_namespace.erase(*declared_namespace);
		}
	}
	conformance_namespace_by_file.erase(p_path);
	return true;
}

void FSLanguage::remove_conformance_file(const String &p_path) {
	String removed_namespace;
	if (!_erase_conformance_file(p_path, &removed_namespace)) {
		return;
	}

	// The file was indexed and no longer declares conformances — it was deleted, moved, or stopped
	// parsing. Whatever a previous analysis registered for it is stale, and the analyzer reads that
	// registry to decide whether a call names a conformance it cannot reach, so leaving it would keep
	// producing diagnostics that point at a file that is gone. Analyzing the file again re-registers.
	FSConformanceRegistry::get_singleton()->clear_file(p_path);

	List<String> changed_namespaces;
	changed_namespaces.push_back(removed_namespace);
	_notify_conformance_namespaces_changed(changed_namespaces);
}

void FSLanguage::_notify_conformance_namespaces_changed(const List<String> &p_namespaces) {
#ifdef TOOLS_ENABLED
	HashSet<String> notified;
	for (const String &changed_namespace : p_namespaces) {
		// The global namespace is never reached implicitly, so no parser holds a namespace-derived
		// edge to it and there is nothing a change to its file set can make stale.
		if (changed_namespace.is_empty() || notified.has(changed_namespace)) {
			continue;
		}
		notified.insert(changed_namespace);
		notify_conformance_namespace_changed(changed_namespace);
	}
#else
	(void)p_namespaces;
#endif // TOOLS_ENABLED
}

void FSLanguage::clear_conformance_files() {
	invalidate_all_declaration_index_claims();

	MutexLock lock(conformance_index_mutex);
	conformance_files_by_namespace.clear();
	conformance_namespace_by_file.clear();
}

Vector<String> FSLanguage::get_conformance_files_in_namespace(const String &p_namespace) const {
	MutexLock lock(conformance_index_mutex);
	const Vector<String> *files = conformance_files_by_namespace.getptr(p_namespace);
	return files == nullptr ? Vector<String>() : *files;
}

Vector<String> FSLanguage::get_all_conformance_files() const {
	MutexLock lock(conformance_index_mutex);
	Vector<String> files;
	files.resize(conformance_namespace_by_file.size());
	int index = 0;
	for (const KeyValue<String, String> &entry : conformance_namespace_by_file) {
		files.write[index++] = entry.key;
	}
	return files;
}

#ifdef TOOLS_ENABLED
void FSLanguage::notify_disk_source_changed(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}

	const HashSet<String> affected = FSCache::collect_parser_invalidation_closure(p_path);
	FSCache::remove_parser(p_path);

#if !defined(FOUNDRY_SCRIPT_NO_LSP)
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	if (protocol != nullptr && protocol->is_initialized()) {
		protocol->reparse_open_scripts(affected);
	}
#endif
}

void FSLanguage::notify_conformance_namespace_changed(const String &p_namespace) {
	// Must run with no declaration-index lock held: the sweep and the eviction below take
	// `FSCache::mutex`, and the established order is `FSCache::mutex` -> `conformance_index_mutex`
	// (a cache-driven parse resolves its conformance dependencies through the index). Every caller
	// therefore collects its deltas under the index locks and notifies after releasing them.
	if (p_namespace.is_empty()) {
		return;
	}

	const Vector<String> members = FSCache::collect_parsers_reaching_namespace(p_namespace);

	HashSet<String> affected;
	for (const String &member : members) {
		for (const String &path : FSCache::collect_parser_invalidation_closure(member)) {
			affected.insert(path);
		}
	}
	for (const String &member : members) {
		// Eviction, never removal: an analysis in flight may still hold a Ref to one of these parsers,
		// and `remove_parser` abandons the entry instead of destroying it.
		FSCache::remove_parser(member);
	}

#if !defined(FOUNDRY_SCRIPT_NO_LSP)
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	if (protocol != nullptr && protocol->is_initialized()) {
		// An open document reaches the namespace whether or not the shared cache happens to hold a
		// parser for it, so the documents to republish are collected from the language server too.
		for (const String &path : protocol->collect_open_scripts_reaching_namespace(p_namespace)) {
			affected.insert(path);
		}
		if (!affected.is_empty()) {
			protocol->reparse_open_scripts(affected);
		}
	}
#endif
}
#endif // TOOLS_ENABLED

void FSLanguage::add_global_annotation(const StringName &p_qualified_name, const String &p_path) {
	MutexLock lock(annotation_index_mutex);

	Vector<String> &paths = global_annotations[p_qualified_name];
	if (!paths.has(p_path)) {
		paths.push_back(p_path);
	}
}

void FSLanguage::remove_global_annotations_by_path(const String &p_path) {
	MutexLock lock(annotation_index_mutex);

	List<StringName> emptied;
	for (KeyValue<StringName, Vector<String>> &entry : global_annotations) {
		entry.value.erase(p_path);
		if (entry.value.is_empty()) {
			emptied.push_back(entry.key);
		}
	}
	for (const StringName &name : emptied) {
		global_annotations.erase(name);
	}
}

void FSLanguage::clear_global_annotations() {
	invalidate_all_declaration_index_claims();

	MutexLock lock(annotation_index_mutex);
	global_annotations.clear();
}

bool FSLanguage::is_global_annotation(const StringName &p_qualified_name) const {
	MutexLock lock(annotation_index_mutex);
	return global_annotations.has(p_qualified_name);
}

bool FSLanguage::is_duplicated_global_annotation(const StringName &p_qualified_name) const {
	MutexLock lock(annotation_index_mutex);
	const Vector<String> *paths = global_annotations.getptr(p_qualified_name);
	return paths != nullptr && paths->size() > 1;
}

String FSLanguage::get_global_annotation_path(const StringName &p_qualified_name) const {
	MutexLock lock(annotation_index_mutex);
	const Vector<String> *paths = global_annotations.getptr(p_qualified_name);
	if (paths == nullptr || paths->is_empty()) {
		return String();
	}
	return (*paths)[0];
}

void FSLanguage::get_global_annotation_list(List<StringName> *r_annotations) const {
	ERR_FAIL_NULL(r_annotations);

	MutexLock lock(annotation_index_mutex);
	for (const KeyValue<StringName, Vector<String>> &entry : global_annotations) {
		r_annotations->push_back(entry.key);
	}
}

bool FSLanguage::namespace_has_annotations(const String &p_namespace) const {
	if (p_namespace.is_empty()) {
		return false;
	}

	const String namespace_prefix = p_namespace + ".";

	MutexLock lock(annotation_index_mutex);
	for (const KeyValue<StringName, Vector<String>> &entry : global_annotations) {
		if (String(entry.key).begins_with(namespace_prefix)) {
			return true;
		}
	}
	return false;
}

thread_local FSLanguage::CallLevel *FSLanguage::_call_stack = nullptr;
thread_local uint32_t FSLanguage::_call_stack_size = 0;

FSLanguage::CallLevel *FSLanguage::_get_stack_level(uint32_t p_level) {
	ERR_FAIL_UNSIGNED_INDEX_V(p_level, _call_stack_size, nullptr);
	CallLevel *level = _call_stack; // Start from top
	uint32_t level_index = 0;
	while (p_level > level_index) {
		level_index++;
		level = level->prev;
	}
	return level;
}

FSLanguage::FSLanguage() {
	ERR_FAIL_COND(singleton);
	singleton = this;
	strings._init = StringName("_init");
	strings._static_init = StringName("_static_init");
	strings._notification = StringName("_notification");
	strings._set = StringName("_set");
	strings._get = StringName("_get");
	strings._get_property_list = StringName("_get_property_list");
	strings._validate_property = StringName("_validate_property");
	strings._property_can_revert = StringName("_property_can_revert");
	strings._property_get_revert = StringName("_property_get_revert");
	strings._script_source = StringName("script/source");
	_debug_parse_err_line = -1;
	_debug_parse_err_file = "";

#ifdef DEBUG_ENABLED
	profiling = false;
	profile_native_calls = false;
	script_frame_time = 0;
#endif // DEBUG_ENABLED

	_debug_max_call_stack = GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "debug/settings/foundry_script/max_call_stack", PROPERTY_HINT_RANGE, "512," + itos(FSFunction::MAX_CALL_DEPTH - 1) + ",1"), 1024);
	track_call_stack = GLOBAL_DEF_RST("debug/settings/foundry_script/always_track_call_stacks", false);
	track_locals = GLOBAL_DEF_RST("debug/settings/foundry_script/always_track_local_variables", false);
	GLOBAL_DEF("debug/foundry_script/analysis/strict_null_checks", false);
	GLOBAL_DEF("debug/foundry_script/analysis/strict_dynamic_checks", false);

#ifdef DEBUG_ENABLED
	track_call_stack = true;
	track_locals = track_locals || EngineDebugger::is_active();
#endif // DEBUG_ENABLED

#ifdef DEBUG_ENABLED
#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	GLOBAL_DEF("debug/foundry_script/warnings/enable", true);

	GLOBAL_DEF(PropertyInfo(Variant::DICTIONARY,
					   "debug/foundry_script/warnings/directory_rules",
					   PROPERTY_HINT_TYPE_STRING,
					   vformat("%d/%d:;%d/%d:Exclude,Include", Variant::STRING, PROPERTY_HINT_DIR, Variant::INT, PROPERTY_HINT_ENUM)),
			Dictionary({ { "res://addons", FSParser::WarningDirectoryRule::DECISION_EXCLUDE } }));

	for (int i = 0; i < (int)FSWarning::WARNING_MAX; i++) {
		const FSWarning::Code code = (FSWarning::Code)i;
		const Variant default_value = FSWarning::get_default_value(code);
		GLOBAL_DEF(FSWarning::get_property_info(code), default_value);
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
#endif // DEBUG_ENABLED
}

FSLanguage::~FSLanguage() {
	singleton = nullptr;
}

void FSLanguage::add_orphan_subclass(const String &p_qualified_name, const ObjectID &p_subclass) {
	orphan_subclasses[p_qualified_name] = p_subclass;
}

Ref<FoundryScript> FSLanguage::get_orphan_subclass(const String &p_qualified_name) {
	HashMap<String, ObjectID>::Iterator orphan_subclass_element = orphan_subclasses.find(p_qualified_name);
	if (!orphan_subclass_element) {
		return Ref<FoundryScript>();
	}
	ObjectID orphan_subclass = orphan_subclass_element->value;
	Object *obj = ObjectDB::get_instance(orphan_subclass);
	orphan_subclasses.remove(orphan_subclass_element);
	if (!obj) {
		return Ref<FoundryScript>();
	}
	return Ref<FoundryScript>(Object::cast_to<FoundryScript>(obj));
}

Ref<FoundryScript> FSLanguage::get_script_by_fully_qualified_name(const String &p_name) {
	{
		MutexLock lock(mutex);

		SelfList<FoundryScript> *elem = script_list.first();
		while (elem) {
			FoundryScript *scr = elem->self();
			if (scr->fully_qualified_name == p_name) {
				return scr;
			}
			elem = elem->next();
		}
	}

	Ref<FoundryScript> scr;
	scr.instantiate();
	scr->fully_qualified_name = p_name;
	return scr;
}

/*************** RESOURCE ***************/

Ref<Resource> ResourceFormatLoaderFoundryScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Error err;
	bool ignoring = p_cache_mode == CACHE_MODE_IGNORE || p_cache_mode == CACHE_MODE_IGNORE_DEEP;
	Ref<FoundryScript> scr = FSCache::get_full_script(p_original_path, err, "", ignoring);

	if (err && scr.is_valid()) {
		// If !scr.is_valid(), the error was likely from scr->load_source_code(), which already generates an error.
		ERR_PRINT_ED(vformat(R"(Failed to load script "%s" with error "%s".)", p_original_path, error_names[err]));
	}

	if (r_error) {
		// Don't fail loading because of parsing error.
		*r_error = scr.is_valid() ? OK : err;
	}

	return scr;
}

void ResourceFormatLoaderFoundryScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("fs");
	p_extensions->push_back("fsc");
	p_extensions->push_back("fsb");
}

bool ResourceFormatLoaderFoundryScript::handles_type(const String &p_type) const {
	return (p_type == "Script" || p_type == "FoundryScript");
}

String ResourceFormatLoaderFoundryScript::get_resource_type(const String &p_path) const {
	String el = p_path.get_extension().to_lower();
	if (el == "fs" || el == "fsc" || el == "fsb") {
		return "FoundryScript";
	}
	return "";
}

void ResourceFormatLoaderFoundryScript::get_dependencies(const String &p_path, List<String> *p_dependencies, bool p_add_types) {
	const String remapped_path = ResourceLoader::path_remap(p_path);
	if (remapped_path.has_extension("fsb")) {
		// Compiled binaries list their dependencies in a section near the file head; the UTF-8
		// parse below would read garbage on binary data.
		Vector<uint8_t> buffer = FileAccess::get_file_as_bytes(remapped_path);
		ERR_FAIL_COND_MSG(buffer.is_empty(), "Cannot open file '" + remapped_path + "'.");
		Vector<String> dependencies;
		FSBytecodeLoader loader;
		if (loader.read_dependencies(buffer, dependencies) != OK) {
			return;
		}
		for (const String &dependency : dependencies) {
			p_dependencies->push_back(dependency);
		}
		return;
	}

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_MSG(file.is_null(), "Cannot open file '" + p_path + "'.");

	String source = file->get_as_utf8_string();
	if (source.is_empty()) {
		return;
	}

	FSParser parser;
	if (OK != parser.parse(source, p_path, false)) {
		return;
	}

	for (const String &E : parser.get_dependencies()) {
		p_dependencies->push_back(E);
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

void ResourceFormatLoaderFoundryScript::get_classes_used(const String &p_path, HashSet<StringName> *r_classes) {
#ifdef FOUNDRY_SCRIPT_NO_FRONTEND
	return;
#else
	Ref<FoundryScript> scr = ResourceLoader::load(p_path);
	if (scr.is_null()) {
		return;
	}

	const String source = scr->get_source_code();
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(source);
	FSTokenizer::Token current = tokenizer.scan();
	while (current.type != FSTokenizer::Token::TK_EOF) {
		if (!current.is_identifier()) {
			current = tokenizer.scan();
			continue;
		}

		int insert_idx = 0;
		for (int i = 0; i < current.start_line - 1; i++) {
			insert_idx = source.find("\n", insert_idx) + 1;
		}
		// Insert the "cursor" character, needed for the lookup to work.
		const String source_with_cursor = source.insert(insert_idx + current.start_column, String::chr(0xFFFF));

		ScriptLanguage::LookupResult result;
		if (scr->get_language()->lookup_code(source_with_cursor, current.get_identifier(), p_path, nullptr, result) == OK) {
			if (!result.class_name.is_empty() && ClassDB::class_exists(result.class_name)) {
				r_classes->insert(result.class_name);
			}

			if (result.type == ScriptLanguage::LOOKUP_RESULT_CLASS_PROPERTY) {
				PropertyInfo prop;
				if (ClassDB::get_property_info(result.class_name, result.class_member, &prop)) {
					if (!prop.class_name.is_empty() && ClassDB::class_exists(prop.class_name)) {
						r_classes->insert(prop.class_name);
					}
					if (!prop.hint_string.is_empty() && ClassDB::class_exists(prop.hint_string)) {
						r_classes->insert(prop.hint_string);
					}
				}
			} else if (result.type == ScriptLanguage::LOOKUP_RESULT_CLASS_METHOD) {
				MethodInfo met;
				if (ClassDB::get_method_info(result.class_name, result.class_member, &met)) {
					if (!met.return_val.class_name.is_empty() && ClassDB::class_exists(met.return_val.class_name)) {
						r_classes->insert(met.return_val.class_name);
					}
					if (!met.return_val.hint_string.is_empty() && ClassDB::class_exists(met.return_val.hint_string)) {
						r_classes->insert(met.return_val.hint_string);
					}
				}
			}
		}

		current = tokenizer.scan();
	}
#endif // FOUNDRY_SCRIPT_NO_FRONTEND
}

Error ResourceFormatSaverFoundryScript::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<FoundryScript> sqscr = p_resource;
	ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

	String source = sqscr->get_source_code();

	{
		Error err;
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);

		ERR_FAIL_COND_V_MSG(err, err, "Cannot save FoundryScript file '" + p_path + "'.");

		file->store_string(source);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
	}

	if (ScriptServer::is_reload_scripts_on_save_enabled()) {
		FSLanguage::get_singleton()->reload_tool_script(p_resource, true);
	}

	return OK;
}

void ResourceFormatSaverFoundryScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<FoundryScript>(*p_resource)) {
		p_extensions->push_back("fs");
	}
}

bool ResourceFormatSaverFoundryScript::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<FoundryScript>(*p_resource) != nullptr;
}
