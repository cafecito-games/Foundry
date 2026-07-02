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
#include "core/io/marshalls.h"
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
			// `StreamPeer::get_var` allocates the encoded length before validating it and swallows
			// decode failures into a nil Variant, so a corrupted length prefix could trigger a huge
			// allocation and corruption would silently decode as nil. Bound and decode manually.
			const uint32_t length = p_stream->get_u32();
			ERR_FAIL_COND_V_MSG((int64_t)length > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
					"Truncated inline Variant in compiled script data.");
			Vector<uint8_t> encoded;
			Error error = encoded.resize(length);
			ERR_FAIL_COND_V_MSG(error != OK, ERR_INVALID_DATA, "Cannot allocate inline Variant from compiled script data.");
			if (length > 0) {
				error = p_stream->get_data(encoded.ptrw(), length);
				ERR_FAIL_COND_V_MSG(error != OK, ERR_INVALID_DATA, "Truncated inline Variant in compiled script data.");
			}
			// Objects always travel behind symbolic tags, so inline leaves never decode them.
			error = decode_variant(r_variant, encoded.ptr(), encoded.size(), nullptr, false);
			ERR_FAIL_COND_V_MSG(error != OK, ERR_INVALID_DATA, "Malformed inline Variant in compiled script data.");
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
			error = _get_string(p_stream->get_u32(), fully_qualified_name);
			if (error != OK) {
				return error;
			}
			ERR_FAIL_NULL_V_MSG(resolver, ERR_UNCONFIGURED, "No external-reference resolver is set on the bytecode loader.");
			// A Variant constant always holds a strong reference regardless of locality; the
			// local-class distinction only matters for data-type linkage.
			bool is_local_class = false;
			const Ref<Script> script = resolver->resolve_script(path, fully_qualified_name, is_local_class);
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
		case FSBytecodeFormat::TAG_NULL_OBJECT: {
			r_variant = Variant((Object *)nullptr);
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
		bool is_local_class = false;
		const Ref<Script> script = resolver->resolve_script(path, fully_qualified_name, is_local_class);
		ERR_FAIL_COND_V_MSG(script.is_null(), ERR_CANT_RESOLVE,
				vformat("Cannot resolve script type '%s' ('%s') from compiled script data.", path, fully_qualified_name));
		if (is_local_class) {
			// A class local to the file being loaded is held as a raw pointer without a strong
			// reference, matching the rule documented on `FoundryScript::TypeArgumentBinding` in
			// foundry_script.h: persisted type descriptors must not keep local classes alive, or
			// CRTP-style declarations like `class Node extends Box[Node]` create reference cycles
			// and leak.
			r_data_type.script_type = script.ptr();
		} else {
			r_data_type.script_type_ref = script;
			r_data_type.script_type = script.ptr();
		}
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

Error FSBytecodeLoader::_read_property_info(StreamPeerBuffer *p_stream, PropertyInfo &r_property_info) {
	const uint32_t type = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
			"Invalid property type in compiled script data.");
	r_property_info.type = (Variant::Type)type;
	String name;
	Error error = _get_string(p_stream->get_u32(), name);
	if (error != OK) {
		return error;
	}
	r_property_info.name = name;
	String class_name;
	error = _get_string(p_stream->get_u32(), class_name);
	if (error != OK) {
		return error;
	}
	r_property_info.class_name = StringName(class_name);
	const uint32_t hint = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG(hint >= PROPERTY_HINT_MAX, ERR_INVALID_DATA,
			"Invalid property hint in compiled script data.");
	r_property_info.hint = (PropertyHint)hint;
	String hint_string;
	error = _get_string(p_stream->get_u32(), hint_string);
	if (error != OK) {
		return error;
	}
	r_property_info.hint_string = hint_string;
	r_property_info.usage = p_stream->get_u32();
	return OK;
}

