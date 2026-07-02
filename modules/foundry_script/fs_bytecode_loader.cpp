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
#include "fs_conformance_registry.h"
#include "fs_function.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/version.h"

// Marks a loader as mid-load for the duration of an entry point, so re-entrant use of the same
// instance (which would clobber the string table and intra-file class list) is rejected with
// ERR_BUSY on every path, including early error returns.
struct FSBytecodeLoadScope {
	bool *load_flag = nullptr;

	explicit FSBytecodeLoadScope(bool *p_load_flag) :
			load_flag(p_load_flag) { *load_flag = true; }
	~FSBytecodeLoadScope() { *load_flag = false; }
};

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

// Mirrors `FSBytecodeExporter::_encode_script_reference`: an intra-file class index resolves
// against the skeleton scripts of the `.fsb` being loaded; an external identity goes through the
// resolver as (path, fully qualified class name).
Error FSBytecodeLoader::_read_script_reference(StreamPeerBuffer *p_stream, Ref<Script> &r_script, bool &r_is_local_class,
		const String &p_context) {
	r_script = Ref<Script>();
	r_is_local_class = false;
	const uint8_t locality = p_stream->get_u8();
	if (locality == 1) {
		const uint32_t class_index = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG(class_index >= (uint32_t)local_classes.size(), ERR_INVALID_DATA,
				vformat("Intra-file class index out of range in compiled script data (%s).", p_context));
		r_script = Ref<Script>(local_classes[class_index]);
		r_is_local_class = true;
		return OK;
	}
	ERR_FAIL_COND_V_MSG(locality != 0, ERR_INVALID_DATA,
			vformat("Malformed script reference in compiled script data (%s).", p_context));
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
	const Ref<Script> script = resolver->resolve_script(path, fully_qualified_name, r_is_local_class);
	ERR_FAIL_COND_V_MSG(script.is_null(), ERR_CANT_RESOLVE,
			vformat("Cannot resolve script reference '%s' ('%s') from compiled script data (%s).",
					path, fully_qualified_name, p_context));
	r_script = script;
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
			// A Variant constant always holds a strong reference regardless of locality; the
			// local-class distinction only matters for data-type linkage.
			Ref<Script> script;
			bool is_local_class = false;
			const Error error = _read_script_reference(p_stream, script, is_local_class, "constant");
			if (error != OK) {
				return error;
			}
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
		Ref<Script> script;
		bool is_local_class = false;
		error = _read_script_reference(p_stream, script, is_local_class, "data type");
		if (error != OK) {
			return error;
		}
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

	// A nested function is always a lambda, and compiled lambdas are always named
	// "<anonymous lambda>" (not a declarable identifier). A hostile lambda name that collides with
	// a registered member function must be rejected before it is committed: the FSFunction
	// destructor unregisters by name, so a later rollback or teardown of the lambda would silently
	// unregister (and orphan) the real member function.
	ERR_FAIL_COND_V_MSG(p_depth > 0 && p_script->member_functions.has(StringName(function_name)), ERR_INVALID_DATA,
			vformat("Malformed lambda name '%s' in compiled script '%s'.", function_name, script_path));

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

Error FSBytecodeLoader::_open_script_stream(const Vector<uint8_t> &p_buffer, Ref<StreamPeerBuffer> &r_stream) {
	int header_size = 0;
	const Error header_error = check_header(p_buffer, &header_size);
	if (header_error != OK) {
		return header_error;
	}
	r_stream.instantiate();
	r_stream->set_data_array(p_buffer);
	r_stream->seek(header_size);
	const uint32_t script_flags = r_stream->get_u32();
	// Bit 0 (tool) is also carried per class in the skeleton; only the static-data bits matter here.
	has_static_data = (script_flags & (1 << 1)) != 0;
	annotated_static_unload = (script_flags & (1 << 2)) != 0;
	const Error section_error = _expect_section(r_stream.ptr(), FSBytecodeFormat::SECTION_STRING_TABLE);
	if (section_error != OK) {
		return section_error;
	}
	return read_string_table(r_stream.ptr());
}

Error FSBytecodeLoader::_expect_section(StreamPeerBuffer *p_stream, FSBytecodeFormat::SectionId p_section) {
	const uint32_t section = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG(section != (uint32_t)p_section, ERR_INVALID_DATA,
			vformat("Compiled script data is corrupted: expected section %d, found %d.", (int)p_section, (int64_t)section));
	return OK;
}

Error FSBytecodeLoader::_read_dependency_section(StreamPeerBuffer *p_stream, Vector<String> *r_dependencies) {
	Error error = _expect_section(p_stream, FSBytecodeFormat::SECTION_DEPENDENCIES);
	if (error != OK) {
		return error;
	}
	const uint32_t dependency_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)dependency_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			"Truncated dependency list in compiled script data.");
	for (uint32_t i = 0; i < dependency_count; i++) {
		String dependency_path;
		error = _get_string(p_stream->get_u32(), dependency_path);
		if (error != OK) {
			return error;
		}
		if (r_dependencies != nullptr) {
			r_dependencies->push_back(dependency_path);
		}
	}
	return OK;
}

