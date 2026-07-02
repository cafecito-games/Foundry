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

#include "foundry_script.h"

#include "core/io/resource.h"
#include "core/io/stream_peer.h"
#include "core/object/script_language.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"

class FSDataType;
class FSFunction;
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
//
// A loader instance carries per-load state (the string table and the intra-file class list), so use
// one instance per `.fsb`; when resolver recursion loads another `.fsb` (e.g. an external base), it
// must do so through a fresh loader.
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

	// Classes local to the `.fsb` being loaded (root + nested subclasses, preorder). Serialized
	// intra-file class indices resolve against this vector without touching the resolver.
	Vector<FoundryScript *> local_classes;
	bool has_static_data = false;
	bool annotated_static_unload = false;

	// A class's base reference parsed from the skeleton section; applied by `load_full` after the
	// whole tree is instantiated (a base may be a later class in preorder).
	struct SkeletonBaseReference {
		uint8_t kind = 0; // 0 = no script base (native only), 1 = local class index, 2 = external script.
		uint32_t local_index = 0;
		String path;
		String fully_qualified_name;
	};

	Error _get_string(uint32_t p_index, String &r_string) const;
	Error _decode_object(StreamPeerBuffer *p_stream, uint8_t p_tag, Variant &r_variant, int p_depth);
	Error _decode_container_type(StreamPeerBuffer *p_stream, ContainerType &r_container_type, int p_depth);
	Error _read_property_info(StreamPeerBuffer *p_stream, PropertyInfo &r_property_info);
	Error _read_method_info(StreamPeerBuffer *p_stream, MethodInfo &r_method_info, int p_depth);
	Error _read_function_body(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *p_function,
			Vector<LoadedLambdaInfo> *r_lambda_info, int p_depth);
	Error _read_script_reference(StreamPeerBuffer *p_stream, Ref<Script> &r_script, bool &r_is_local_class,
			const String &p_context);

	Error _open_script_stream(const Vector<uint8_t> &p_buffer, Ref<StreamPeerBuffer> &r_stream);
	Error _expect_section(StreamPeerBuffer *p_stream, FSBytecodeFormat::SectionId p_section);
	Error _read_dependency_section(StreamPeerBuffer *p_stream, Vector<String> *r_dependencies);
	Error _read_skeleton_class(StreamPeerBuffer *p_stream, const Ref<FoundryScript> &p_class, const String &p_root_path,
			Vector<SkeletonBaseReference> &r_base_references, int p_depth);
	Error _read_member_info(StreamPeerBuffer *p_stream, const String &p_script_path, StringName &r_name,
			FoundryScript::MemberInfo &r_member_info);
	Error _read_type_argument_binding(StreamPeerBuffer *p_stream, FoundryScript::TypeArgumentBinding &r_binding);
	Error _read_annotation_usages(StreamPeerBuffer *p_stream, const String &p_script_path,
			Vector<FoundryScript::AnnotationUsage> &r_usages);
	Error _read_annotation_usage_map(StreamPeerBuffer *p_stream, const String &p_script_path,
			HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &r_annotation_map);
	Error _read_parameter_annotation_map(StreamPeerBuffer *p_stream, const String &p_script_path,
			HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &r_parameter_map);
	Error _read_optional_function(StreamPeerBuffer *p_stream, FoundryScript *p_script, FSFunction *&r_function,
			Vector<LoadedLambdaInfo> *r_lambda_info);
	Error _read_class_body(StreamPeerBuffer *p_stream, FoundryScript *p_script);
	Error _read_witness_section(StreamPeerBuffer *p_stream, FoundryScript *p_script);

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

	// Instantiates the root script's full class tree from the skeleton section only — names, fully
	// qualified names, native base classes, class flags, and the `subclasses` map — the `.fsb`
	// analog of `FSCompiler::make_scripts` plus the shallow-script identity surface. It performs
	// no I/O beyond the buffer and never touches the resolver: base links (including external ones)
	// are deferred to `load_full`, mirroring how `make_scripts` leaves `base` unset.
	Error load_skeleton(const Vector<uint8_t> &p_buffer, const Ref<FoundryScript> &p_script);

	// Reconstructs the complete runnable script graph onto `p_script` (which may or may not have
	// gone through `load_skeleton`; existing skeleton scripts are reused). Requires the resolver.
	// Ordered steps: skeleton, base links (external bases recurse through the resolver — the
	// caller's cache publishing the shell first is what makes cycles work), class bodies (plain
	// data, then functions; external constants resolve inline through the resolver), witness
	// re-registration, then finalization mirroring `reload()`'s tail: `_static_default_init()`,
	// `valid = true`, `_static_init()`. Calling `FSCache::add_static_script` for scripts whose
	// flags report static data is the load-path integration's job; the flags are exposed through
	// `get_has_static_data()`/`get_annotated_static_unload()` after any load call.
	Error load_full(const Vector<uint8_t> &p_buffer, const Ref<FoundryScript> &p_script);

	// Reads the external script/resource paths the `.fsb` references, from the dependency section
	// at the head of the file, without touching any class data.
	Error read_dependencies(const Vector<uint8_t> &p_buffer, Vector<String> &r_dependencies);

	bool get_has_static_data() const { return has_static_data; }
	bool get_annotated_static_unload() const { return annotated_static_unload; }
};
