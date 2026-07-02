/**************************************************************************/
/*  fs_bytecode_export.cpp                                                */
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

#include "fs_bytecode_export.h"

#include "foundry_script.h"
#include "fs_conformance_registry.h"
#include "fs_function.h"

#include "core/config/engine.h"
#include "core/templates/hash_set.h"
#include "core/version.h"

#ifdef TOOLS_ENABLED

uint32_t FSBytecodeExporter::StringTable::insert(const String &p_string) {
	HashMap<String, uint32_t>::ConstIterator existing = indices.find(p_string);
	if (existing) {
		return existing->value;
	}
	const uint32_t index = (uint32_t)strings.size();
	indices.insert(p_string, index);
	strings.push_back(p_string);
	return index;
}

void FSBytecodeExporter::StringTable::write(StreamPeerBuffer *r_stream) const {
	r_stream->put_u32((uint32_t)strings.size());
	for (const String &string : strings) {
		r_stream->put_utf8_string(string);
	}
}

Vector<uint8_t> FSBytecodeExporter::write_header() {
	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	stream->put_data(FSBytecodeFormat::MAGIC, sizeof(FSBytecodeFormat::MAGIC));
	stream->put_u32(FSBytecodeFormat::FORMAT_VERSION);
	// Engine guard: a `.fsb` bakes validated pointers and opcode layouts of the exporting build, so
	// the loader refuses anything not produced by the exact same engine build.
	stream->put_utf8_string(VERSION_FULL_CONFIG);
	stream->put_utf8_string(VERSION_HASH);
	stream->put_u8((uint8_t)sizeof(real_t));
	stream->put_u32((uint32_t)FSFunction::OPCODE_END);
	stream->put_u32((uint32_t)FSFunction::OPCODE_END + 1);
	return stream->get_data_array();
}

Error FSBytecodeExporter::encode_variant_tagged(StreamPeerBuffer *r_stream, const Variant &p_variant, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			"Variant is too deeply nested to serialize to compiled bytecode (potential cycle).");
	switch (p_variant.get_type()) {
		case Variant::ARRAY: {
			const Array array = p_variant;
			r_stream->put_u8(FSBytecodeFormat::TAG_ARRAY);
			Error error = _encode_container_type(r_stream, array.get_element_type(), p_depth + 1);
			if (error != OK) {
				return error;
			}
			r_stream->put_u32((uint32_t)array.size());
			for (int i = 0; i < array.size(); i++) {
				error = encode_variant_tagged(r_stream, array[i], p_depth + 1);
				if (error != OK) {
					return error;
				}
			}
			return OK;
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_variant;
			r_stream->put_u8(FSBytecodeFormat::TAG_DICTIONARY);
			Error error = _encode_container_type(r_stream, dictionary.get_key_type(), p_depth + 1);
			if (error != OK) {
				return error;
			}
			error = _encode_container_type(r_stream, dictionary.get_value_type(), p_depth + 1);
			if (error != OK) {
				return error;
			}
			r_stream->put_u32((uint32_t)dictionary.size());
			for (const KeyValue<Variant, Variant> &element : dictionary) {
				error = encode_variant_tagged(r_stream, element.key, p_depth + 1);
				if (error != OK) {
					return error;
				}
				error = encode_variant_tagged(r_stream, element.value, p_depth + 1);
				if (error != OK) {
					return error;
				}
			}
			return OK;
		} break;
		case Variant::OBJECT: {
			bool previously_freed = false;
			Object *object = p_variant.get_validated_object_with_check(previously_freed);
			ERR_FAIL_COND_V_MSG(previously_freed, ERR_INVALID_PARAMETER,
					"A freed Object cannot be serialized to compiled bytecode.");
			if (object == nullptr) {
				// Object-typed null occurs legitimately, e.g. the script slot of the typed-container
				// descriptors the codegen bakes into constant pools; preserve it exactly.
				r_stream->put_u8(FSBytecodeFormat::TAG_NULL_OBJECT);
				return OK;
			}
			return _encode_object(r_stream, object, p_depth);
		} break;
		// These hold process-local identities (ObjectIDs, server handles); encode_variant would
		// emit meaningless bytes, so they are rejected outright.
		case Variant::CALLABLE: {
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "A Callable cannot be serialized to compiled bytecode.");
		} break;
		case Variant::SIGNAL: {
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "A Signal cannot be serialized to compiled bytecode.");
		} break;
		case Variant::RID: {
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "An RID cannot be serialized to compiled bytecode.");
		} break;
		default: {
			r_stream->put_u8(FSBytecodeFormat::TAG_INLINE_VARIANT);
			// Never pass p_full_objects = true here: it would property-dump objects, including a
			// preloaded script's source. Objects and containers never reach this leaf encoding.
			r_stream->put_var(p_variant, false);
			return OK;
		} break;
	}
}