Error FSBytecodeLoader::_read_skeleton_class(StreamPeerBuffer *p_stream, const Ref<FoundryScript> &p_class, const String &p_root_path,
		Vector<SkeletonBaseReference> &r_base_references, int p_depth) {
	ERR_FAIL_COND_V_MSG(p_depth > Variant::MAX_RECURSION_DEPTH, ERR_INVALID_DATA,
			vformat("Inner classes are too deeply nested in compiled script '%s'.", p_root_path));
	local_classes.push_back(p_class.ptr());

	String fully_qualified_name;
	Error error = _get_string(p_stream->get_u32(), fully_qualified_name);
	if (error != OK) {
		return error;
	}
	String local_name;
	error = _get_string(p_stream->get_u32(), local_name);
	if (error != OK) {
		return error;
	}
	String global_name;
	error = _get_string(p_stream->get_u32(), global_name);
	if (error != OK) {
		return error;
	}
	String simplified_icon_path;
	error = _get_string(p_stream->get_u32(), simplified_icon_path);
	if (error != OK) {
		return error;
	}
	String native_class_name;
	error = _get_string(p_stream->get_u32(), native_class_name);
	if (error != OK) {
		return error;
	}
	const uint8_t class_flags = p_stream->get_u8();
	String trait_type_name;
	error = _get_string(p_stream->get_u32(), trait_type_name);
	if (error != OK) {
		return error;
	}

	p_class->fully_qualified_name = fully_qualified_name;
	p_class->local_name = StringName(local_name);
	p_class->global_name = StringName(global_name);
	p_class->simplified_icon_path = simplified_icon_path;
	p_class->tool = (class_flags & (1 << 0)) != 0;
	p_class->_is_abstract = (class_flags & (1 << 1)) != 0;
	p_class->_is_final = (class_flags & (1 << 2)) != 0;
	p_class->_is_trait_type = (class_flags & (1 << 3)) != 0;
	p_class->trait_type_name = StringName(trait_type_name);
	p_class->compiled_binary = true;

	// The native base is the shared handle in the language's global array, exactly as the compiler
	// links it, so name-dispatched native identity behaves identically.
	const HashMap<StringName, int> &global_map = FSLanguage::get_singleton()->get_global_map();
	const int *native_index = global_map.getptr(StringName(native_class_name));
	ERR_FAIL_NULL_V_MSG(native_index, ERR_CANT_RESOLVE,
			vformat("Cannot load compiled script '%s': native base class '%s' is not registered in this build.",
					p_root_path, native_class_name));
	p_class->native = FSLanguage::get_singleton()->get_global_array()[*native_index];
	ERR_FAIL_COND_V_MSG(p_class->native.is_null(), ERR_CANT_RESOLVE,
			vformat("Cannot load compiled script '%s': global '%s' is not a native class in this build.",
					p_root_path, native_class_name));

	// Base references are collected (aligned with the preorder class list) and applied by
	// `load_full` once the whole tree exists: a local base may be a later class in preorder. The
	// parse below reads the wire format `FSBytecodeExporter::_encode_script_reference` writes and
	// `_read_script_reference` decodes; it is inlined here because resolution must be deferred, so
	// any wire-format change must update all three sites.
	SkeletonBaseReference base_reference;
	if (p_stream->get_u8() != 0) {
		const uint8_t locality = p_stream->get_u8();
		if (locality == 1) {
			base_reference.kind = 1;
			base_reference.local_index = p_stream->get_u32();
		} else if (locality == 0) {
			base_reference.kind = 2;
			error = _get_string(p_stream->get_u32(), base_reference.path);
			if (error != OK) {
				return error;
			}
			error = _get_string(p_stream->get_u32(), base_reference.fully_qualified_name);
			if (error != OK) {
				return error;
			}
		} else {
			ERR_FAIL_V_MSG(ERR_INVALID_DATA, vformat("Malformed base class reference in compiled script '%s'.", p_root_path));
		}
	}
	r_base_references.push_back(base_reference);

	const uint32_t subclass_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)subclass_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated inner class list in compiled script '%s'.", p_root_path));
	// Rebuild the subclass map in serialized order, reusing the scripts a previous `load_skeleton`
	// instantiated so shallow references handed out earlier stay valid.
	HashMap<StringName, Ref<FoundryScript>> previous_subclasses = p_class->subclasses;
	p_class->subclasses.clear();
	for (uint32_t i = 0; i < subclass_count; i++) {
		String subclass_name;
		error = _get_string(p_stream->get_u32(), subclass_name);
		if (error != OK) {
			return error;
		}
		Ref<FoundryScript> subclass;
		if (const Ref<FoundryScript> *existing = previous_subclasses.getptr(StringName(subclass_name))) {
			subclass = *existing;
		}
		if (subclass.is_null()) {
			subclass.instantiate();
		}
		subclass->_owner = p_class.ptr();
		subclass->path = p_root_path;
		p_class->subclasses.insert(StringName(subclass_name), subclass);
		error = _read_skeleton_class(p_stream, subclass, p_root_path, r_base_references, p_depth + 1);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

Error FSBytecodeLoader::load_skeleton(const Vector<uint8_t> &p_buffer, const Ref<FoundryScript> &p_script) {
	ERR_FAIL_COND_V(p_script.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(load_in_progress, ERR_BUSY,
			"This bytecode loader is already mid-load; nested loads need a fresh FSBytecodeLoader.");
	FSBytecodeLoadScope load_scope(&load_in_progress);
	Ref<StreamPeerBuffer> stream;
	Error error = _open_script_stream(p_buffer, stream);
	if (error != OK) {
		return error;
	}
	error = _read_dependency_section(stream.ptr(), nullptr);
	if (error != OK) {
		return error;
	}
	error = _expect_section(stream.ptr(), FSBytecodeFormat::SECTION_SKELETON);
	if (error != OK) {
		return error;
	}
	local_classes.clear();
	Vector<SkeletonBaseReference> base_references;
	return _read_skeleton_class(stream.ptr(), p_script, p_script->get_script_path(), base_references, 0);
}

Error FSBytecodeLoader::read_dependencies(const Vector<uint8_t> &p_buffer, Vector<String> &r_dependencies) {
	r_dependencies.clear();
	ERR_FAIL_COND_V_MSG(load_in_progress, ERR_BUSY,
			"This bytecode loader is already mid-load; nested loads need a fresh FSBytecodeLoader.");
	FSBytecodeLoadScope load_scope(&load_in_progress);
	Ref<StreamPeerBuffer> stream;
	const Error error = _open_script_stream(p_buffer, stream);
	if (error != OK) {
		return error;
	}
	return _read_dependency_section(stream.ptr(), &r_dependencies);
}

Error FSBytecodeLoader::_read_member_info(StreamPeerBuffer *p_stream, const String &p_script_path, StringName &r_name,
		FoundryScript::MemberInfo &r_member_info) {
	String member_name;
	Error error = _get_string(p_stream->get_u32(), member_name);
	if (error != OK) {
		return error;
	}
	r_name = StringName(member_name);
	r_member_info.index = p_stream->get_32();
	String setter;
	error = _get_string(p_stream->get_u32(), setter);
	if (error != OK) {
		return error;
	}
	r_member_info.setter = StringName(setter);
	String getter;
	error = _get_string(p_stream->get_u32(), getter);
	if (error != OK) {
		return error;
	}
	r_member_info.getter = StringName(getter);
	error = decode_data_type(p_stream, r_member_info.data_type);
	if (error != OK) {
		return error;
	}
	error = _read_property_info(p_stream, r_member_info.property_info);
	if (error != OK) {
		return error;
	}
	return _read_type_argument_binding(p_stream, r_member_info.type_argument_binding);
}

Error FSBytecodeLoader::_read_type_argument_binding(StreamPeerBuffer *p_stream, FoundryScript::TypeArgumentBinding &r_binding) {
	const uint8_t kind = p_stream->get_u8();
	ERR_FAIL_COND_V_MSG(kind > (uint8_t)FoundryScript::TypeArgumentBinding::OPEN, ERR_INVALID_DATA,
			"Invalid type argument binding in compiled script data.");
	r_binding.kind = (FoundryScript::TypeArgumentBinding::Kind)kind;
	const uint8_t binding_flags = p_stream->get_u8();
	r_binding.fixed_is_dependent = (binding_flags & (1 << 0)) != 0;
	r_binding.is_type_handle = (binding_flags & (1 << 1)) != 0;
	r_binding.leaf_ordinal = p_stream->get_32();
	return decode_data_type(p_stream, r_binding.fixed);
}

Error FSBytecodeLoader::_read_annotation_usages(StreamPeerBuffer *p_stream, const String &p_script_path,
		Vector<FoundryScript::AnnotationUsage> &r_usages) {
	const uint32_t usage_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)usage_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated annotation metadata in compiled script '%s'.", p_script_path));
	for (uint32_t i = 0; i < usage_count; i++) {
		FoundryScript::AnnotationUsage usage;
		String annotation_name;
		Error error = _get_string(p_stream->get_u32(), annotation_name);
		if (error != OK) {
			return error;
		}
		usage.name = StringName(annotation_name);
		String qualified_name;
		error = _get_string(p_stream->get_u32(), qualified_name);
		if (error != OK) {
			return error;
		}
		usage.qualified_name = StringName(qualified_name);
		usage.is_builtin = p_stream->get_u8() != 0;
		Variant args;
		error = decode_variant_tagged(p_stream, args);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(args.get_type() != Variant::ARRAY, ERR_INVALID_DATA,
				vformat("Malformed annotation arguments in compiled script '%s'.", p_script_path));
		usage.args = args;
		Variant kwargs;
		error = decode_variant_tagged(p_stream, kwargs);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(kwargs.get_type() != Variant::DICTIONARY, ERR_INVALID_DATA,
				vformat("Malformed annotation named arguments in compiled script '%s'.", p_script_path));
		usage.kwargs = kwargs;
		r_usages.push_back(usage);
	}
	return OK;
}

Error FSBytecodeLoader::_read_annotation_usage_map(StreamPeerBuffer *p_stream, const String &p_script_path,
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &r_annotation_map) {
	const uint32_t entry_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)entry_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated annotation metadata in compiled script '%s'.", p_script_path));
	for (uint32_t i = 0; i < entry_count; i++) {
		String owner_name;
		Error error = _get_string(p_stream->get_u32(), owner_name);
		if (error != OK) {
			return error;
		}
		Vector<FoundryScript::AnnotationUsage> usages;
		error = _read_annotation_usages(p_stream, p_script_path, usages);
		if (error != OK) {
			return error;
		}
		r_annotation_map.insert(StringName(owner_name), usages);
	}
	return OK;
}

