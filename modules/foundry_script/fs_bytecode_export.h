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

#include "foundry_script.h"

#include "core/io/stream_peer.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
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
//
// Whole-script container layout produced by `serialize()`:
//
//   header (see `write_header`) | u32 script flags | SECTION_STRING_TABLE | SECTION_DEPENDENCIES |
//   SECTION_SKELETON | SECTION_CLASS_BODIES | SECTION_WITNESSES
//
// The string table must precede every section that references it, but its contents are only known
// after those sections are encoded; the writer therefore encodes the skeleton/body/witness sections
// into a scratch buffer first (populating the table as a side effect) and emits the table before
// splicing the scratch bytes into the final buffer. The dependency section lives ahead of the
// skeleton so an exported game's dependency scan reads only the file's head.
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
		void clear();
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

	// Serializes a complete compiled root script — the whole class tree, member layouts, constants,
	// signals, functions, static variables, generics metadata, and conformance witnesses — into a
	// `.fsb` byte buffer. `p_annotated_static_unload` is the parse tree's `@static_unload` flag; it
	// is not recoverable from the compiled script, so the export integration passes it from the
	// cached parser. Both it and the derived has-static-data flag are stored in the script flags for
	// the load integration to consume (`FSCache::add_static_script` is the loader caller's job).
	Error serialize(const Ref<FoundryScript> &p_script, Vector<uint8_t> &r_buffer, bool p_annotated_static_unload = false);

	// Named globals the compiled script reads through the TOOLS-only OPCODE_STORE_NAMED_GLOBAL
	// whose lookup an exported template runtime cannot satisfy. The runtime named-global map only
	// carries the names the language itself registers in every build (the reserved globals, see
	// FSLanguage::get_reserved_global_names); anything else — typically an autoload singleton the
	// editor session registered as a named global — would fail the opcode's map lookup in a
	// template, so the export must refuse to ship the script. Walks every compiled function of
	// the script graph: member functions, implicit/static initializers, conformance witnesses,
	// nested lambdas, and all subclasses. Returned names are sorted for stable error messages.
	static Vector<StringName> collect_unsupported_named_globals(const Ref<FoundryScript> &p_script);

private:
	StringTable string_table;

	// Whole-script serialization state: preorder indices of the classes local to the script being
	// serialized (root + nested subclasses), and the external paths its serialized references name.
	HashMap<const Script *, uint32_t> local_class_indices;
	Vector<String> external_dependencies;
	HashSet<String> external_dependency_set;

	void _record_external_dependency(const String &p_path);
	void _index_local_classes(const FoundryScript *p_class);

	static void _collect_unsupported_named_globals_from_function(const FSFunction *p_function, HashSet<StringName> &r_names);
	static void _collect_unsupported_named_globals_from_class(const FoundryScript *p_class, HashSet<StringName> &r_names);

	Error _encode_object(StreamPeerBuffer *r_stream, Object *p_object, int p_depth);
	Error _encode_container_type(StreamPeerBuffer *r_stream, const ContainerType &p_container_type, int p_depth);
	Error _encode_script_reference(StreamPeerBuffer *r_stream, Script *p_script);
	Error _encode_method_info(StreamPeerBuffer *r_stream, const MethodInfo &p_method_info, int p_depth);
	void _encode_property_info(StreamPeerBuffer *r_stream, const PropertyInfo &p_property_info);

	Error _write_skeleton_class(StreamPeerBuffer *r_stream, const FoundryScript *p_class, int p_depth);
	Error _write_class_bodies(StreamPeerBuffer *r_stream, const FoundryScript *p_class, int p_depth);
	Error _write_class_body(StreamPeerBuffer *r_stream, const FoundryScript *p_class);
	Error _write_member_info(StreamPeerBuffer *r_stream, const StringName &p_name, const FoundryScript::MemberInfo &p_member_info);
	Error _write_type_argument_binding(StreamPeerBuffer *r_stream, const FoundryScript::TypeArgumentBinding &p_binding);
	Error _write_annotation_usages(StreamPeerBuffer *r_stream, const Vector<FoundryScript::AnnotationUsage> &p_usages);
	Error _write_annotation_usage_map(StreamPeerBuffer *r_stream, const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &p_annotation_map);
	Error _write_parameter_annotation_map(StreamPeerBuffer *r_stream, const HashMap<StringName, HashMap<StringName, Vector<FoundryScript::AnnotationUsage>>> &p_parameter_map);
	Error _write_optional_function(StreamPeerBuffer *r_stream, const FSFunction *p_function);
	Error _write_witness_section(StreamPeerBuffer *r_stream, const FoundryScript *p_script);
};

#endif // TOOLS_ENABLED