Error FSBytecodeLoader::_read_method_info(StreamPeerBuffer *p_stream, MethodInfo &r_method_info, int p_depth) {
	String name;
	Error error = _get_string(p_stream->get_u32(), name);
	if (error != OK) {
		return error;
	}
	r_method_info.name = name;
	error = _read_property_info(p_stream, r_method_info.return_val);
	if (error != OK) {
		return error;
	}
	r_method_info.flags = p_stream->get_u32();
	r_method_info.id = p_stream->get_32();
	const uint32_t argument_count = p_stream->get_u32();
	// Every serialized PropertyInfo occupies six 4-byte fields.
	ERR_FAIL_COND_V_MSG((int64_t)argument_count * 24 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated method info in compiled script data.");
	for (uint32_t i = 0; i < argument_count; i++) {
		PropertyInfo argument;
		error = _read_property_info(p_stream, argument);
		if (error != OK) {
			return error;
		}
		r_method_info.arguments.push_back(argument);
	}
	const uint32_t default_argument_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)default_argument_count > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated method info in compiled script data.");
	for (uint32_t i = 0; i < default_argument_count; i++) {
		Variant default_argument;
		error = decode_variant_tagged(p_stream, default_argument, p_depth + 1);
		if (error != OK) {
			return error;
		}
		r_method_info.default_arguments.push_back(default_argument);
	}
	r_method_info.return_val_metadata = p_stream->get_32();
	const uint32_t arguments_metadata_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)arguments_metadata_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated method info in compiled script data.");
	for (uint32_t i = 0; i < arguments_metadata_count; i++) {
		r_method_info.arguments_metadata.push_back(p_stream->get_32());
	}
	return OK;
}

Error FSBytecodeLoader::read_function(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *&r_function,
		Vector<LoadedLambdaInfo> *r_lambda_info, int p_depth) {
	r_function = nullptr;
	ERR_FAIL_NULL_V(p_stream, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V_MSG(p_script, ERR_INVALID_PARAMETER,
			"A compiled function needs an owning script to deserialize into.");
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_DATA,
			"Function lambdas are too deeply nested in compiled script data.");

	FSFunction *function = memnew(FSFunction);
	// The FSFunction destructor unregisters itself from the owning script by name, so ownership is
	// wired before anything can fail; the name itself is only committed once the read succeeds.
	function->_script = p_script;
	function->source = p_script->get_script_path();

	const int lambda_info_initial_size = r_lambda_info != nullptr ? r_lambda_info->size() : 0;
	const Error error = _read_function_body(p_stream, p_script, function, r_lambda_info, p_depth);
	if (error != OK) {
		// Deleting the partial function cascades into any lambdas already attached to it, so roll
		// the metadata entries appended for them back too; they would otherwise dangle.
		if (r_lambda_info != nullptr) {
			r_lambda_info->resize(lambda_info_initial_size);
		}
		memdelete(function);
		return error;
	}
	r_function = function;
	return OK;
}

// Any fixup key that no longer resolves in this engine build is a hard load error; the engine-build
// guard in the header makes this unreachable in practice, so this is defense in depth.
#define FSB_LINK_CHECK(m_condition, m_table, m_key)                                                                                                                          \
	ERR_FAIL_COND_V_MSG(m_condition, ERR_CANT_RESOLVE,                                                                                                                       \
			vformat("Cannot link compiled function '%s' in script '%s': %s '%s' does not resolve in this engine build. Was the game exported with a matching engine build?", \
					function_name, script_path, String(m_table), String(m_key)))

