/**************************************************************************/
/*  fs_bytecode_loader.h                                                  */
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

#include "core/io/resource.h"
#include "core/io/stream_peer.h"
#include "core/object/script_language.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"

class FSDataType;
class FSFunction;
class FoundryScript;
struct MethodInfo;
struct PropertyInfo;

// Resolves the external references a `.fsb` stores symbolically (resource paths, script paths plus
// fully qualified class names). The production resolver routes scripts through `FSCache` and
// resources through `ResourceLoader`; tests substitute stubs so the codecs never touch either.
class FSBytecodeExternalResolver {
public:
	virtual Ref<Resource> resolve_resource(const String &p_path) = 0;
	// `r_is_local_class` must be set to true when the resolved script is a class local to the
	// `.fsb` being loaded (the root script or one of its inner classes); data-type linkage then
	// holds it as a raw pointer without a strong reference.
	virtual Ref<Script> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class) = 0;

	virtual ~FSBytecodeExternalResolver() {}
};

// Reader half of the `.fsb` compiled-bytecode format. Compiled into all builds, including export
// templates.
class FSBytecodeLoader {
public:
	// Lambda metadata read alongside a deserialized function; the caller rebuilds the owning
	// script's `lambda_info` map from these entries.
	struct LoadedLambdaInfo {
		FSFunction *function = nullptr;
		int capture_count = 0;
		bool use_self = false;
	};

private:
	Vector<String> string_table;
	FSBytecodeExternalResolver *resolver = nullptr;

	Error _get_string(uint32_t p_index, String &r_string) const;
	Error _decode_object(StreamPeerBuffer *p_stream, uint8_t p_tag, Variant &r_variant, int p_depth);
	Error _decode_container_type(StreamPeerBuffer *p_stream, ContainerType &r_container_type, int p_depth);
	Error _read_property_info(StreamPeerBuffer *p_stream, PropertyInfo &r_property_info);
	Error _read_method_info(StreamPeerBuffer *p_stream, MethodInfo &r_method_info, int p_depth);
	Error _read_function_body(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *p_function,
			Vector<LoadedLambdaInfo> *r_lambda_info, int p_depth);

public:
	static Error check_header(const Vector<uint8_t> &p_buffer, int *r_header_size = nullptr);

	void set_resolver(FSBytecodeExternalResolver *p_resolver) { resolver = p_resolver; }

	Error read_string_table(StreamPeerBuffer *p_stream);
	String get_string(uint32_t p_index) const;

	Error decode_variant_tagged(StreamPeerBuffer *p_stream, Variant &r_variant, int p_depth = 0);
	Error decode_data_type(StreamPeerBuffer *p_stream, FSDataType &r_data_type, int p_depth = 0);

	// Deserializes and links one compiled function (plus its nested lambdas) written by
	// `FSBytecodeExporter::serialize_function`. On success the caller owns `r_function` and is
	// expected to register it on `p_script` (the FSFunction destructor unregisters itself from the
	// owning script's member-function map by name). Any unresolvable fixup is a hard error that
	// leaves `r_function` null and rolls `r_lambda_info` back to the size it had on entry, since
	// entries appended by the failed call would point at freed functions.
	Error read_function(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *&r_function,
			Vector<LoadedLambdaInfo> *r_lambda_info = nullptr, int p_depth = 0);
};
