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

// Writes a script identity as string-table indices for (path, fully qualified class name). This is
// the single place that decides how a script reference is spelled in a `.fsb`; intra-file class
// indices will be added here when whole-script serialization lands.
Error FSBytecodeExporter::_encode_script_reference(StreamPeerBuffer *r_stream, Script *p_script) {
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

	r_stream->put_u32((uint32_t)p_function->lambdas.size());
	for (FSFunction *lambda : p_function->lambdas) {
		// Each lambda pointer appears in exactly one parent's table (the codegen keys them by
		// pointer per function and lambdas belong to their lexical parent), so the depth-first
		// recursion serializes every lambda exactly once.
		ERR_FAIL_NULL_V_MSG(p_function->_script, ERR_INVALID_PARAMETER,
				vformat("Cannot serialize compiled function '%s': it has lambdas but no owning script.", p_function->name));
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

#endif // TOOLS_ENABLED