Error FSBytecodeExporter::_encode_object(StreamPeerBuffer *r_stream, Object *p_object, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			"Object reference is too deeply nested to serialize to compiled bytecode.");
	if (Script *script = Object::cast_to<Script>(p_object)) {
		r_stream->put_u8(Object::cast_to<FoundryScript>(p_object) != nullptr
						? FSBytecodeFormat::TAG_SCRIPT_REF
						: FSBytecodeFormat::TAG_EXTERNAL_SCRIPT);
		return _encode_script_reference(r_stream, script);
	}
	if (FSNativeClass *native_class = Object::cast_to<FSNativeClass>(p_object)) {
		r_stream->put_u8(FSBytecodeFormat::TAG_NATIVE_CLASS);
		r_stream->put_u32(string_table.insert(native_class->get_name()));
		return OK;
	}
	if (FSSpecializedClassHandle *specialized_handle = Object::cast_to<FSSpecializedClassHandle>(p_object)) {
		const Ref<FoundryScript> &specialized_script = specialized_handle->get_specialized_script();
		ERR_FAIL_COND_V_MSG(specialized_script.is_null(), ERR_INVALID_PARAMETER,
				"A specialized class handle without a script cannot be serialized to compiled bytecode.");
		r_stream->put_u8(FSBytecodeFormat::TAG_SPECIALIZED_HANDLE);
		Error error = _encode_object(r_stream, specialized_script.ptr(), p_depth + 1);
		if (error != OK) {
			return error;
		}
		const Vector<ContainerType> &type_arguments = specialized_handle->get_type_arguments();
		r_stream->put_u32((uint32_t)type_arguments.size());
		for (const ContainerType &type_argument : type_arguments) {
			error = _encode_container_type(r_stream, type_argument, p_depth + 1);
			if (error != OK) {
				return error;
			}
		}
		return OK;
	}
	if (Resource *resource = Object::cast_to<Resource>(p_object)) {
		const String path = resource->get_path();
		ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_INVALID_PARAMETER,
				vformat("A resource of type '%s' without a resource path cannot be serialized to compiled bytecode.",
						resource->get_class()));
		// Only the path travels; the resource itself is reloaded at link time, so none of its
		// property data (which may include script source) reaches the buffer.
		r_stream->put_u8(FSBytecodeFormat::TAG_EXTERNAL_RESOURCE);
		r_stream->put_u32(string_table.insert(path));
		_record_external_dependency(path);
		return OK;
	}
	List<Engine::Singleton> singletons;
	Engine::get_singleton()->get_singletons(&singletons);
	for (const Engine::Singleton &singleton : singletons) {
		if (singleton.ptr == p_object) {
			r_stream->put_u8(FSBytecodeFormat::TAG_ENGINE_SINGLETON);
			r_stream->put_u32(string_table.insert(singleton.name));
			return OK;
		}
	}
	ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER,
			vformat("An object of type '%s' cannot be serialized to compiled bytecode.", p_object->get_class()));
}