Error FSBytecodeLoader::_read_parameter_annotation_map(StreamPeerBuffer *p_stream, const String &p_script_path,
		HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &r_parameter_map) {
	const uint32_t entry_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)entry_count * 8 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated annotation metadata in compiled script '%s'.", p_script_path));
	for (uint32_t i = 0; i < entry_count; i++) {
		String owner_name;
		Error error = _get_string(p_stream->get_u32(), owner_name);
		if (error != OK) {
			return error;
		}
		HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> parameter_usages;
		error = _read_annotation_usage_map(p_stream, p_script_path, parameter_usages);
		if (error != OK) {
			return error;
		}
		r_parameter_map.insert(StringName(owner_name), parameter_usages);
	}
	return OK;
}

Error FSBytecodeLoader::_read_optional_function(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *&r_function,
		Vector<LoadedLambdaInfo> *r_lambda_info) {
	r_function = nullptr;
	if (p_stream->get_u8() == 0) {
		return OK;
	}
	return read_function(p_stream, p_script, r_function, r_lambda_info);
}

Error FSBytecodeLoader::_read_class_body(StreamPeerBuffer *p_stream, FoundryScript *p_script) {
	const String script_path = p_script->get_script_path();

	// Members were serialized post-compile with the base members already flattened in, so no
	// inheritance recomputation happens here.
	const uint32_t member_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)member_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated member table in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < member_count; i++) {
		StringName member_name;
		FoundryScript::MemberInfo member_info;
		const Error error = _read_member_info(p_stream, script_path, member_name, member_info);
		if (error != OK) {
			return error;
		}
		// Instance member slots are indexed straight from this value, so a wild index would write
		// outside the instance's member array.
		ERR_FAIL_COND_V_MSG(member_info.index < 0 || member_info.index >= (int)member_count, ERR_INVALID_DATA,
				vformat("Malformed member index for '%s' in compiled script '%s'.", member_name, script_path));
		p_script->member_indices.insert(member_name, member_info);
	}

	const uint32_t own_member_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)own_member_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated member list in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < own_member_count; i++) {
		String member_name;
		const Error error = _get_string(p_stream->get_u32(), member_name);
		if (error != OK) {
			return error;
		}
		p_script->members.insert(StringName(member_name));
	}

	const uint32_t static_variable_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)static_variable_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated static variable table in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < static_variable_count; i++) {
		StringName static_variable_name;
		FoundryScript::MemberInfo static_variable_info;
		const Error error = _read_member_info(p_stream, script_path, static_variable_name, static_variable_info);
		if (error != OK) {
			return error;
		}
		ERR_FAIL_COND_V_MSG(static_variable_info.index < 0 || static_variable_info.index >= (int)static_variable_count,
				ERR_INVALID_DATA,
				vformat("Malformed static variable index for '%s' in compiled script '%s'.", static_variable_name, script_path));
		p_script->static_variables_indices.insert(static_variable_name, static_variable_info);
	}
	p_script->static_variables.resize(p_script->static_variables_indices.size());

	const uint32_t constant_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)constant_count * 5 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated constant table in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < constant_count; i++) {
		String constant_name;
		Error error = _get_string(p_stream->get_u32(), constant_name);
		if (error != OK) {
			return error;
		}
		Variant constant_value;
		error = decode_variant_tagged(p_stream, constant_value);
		if (error != OK) {
			return error;
		}
		p_script->constants.insert(StringName(constant_name), constant_value);
	}

	const uint32_t signal_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)signal_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated signal table in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < signal_count; i++) {
		String signal_name;
		Error error = _get_string(p_stream->get_u32(), signal_name);
		if (error != OK) {
			return error;
		}
		MethodInfo signal_info;
		error = _read_method_info(p_stream, signal_info, 0);
		if (error != OK) {
			return error;
		}
		p_script->_signals.insert(StringName(signal_name), signal_info);
	}

	const uint32_t trait_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)trait_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated trait list in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < trait_count; i++) {
		String trait_name;
		const Error error = _get_string(p_stream->get_u32(), trait_name);
		if (error != OK) {
			return error;
		}
		p_script->script_trait_list.push_back(StringName(trait_name));
	}

	const uint32_t abstract_requirement_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)abstract_requirement_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated abstract trait requirements in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < abstract_requirement_count; i++) {
		String requirement_name;
		Error error = _get_string(p_stream->get_u32(), requirement_name);
		if (error != OK) {
			return error;
		}
		FoundryScript::AbstractTraitRequirement requirement;
		error = decode_data_type(p_stream, requirement.return_type);
		if (error != OK) {
			return error;
		}
		error = _read_method_info(p_stream, requirement.method_info, 0);
		if (error != OK) {
			return error;
		}
		p_script->abstract_trait_requirements.insert(StringName(requirement_name), requirement);
	}

	const uint32_t type_parameter_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)type_parameter_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated type parameter list in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < type_parameter_count; i++) {
		FoundryScript::TypeParameter type_parameter;
		String type_parameter_name;
		Error error = _get_string(p_stream->get_u32(), type_parameter_name);
		if (error != OK) {
			return error;
		}
		type_parameter.name = StringName(type_parameter_name);
		type_parameter.index = p_stream->get_32();
		type_parameter.has_bound = p_stream->get_u8() != 0;
		error = _read_property_info(p_stream, type_parameter.bound);
		if (error != OK) {
			return error;
		}
		p_script->type_parameters.push_back(type_parameter);
	}

	// The per-ancestor binding table is re-keyed from serialized script identities to the live
	// scripts: intra-file ancestors to skeleton scripts, external ones through the resolver. The
	// raw keys follow the compile-time lifetime rules (ancestors stay alive via the `base` chain,
	// external trait scripts via the loading cache).
	const uint32_t ancestor_binding_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)ancestor_binding_count * 5 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated type parameter bindings in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < ancestor_binding_count; i++) {
		Ref<Script> ancestor_reference;
		bool ancestor_is_local = false;
		Error error = _read_script_reference(p_stream, ancestor_reference, ancestor_is_local, "type parameter ancestor");
		if (error != OK) {
			return error;
		}
		FoundryScript *ancestor_script = Object::cast_to<FoundryScript>(ancestor_reference.ptr());
		ERR_FAIL_NULL_V_MSG(ancestor_script, ERR_INVALID_DATA,
				vformat("Malformed type parameter ancestor in compiled script '%s'.", script_path));
		const uint32_t binding_count = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG((int64_t)binding_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
				vformat("Truncated type parameter bindings in compiled script '%s'.", script_path));
		Vector<FoundryScript::TypeArgumentBinding> bindings;
		for (uint32_t binding_index = 0; binding_index < binding_count; binding_index++) {
			FoundryScript::TypeArgumentBinding binding;
			error = _read_type_argument_binding(p_stream, binding);
			if (error != OK) {
				return error;
			}
			bindings.push_back(binding);
		}
		p_script->type_parameter_bindings_by_ancestor.insert(ancestor_script, bindings);
	}

	// `rpc_config` was serialized post-merge, so it is taken verbatim.
	Variant rpc_config;
	Error error = decode_variant_tagged(p_stream, rpc_config);
	if (error != OK) {
		return error;
	}
	ERR_FAIL_COND_V_MSG(rpc_config.get_type() != Variant::DICTIONARY, ERR_INVALID_DATA,
			vformat("Malformed RPC configuration in compiled script '%s'.", script_path));
	p_script->rpc_config = rpc_config;

	error = _read_annotation_usages(p_stream, script_path, p_script->class_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_annotation_usage_map(p_stream, script_path, p_script->method_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_annotation_usage_map(p_stream, script_path, p_script->variable_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_annotation_usage_map(p_stream, script_path, p_script->signal_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_annotation_usage_map(p_stream, script_path, p_script->constant_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_parameter_annotation_map(p_stream, script_path, p_script->method_parameter_annotations);
	if (error != OK) {
		return error;
	}
	error = _read_parameter_annotation_map(p_stream, script_path, p_script->signal_parameter_annotations);
	if (error != OK) {
		return error;
	}

	// Functions become the real registered member functions of the loaded script; any function
	// registered before a later failure is owned by the script and freed by its destructor.
	Vector<LoadedLambdaInfo> lambda_entries;
	const uint32_t function_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)function_count * 5 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated function table in compiled script '%s'.", script_path));
	for (uint32_t i = 0; i < function_count; i++) {
		const bool is_initializer = p_stream->get_u8() != 0;
		FSFunction *function = nullptr;
		error = read_function(p_stream, p_script, function, &lambda_entries);
		if (error != OK) {
			return error;
		}
		if (p_script->member_functions.has(function->name)) {
			// Deleting the duplicate erases its name from the map (the destructor unregisters by
			// name), which would orphan the surviving original; put the original back so the script
			// still owns and frees it after this load fails.
			FSFunction *shadowed_function = p_script->member_functions[function->name];
			const StringName duplicate_name = function->name;
			memdelete(function);
			p_script->member_functions.insert(duplicate_name, shadowed_function);
			ERR_FAIL_V_MSG(ERR_INVALID_DATA,
					vformat("Duplicate function '%s' in compiled script '%s'.", duplicate_name, script_path));
		}
		p_script->member_functions.insert(function->name, function);
		if (is_initializer) {
			p_script->initializer = function;
		}
	}

	error = _read_optional_function(p_stream, p_script, p_script->implicit_initializer, &lambda_entries);
	if (error != OK) {
		return error;
	}
	error = _read_optional_function(p_stream, p_script, p_script->implicit_ready, &lambda_entries);
	if (error != OK) {
		return error;
	}
	error = _read_optional_function(p_stream, p_script, p_script->static_initializer, &lambda_entries);
	if (error != OK) {
		return error;
	}

	for (const LoadedLambdaInfo &lambda_entry : lambda_entries) {
		FoundryScript::LambdaInfo lambda_info;
		lambda_info.capture_count = lambda_entry.capture_count;
		lambda_info.use_self = lambda_entry.use_self;
		p_script->lambda_info.insert(lambda_entry.function, lambda_info);
	}

	// Rebuild the slot-indexed binding table exactly as `_prepare_compilation` finalizes it.
	p_script->member_type_argument_bindings.resize(p_script->member_indices.size());
	for (const KeyValue<StringName, FoundryScript::MemberInfo> &member : p_script->member_indices) {
		if (member.value.index >= 0 && member.value.index < p_script->member_type_argument_bindings.size()) {
			p_script->member_type_argument_bindings.write[member.value.index] = member.value.type_argument_binding;
		}
	}

	return OK;
}

// Rebuilds this script's retroactive-conformance witnesses and re-registers them with the runtime
// registry, mirroring what `FSCompiler::_compile_conformance_witnesses` does after compilation:
// the declaring script owns the functions, holds its external targets alive, and records the
// registration key so reload/unload can drop the borrowed pointers first.
Error FSBytecodeLoader::_read_witness_section(StreamPeerBuffer *p_stream, FoundryScript *p_script) {
	const String script_path = p_script->get_script_path();
	const uint32_t conformance_count = p_stream->get_u32();
	ERR_FAIL_COND_V_MSG((int64_t)conformance_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
			vformat("Truncated conformance section in compiled script '%s'.", script_path));
	if (conformance_count == 0) {
		return OK;
	}

	Vector<FSConformanceRegistry::RuntimeConformance> conformances;
	for (uint32_t i = 0; i < conformance_count; i++) {
		Ref<Script> target_reference;
		bool target_is_local = false;
		Error error = _read_script_reference(p_stream, target_reference, target_is_local, "conformance target");
		if (error != OK) {
			return error;
		}
		FoundryScript *target_script = Object::cast_to<FoundryScript>(target_reference.ptr());
		ERR_FAIL_NULL_V_MSG(target_script, ERR_INVALID_DATA,
				vformat("Malformed conformance target in compiled script '%s'.", script_path));

		FSConformanceRegistry::RuntimeConformance conformance;
		const uint32_t target_key_count = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG((int64_t)target_key_count * 4 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
				vformat("Truncated conformance section in compiled script '%s'.", script_path));
		for (uint32_t key_index = 0; key_index < target_key_count; key_index++) {
			String target_key;
			error = _get_string(p_stream->get_u32(), target_key);
			if (error != OK) {
				return error;
			}
			conformance.target_keys.push_back(target_key);
		}
		String trait_name;
		error = _get_string(p_stream->get_u32(), trait_name);
		if (error != OK) {
			return error;
		}
		conformance.trait_name = StringName(trait_name);

		const uint32_t witness_count = p_stream->get_u32();
		ERR_FAIL_COND_V_MSG((int64_t)witness_count * 5 > (int64_t)p_stream->get_available_bytes(), ERR_INVALID_DATA,
				vformat("Truncated conformance section in compiled script '%s'.", script_path));
		for (uint32_t witness_index = 0; witness_index < witness_count; witness_index++) {
			String method_name;
			error = _get_string(p_stream->get_u32(), method_name);
			if (error != OK) {
				return error;
			}
			// Witnesses were compiled against the target's member layout, so they deserialize with
			// the target as their script; the declaring script owns them for lifetime, exactly as
			// at compile time.
			Vector<LoadedLambdaInfo> lambda_entries;
			FSFunction *witness = nullptr;
			error = read_function(p_stream, target_script, witness, &lambda_entries);
			if (error != OK) {
				return error;
			}
			p_script->witness_functions.push_back(witness);
			for (const LoadedLambdaInfo &lambda_entry : lambda_entries) {
				FoundryScript::LambdaInfo lambda_info;
				lambda_info.capture_count = lambda_entry.capture_count;
				lambda_info.use_self = lambda_entry.use_self;
				target_script->lambda_info.insert(lambda_entry.function, lambda_info);
			}
			conformance.functions[StringName(method_name)] = witness;
		}

		if (!conformance.functions.is_empty()) {
			conformances.push_back(conformance);
			// Keep the target alive as long as the witnesses whose `_script` points at it live. A
			// self-reference is skipped to avoid the script holding a strong reference to itself.
			if (target_script != p_script && !p_script->witness_target_scripts.has(target_reference)) {
				p_script->witness_target_scripts.push_back(target_reference);
			}
		}
	}

	p_script->registered_conformance_source = script_path;
	FSConformanceRegistry::get_singleton()->register_runtime_witnesses(script_path, conformances);
	return OK;
}

Error FSBytecodeLoader::load_full(const Vector<uint8_t> &p_buffer, const Ref<FoundryScript> &p_script) {
	ERR_FAIL_COND_V(p_script.is_null(), ERR_INVALID_PARAMETER);
	const String script_path = p_script->get_script_path();
	ERR_FAIL_COND_V_MSG(load_in_progress, ERR_BUSY,
			vformat("Cannot load compiled script '%s': this bytecode loader is already mid-load; nested loads need a fresh FSBytecodeLoader.",
					script_path));
	FSBytecodeLoadScope load_scope(&load_in_progress);
	ERR_FAIL_COND_V_MSG(p_script->valid, ERR_ALREADY_IN_USE,
			vformat("Compiled script '%s' is already loaded.", script_path));

	Ref<StreamPeerBuffer> stream;
	Error error = _open_script_stream(p_buffer, stream);
	if (error != OK) {
		return error;
	}
	error = _read_dependency_section(stream.ptr(), nullptr);
	if (error != OK) {
		return error;
	}

	error = _expect_section(stream.ptr(), FSBytecodeFormat::SECTION_SKELETON);
	if (error != OK) {
		return error;
	}
	local_classes.clear();
	Vector<SkeletonBaseReference> base_references;
	error = _read_skeleton_class(stream.ptr(), p_script, script_path, base_references, 0);
	if (error != OK) {
		return error;
	}
	ERR_FAIL_COND_V(base_references.size() != local_classes.size(), ERR_BUG);

	// Base links come before any body decodes. External bases recurse through the resolver — the
	// dependency-ordering mechanism; cycles rely on the caller's cache having published this
	// script's shell before calling load_full.
	for (int class_index = 0; class_index < local_classes.size(); class_index++) {
		const SkeletonBaseReference &base_reference = base_references[class_index];
		switch (base_reference.kind) {
			case 0: {
				// Native base only.
			} break;
			case 1: {
				ERR_FAIL_COND_V_MSG(
						base_reference.local_index >= (uint32_t)local_classes.size() ||
								local_classes[base_reference.local_index] == local_classes[class_index],
						ERR_INVALID_DATA,
						vformat("Malformed base class reference in compiled script '%s'.", script_path));
				local_classes[class_index]->base = Ref<FoundryScript>(local_classes[base_reference.local_index]);
			} break;
			case 2: {
				ERR_FAIL_NULL_V_MSG(resolver, ERR_UNCONFIGURED,
						"No external-reference resolver is set on the bytecode loader.");
				bool base_is_local = false;
				const Ref<FoundryScript> base =
						resolver->resolve_script(base_reference.path, base_reference.fully_qualified_name, base_is_local);
				ERR_FAIL_COND_V_MSG(base.is_null(), ERR_CANT_RESOLVE,
						vformat("Cannot load compiled script '%s': base class '%s' ('%s') does not resolve.",
								script_path, base_reference.fully_qualified_name, base_reference.path));
				local_classes[class_index]->base = base;
			} break;
			default: {
				ERR_FAIL_V_MSG(ERR_INVALID_DATA, vformat("Malformed base class reference in compiled script '%s'.", script_path));
			} break;
		}
	}

	error = _expect_section(stream.ptr(), FSBytecodeFormat::SECTION_CLASS_BODIES);
	if (error != OK) {
		return error;
	}
	// Bodies are stored in the same preorder as the skeleton.
	for (FoundryScript *loaded_class : local_classes) {
		error = _read_class_body(stream.ptr(), loaded_class);
		if (error != OK) {
			return error;
		}
	}

	error = _expect_section(stream.ptr(), FSBytecodeFormat::SECTION_WITNESSES);
	if (error != OK) {
		return error;
	}
	error = _read_witness_section(stream.ptr(), p_script.ptr());
	if (error != OK) {
		return error;
	}

	// Finalization mirrors `_compile_class`'s per-class tail and `reload()`'s root tail: static
	// defaults, then `valid`, then the static initializers.
	for (FoundryScript *loaded_class : local_classes) {
		loaded_class->_static_default_init();
		loaded_class->valid = true;
	}
	if (ScriptServer::is_scripting_enabled() || p_script->is_tool()) {
		error = p_script->_static_init();
		ERR_FAIL_COND_V_MSG(error != OK, error,
				vformat("Compiled script '%s' failed to run its static initializer.", script_path));
	}
	return OK;
}
