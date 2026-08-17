/**************************************************************************/
/*  fs_conformance_registry.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "fs_function.h"
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

	// One trait type argument as the declaration side can safely remember it. The registry is a
	// process-global singleton that outlives the parse trees it describes, so it must not hold an
	// `FSParser::DataType` (a borrowed `ClassNode *` plus an owning `Ref<Script>`) and cannot hold an
	// `FSWeakContainerType` (whose `ObjectID` names a compiled script that does not exist yet at
	// analysis time). This is a flattened identity: enough to answer "is this the same type as that
	// one?" with certainty, and nothing else. Anything it cannot represent is `UNKNOWN`, which is an
	// absence of evidence and never a wildcard.
	struct RecordedTypeArgument {
		enum Kind : uint8_t {
			UNKNOWN,
			BUILTIN,
			NATIVE_CLASS,
			SCRIPT_CLASS,
		};
		Kind kind = UNKNOWN;
		bool is_nullable = false;
		Variant::Type builtin_type = Variant::NIL;
		NumericType numeric_type = NumericType::NONE;
		StringName native_class;
		String script_fqcn;
		String script_global_name;
	};

	// The single reduction both sides of the comparison go through, so the recorded side and the
	// expected side can never disagree about what a type is.
	static RecordedTypeArgument reduce_type_argument(const FSParser::DataType &p_type);

	struct Conformance {
		// Alias keys the target can be looked up by (FQCN, global class name, script path). Registration
		// drops any alias that does not identify the target, so what the registry stores may be narrower
		// than what a producer supplied.
		Vector<String> target_keys;
		// The target's fully-qualified class name: the one alias in `target_keys` that identifies the
		// target exactly. The others are shared — every class in a file registers the file's path, and a
		// root class without `class_name` has that path as its FQCN — so a lookup that must not cross
		// class boundaries matches on this instead.
		String target_fqcn;
		// The resource path of the file that defines the target, which is one of `target_keys` so a caller
		// holding nothing but a path can still find the conformance.
		String target_script_path;
		// True when the target is the root class of its file, which is what makes the script-path alias a
		// real identity for it. An inner-class target shares that path with its siblings and with the root
		// class, so registration drops the alias for it. Defaults to the permissive value so a producer
		// that cannot tell keeps the full alias set.
		bool target_is_root_class = true;
		// A direct trait from the declaration or one of its implied supertraits. Implied entries retain
		// the declaring conformance's source, index, and witness map.
		StringName trait_name;
		// The arguments this conformance supplied for `trait_name`, indexed by that trait's own
		// type-parameter ordinals. Empty when the trait is not generic or the declaration supplied none,
		// which is an absence of evidence, never a wildcard. Flattened rather than held as parse types
		// because the registry outlives the parse tree the declaration came from.
		Vector<RecordedTypeArgument> trait_type_arguments;
		String source_file;
		// Position of the declaring `ConformanceNode` in the source file's root-class conformance list.
		// Lets a consumer re-find this conformance in a *live* parse tree instead of dereferencing the
		// borrowed witness nodes below, which a registration can outlive.
		int conformance_index = -1;
		WitnessMap witnesses;
	};

	// Limits which declaring files a caller is allowed to see.
	//
	// A conformance takes effect for code that loads its declaring file, the way an import does. The
	// registry itself is process-global and is filled as a side effect of analyzing whatever files a
	// process happens to touch, so without a filter a conformance would type-check in a file that never
	// loads it and then fail at run time, where nothing registered the witness. A caller that knows its
	// own dependencies installs one of these for the duration of its work; with none installed every
	// entry is visible, which is what the runtime and the tooling that reports on the whole registry
	// want.
	class Visibility {
	public:
		virtual bool can_see(const String &p_source_file) const = 0;
		virtual ~Visibility() = default;
	};

	// Installs a `Visibility` for the current thread until it goes out of scope. Thread-local because
	// parsers are analyzed concurrently and each has its own dependency set. Nests: the previous
	// visibility is restored, so a nested analysis cannot widen an outer one by accident.
	class ScopedVisibility {
		const Visibility *previous = nullptr;

	public:
		explicit ScopedVisibility(const Visibility *p_visibility);
		~ScopedVisibility();
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
		// See `Conformance::target_keys`: registration narrows this to the aliases that identify
		// `target_script`, using the pointer rather than a stored flag to decide.
		Vector<String> target_keys;
		// A direct trait from the declaration or one of its implied supertraits. Every identity emitted
		// from one declaration borrows the same compiled witness functions.
		StringName trait_name;
		// The type arguments this conformance supplied for `trait_name`, indexed by that trait's own
		// type-parameter ordinals. Empty when the trait is not generic or the declaration supplied none,
		// which is an absence of evidence, never a wildcard. Held weakly for the same reason receiver
		// descriptors are: this registry is a process-global singleton that outlives the scripts it
		// describes, and a strong `ContainerType::script` here would keep an argument script reachable
		// past its own unload.
		Vector<FSWeakContainerType> trait_type_arguments;
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
	static thread_local const Visibility *active_visibility;

	// True when the installed `Visibility`, if any, allows `p_source_file`. Guards the queries the type
	// system asks — never the cross-file collision diagnostics, which must see every declaring file to
	// report a duplicate, and never the runtime witness store, whose contents are by definition loaded.
	static bool _is_visible(const String &p_source_file);

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

	// What a runtime membership hit carries besides the fact of membership: the declaring file, and the
	// type arguments the conformance supplied for that trait identity.
	struct RuntimeTraitEntry {
		String source_file;
		Vector<FSWeakContainerType> type_arguments;
	};

	// Runtime-loaded bytecode has no parser tree, so its serialized trait identities also provide the
	// membership index used by `is`/`as` and typed assignment checks.
	HashMap<String, HashMap<StringName, RuntimeTraitEntry>> runtime_trait_index;

	// The declaration-side arguments recorded for `p_target_key`'s visible conformance to
	// `p_trait_name`. Callers must hold `mutex`.
	bool _recorded_trait_arguments_for_key(const String &p_target_key, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	// True when `p_target_key` has a conformance to `p_trait_name` the caller is allowed to see.
	// Callers must hold `mutex`.
	bool _has_visible_conformance(const String &p_target_key, const StringName &p_trait_name) const;

	// The live argument vector recorded for a runtime membership hit, or false when the entry records
	// none or any argument script has been freed. Callers must hold `mutex`.
	bool _live_runtime_type_arguments(const RuntimeTraitEntry &p_entry, Vector<ContainerType> &r_arguments) const;

	void _rebuild_runtime_index();

public:
	static FSConformanceRegistry *get_singleton();

	// Replaces every conformance previously registered by `p_source_file` with `p_conformances`. Alias
	// sets are narrowed to the keys that identify the target (see `Conformance::target_keys`) before
	// they are stored, so every consumer — index lookups, witness scans, and bytecode serialization —
	// observes the same identity rules regardless of which producer built the entry.
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	// Drops every conformance previously registered by `p_source_file`.
	void clear_file(const String &p_source_file);

	void clear();

	// Drops only the declaration side (`conformances_by_file`, `index`) used by analyzer visibility
	// and coherence checks. Deliberately leaves the runtime witness store (`runtime_by_file`,
	// `runtime_index`, `runtime_trait_index`) untouched: those entries are borrowed `FSFunction *`s
	// owned by the declaring script, which already drops them itself on reload, recompile, and
	// teardown. Discarding them here without recompiling the owner would leave a live, still-cached
	// compiled script whose witnesses can no longer be dispatched.
	void clear_declarations();

	// True when some *visible* target alias `p_target_key` declares an external conformance to
	// `p_trait_name`.
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

	// The type arguments a *visible declaration-side* conformance of `p_target_key` to `p_trait_name`
	// recorded. False when no visible conformance exists or it recorded none -- both an absence of
	// evidence, never a wildcard. Deliberately not the runtime store: the static type relation must
	// answer the same way whether or not the declaring file has been compiled yet, and must not see a
	// conformance whose membership it would deny.
	bool get_recorded_trait_arguments(const String &p_target_key, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	// The same, for an engine class. Walks `ClassDB::get_parent_class`; the nearest conforming ancestor
	// wins, matching `native_class_conforms()`.
	bool get_native_recorded_trait_arguments(const StringName &p_native_class,
			const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const;

	// The same, for a builtin value type. Builtins have no inheritance chain, so this is an exact-key
	// lookup.
	bool get_builtin_recorded_trait_arguments(Variant::Type p_type, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	// The type arguments a *runtime-registered* conformance of `p_target_key` to `p_trait_name`
	// supplied. False when no runtime record exists, when the record supplied no arguments, or when any
	// argument script has been freed — all of which are an absence of evidence, not a wildcard. Reads
	// the runtime store only: a declaring file that was analyzed but never compiled registers no
	// witnesses and therefore no arguments, so a specialized target against it fails closed, coherently
	// with the fact that its witnesses would also miss at run time.
	bool get_conformance_type_arguments(const String &p_target_key, const StringName &p_trait_name,
			Vector<ContainerType> &r_arguments) const;

	// The same, for an engine class. Walks `ClassDB::get_parent_class`; the nearest conforming ancestor
	// wins, exactly as `native_class_conforms` and `find_native_trait_witness_function` already do.
	bool get_native_conformance_type_arguments(const StringName &p_native_class,
			const StringName &p_trait_name, Vector<ContainerType> &r_arguments) const;

	// The same, for a builtin value type. Builtins have no inheritance chain, so this is an exact-key
	// lookup.
	bool get_builtin_conformance_type_arguments(Variant::Type p_type, const StringName &p_trait_name,
			Vector<ContainerType> &r_arguments) const;

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

	// Locates the conformance that supplies a witness for `p_method` on the target whose fully-qualified
	// class name is `p_target_fqcn`, reporting the declaring file and the conformance's position in that
	// file's root-class conformance list. Matching is on the exact FQCN, not the looser aliases, so a
	// witness can never be found through a class it was not declared for. Returns identifiers only —
	// never the borrowed witness node, which does not outlive the declaring file's parse tree — so a
	// caller that needs the node can re-find it in a live parse tree.
	bool find_witness_location(const String &p_target_fqcn, const StringName &p_method,
			String &r_source_file, int &r_conformance_index) const;

	// The declaring file and trait of a witness for `(p_target_fqcn, p_method)` that the installed
	// `Visibility` *hides*. The deliberate inverse of the queries the type system asks: it reports
	// exactly the conformances a caller must not type-check against, so an otherwise unresolved call
	// can be rejected with the reason instead of being deferred to a run-time member-miss that finds
	// nothing. Matches on the exact FQCN, like `find_witness_location`, so a diagnostic can never be
	// raised on behalf of an unrelated class that merely shares a file with the target.
	bool find_hidden_witness_declaration(const String &p_target_fqcn, const StringName &p_method,
			String &r_source_file, StringName &r_trait_name) const;

	// Replaces every compiled runtime witness previously registered by `p_source_file`. The
	// `FSFunction *` in `p_conformances` stay owned by the declaring script; the registry borrows
	// them until the next re-registration or `clear_runtime_witnesses`. Alias sets are narrowed the
	// same way `register_file_conformances` narrows them, so a conformance compiled from source and the
	// same conformance restored from bytecode answer identically.
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

	// The compiled witness for `p_method` compiled against exactly `p_target_script`, or `nullptr` when
	// none is registered. Unlike `find_witness_function`, this cannot answer with another class's
	// witness: the alias index it uses holds one function per (alias, method), and two classes in one
	// file share aliases, so an alias hit is neither unique nor complete. Scans the runtime store, so
	// it belongs on a miss path, not a hot one.
	FSFunction *find_witness_function_for_target(const FoundryScript *p_target_script, const StringName &p_method) const;

	// The compiled witness for `p_method` on `p_native_class` or any of its ancestors, or `nullptr` when
	// none is registered. Walks `ClassDB::get_parent_class` so a witness declared on a base engine class
	// dispatches for a subclass instance. Consulted by the runtime after a native call misses.
	FSFunction *find_native_witness_function(const StringName &p_native_class, const StringName &p_method) const;

	// The compiled witness for `p_method` supplied by a conformance of `p_native_class`, or of its
	// nearest conforming ancestor, to `p_trait_name` specifically. Unlike `find_native_witness_function`
	// this cannot answer with a witness another trait happens to supply under the same method name,
	// which matters for a caller that decides whether an object opted into a protocol at all: pairing
	// an unrelated witness with a conformance the caller only *believes* is loaded would silently
	// change behavior. Scans the runtime store, so it belongs on a decision path, not a hot one.
	FSFunction *find_native_trait_witness_function(const StringName &p_native_class,
			const StringName &p_trait_name, const StringName &p_method) const;

	// The compiled witness for `p_method` on builtin type `p_type`, or `nullptr` when none is registered.
	// Consulted by the runtime after a builtin `Variant::callp` misses.
	FSFunction *find_builtin_witness_function(Variant::Type p_type, const StringName &p_method) const;

	FSConformanceRegistry();
	~FSConformanceRegistry();
};
