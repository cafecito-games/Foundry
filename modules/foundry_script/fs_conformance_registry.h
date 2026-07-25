/**************************************************************************/
/*  fs_conformance_registry.h                                             */
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

#include "fs_parser.h"

#include "core/os/mutex.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class FSFunction;
class FoundryScript;

// Process-global registry of retroactive trait conformances (`extend Target uses Trait: ...`).
//
// A conformance declares, from outside the target's own definition, that a type conforms to a
// trait and supplies the required methods externally as witnesses. The registry answers, by
// stable string identity (target FQCN / global class name) and trait identity name:
//   - does a target conform to a trait?
//   - what are the witness methods for a (target, trait) pair?
//
// Entries are grouped by the source file that declared them so a file can re-register its
// conformances on reload without leaving stale duplicates, mirroring how global classes are
// re-registered. Witnesses are keyed by method name; Phase 3 (compiler/runtime) extends the
// stored witness payload to also hold compiled functions.
class FSConformanceRegistry {
public:
	// Witnesses for a single (target, trait) conformance, keyed by method name. The parser-owned
	// `FunctionNode *` is valid while the declaring file's parse tree is alive; the boolean
	// conformance queries used by the type system never dereference it.
	using WitnessMap = HashMap<StringName, FSParser::FunctionNode *>;

	struct Conformance {
		// Alias keys the target can be looked up by (FQCN, global class name, script path).
		Vector<String> target_keys;
		StringName trait_name;
		String source_file;
		WitnessMap witnesses;
	};

	// Runtime witnesses for a single (target, trait) conformance, keyed by method name. These are
	// compiled `FSFunction *` owned by the conformance-declaring `FoundryScript`; the registry only
	// borrows them and must drop them (via `clear_runtime_witnesses`) when that script is reloaded
	// or unloaded so no dangling pointer is ever dispatched.
	using WitnessFunctionMap = HashMap<StringName, FSFunction *>;

	struct RuntimeConformance {
		// The target whose member layout witness functions use. This explicit pointer also preserves
		// the target identity for marker conformances, whose function map is intentionally empty.
		FoundryScript *target_script = nullptr;
		Vector<String> target_keys;
		StringName trait_name;
		WitnessFunctionMap functions;
	};

	// Validates that a runtime entry cannot serialize witnesses against a different member layout
	// than the target its authoritative aliases denote. Native and builtin targets deliberately use
	// the declaring FoundryScript as a codegen stand-in; that is the only pointer/alias mismatch
	// accepted. Every non-null witness must be owned by the selected target representation.
	static bool validate_runtime_conformance_target(
			const RuntimeConformance &p_conformance,
			const FoundryScript *p_declaring_script,
			const Vector<String> &p_authoritative_target_keys,
			String &r_error);

private:
	static FSConformanceRegistry *singleton;

	mutable Mutex mutex;

	// Owning store, grouped by declaring file so a reload can replace a file's entries wholesale.
	HashMap<String, Vector<Conformance>> conformances_by_file;

	// Flattened lookup index: target alias key -> trait identity name -> declaring file.
	// Rebuilt whenever the owning store changes; registrations/clears are infrequent.
	HashMap<String, HashMap<StringName, String>> index;

	void _rebuild_index();

	// Runtime witness store, grouped by declaring file so a reload/unload can drop a file's compiled
	// witnesses wholesale. Held separately from the parse-tree `conformances_by_file` because the
	// analyzer re-registers the latter on every re-analysis (LSP/completion), which must NOT wipe the
	// compiled functions the runtime dispatches through.
	HashMap<String, Vector<RuntimeConformance>> runtime_by_file;

	// Flattened runtime lookup: target alias key -> method name -> compiled witness function (borrowed).
	HashMap<String, WitnessFunctionMap> runtime_index;

	// Runtime-loaded bytecode has no parser tree, so its serialized trait identities also provide the
	// membership index used by `is`/`as` and typed assignment checks.
	HashMap<String, HashMap<StringName, String>> runtime_trait_index;

	void _rebuild_runtime_index();

public:
	static FSConformanceRegistry *get_singleton();