Error FSBytecodeLoader::_read_function_body(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *p_function,
		Vector<LoadedLambdaInfo> *r_lambda_info, int p_depth) {
	String function_name;
	Error error = _get_string(p_stream->get_u32(), function_name);
	if (error != OK) {
		return error;
	}
	const String script_path = p_function->source;

	const uint8_t function_flags = p_stream->get_u8();
	p_function->_static = (function_flags & (1 << 0)) != 0;
	p_function->_initial_line = p_stream->get_32();
	p_function->_argument_count = p_stream->get_32();
	p_function->_vararg_index = p_stream->get_32();
	p_function->_stack_size = p_stream->get_32();
	p_function->_instruction_args_size = p_stream->get_32();
	ERR_FAIL_COND_V_MSG(
			p_function->_argument_count < 0 || p_function->_vararg_index < -1 || p_function->_stack_size < 0 ||
					p_function->_instruction_args_size < 0,
			ERR_INVALID_DATA,
			vformat("Malformed compiled function '%s' in script '%s'.", function_name, script_path));

	const uint32_t argument_type_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)argument_type_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	// The VM indexes `argument_types` for every declared argument, so an argument count larger than
	// the type table would read out of bounds at call time.
	ERR_FAIL_COND_V_MSG((int64_t)p_function->_argument_count > (int64_t)argument_type_count, ERR_INVALID_DATA,
			vformat("Malformed compiled function '%s' in script '%s': argument count exceeds its argument type table.",
					function_name, script_path));
	for (uint32_t i = 0; i < argument_type_count; i++) {
		FSDataType argument_type;
		error = decode_data_type(p_stream, argument_type, p_depth + 1);
		if (error != OK) {
			return error;
		}
		p_function->argument_types.push_back(argument_type);
	}
	error = decode_data_type(p_stream, p_function->return_type, p_depth + 1);
	if (error != OK) {
		return error;
	}
	error = _read_method_info(p_stream, p_function->method_info, p_depth + 1);
	if (error != OK) {
		return error;
	}
	error = decode_variant_tagged(p_stream, p_function->rpc_config, p_depth + 1);
	if (error != OK) {
		return error;
	}

	const uint32_t temporary_slot_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)temporary_slot_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < temporary_slot_count; i++) {
		const int32_t slot = p_stream->get_32();
		const uint32_t slot_type = p_stream->get_u32();
		// The VM initializes typed stack slots straight from this map, so a wild index would write
		// outside the call stack.
		ERR_FAIL_COND_V_MSG(slot < 0 || slot >= p_function->_stack_size || slot_type >= Variant::VARIANT_MAX,
				ERR_INVALID_DATA,
				vformat("Malformed temporary slot in compiled function '%s' in script '%s'.", function_name, script_path));
		p_function->temporary_slots[slot] = (Variant::Type)slot_type;
	}

	const uint32_t code_size = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)code_size * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	error = p_function->code.resize(code_size);
	ERR_FAIL_COND_V(error != OK, ERR_OUT_OF_MEMORY);
	for (uint32_t i = 0; i < code_size; i++) {
		p_function->code.write[i] = p_stream->get_32();
	}

	const uint32_t default_argument_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)default_argument_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < default_argument_count; i++) {
		const int32_t default_argument_offset = p_stream->get_32();
		// Default-argument entries are jump targets inside `code`.
		ERR_FAIL_COND_V_MSG(default_argument_offset < 0 || default_argument_offset > (int32_t)code_size, ERR_INVALID_DATA,
				vformat("Malformed default argument in compiled function '%s' in script '%s'.", function_name, script_path));
		p_function->default_arguments.push_back(default_argument_offset);
	}

	const uint32_t constant_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)constant_count > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < constant_count; i++) {
		Variant constant;
		error = decode_variant_tagged(p_stream, constant, p_depth + 1);
		if (error != OK) {
			return error;
		}
		p_function->constants.push_back(constant);
	}

	const uint32_t global_name_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)global_name_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < global_name_count; i++) {
		String global_name;
		error = _get_string(p_stream->get_u32(), global_name);
		if (error != OK) {
			return error;
		}
		p_function->global_names.push_back(StringName(global_name));
	}

	const uint32_t builtin_method_name_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)builtin_method_name_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < builtin_method_name_count; i++) {
		String builtin_method_name;
		error = _get_string(p_stream->get_u32(), builtin_method_name);
		if (error != OK) {
			return error;
		}
		p_function->builtin_method_names.push_back(StringName(builtin_method_name));
	}

#ifdef TOOLS_ENABLED
	// Restoring the symbolic keys keeps a deserialized function re-serializable in tools builds.
	FSFunction::ExportFixups &restored_fixups = p_function->export_fixups;
