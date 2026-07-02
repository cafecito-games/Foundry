/**************************************************************************/
/*  fs_bytecode_loader.cpp                                                */
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

#include "fs_bytecode_loader.h"

#include "foundry_script.h"
#include "fs_function.h"

#include "core/config/engine.h"
#include "core/version.h"

// Reads a length-prefixed UTF-8 string, rejecting lengths the stream cannot hold so a corrupted
// prefix cannot trigger a huge allocation.
static Error read_bounded_utf8_string(StreamPeerBuffer *p_stream, String &r_string) {
	const uint32_t length = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)length > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated string in compiled script data.");
	r_string = p_stream->get_utf8_string(length);
	return OK;
}

Error FSBytecodeLoader::check_header(const Vector<uint8_t> &p_buffer, int *r_header_size) {
	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	stream->set_data_array(p_buffer);

	uint8_t magic[sizeof(FSBytecodeFormat::MAGIC)] = {};
	const Error magic_error = stream->get_data(magic, sizeof(magic));
	ERR_FAIL_COND_V_MSG(magic_error != OK || memcmp(magic, FSBytecodeFormat::MAGIC, sizeof(magic)) != 0,
			ERR_INVALID_DATA, "Not a compiled Foundry Script binary (bad magic).");

	const uint32_t format_version = stream->get_u32();
	ERR_FAIL_COND_V_MSG(format_version != FSBytecodeFormat::FORMAT_VERSION, ERR_INVALID_DATA,
			vformat("Unsupported compiled script format version %d (this build reads version %d).",
					format_version, FSBytecodeFormat::FORMAT_VERSION));

	String file_version_config;
	Error error = read_bounded_utf8_string(stream.ptr(), file_version_config);
	if (error != OK) {
		return error;
	}
	String file_version_hash;
	error = read_bounded_utf8_string(stream.ptr(), file_version_hash);
	if (error != OK) {
		return error;
	}
	const uint32_t file_real_t_size = stream->get_u8();
	const uint32_t file_opcode_end = stream->get_u32();
	const uint32_t file_opcode_count = stream->get_u32();

	const String runtime_version_config = VERSION_FULL_CONFIG;
	const String runtime_version_hash = VERSION_HASH;
	const bool guard_matches = file_version_config == runtime_version_config &&
			file_version_hash == runtime_version_hash &&
			file_real_t_size == (uint32_t)sizeof(real_t) &&
			file_opcode_end == (uint32_t)FSFunction::OPCODE_END &&
			file_opcode_count == (uint32_t)FSFunction::OPCODE_END + 1;
	ERR_FAIL_COND_V_MSG(!guard_matches, ERR_INVALID_DATA,
			vformat("Compiled script was exported by a different engine build: file has version \"%s\" hash \"%s\", this runtime is version \"%s\" hash \"%s\". Re-export the game with a matching engine build.",
					file_version_config, file_version_hash, runtime_version_config, runtime_version_hash));

	if (r_header_size != nullptr) {
		*r_header_size = stream->get_position();
	}
	return OK;
}

Error FSBytecodeLoader::read_string_table(StreamPeerBuffer *p_stream) {
	string_table.clear();
	const uint32_t count = p_stream->get_u32();
	// Every entry occupies at least its 4-byte length prefix; reject counts the stream cannot hold.
	ERR_FAIL_COND_V_MSG((int64_t)count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated string table in compiled script data.");
	for (uint32_t i = 0; i < count; i++) {
		String string;
		const Error error = read_bounded_utf8_string(p_stream, string);
		if (error != OK) {
			string_table.clear();
			return error;
		}
		string_table.push_back(string);
	}
	return OK;
}

String FSBytecodeLoader::get_string(uint32_t p_index) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, (uint32_t)string_table.size(), String());
	return string_table[p_index];
}

Error FSBytecodeLoader::_get_string(uint32_t p_index, String &r_string) const {
	ERR_FAIL_UNSIGNED_INDEX_V_MSG(p_index, (uint32_t)string_table.size(), ERR_INVALID_DATA,
			"String index out of range in compiled script data.");
	r_string = string_table[p_index];
	return OK;
}

