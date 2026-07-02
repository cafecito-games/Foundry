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
			Object *object = p_variant.get_validated_object();
			ERR_FAIL_NULL_V_MSG(object, ERR_INVALID_PARAMETER,
					"A null or freed Object cannot be serialized to compiled bytecode.");
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

#endif // TOOLS_ENABLED