Error FSBytecodeExporter::_encode_container_type(StreamPeerBuffer *r_stream, const ContainerType &p_container_type, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			"Container type is too deeply nested to serialize to compiled bytecode.");
	r_stream->put_u32((uint32_t)p_container_type.builtin_type);
	r_stream->put_u32(string_table.insert(p_container_type.class_name));
	if (p_container_type.script.is_valid()) {
		r_stream->put_u8(1);
		Error error = _encode_object(r_stream, p_container_type.script.ptr(), p_depth + 1);
		if (error != OK) {
			return error;
		}
	} else {
		r_stream->put_u8(0);
	}
	r_stream->put_u32((uint32_t)p_container_type.element_types.size());
	for (const ContainerType &element_type : p_container_type.element_types) {
		const Error error = _encode_container_type(r_stream, element_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	r_stream->put_u32((uint32_t)p_container_type.type_arguments.size());
	for (const ContainerType &type_argument : p_container_type.type_arguments) {
		const Error error = _encode_container_type(r_stream, type_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

// Writes a script identity. This is the single place that decides how a script reference is
// spelled in a `.fsb`: a class local to the script being serialized (the root or one of its nested
// subclasses) travels as its preorder class index, which the loader links to the already
// instantiated skeleton script without any I/O; everything else travels as string-table indices
// for (path, fully qualified class name), re-resolved through the loader's external resolver.
Error FSBytecodeExporter::_encode_script_reference(StreamPeerBuffer *r_stream, Script *p_script) {
	const uint32_t *local_class_index = local_class_indices.getptr(p_script);
	if (local_class_index != nullptr) {
		r_stream->put_u8(1);
		r_stream->put_u32(*local_class_index);
		return OK;
	}
	r_stream->put_u8(0);
	String path = p_script->get_path();
	String fully_qualified_name;
	if (FoundryScript *foundry_script = Object::cast_to<FoundryScript>(p_script)) {
		// Inner classes share the root script's path but only carry it in the script-local path
		// member, so the resource path alone is not enough here.
		path = foundry_script->get_script_path();
		fully_qualified_name = foundry_script->get_fully_qualified_name();
	}
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_INVALID_PARAMETER,
			vformat("A script of type '%s' without a resource path cannot be serialized to compiled bytecode.",
					p_script->get_class()));
	r_stream->put_u32(string_table.insert(path));
	r_stream->put_u32(string_table.insert(fully_qualified_name));
	_record_external_dependency(path);
	return OK;
}

Error FSBytecodeExporter::encode_data_type(StreamPeerBuffer *r_stream, const FSDataType &p_data_type, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			"Data type is too deeply nested to serialize to compiled bytecode.");
	r_stream->put_u8((uint8_t)p_data_type.kind);
	r_stream->put_u32((uint32_t)p_data_type.builtin_type);
	r_stream->put_u32(string_table.insert(p_data_type.native_type));
	uint8_t flags = 0;
	if (p_data_type.is_nullable) {
		flags |= 1 << 0;
	}
	if (p_data_type.is_type_handle) {
		flags |= 1 << 1;
	}
	if (p_data_type.is_self_type) {
		flags |= 1 << 2;
	}
	if (p_data_type.is_script_trait) {
		flags |= 1 << 3;
	}
	r_stream->put_u8(flags);
	r_stream->put_u32(string_table.insert(p_data_type.script_trait));
	r_stream->put_u32(string_table.insert(p_data_type.type_parameter_name));
	r_stream->put_32(p_data_type.type_parameter_index);
	r_stream->put_u8((uint8_t)p_data_type.type_parameter_scope);
	// Script identity travels as (path, fully qualified class name), never as a pointer or a
	// property dump; the loader re-resolves it through its external resolver.
	Script *script = p_data_type.script_type_ref.is_valid() ? p_data_type.script_type_ref.ptr() : p_data_type.script_type;
	if (script != nullptr) {
		r_stream->put_u8(1);
		const Error error = _encode_script_reference(r_stream, script);
		if (error != OK) {
			return error;
		}
	} else {
		r_stream->put_u8(0);
	}
	r_stream->put_u32((uint32_t)p_data_type.container_element_types.size());
	for (const FSDataType &element_type : p_data_type.container_element_types) {
		const Error error = encode_data_type(r_stream, element_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	r_stream->put_u32((uint32_t)p_data_type.type_arguments.size());
	for (const FSDataType &type_argument : p_data_type.type_arguments) {
		const Error error = encode_data_type(r_stream, type_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

void FSBytecodeExporter::_encode_property_info(StreamPeerBuffer *r_stream, const PropertyInfo &p_property_info) {
	r_stream->put_u32((uint32_t)p_property_info.type);
	r_stream->put_u32(string_table.insert(p_property_info.name));
	r_stream->put_u32(string_table.insert(p_property_info.class_name));
	r_stream->put_u32((uint32_t)p_property_info.hint);
	r_stream->put_u32(string_table.insert(p_property_info.hint_string));
	r_stream->put_u32(p_property_info.usage);
}

Error FSBytecodeExporter::_encode_method_info(StreamPeerBuffer *r_stream, const MethodInfo &p_method_info, int p_depth) {
	r_stream->put_u32(string_table.insert(p_method_info.name));
	_encode_property_info(r_stream, p_method_info.return_val);
	r_stream->put_u32(p_method_info.flags);
	r_stream->put_32(p_method_info.id);
	r_stream->put_u32((uint32_t)p_method_info.arguments.size());
	for (const PropertyInfo &argument : p_method_info.arguments) {
		_encode_property_info(r_stream, argument);
	}
	r_stream->put_u32((uint32_t)p_method_info.default_arguments.size());
	for (const Variant &default_argument : p_method_info.default_arguments) {
		const Error error = encode_variant_tagged(r_stream, default_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	r_stream->put_32(p_method_info.return_val_metadata);
	r_stream->put_u32((uint32_t)p_method_info.arguments_metadata.size());
	for (const int argument_metadata : p_method_info.arguments_metadata) {
		r_stream->put_32(argument_metadata);
	}
	return OK;
}

Error FSBytecodeExporter::serialize_function(StreamPeerBuffer *r_stream, const FSFunction *p_function, int p_depth) {
	ERR_FAIL_NULL_V(p_function, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			vformat("Lambdas of compiled function '%s' are too deeply nested to serialize to compiled bytecode.",
					p_function->name));

	const FSFunction::ExportFixups &fixups = p_function->export_fixups;
	// Serialization replaces every pointer table with the symbolic keys the codegen recorded; a
	// size mismatch means this function was not produced by FSByteCodeGenerator in this process.
	const bool fixups_cover_tables =
			fixups.operators.size() == p_function->operator_funcs.size() &&
			fixups.setters.size() == p_function->setters.size() &&
			fixups.getters.size() == p_function->getters.size() &&
			fixups.keyed_setters.size() == p_function->keyed_setters.size() &&
			fixups.keyed_getters.size() == p_function->keyed_getters.size() &&
			fixups.indexed_setters.size() == p_function->indexed_setters.size() &&
			fixups.indexed_getters.size() == p_function->indexed_getters.size() &&
			fixups.builtin_methods.size() == p_function->builtin_methods.size() &&
			fixups.constructors.size() == p_function->constructors.size() &&
			fixups.utilities.size() == p_function->utilities.size() &&
			fixups.gds_utilities.size() == p_function->gds_utilities.size() &&
			fixups.method_binds.size() == p_function->methods.size();
	ERR_FAIL_COND_V_MSG(!fixups_cover_tables, ERR_INVALID_PARAMETER,
			vformat("Cannot serialize compiled function '%s' of script '%s': its fixup descriptors do not cover its pointer tables.",
					p_function->name, p_function->source));

	r_stream->put_u32(string_table.insert(p_function->name));
	uint8_t function_flags = 0;
	if (p_function->_static) {
		function_flags |= 1 << 0;
	}
	r_stream->put_u8(function_flags);
	r_stream->put_32(p_function->_initial_line);
	r_stream->put_32(p_function->_argument_count);
	r_stream->put_32(p_function->_vararg_index);
	r_stream->put_32(p_function->_stack_size);
	r_stream->put_32(p_function->_instruction_args_size);

	r_stream->put_u32((uint32_t)p_function->argument_types.size());
	for (const FSDataType &argument_type : p_function->argument_types) {
		const Error error = encode_data_type(r_stream, argument_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	Error error = encode_data_type(r_stream, p_function->return_type, p_depth + 1);
	if (error != OK) {
		return error;
	}
	error = _encode_method_info(r_stream, p_function->method_info, p_depth + 1);
	if (error != OK) {
		return error;
	}
	error = encode_variant_tagged(r_stream, p_function->rpc_config, p_depth + 1);
	if (error != OK) {
		return error;
	}

	r_stream->put_u32((uint32_t)p_function->temporary_slots.size());
	for (const KeyValue<int, Variant::Type> &temporary_slot : p_function->temporary_slots) {
		r_stream->put_32(temporary_slot.key);
		r_stream->put_u32((uint32_t)temporary_slot.value);
	}

	// `OPCODE_STORE_GLOBAL` operands bake this process's global-array index, which is meaningless
	// in another build; mask them out so the loader has to rebake every one from its name.
	HashSet<int> masked_code_offsets;
	for (const FSFunction::ExportFixups::GlobalStore &global_store : fixups.global_stores) {
		ERR_FAIL_INDEX_V_MSG(global_store.code_offset, p_function->code.size(), ERR_INVALID_PARAMETER,
				vformat("Cannot serialize compiled function '%s' of script '%s': store-global fixup offset is out of code bounds.",
						p_function->name, p_function->source));
		masked_code_offsets.insert(global_store.code_offset);
	}
	r_stream->put_u32((uint32_t)p_function->code.size());
	for (int i = 0; i < p_function->code.size(); i++) {
		r_stream->put_32(masked_code_offsets.has(i) ? 0 : p_function->code[i]);
	}

	r_stream->put_u32((uint32_t)p_function->default_arguments.size());
	for (const int default_argument_offset : p_function->default_arguments) {
		r_stream->put_32(default_argument_offset);
	}

	r_stream->put_u32((uint32_t)p_function->constants.size());
	for (const Variant &constant : p_function->constants) {
		error = encode_variant_tagged(r_stream, constant, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_function->global_names.size());
	for (const StringName &global_name : p_function->global_names) {
		r_stream->put_u32(string_table.insert(global_name));
	}

	r_stream->put_u32((uint32_t)p_function->builtin_method_names.size());
	for (const StringName &builtin_method_name : p_function->builtin_method_names) {
		r_stream->put_u32(string_table.insert(builtin_method_name));
	}

	r_stream->put_u32((uint32_t)fixups.operators.size());
	for (const FSFunction::ExportFixups::OperatorKey &key : fixups.operators) {
		r_stream->put_u32((uint32_t)key.op);
		r_stream->put_u32((uint32_t)key.left_type);
		r_stream->put_u32((uint32_t)key.right_type);
	}

	r_stream->put_u32((uint32_t)fixups.setters.size());
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.setters) {
		r_stream->put_u32((uint32_t)key.type);
		r_stream->put_u32(string_table.insert(key.name));
	}

	r_stream->put_u32((uint32_t)fixups.getters.size());
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.getters) {
		r_stream->put_u32((uint32_t)key.type);
		r_stream->put_u32(string_table.insert(key.name));
	}

	r_stream->put_u32((uint32_t)fixups.keyed_setters.size());
	for (const Variant::Type type : fixups.keyed_setters) {
		r_stream->put_u32((uint32_t)type);
	}

	r_stream->put_u32((uint32_t)fixups.keyed_getters.size());
	for (const Variant::Type type : fixups.keyed_getters) {
		r_stream->put_u32((uint32_t)type);
	}

	r_stream->put_u32((uint32_t)fixups.indexed_setters.size());
	for (const Variant::Type type : fixups.indexed_setters) {
		r_stream->put_u32((uint32_t)type);
	}

	r_stream->put_u32((uint32_t)fixups.indexed_getters.size());
	for (const Variant::Type type : fixups.indexed_getters) {
		r_stream->put_u32((uint32_t)type);
	}

	r_stream->put_u32((uint32_t)fixups.builtin_methods.size());
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.builtin_methods) {
		r_stream->put_u32((uint32_t)key.type);
		r_stream->put_u32(string_table.insert(key.name));
	}

	r_stream->put_u32((uint32_t)fixups.constructors.size());
	for (const FSFunction::ExportFixups::ConstructorKey &key : fixups.constructors) {
		r_stream->put_u32((uint32_t)key.type);
		r_stream->put_32(key.constructor_index);
		// The argument count pins the constructor's signature, so the loader can reject an index
		// that silently came to mean a different overload.
		r_stream->put_32(Variant::get_constructor_argument_count((Variant::Type)key.type, key.constructor_index));
	}

	r_stream->put_u32((uint32_t)fixups.utilities.size());
	for (const StringName &utility_name : fixups.utilities) {
		r_stream->put_u32(string_table.insert(utility_name));
	}

	r_stream->put_u32((uint32_t)fixups.gds_utilities.size());
	for (const StringName &utility_name : fixups.gds_utilities) {
		r_stream->put_u32(string_table.insert(utility_name));
	}

	r_stream->put_u32((uint32_t)fixups.method_binds.size());
	for (const FSFunction::ExportFixups::MethodBindKey &key : fixups.method_binds) {
		r_stream->put_u32(string_table.insert(key.class_name));
		r_stream->put_u32(string_table.insert(key.method_name));
	}

	r_stream->put_u32((uint32_t)fixups.global_stores.size());
	for (const FSFunction::ExportFixups::GlobalStore &global_store : fixups.global_stores) {
		r_stream->put_32(global_store.code_offset);
		r_stream->put_u32(string_table.insert(global_store.global_name));
	}

	// `named_globals` is deliberately not serialized: it exists for export-time validation of
	// OPCODE_STORE_NAMED_GLOBAL names, which dispatch by name at runtime and need no relinking.

	ERR_FAIL_COND_V_MSG(!p_function->lambdas.is_empty() && p_function->_script == nullptr, ERR_INVALID_PARAMETER,
			vformat("Cannot serialize compiled function '%s': it has lambdas but no owning script.", p_function->name));
	r_stream->put_u32((uint32_t)p_function->lambdas.size());
	for (FSFunction *lambda : p_function->lambdas) {
		// Each lambda pointer appears in exactly one parent's table (the codegen keys them by
		// pointer per function and lambdas belong to their lexical parent), so the depth-first
		// recursion serializes every lambda exactly once.
		const FoundryScript::LambdaInfo *lambda_info = p_function->_script->get_lambda_info().getptr(lambda);
		ERR_FAIL_NULL_V_MSG(lambda_info, ERR_INVALID_PARAMETER,
				vformat("Cannot serialize compiled function '%s' of script '%s': its owning script has no lambda info for lambda '%s'.",
						p_function->name, p_function->source, lambda->name));
		r_stream->put_32(lambda_info->capture_count);
		r_stream->put_u8(lambda_info->use_self ? 1 : 0);
		error = serialize_function(r_stream, lambda, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}

	return OK;
}

void FSBytecodeExporter::_record_external_dependency(const String &p_path) {
	if (external_dependency_set.has(p_path)) {
		return;
	}
	external_dependency_set.insert(p_path);
	external_dependencies.push_back(p_path);
}

void FSBytecodeExporter::_index_local_classes(const FoundryScript *p_class) {
	local_class_indices.insert(p_class, (uint32_t)local_class_indices.size());
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		_index_local_classes(subclass.value.ptr());
	}
}

// A class or a subclass tree carries static data when anything in it declared static variables or
// needed a static initializer; this is the post-compile equivalent of the parse-tree scan
// `FSCompiler::compile` uses to decide whether the script must be pinned by the static cache.
static bool fsb_script_tree_has_static_data(const FoundryScript *p_class) {
	if (p_class->get_static_initializer() != nullptr) {
		return true;
	}
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->get_subclasses()) {
		if (fsb_script_tree_has_static_data(subclass.value.ptr())) {
			return true;
		}
	}
	return false;
}

Error FSBytecodeExporter::serialize(const Ref<FoundryScript> &p_script, Vector<uint8_t> &r_buffer, bool p_annotated_static_unload) {
	r_buffer.clear();
	ERR_FAIL_COND_V(p_script.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(!p_script->valid, ERR_INVALID_PARAMETER,
			vformat("Cannot serialize invalid script '%s' to compiled bytecode.", p_script->get_script_path()));
	ERR_FAIL_COND_V_MSG(!p_script->is_root_script(), ERR_INVALID_PARAMETER,
			vformat("Only a root script can be serialized to compiled bytecode; '%s' is an inner class.",
					p_script->fully_qualified_name));

	local_class_indices.clear();
	external_dependencies.clear();
	external_dependency_set.clear();
	_index_local_classes(p_script.ptr());

	// Sections are encoded to a scratch buffer first: encoding discovers the string table entries,
	// and the table must precede the sections in the final buffer.
	Ref<StreamPeerBuffer> sections;
	sections.instantiate();

	sections->put_u32(FSBytecodeFormat::SECTION_SKELETON);
	Error error = _write_skeleton_class(sections.ptr(), p_script.ptr(), 0);
	if (error != OK) {
		return error;
	}

	sections->put_u32(FSBytecodeFormat::SECTION_CLASS_BODIES);
	error = _write_class_bodies(sections.ptr(), p_script.ptr(), 0);
	if (error != OK) {
		return error;
	}

	sections->put_u32(FSBytecodeFormat::SECTION_WITNESSES);
	error = _write_witness_section(sections.ptr(), p_script.ptr());
	if (error != OK) {
		return error;
	}

	// The dependency section is assembled after the sections above discovered every external path,
	// but is spliced in ahead of them so it can be read without touching class data.
	Ref<StreamPeerBuffer> dependency_section;
	dependency_section.instantiate();
	dependency_section->put_u32(FSBytecodeFormat::SECTION_DEPENDENCIES);
	dependency_section->put_u32((uint32_t)external_dependencies.size());
	for (const String &dependency_path : external_dependencies) {
		dependency_section->put_u32(string_table.insert(dependency_path));
	}

	Ref<StreamPeerBuffer> output;
	output.instantiate();
	const Vector<uint8_t> header = write_header();
	output->put_data(header.ptr(), header.size());
	uint32_t script_flags = 0;
	if (p_script->tool) {
		script_flags |= 1 << 0;
	}
	if (fsb_script_tree_has_static_data(p_script.ptr())) {
		script_flags |= 1 << 1;
	}
	if (p_annotated_static_unload) {
		script_flags |= 1 << 2;
	}
	output->put_u32(script_flags);
	output->put_u32(FSBytecodeFormat::SECTION_STRING_TABLE);
	string_table.write(output.ptr());
	const Vector<uint8_t> dependency_bytes = dependency_section->get_data_array();
	output->put_data(dependency_bytes.ptr(), dependency_bytes.size());
	const Vector<uint8_t> section_bytes = sections->get_data_array();
	output->put_data(section_bytes.ptr(), section_bytes.size());
	r_buffer = output->get_data_array();
	return OK;
}

// The `.fsb` analog of `FSCompiler::make_scripts`: everything needed to instantiate the class
// tree and answer identity queries, readable without touching the class bodies.
Error FSBytecodeExporter::_write_skeleton_class(StreamPeerBuffer *r_stream, const FoundryScript *p_class, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER,
			vformat("Inner classes of script '%s' are too deeply nested to serialize to compiled bytecode.",
					p_class->get_script_path()));

	r_stream->put_u32(string_table.insert(p_class->fully_qualified_name));
	r_stream->put_u32(string_table.insert(p_class->local_name));
	r_stream->put_u32(string_table.insert(p_class->global_name));
	r_stream->put_u32(string_table.insert(p_class->simplified_icon_path));
	ERR_FAIL_COND_V_MSG(p_class->native.is_null(), ERR_INVALID_PARAMETER,
			vformat("Cannot serialize class '%s' of script '%s' to compiled bytecode: it has no native base class.",
					p_class->fully_qualified_name, p_class->get_script_path()));
	r_stream->put_u32(string_table.insert(p_class->native->get_name()));
	uint8_t class_flags = 0;
	if (p_class->tool) {
		class_flags |= 1 << 0;
	}
	if (p_class->_is_abstract) {
		class_flags |= 1 << 1;
	}
	if (p_class->_is_final) {
		class_flags |= 1 << 2;
	}
	if (p_class->_is_trait_type) {
		class_flags |= 1 << 3;
	}
	r_stream->put_u8(class_flags);
	r_stream->put_u32(string_table.insert(p_class->trait_type_name));

	if (p_class->base.is_valid()) {
		r_stream->put_u8(1);
		const Error error = _encode_script_reference(r_stream, p_class->base.ptr());
		if (error != OK) {
			return error;
		}
	} else {
		r_stream->put_u8(0);
	}

	r_stream->put_u32((uint32_t)p_class->subclasses.size());
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		r_stream->put_u32(string_table.insert(subclass.key));
		const Error error = _write_skeleton_class(r_stream, subclass.value.ptr(), p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeExporter::_write_class_bodies(StreamPeerBuffer *r_stream, const FoundryScript *p_class, int p_depth) {
	ERR_FAIL_COND_V(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_PARAMETER);
	Error error = _write_class_body(r_stream, p_class);
	if (error != OK) {
		return error;
	}
	// Bodies follow the same preorder as the skeleton so the loader pairs them by position.
	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_class->subclasses) {
		error = _write_class_bodies(r_stream, subclass.value.ptr(), p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeExporter::_write_member_info(StreamPeerBuffer *r_stream, const StringName &p_name, const FoundryScript::MemberInfo &p_member_info) {
	r_stream->put_u32(string_table.insert(p_name));
	r_stream->put_32(p_member_info.index);
	r_stream->put_u32(string_table.insert(p_member_info.setter));
	r_stream->put_u32(string_table.insert(p_member_info.getter));
	Error error = encode_data_type(r_stream, p_member_info.data_type);
	if (error != OK) {
		return error;
	}
	_encode_property_info(r_stream, p_member_info.property_info);
	return _write_type_argument_binding(r_stream, p_member_info.type_argument_binding);
}

Error FSBytecodeExporter::_write_type_argument_binding(StreamPeerBuffer *r_stream, const FoundryScript::TypeArgumentBinding &p_binding) {
	r_stream->put_u8((uint8_t)p_binding.kind);
	uint8_t binding_flags = 0;
	if (p_binding.fixed_is_dependent) {
		binding_flags |= 1 << 0;
	}
	if (p_binding.is_type_handle) {
		binding_flags |= 1 << 1;
	}
	r_stream->put_u8(binding_flags);
	r_stream->put_32(p_binding.leaf_ordinal);
	return encode_data_type(r_stream, p_binding.fixed);
}

Error FSBytecodeExporter::_write_annotation_usages(StreamPeerBuffer *r_stream, const Vector<FoundryScript::AnnotationUsage> &p_usages) {
	r_stream->put_u32((uint32_t)p_usages.size());
	for (const FoundryScript::AnnotationUsage &usage : p_usages) {
		r_stream->put_u32(string_table.insert(usage.name));
		r_stream->put_u32(string_table.insert(usage.qualified_name));
		r_stream->put_u8(usage.is_builtin ? 1 : 0);
		Error error = encode_variant_tagged(r_stream, usage.args);
		if (error != OK) {
			return error;
		}
		error = encode_variant_tagged(r_stream, usage.kwargs);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeExporter::_write_annotation_usage_map(StreamPeerBuffer *r_stream, const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &p_annotation_map) {
	r_stream->put_u32((uint32_t)p_annotation_map.size());
	for (const KeyValue<StringName, Vector<FoundryScript::AnnotationUsage>> &entry : p_annotation_map) {
		r_stream->put_u32(string_table.insert(entry.key));
		const Error error = _write_annotation_usages(r_stream, entry.value);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeExporter::_write_parameter_annotation_map(StreamPeerBuffer *r_stream, const HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &p_parameter_map) {
	r_stream->put_u32((uint32_t)p_parameter_map.size());
	for (const KeyValue<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &entry : p_parameter_map) {
		r_stream->put_u32(string_table.insert(entry.key));
		const Error error = _write_annotation_usage_map(r_stream, entry.value);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeExporter::_write_optional_function(StreamPeerBuffer *r_stream, const FSFunction *p_function) {
	if (p_function == nullptr) {
		r_stream->put_u8(0);
		return OK;
	}
	r_stream->put_u8(1);
	return serialize_function(r_stream, p_function);
}

Error FSBytecodeExporter::_write_class_body(StreamPeerBuffer *r_stream, const FoundryScript *p_class) {
	// Members are serialized post-compile, so `member_indices` already includes the flattened base
	// members; the loader never recomputes inheritance.
	r_stream->put_u32((uint32_t)p_class->member_indices.size());
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &member : p_class->member_indices) {
		const Error error = _write_member_info(r_stream, member.key, member.value);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_class->members.size());
	for (const StringName &member_name : p_class->members) {
		r_stream->put_u32(string_table.insert(member_name));
	}

	r_stream->put_u32((uint32_t)p_class->static_variables_indices.size());
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &static_variable : p_class->static_variables_indices) {
		const Error error = _write_member_info(r_stream, static_variable.key, static_variable.value);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_class->constants.size());
	for (const KeyValue<StringName, Variant> &constant : p_class->constants) {
		r_stream->put_u32(string_table.insert(constant.key));
		const Error error = encode_variant_tagged(r_stream, constant.value);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_class->_signals.size());
	for (const KeyValue<StringName, MethodInfo> &signal_entry : p_class->_signals) {
		r_stream->put_u32(string_table.insert(signal_entry.key));
		const Error error = _encode_method_info(r_stream, signal_entry.value, 0);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_class->script_trait_list.size());
	for (const StringName &trait_name : p_class->script_trait_list) {
		r_stream->put_u32(string_table.insert(trait_name));
	}

	r_stream->put_u32((uint32_t)p_class->abstract_trait_requirements.size());
	for (const KeyValue<StringName, FoundryScript::AbstractTraitRequirement> &requirement : p_class->abstract_trait_requirements) {
		r_stream->put_u32(string_table.insert(requirement.key));
		Error error = encode_data_type(r_stream, requirement.value.return_type);
		if (error != OK) {
			return error;
		}
		error = _encode_method_info(r_stream, requirement.value.method_info, 0);
		if (error != OK) {
			return error;
		}
	}

	r_stream->put_u32((uint32_t)p_class->type_parameters.size());
	for (const FoundryScript::TypeParameter &type_parameter : p_class->type_parameters) {
		r_stream->put_u32(string_table.insert(type_parameter.name));
		r_stream->put_32(type_parameter.index);
		r_stream->put_u8(type_parameter.has_bound ? 1 : 0);
		_encode_property_info(r_stream, type_parameter.bound);
	}

	// Ancestor keys are serialized as script references (class index or (path, fqcn)); the loader
	// re-keys the table onto the live scripts it resolves.
	r_stream->put_u32((uint32_t)p_class->type_parameter_bindings_by_ancestor.size());
	for (const KeyValue<FoundryScript *, Vector<FoundryScript::TypeArgumentBinding>> &ancestor_entry : p_class->type_parameter_bindings_by_ancestor) {
		Error error = _encode_script_reference(r_stream, ancestor_entry.key);
		if (error != OK) {
			return error;
		}
		r_stream->put_u32((uint32_t)ancestor_entry.value.size());
		for (const FoundryScript::TypeArgumentBinding &binding : ancestor_entry.value) {
			error = _write_type_argument_binding(r_stream, binding);
			if (error != OK) {
				return error;
			}
		}
	}

	// `rpc_config` is stored post-merge (it already contains inherited entries), so the loader
	// takes it verbatim.
	Error error = encode_variant_tagged(r_stream, p_class->rpc_config);
	if (error != OK) {
		return error;
	}

	error = _write_annotation_usages(r_stream, p_class->class_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_annotation_usage_map(r_stream, p_class->method_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_annotation_usage_map(r_stream, p_class->variable_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_annotation_usage_map(r_stream, p_class->signal_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_annotation_usage_map(r_stream, p_class->constant_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_parameter_annotation_map(r_stream, p_class->method_parameter_annotations);
	if (error != OK) {
		return error;
	}
	error = _write_parameter_annotation_map(r_stream, p_class->signal_parameter_annotations);
	if (error != OK) {
		return error;
	}

	r_stream->put_u32((uint32_t)p_class->member_functions.size());
	for (const KeyValue<StringName, FSFunction *> &member_function : p_class->member_functions) {
		r_stream->put_u8(member_function.value == p_class->initializer ? 1 : 0);
		error = serialize_function(r_stream, member_function.value);
		if (error != OK) {
			return error;
		}
	}

	error = _write_optional_function(r_stream, p_class->implicit_initializer);
	if (error != OK) {
		return error;
	}
	error = _write_optional_function(r_stream, p_class->implicit_ready);
	if (error != OK) {
		return error;
	}
	return _write_optional_function(r_stream, p_class->static_initializer);
}

// Serializes the retroactive-conformance witnesses this script registered, per the entries
// `FSCompiler::_compile_conformance_witnesses` recorded in the conformance registry. Each entry's
// target script identity travels alongside its witness functions because those functions were
// compiled against the target's member layout and must be re-owned by it at load.
Error FSBytecodeExporter::_write_witness_section(StreamPeerBuffer *r_stream, const FoundryScript *p_script) {
	Vector<FSConformanceRegistry::RuntimeConformance> conformances;
	if (!p_script->registered_conformance_source.is_empty()) {
		conformances = FSConformanceRegistry::get_singleton()->get_runtime_witnesses(p_script->registered_conformance_source);
	}
	r_stream->put_u32((uint32_t)conformances.size());
	for (const FSConformanceRegistry::RuntimeConformance &conformance : conformances) {
		ERR_FAIL_COND_V_MSG(conformance.functions.is_empty(), ERR_INVALID_PARAMETER,
				vformat("Cannot serialize script '%s' to compiled bytecode: it registered a conformance without witness functions.",
						p_script->get_script_path()));
		FoundryScript *target_script = conformance.functions.begin()->value->_script;
		ERR_FAIL_NULL_V_MSG(target_script, ERR_INVALID_PARAMETER,
				vformat("Cannot serialize script '%s' to compiled bytecode: a conformance witness has no target script.",
						p_script->get_script_path()));
		Error error = _encode_script_reference(r_stream, target_script);
		if (error != OK) {
			return error;
		}
		r_stream->put_u32((uint32_t)conformance.target_keys.size());
		for (const String &target_key : conformance.target_keys) {
			r_stream->put_u32(string_table.insert(target_key));
		}
		r_stream->put_u32(string_table.insert(conformance.trait_name));
		r_stream->put_u32((uint32_t)conformance.functions.size());
		for (const KeyValue<StringName, FSFunction *> &witness : conformance.functions) {
			r_stream->put_u32(string_table.insert(witness.key));
			error = serialize_function(r_stream, witness.value);
			if (error != OK) {
				return error;
			}
		}
	}
	return OK;
}

#endif // TOOLS_ENABLED