Error FSBytecodeLoader::decode_variant_tagged(StreamPeerBuffer *p_stream, Variant &r_variant, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_DATA,
			"Variant is too deeply nested in compiled script data.");
	const uint8_t tag = p_stream->get_u8();
	switch (tag) {
		case FSBytecodeFormat::TAG_INLINE_VARIANT: {
			r_variant = p_stream->get_var(false);
			return OK;
		} break;
		case FSBytecodeFormat::TAG_ARRAY: {
			ContainerType element_type;
			Error error = _decode_container_type(p_stream, element_type, p_depth + 1);
			if (error != OK) {
				return error;
			}
			Array array;
			if (element_type.builtin_type != Variant::NIL || element_type.class_name != StringName() || element_type.script.is_valid()) {
				array.set_typed(element_type);
			}
			const uint32_t count = p_stream->get_u32();
			// Every element occupies at least its 1-byte tag.
			ERR_FAIL_COND_V_MSG((int64_t)count > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
					"Truncated array in compiled script data.");
			for (uint32_t i = 0; i < count; i++) {
				Variant element;
				error = decode_variant_tagged(p_stream, element, p_depth + 1);
				if (error != OK) {
					return error;
				}
				array.push_back(element);
			}
			r_variant = array;
			return OK;
		} break;
		case FSBytecodeFormat::TAG_DICTIONARY: {
			ContainerType key_type;
			Error error = _decode_container_type(p_stream, key_type, p_depth + 1);
			if (error != OK) {
				return error;
			}
			ContainerType value_type;
			error = _decode_container_type(p_stream, value_type, p_depth + 1);
			if (error != OK) {
				return error;
			}
			Dictionary dictionary;
			const bool key_typed = key_type.builtin_type != Variant::NIL || key_type.class_name != StringName() || key_type.script.is_valid();
			const bool value_typed = value_type.builtin_type != Variant::NIL || value_type.class_name != StringName() || value_type.script.is_valid();
			if (key_typed || value_typed) {
				dictionary.set_typed(key_type, value_type);
			}
			const uint32_t count = p_stream->get_u32();
			// Every entry occupies at least the key and value tags.
			ERR_FAIL_COND_V_MSG((int64_t)count * 2 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
					"Truncated dictionary in compiled script data.");
			for (uint32_t i = 0; i < count; i++) {
				Variant key;
				error = decode_variant_tagged(p_stream, key, p_depth + 1);
				if (error != OK) {
					return error;
				}
				Variant value;
				error = decode_variant_tagged(p_stream, value, p_depth + 1);
				if (error != OK) {
					return error;
				}
				dictionary[key] = value;
			}
			r_variant = dictionary;
			return OK;
		} break;
		default: {
			return _decode_object(p_stream, tag, r_variant, p_depth);
		} break;
	}
}