	// Replaces every conformance previously registered by `p_source_file` with `p_conformances`.
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	// Drops every conformance previously registered by `p_source_file`.
	void clear_file(const String &p_source_file);

	void clear();

	// True when some target alias `p_target_key` declares an external conformance to `p_trait_name`.
	// Analyzer/type-system callers use the parse registry alone. Runtime checks for bytecode-loaded
	// scripts opt into serialized runtime membership with `p_include_runtime`.
	bool has_conformance(const String &p_target_key, const StringName &p_trait_name, bool p_include_runtime = false) const;

	// True when `p_native_class` (a ClassDB-registered engine class) or any of its ancestors declares
	// an external conformance to `p_trait_name`. Native conformances are keyed by the bare class name,
	// and inheritance is honored by walking `ClassDB::get_parent_class` so a subclass instance satisfies
	// a conformance declared on a base class (e.g. a `Sprite2D` value satisfies `extend Node2D uses ...`).
	bool native_class_conforms(const StringName &p_native_class, const StringName &p_trait_name, bool p_include_runtime = false) const;

	// True when the builtin value type `p_type` (keyed by `Variant::get_type_name`) declares an external
	// conformance to `p_trait_name`. Builtins have no inheritance chain, so this is an exact-key lookup.
	bool builtin_type_conforms(Variant::Type p_type, const StringName &p_trait_name, bool p_include_runtime = false) const;

	// The declaring file of the (target, trait) conformance, or an empty string when none exists.
	// Useful for diagnosing cross-file duplicate conformances.
	String get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const;

	// The witnesses for the (target, trait) conformance, or an empty map when none exists.
	WitnessMap get_witnesses(const String &p_target_key, const StringName &p_trait_name) const;

	// Every analyzed conformance `p_source_file` registered, exactly as registered. The parser-owned
	// witness nodes remain borrowed from the declaring file's parse tree.
	Vector<Conformance> get_file_conformances(const String &p_source_file) const;

	// The declaring file and trait identity of a witness for `(target, method)`, or empty values when
	// none exists. Used for diagnosing cross-file witness method-name collisions.
	String get_witness_source(const String &p_target_key, const StringName &p_method, StringName &r_trait_name) const;

	// Replaces every compiled runtime witness previously registered by `p_source_file`. The
	// `FSFunction *` in `p_conformances` stay owned by the declaring script; the registry borrows
	// them until the next re-registration or `clear_runtime_witnesses`.
	void register_runtime_witnesses(const String &p_source_file, const Vector<RuntimeConformance> &p_conformances);

	// Drops every compiled runtime witness previously registered by `p_source_file`. Call this before
	// the declaring script frees the underlying `FSFunction`s so no borrowed pointer dangles.
	void clear_runtime_witnesses(const String &p_source_file);

	// Every compiled runtime witness `p_source_file` registered, exactly as registered. Used by the
	// compiled-bytecode exporter to serialize a declaring script's conformances; the returned
	// `FSFunction *` stay owned by the declaring script.
	Vector<RuntimeConformance> get_runtime_witnesses(const String &p_source_file) const;

	// The compiled witness for `p_method` on a target alias `p_target_key`, or `nullptr` when none is
	// registered. Consulted by the runtime only after a normal member-function lookup misses.
	FSFunction *find_witness_function(const String &p_target_key, const StringName &p_method) const;

	// The compiled witness for `p_method` on `p_native_class` or any of its ancestors, or `nullptr` when
	// none is registered. Walks `ClassDB::get_parent_class` so a witness declared on a base engine class
	// dispatches for a subclass instance. Consulted by the runtime after a native call misses.
	FSFunction *find_native_witness_function(const StringName &p_native_class, const StringName &p_method) const;

	// The compiled witness for `p_method` on builtin type `p_type`, or `nullptr` when none is registered.
	// Consulted by the runtime after a builtin `Variant::callp` misses.
	FSFunction *find_builtin_witness_function(Variant::Type p_type, const StringName &p_method) const;

	FSConformanceRegistry();
	~FSConformanceRegistry();
};
