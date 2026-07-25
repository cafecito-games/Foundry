/**************************************************************************/
/*  fs_bytecode_format.h                                                  */
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

#include "core/typedefs.h"

// Constants shared by the `.fsb` compiled-bytecode writer (`FSBytecodeExporter`) and reader
// (`FSBytecodeLoader`). A `.fsb` ships the serialized compiled `FoundryScript` object graph, so an
// exported game never tokenizes, parses, analyzes, or compiles source. Original source structure,
// function-local variable names, parameter names outside `method_info`, and comments are not
// serialized and cannot be recovered.
//
// Accepted residual information: the VM dispatches by StringName at runtime, so any faithful
// compiled form retains:
//
// - Member/method/signal names used by name-dispatched opcodes (`global_names`).
// - `MethodInfo` names and argument names (reflection, `Callable`, RPC, virtual dispatch).
// - String/NodePath/signal-name constants.
// - Engine API names inside pointer-fixup keys.
//
// Stripping these is a separate follow-up epic (an export-time whole-program name mangler); this
// format only guarantees that nothing outside the list above leaks into the serialized bytes.
class FSBytecodeFormat {
public:
	static constexpr uint8_t MAGIC[4] = { 'F', 'S', 'B', 'C' };
	// Bump on ANY layout change; the reader rejects other versions outright.
	static constexpr uint32_t FORMAT_VERSION = 2;

	enum SectionId : uint32_t {
		SECTION_STRING_TABLE,
		SECTION_EXTERNAL_REFS,
		SECTION_SKELETON,
		SECTION_CLASS_BODIES,
		SECTION_WITNESSES,
		SECTION_DEPENDENCIES,
		SECTION_MAX,
	};

	enum VariantTag : uint8_t {
		TAG_INLINE_VARIANT, // encode_variant payload, full_objects = false.
		TAG_ARRAY, // Read-only flag, then element-wise recursion (typed metadata included).
		TAG_DICTIONARY, // Read-only flag, then element-wise recursion (typed metadata included).
		TAG_SCRIPT_REF, // Foundry Script reference as (path, fully qualified class name).
		TAG_EXTERNAL_SCRIPT, // Non-Foundry Script reference as (path, empty fully qualified name).
		TAG_EXTERNAL_RESOURCE, // Resource reference as (path); property data is never serialized.
		TAG_NATIVE_CLASS, // FSNativeClass value as (class name).
		TAG_ENGINE_SINGLETON, // Engine singleton object as (singleton name).
		TAG_SPECIALIZED_HANDLE, // FSSpecializedClassHandle as (script reference, type arguments).
		TAG_NULL_OBJECT, // Object-typed null (e.g. the script slot of a typed-container descriptor).
		TAG_DEFAULT_VALUE, // Default-constructed value as (Variant type); the process-bound types (Callable, Signal, RID) have a portable empty value.
		TAG_UTILITY_CALLABLE, // Utility-function Callable (FSUtilityCallable) as (function name); rebuilt by name at load.
		TAG_REFLECTION_SINGLETON, // The language's FSReflection singleton (the `reflection` member of the reflection namespace).
		TAG_REFLECTION_NAMESPACE, // The language's FSNamespace singleton (the reflection namespace global).
	};

	enum FixupTable : uint8_t {
		FIXUP_OPERATOR,
		FIXUP_SETTER,
		FIXUP_GETTER,
		FIXUP_KEYED_SETTER,
		FIXUP_KEYED_GETTER,
		FIXUP_INDEXED_SETTER,
		FIXUP_INDEXED_GETTER,
		FIXUP_BUILTIN_METHOD,
		FIXUP_CONSTRUCTOR,
		FIXUP_UTILITY,
		FIXUP_GDS_UTILITY,
		FIXUP_METHOD_BIND,
		FIXUP_MAX,
	};
};