Error FSBytecodeLoader::_decode_object(StreamPeerBuffer *p_stream, uint8_t p_tag, Variant &r_variant, int p_depth) {
	switch (p_tag) {
		case FSBytecodeFormat::TAG_SCRIPT_REF:
		case FSBytecodeFormat::TAG_EXTERNAL_SCRIPT: {
			String path;
			Error error = _get_string(p_stream->get_u32(), path);
			if (error != OK) {
				return error;
			}
			String fully_qualified_name;
			if (p_tag == FSBytecodeFormat::TAG_SCRIPT_REF) {
				error = _get_string(p_stream->get_u32(), fully_qualified_name);
				if (error != OK) {
					return error;
				}
			}
			ERR_FAIL_NULL_V_MSG(resolver, ERR_UNCONFIGURED, "No external-reference resolver is set on the bytecode loader.");
			const Ref<Script> script = resolver->resolve_script(path, fully_qualified_name);
			ERR_FAIL_COND_V_MSG(script.is_null(), ERR_CANT_RESOLVE,
					vformat("Cannot resolve script reference '%s' ('%s') from compiled script data.", path, fully_qualified_name));
			r_variant = script;
			return OK;
		} break;
		case FSBytecodeFormat::TAG_EXTERNAL_RESOURCE: {
			String path;
			const Error error = _get_string(p_stream->get_u32(), path);
			if (error != OK) {
				return error;
			}
			ERR_FAIL_NULL_V_MSG(resolver, ERR_UNCONFIGURED, "No external-reference resolver is set on the bytecode loader.");
			const Ref<Resource> resource = resolver->resolve_resource(path);
			ERR_FAIL_COND_V_MSG(resource.is_null(), ERR_CANT_RESOLVE,
					vformat("Cannot resolve resource reference '%s' from compiled script data.", path));
			r_variant = resource;
			return OK;
		} break;
		case FSBytecodeFormat::TAG_NATIVE_CLASS: {
			String class_name;
			const Error error = _get_string(p_stream->get_u32(), class_name);
			if (error != OK) {
				return error;
			}
			// FSNativeClass dispatches by name, so a fresh handle is equivalent to the exported one.
			r_variant = Ref<FSNativeClass>(memnew(FSNativeClass(StringName(class_name))));
			return OK;
		} break;
		case FSBytecodeFormat::TAG_ENGINE_SINGLETON: {
			String singleton_name;
			const Error error = _get_string(p_stream->get_u32(), singleton_name);
			if (error != OK) {
				return error;
			}
			Object *singleton_object = Engine::get_singleton()->get_singleton_object(StringName(singleton_name));
			ERR_FAIL_NULL_V_MSG(singleton_object, ERR_CANT_RESOLVE,
					vformat("Engine singleton '%s' referenced by compiled script data is not registered in this build.", singleton_name));
			r_variant = singleton_object;
			return OK;
		} break;
		case FSBytecodeFormat::TAG_SPECIALIZED_HANDLE: {
			const uint8_t script_tag = p_stream->get_u8();
			Variant script_variant;
			Error error = _decode_object(p_stream, script_tag, script_variant, p_depth + 1);
			if (error != OK) {
				return error;
			}
			const Ref<FoundryScript> specialized_script = script_variant;
			ERR_FAIL_COND_V_MSG(specialized_script.is_null(), ERR_INVALID_DATA,
					"Specialized class handle in compiled script data does not reference a Foundry Script.");
			const uint32_t argument_count = p_stream->get_u32();
			ERR_FAIL_COND_V_MSG((int64_t)argument_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
					"Truncated specialized class handle in compiled script data.");
			Vector<ContainerType> type_arguments;
			for (uint32_t i = 0; i < argument_count; i++) {
				ContainerType type_argument;
				error = _decode_container_type(p_stream, type_argument, p_depth + 1);
				if (error != OK) {
					return error;
				}
				type_arguments.push_back(type_argument);
			}
			r_variant = FSSpecializedClassHandle::create(specialized_script, type_arguments);
			return OK;
		} break;
		default: {
			ERR_FAIL_V_MSG(ERR_INVALID_DATA, vformat("Unknown variant tag %d in compiled script data.", p_tag));
		} break;
	}
}

Error FSBytecodeLoader::_decode_container_type(StreamPeerBuffer *p_stream, ContainerType &r_container_type, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_DATA,
			"Container type is too deeply nested in compiled script data.");
	const uint32_t builtin_type = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG(builtin_type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
			"Invalid builtin type in compiled script container type.");
	r_container_type.builtin_type = (Variant::Type)builtin_type;
	String class_name;
	Error error = _get_string(p_stream->get_u32(), class_name);
	if (error != OK) {
		return error;
	}
	r_container_type.class_name = StringName(class_name);
	if (p_stream->get_u8() != 0) {
		const uint8_t script_tag = p_stream->get_u8();
		Variant script_variant;
		error = _decode_object(p_stream, script_tag, script_variant, p_depth + 1);
		if (error != OK) {
			return error;
		}
		const Ref<Script> script = script_variant;
		ERR_FAIL_COND_V_MSG(script.is_null(), ERR_INVALID_DATA,
				"Container type in compiled script data does not reference a script.");
		r_container_type.script = script;
	}
	const uint32_t element_type_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)element_type_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated container type in compiled script data.");
	for (uint32_t i = 0; i < element_type_count; i++) {
		ContainerType element_type;
		error = _decode_container_type(p_stream, element_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
		r_container_type.element_types.push_back(element_type);
	}
	const uint32_t type_argument_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)type_argument_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated container type in compiled script data.");
	for (uint32_t i = 0; i < type_argument_count; i++) {
		ContainerType type_argument;
		error = _decode_container_type(p_stream, type_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
		r_container_type.type_arguments.push_back(type_argument);
	}
	return OK;
}