#endif

	const uint32_t operator_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)operator_count * 12 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < operator_count; i++) {
		const uint32_t variant_operator = p_stream->get_u32();
		const uint32_t left_type = p_stream->get_u32();
		const uint32_t right_type = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(
				variant_operator >= Variant::OP_MAX || left_type >= Variant::VARIANT_MAX || right_type >= Variant::VARIANT_MAX,
				ERR_INVALID_DATA,
				vformat("Malformed operator fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedOperatorEvaluator evaluator = Variant::get_validated_operator_evaluator(
				(Variant::Operator)variant_operator, (Variant::Type)left_type, (Variant::Type)right_type);
		FSB_LINK_CHECK(evaluator == nullptr, "operator",
				vformat("%s (%s, %s)", Variant::get_operator_name((Variant::Operator)variant_operator),
						Variant::get_type_name((Variant::Type)left_type), Variant::get_type_name((Variant::Type)right_type)));
		p_function->operator_funcs.push_back(evaluator);
#ifdef DEBUG_ENABLED
		p_function->operator_names.push_back(Variant::get_operator_name((Variant::Operator)variant_operator));
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.operators.push_back({ (Variant::Operator)variant_operator, (Variant::Type)left_type, (Variant::Type)right_type });
#endif
	}

	const uint32_t setter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)setter_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < setter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		String member_name;
		error = _get_string(p_stream->get_u32(), member_name);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed setter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedSetter setter = Variant::get_member_validated_setter((Variant::Type)type, StringName(member_name));
		FSB_LINK_CHECK(setter == nullptr, "member setter",
				vformat("%s.%s", Variant::get_type_name((Variant::Type)type), member_name));
		p_function->setters.push_back(setter);
#ifdef DEBUG_ENABLED
		p_function->setter_names.push_back(member_name);
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.setters.push_back({ (Variant::Type)type, StringName(member_name) });
#endif
	}

	const uint32_t getter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)getter_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < getter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		String member_name;
		error = _get_string(p_stream->get_u32(), member_name);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed getter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedGetter getter = Variant::get_member_validated_getter((Variant::Type)type, StringName(member_name));
		FSB_LINK_CHECK(getter == nullptr, "member getter",
				vformat("%s.%s", Variant::get_type_name((Variant::Type)type), member_name));
		p_function->getters.push_back(getter);
#ifdef DEBUG_ENABLED
		p_function->getter_names.push_back(member_name);
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.getters.push_back({ (Variant::Type)type, StringName(member_name) });
#endif
	}

	const uint32_t keyed_setter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)keyed_setter_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < keyed_setter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed keyed setter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedKeyedSetter keyed_setter = Variant::get_member_validated_keyed_setter((Variant::Type)type);
		FSB_LINK_CHECK(keyed_setter == nullptr, "keyed setter", Variant::get_type_name((Variant::Type)type));
		p_function->keyed_setters.push_back(keyed_setter);
#ifdef TOOLS_ENABLED
		restored_fixups.keyed_setters.push_back((Variant::Type)type);
#endif
	}

	const uint32_t keyed_getter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)keyed_getter_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < keyed_getter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed keyed getter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedKeyedGetter keyed_getter = Variant::get_member_validated_keyed_getter((Variant::Type)type);
		FSB_LINK_CHECK(keyed_getter == nullptr, "keyed getter", Variant::get_type_name((Variant::Type)type));
		p_function->keyed_getters.push_back(keyed_getter);
#ifdef TOOLS_ENABLED
		restored_fixups.keyed_getters.push_back((Variant::Type)type);
#endif
	}

	const uint32_t indexed_setter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)indexed_setter_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < indexed_setter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed indexed setter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedIndexedSetter indexed_setter = Variant::get_member_validated_indexed_setter((Variant::Type)type);
		FSB_LINK_CHECK(indexed_setter == nullptr, "indexed setter", Variant::get_type_name((Variant::Type)type));
		p_function->indexed_setters.push_back(indexed_setter);
#ifdef TOOLS_ENABLED
		restored_fixups.indexed_setters.push_back((Variant::Type)type);
