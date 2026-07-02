/**************************************************************************/
/*  fs_bytecode_export.h                                                  */
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

#pragma once

#include "fs_bytecode_format.h"

#include "core/io/stream_peer.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"

#ifdef TOOLS_ENABLED

class FSDataType;
class FSFunction;
struct MethodInfo;
struct PropertyInfo;

// Writer half of the `.fsb` compiled-bytecode format. Serialization externalizes every
// process-bound object behind a symbolic tag (see `FSBytecodeFormat::VariantTag`), so the produced
// bytes never contain object property dumps, raw pointers, or script source.
class FSBytecodeExporter {
public:
	// Deduplicating table of every string and StringName referenced by the serialized data. All
	// other sections refer to strings by u32 index into this table.
	class StringTable {
		HashMap<String, uint32_t> indices;
		Vector<String> strings;

	public:
		uint32_t insert(const String &p_string);
		void write(StreamPeerBuffer *r_stream) const;
	};

	static Vector<uint8_t> write_header();

	StringTable &get_string_table() { return string_table; }

	Error encode_variant_tagged(StreamPeerBuffer *r_stream, const Variant &p_variant, int p_depth = 0);
	Error encode_data_type(StreamPeerBuffer *r_stream, const FSDataType &p_data_type, int p_depth = 0);

	// Serializes one compiled function, including its nested lambda functions depth-first. Every
	// process-bound pointer table travels as the symbolic keys recorded in
	// `FSFunction::export_fixups`; `OPCODE_STORE_GLOBAL` operands are masked out and travel as
	// {code offset, global name} pairs so the loader must rebake them for its own global map.
	Error serialize_function(StreamPeerBuffer *r_stream, const FSFunction *p_function, int p_depth = 0);

private:
	StringTable string_table;

	Error _encode_object(StreamPeerBuffer *r_stream, Object *p_object, int p_depth);
	Error _encode_container_type(StreamPeerBuffer *r_stream, const ContainerType &p_container_type, int p_depth);
	Error _encode_script_reference(StreamPeerBuffer *r_stream, Script *p_script);
	Error _encode_method_info(StreamPeerBuffer *r_stream, const MethodInfo &p_method_info, int p_depth);
	void _encode_property_info(StreamPeerBuffer *r_stream, const PropertyInfo &p_property_info);
};

#endif // TOOLS_ENABLED