Error FSBytecodeLoader::decode_data_type(StreamPeerBuffer *p_stream, FSDataType &r_data_type, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_DATA,
			"Data type is too deeply nested in compiled script data.");
	const uint8_t kind = p_stream->get_u8();
	ERR_FAIL_COND_V_MSG(kind > (uint8_t)FSDataType::TYPE_PARAMETER, ERR_INVALID_DATA,
			"Invalid data type kind in compiled script data.");
	r_data_type.kind = (FSDataType::Kind)kind;
	const uint32_t builtin_type = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG(builtin_type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
			"Invalid builtin type in compiled script data type.");
	r_data_type.builtin_type = (Variant::Type)builtin_type;
	String native_type;
	Error error = _get_string(p_stream->get_u32(), native_type);
	if (error != OK) {
		return error;
	}
	r_data_type.native_type = StringName(native_type);
	const uint8_t flags = p_stream->get_u8();
	r_data_type.is_nullable = (flags & (1 << 0)) != 0;
	r_data_type.is_type_handle = (flags & (1 << 1)) != 0;
	r_data_type.is_self_type = (flags & (1 << 2)) != 0;
	r_data_type.is_script_trait = (flags & (1 << 3)) != 0;
	String script_trait;
	error = _get_string(p_stream->get_u32(), script_trait);
	if (error != OK) {
		return error;
	}
	r_data_type.script_trait = StringName(script_trait);
	String type_parameter_name;
	error = _get_string(p_stream->get_u32(), type_parameter_name);
	if (error != OK) {
		return error;
	}
	r_data_type.type_parameter_name = StringName(type_parameter_name);
	r_data_type.type_parameter_index = p_stream->get_32();
	const uint8_t type_parameter_scope = p_stream->get_u8();
	ERR_FAIL_COND_V_MSG(type_parameter_scope > (uint8_t)FSDataType::TYPE_PARAMETER_METHOD, ERR_INVALID_DATA,
			"Invalid type parameter scope in compiled script data type.");
	r_data_type.type_parameter_scope = (FSDataType::TypeParameterScope)type_parameter_scope;
	if (p_stream->get_u8() != 0) {
		String path;
		error = _get_string(p_stream->get_u32(), path);
		if (error != OK) {
			return error;
		}
		String fully_qualified_name;
		error = _get_string(p_stream->get_u32(), fully_qualified_name);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_NULL_V_MSG(resolver, ERR_UNCONFIGURED, "No external-reference resolver is set on the bytecode loader.");
		const Ref<Script> script = resolver->resolve_script(path, fully_qualified_name);
		ERR_FAIL_COND_V_MSG(script.is_null(), ERR_CANT_RESOLVE,
				vformat("Cannot resolve script type '%s' ('%s') from compiled script data.", path, fully_qualified_name));
		r_data_type.script_type_ref = script;
		r_data_type.script_type = script.ptr();
	}
	const uint32_t element_type_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)element_type_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated data type in compiled script data.");
	for (uint32_t i = 0; i < element_type_count; i++) {
		FSDataType element_type;
		error = decode_data_type(p_stream, element_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
		r_data_type.container_element_types.push_back(element_type);
	}
	const uint32_t type_argument_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)type_argument_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated data type in compiled script data.");
	for (uint32_t i = 0; i < type_argument_count; i++) {
		FSDataType type_argument;
		error = decode_data_type(p_stream, type_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
		r_data_type.type_arguments.push_back(type_argument);
	}
	return OK;
}