#endif
	}

	const uint32_t indexed_getter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)indexed_getter_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < indexed_getter_count; i++) {
		const uint32_t type = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed indexed getter fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const Variant::ValidatedIndexedGetter indexed_getter = Variant::get_member_validated_indexed_getter((Variant::Type)type);
		FSB_LINK_CHECK(indexed_getter == nullptr, "indexed getter", Variant::get_type_name((Variant::Type)type));
		p_function->indexed_getters.push_back(indexed_getter);
#ifdef TOOLS_ENABLED
		restored_fixups.indexed_getters.push_back((Variant::Type)type);
#endif
	}

	const uint32_t builtin_method_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)builtin_method_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < builtin_method_count; i++) {
		const uint32_t type = p_stream->get_u32();
		String method_name;
		error = _get_string(p_stream->get_u32(), method_name);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed builtin method fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		FSB_LINK_CHECK(!Variant::has_builtin_method((Variant::Type)type, StringName(method_name)), "builtin method",
				vformat("%s.%s", Variant::get_type_name((Variant::Type)type), method_name));
		const Variant::ValidatedBuiltInMethod builtin_method =
				Variant::get_validated_builtin_method((Variant::Type)type, StringName(method_name));
		FSB_LINK_CHECK(builtin_method == nullptr, "builtin method",
				vformat("%s.%s", Variant::get_type_name((Variant::Type)type), method_name));
		p_function->builtin_methods.push_back(builtin_method);
#ifdef DEBUG_ENABLED
		p_function->builtin_methods_names.push_back(method_name);
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.builtin_methods.push_back({ (Variant::Type)type, StringName(method_name) });
#endif
	}

	const uint32_t constructor_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)constructor_count * 12 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < constructor_count; i++) {
		const uint32_t type = p_stream->get_u32();
		const int32_t constructor_index = p_stream->get_32();
		const int32_t constructor_argument_count = p_stream->get_32();
		ERR_FAIL_COND_V_MSG(type >= Variant::VARIANT_MAX, ERR_INVALID_DATA,
				vformat("Malformed constructor fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		FSB_LINK_CHECK(constructor_index < 0 || constructor_index >= Variant::get_constructor_count((Variant::Type)type),
				"constructor", vformat("%s #%d", Variant::get_type_name((Variant::Type)type), constructor_index));
		// The index alone could silently come to mean a different overload; the recorded argument
		// count pins the signature the code was compiled against.
		FSB_LINK_CHECK(constructor_argument_count != Variant::get_constructor_argument_count((Variant::Type)type, constructor_index),
				"constructor", vformat("%s #%d (%d arguments)", Variant::get_type_name((Variant::Type)type), constructor_index, constructor_argument_count));
		const Variant::ValidatedConstructor constructor =
				Variant::get_validated_constructor((Variant::Type)type, constructor_index);
		FSB_LINK_CHECK(constructor == nullptr, "constructor",
				vformat("%s #%d", Variant::get_type_name((Variant::Type)type), constructor_index));
		p_function->constructors.push_back(constructor);
#ifdef DEBUG_ENABLED
		p_function->constructors_names.push_back(Variant::get_type_name((Variant::Type)type));
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.constructors.push_back({ (Variant::Type)type, constructor_index });
#endif
	}

	const uint32_t utility_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)utility_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < utility_count; i++) {
		String utility_name;
		error = _get_string(p_stream->get_u32(), utility_name);
		if (error != OK) {
			return error;
		}
		FSB_LINK_CHECK(!Variant::has_utility_function(StringName(utility_name)), "utility function", utility_name);
		const Variant::ValidatedUtilityFunction utility = Variant::get_validated_utility_function(StringName(utility_name));
		FSB_LINK_CHECK(utility == nullptr, "utility function", utility_name);
		p_function->utilities.push_back(utility);
#ifdef DEBUG_ENABLED
		p_function->utilities_names.push_back(utility_name);
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.utilities.push_back(StringName(utility_name));
#endif
	}

	const uint32_t gds_utility_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)gds_utility_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < gds_utility_count; i++) {
		String utility_name;
		error = _get_string(p_stream->get_u32(), utility_name);
		if (error != OK) {
			return error;
		}
		FSB_LINK_CHECK(!FSUtilityFunctions::function_exists(StringName(utility_name)), "script utility function", utility_name);
		const FSUtilityFunctions::FunctionPtr utility = FSUtilityFunctions::get_function(StringName(utility_name));
		FSB_LINK_CHECK(utility == nullptr, "script utility function", utility_name);
		p_function->gds_utilities.push_back(utility);
#ifdef DEBUG_ENABLED
		p_function->gds_utilities_names.push_back(utility_name);
#endif
#ifdef TOOLS_ENABLED
		restored_fixups.gds_utilities.push_back(StringName(utility_name));
#endif
	}

	const uint32_t method_bind_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)method_bind_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < method_bind_count; i++) {
		String class_name;
		error = _get_string(p_stream->get_u32(), class_name);
		if (error != OK) {
			return error;
		}
		String method_name;
		error = _get_string(p_stream->get_u32(), method_name);
		if (error != OK) {
			return error;
		}
		MethodBind *method_bind = ClassDB::get_method(StringName(class_name), StringName(method_name));
		FSB_LINK_CHECK(method_bind == nullptr, "native method", vformat("%s.%s", class_name, method_name));
		p_function->methods.push_back(method_bind);
#ifdef TOOLS_ENABLED
		restored_fixups.method_binds.push_back({ StringName(class_name), StringName(method_name) });
#endif
	}

	const uint32_t global_store_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)global_store_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	const HashMap<StringName, int> &global_map = FSLanguage::get_singleton()->get_global_map();
	for (uint32_t i = 0; i < global_store_count; i++) {
		const int32_t code_offset = p_stream->get_32();
		String global_name;
		error = _get_string(p_stream->get_u32(), global_name);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(code_offset < 0 || code_offset >= (int32_t)code_size, ERR_INVALID_DATA,
				vformat("Malformed store-global fixup in compiled function '%s' in script '%s'.", function_name, script_path));
		const int *global_index = global_map.getptr(StringName(global_name));
		FSB_LINK_CHECK(global_index == nullptr, "global", global_name);
		// The exporter masked this operand out; bake this process's global-array index in its place.
		p_function->code.write[code_offset] = *global_index;
#ifdef TOOLS_ENABLED
		restored_fixups.global_stores.push_back({ code_offset, StringName(global_name) });
#endif
	}

	const uint32_t lambda_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)lambda_count * 5 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated compiled function '%s' in script '%s'.", function_name, script_path));
	for (uint32_t i = 0; i < lambda_count; i++) {
		const int32_t capture_count = p_stream->get_32();
		const bool use_self = p_stream->get_u8() != 0;
		ERR_FAIL_COND_V_MSG(capture_count < 0, ERR_INVALID_DATA,
				vformat("Malformed lambda info in compiled function '%s' in script '%s'.", function_name, script_path));
		FSFunction *lambda = nullptr;
		error = read_function(p_stream, p_script, lambda, r_lambda_info, p_depth + 1);
		if (error != OK) {
			return error;
		}
		// The parent owns its lambdas from this point on (the FSFunction destructor deletes them),
		// so a later failure in this body still cleans them up.
		p_function->lambdas.push_back(lambda);
		if (r_lambda_info != nullptr) {
			LoadedLambdaInfo lambda_info;
			lambda_info.function = lambda;
			lambda_info.capture_count = capture_count;
			lambda_info.use_self = use_self;
			r_lambda_info->push_back(lambda_info);
		}
	}

	p_function->setup_runtime_pointers();
	p_function->name = StringName(function_name);
#ifdef DEBUG_ENABLED
	// Keeps profiler and debugger signatures meaningful when a debug export template loads compiled
	// bytecode. The debug display-name vectors were synthesized alongside each fixup table above,
	// because the disassembler indexes them unguarded.
	p_function->func_cname = (String(p_function->source) + " - " + function_name).utf8();
	p_function->_func_cname = p_function->func_cname.get_data();
#endif
	return OK;
}

#undef FSB_LINK_CHECK
