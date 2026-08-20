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
#include "core/templates/hash_set.h"
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
	//
	// A composite argument keeps its shape: `Array[int]` records the container's own identity plus one
	// child per element, and `Pair[int, U]` records `Pair` plus a known `int` and an `UNKNOWN`. Each
	// component carries its own certainty, so a contradiction in a known component is still seen even
	// when a sibling is unrepresentable.
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
		// Components of a composite argument, each recorded by the same reduction as its parent. An
		// empty vector means the argument declared none, which is why arity is compared before the
		// components are: a side that declared no components states nothing about them.
		Vector<RecordedTypeArgument> type_arguments;
		Vector<RecordedTypeArgument> container_element_types;
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
		// The engine class the target's inheritance chain bottoms out at, for a script-class target. A
		// conformance declared anywhere on that engine chain answers for this target's receivers too, so
		// the coherence rule needs the chain's terminus without re-loading the target's parse tree.
		// Empty for an engine or builtin target, and for a producer that could not resolve the chain,
		// which states nothing rather than placing the target on `Object`'s chain.
		StringName target_native_base;
		// The fully-qualified class names of the script classes above a script-class target on its own
		// inheritance chain, nearest first. One semantic chain runs from a script class through its script
		// bases into the engine ancestry they end on, so the coherence rule needs the script half the way
		// `target_native_base` gives it the engine half — without re-loading the target's parse tree.
		// Empty for an engine or builtin target, for a target with no script base, and for a producer that
		// could not follow the chain, all of which state nothing rather than claiming an ancestor.
		Vector<String> target_script_ancestor_fqcns;
		// The target's declared name, as a diagnostic should spell it. The FQCN identifies the target but
		// reads as a path for a file-scoped class, so it is kept separately rather than reconstructed.
		String target_label;
		String source_file;
		// Position of the declaring `ConformanceNode` in the source file's root-class conformance list.
		// Lets a consumer re-find this conformance in a *live* parse tree instead of dereferencing the
		// borrowed witness nodes below, which a registration can outlive.
		int conformance_index = -1;
		WitnessMap witnesses;
	};

	// One class's own `uses` clause, as another file's coherence check needs to see it.
	//
	// A class that applies a generic trait through `uses` fixes that trait's arguments for every receiver
	// on its inheritance chain exactly as a conformance declared on that chain would, but it registers no
	// `Conformance`: it declares no external membership and supplies no witnesses. Without a record of it,
	// a declaration in another file could contradict the binding and be accepted, and a value widened to
	// the contradicting side would then dispatch a witness written for the other argument list.
	//
	// These records exist for that comparison alone. They answer no membership, witness, argument, or
	// runtime query, and only a binding that actually supplied arguments is recorded: an empty argument
	// list is an absence of evidence, never a wildcard.
	struct ClassTraitBinding {
		// The binding class's fully-qualified name and the way a diagnostic should spell it, mirroring
		// `Conformance::target_fqcn` / `Conformance::target_label`.
		String target_fqcn;
		String target_label;
		// See `Conformance::target_native_base` and `Conformance::target_script_ancestor_fqcns`: the two
		// halves of the class's semantic chain, so another file can decide the chain relation without
		// re-loading this file's parse tree.
		StringName target_native_base;
		Vector<String> target_script_ancestor_fqcns;
		// A trait identity from the class's `uses` closure, and the arguments the class binds it to.
		StringName trait_name;
		// How that identity should be spelled in a diagnostic. An identity name is fully qualified for a
		// trait declared without an explicit name, so the readable form is remembered where the parse tree
		// that has it is at hand.
		String trait_label;
		Vector<RecordedTypeArgument> trait_type_arguments;
		String source_file;
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

	// Hides one declaring file from *this thread's* registry queries until it goes out of scope.
	//
	// A file being reanalyzed must not read its own previous declarations back as part of the surface it
	// is validating against: a witness would find its own stale registration as the method it overrides
	// and contradict itself, and the coherence rules would see the file arguing with the version of
	// itself they are about to replace. The entries stay in the store, so every other reader keeps
	// seeing the file's conformances right up to the moment its replacement commits; only the thread
	// performing that replacement looks past them.
	//
	// Nests, restoring the previous file, so a nested analysis cannot un-hide an outer one's file.
	class ScopedInFlightReplacement {
		String previous;

	public:
		explicit ScopedInFlightReplacement(const String &p_source_file);
		~ScopedInFlightReplacement();
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

	// One candidate declaration the registry refused, as a value a caller can turn into a diagnostic
	// after the registry lock is released.
	//
	// Deliberately holds no parser or AST pointer. The registry outlives every parse tree it describes
	// and runs its comparisons while holding the mutex, so a record that borrowed a node would either
	// dangle or force diagnostics to be produced under the lock. `conformance_index` is the position of
	// the rejected `ConformanceNode` in the submitting file's root-class conformance list, which is what
	// lets the caller re-find the node in the live tree it already holds.
	struct RegistrationConflict {
		enum Kind : uint8_t {
			// Another file already registered this (target, trait) membership.
			DUPLICATE_MEMBERSHIP,
			// Another file already supplies a witness for this method on this target.
			WITNESS_COLLISION,
			// Another declaration on the same native or mixed script/native ancestry applies the trait
			// with contradicting type arguments.
			CHAIN_COHERENCE,
		};
		Kind kind = DUPLICATE_MEMBERSHIP;
		int conformance_index = -1;
		// The candidate's own target, as a diagnostic should spell it, and the trait identity the
		// conflict was found on. The caller renders the trait's own label from the identity.
		String target_label;
		StringName trait_name;
		// The colliding witness method, for `WITNESS_COLLISION` only.
		StringName method_name;
		// The already-registered side: how the conflicting target should be spelled, and the file that
		// declared it. The declaring file may be the submitting file itself, when one of its own
		// accepted declarations is what the rejected one contradicts.
		String conflicting_target_label;
		String conflicting_source_file;
	};

	// One class-`uses` binding the registry found contradicted by a conformance already registered by a
	// file this one loads. The binding is still stored: a `uses` clause is source, not a declaration the
	// registry may refuse, and dropping it would leave the chain it binds recorded nowhere at all, which
	// is exactly the blindness these records exist to remove.
	//
	// This is the mirror of `RegistrationConflict::CHAIN_COHERENCE`. Together they make the verdict
	// independent of which of the two files reaches the mutex first: whichever side is published second
	// sees the other and reports. Anchored by the binding class's fully-qualified name rather than by a
	// `ConformanceNode` index, because a `uses` clause is not a conformance and the diagnostic belongs on
	// the class that wrote it.
	struct BindingConflict {
		String target_fqcn;
		String target_label;
		StringName trait_name;
		String trait_label;
		String conflicting_target_label;
		String conflicting_source_file;
	};

	// The outcome of one atomic validate-and-replace. `registered_count` counts the entries actually
	// stored, which is the candidate set minus every entry belonging to a rejected declaration.
	struct RegistrationResult {
		Vector<RegistrationConflict> conflicts;
		// Contradictions found for the submitted bindings. Reported, never arbitrated: every binding is
		// stored regardless of what appears here.
		Vector<BindingConflict> binding_conflicts;
		int registered_count = 0;
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
	// The declaring file whose replacement this thread is currently computing, if any.
	static thread_local String in_flight_source_file;

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

	// Declaration-side class-`uses` bindings, grouped by declaring file so a reload replaces them
	// wholesale alongside that file's conformances. Deliberately not indexed with `index`: these records
	// state nothing about membership and must never answer a lookup that decides whether a type conforms.
	HashMap<String, Vector<ClassTraitBinding>> trait_bindings_by_file;

	// The files each conformance-declaring file loads, as that file itself resolved them. Visibility is
	// directional -- a thread analyzing a file can only see what that file loads -- so a file publishing
	// a binding cannot tell whether some already-registered conformance belongs to a file that loads it.
	// This is what lets it ask: the load edge that licenses the comparison is recorded by the side that
	// has it, and stays readable from the other end no matter which side reaches the mutex first.
	//
	// Only files that declare conformances submit one, which is the only side these edges are consulted
	// for and keeps the store to the handful of files that use `extend` at all.
	HashMap<String, HashSet<String>> loaded_files_by_file;

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

	// The conflict, if any, that rejects `p_candidate`. `p_view` is every entry the candidate must agree
	// with: the store minus the submitting file's own previous entries, plus the candidates this
	// submission has already accepted. The pointers are borrowed for the duration of one locked
	// replacement and never stored. Callers must hold `mutex`.
	bool _candidate_conflicts(const Conformance &p_candidate, const String &p_source_file,
			const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const;

	// The chain-coherence conflict, if any, between one candidate and a class-`uses` binding recorded by
	// another file the candidate may see. Callers must hold `mutex`.
	bool _candidate_conflicts_with_trait_binding(const Conformance &p_candidate, const String &p_source_file,
			RegistrationConflict &r_conflict) const;

	// True when `p_loader` recorded a load edge to `p_loaded`. Callers must hold `mutex`.
	bool _file_loads(const String &p_loader, const String &p_loaded) const;

	// The mirror: the chain-coherence conflict, if any, between one submitted binding and a conformance
	// declared by a file that either the binding's file loads, or that loads the binding's file. `p_view` is the same borrowed set the
	// candidates are judged against, so a conformance this submission is publishing in the same call is
	// compared too. Callers must hold `mutex`.
	bool _binding_conflicts_with_conformance(const ClassTraitBinding &p_binding, const String &p_source_file,
			const Vector<const Conformance *> &p_view, BindingConflict &r_conflict) const;

	// The witness-name collision, if any, between one candidate declaration and `p_view`. Checked once
	// per declaration because every entry a declaration emits borrows the same witness map. Callers must
	// hold `mutex`.
	bool _declaration_witnesses_collide(const Conformance &p_candidate, const Vector<const Conformance *> &p_view,
			RegistrationConflict &r_conflict) const;

	// The declaration-side arguments recorded for `p_target_key`'s visible conformance to
	// `p_trait_name`. Callers must hold `mutex`.
	bool _recorded_trait_arguments_for_key(const String &p_target_key, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	// True when `p_target_key` has a conformance to `p_trait_name` the caller is allowed to see.
	// Callers must hold `mutex`.
	bool _has_visible_conformance(const String &p_target_key, const StringName &p_trait_name) const;

	// The argument vector recorded for a runtime membership hit, or false when the entry records none.
	// Arity is always preserved: a position whose argument script has been freed materializes as an
	// unconstrained descriptor instead of dropping the whole vector. Callers must hold `mutex`.
	bool _runtime_type_arguments(const RuntimeTraitEntry &p_entry, Vector<ContainerType> &r_arguments) const;

	void _rebuild_runtime_index();

public:
	static FSConformanceRegistry *get_singleton();

	// Replaces every conformance previously registered by `p_source_file` with `p_conformances`. Alias
	// sets are narrowed to the keys that identify the target (see `Conformance::target_keys`) before
	// they are stored, so every consumer — index lookups, witness scans, and bytecode serialization —
	// observes the same identity rules regardless of which producer built the entry.
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	// Validates `p_candidates` against the rest of the registry and replaces `p_source_file`'s entries
	// with the ones that survive, as one indivisible step.
	//
	// The check-then-register shape this replaces could not be made correct by locking each half: two
	// analyzers reanalyzing two files could both observe the absence of the other's conformance and both
	// publish, leaving the registry holding an incoherent pair no single-file analysis can detect
	// afterwards. Under one lock the answer is decided against the store as it actually is at the moment
	// of the write.
	//
	// The view a candidate is judged against deliberately excludes `p_source_file`'s *previous* entries,
	// so reanalysis of an unchanged file never conflicts with itself, and includes the candidates this
	// same call has already accepted, so two declarations in one file are held to the same rule two
	// declarations in two files are. Those previous entries stay visible to every other reader until the
	// replacement commits: reanalysis opens no window in which the file's conformances do not exist.
	//
	// Rejection granularity is one `ConformanceNode`: a conflict on any identity or witness a declaration
	// emits registers none of that declaration's entries, including the ones its implied supertraits
	// produced, because a partially registered declaration would answer some membership queries with a
	// conformance the program was told it does not have. Other declarations in the same file are
	// unaffected, and for non-conflicting declarations the outcome does not depend on submission order.
	//
	// An empty `p_candidates` is a replacement like any other: it drops the file's entries.
	//
	// Returns value-only conflict records. Diagnostics must be produced from them after this call
	// returns, never from inside the registry, which holds the mutex and knows nothing about source
	// locations.
	//
	// `p_trait_bindings` is the file's class-`uses` bindings, replaced in the same indivisible step. They
	// are never rejected — a `uses` clause is not a declaration the registry arbitrates — but they take
	// part in deciding the candidates, so a file cannot publish a conformance that contradicts a binding
	// another file published a moment earlier.
	//
	// `p_loaded_files` is the set of files `p_source_file` loads, recorded when it declares conformances
	// so that a file publishing a binding later can still tell that this file's conformance was licensed
	// to be compared against it.
	RegistrationResult try_replace_file_conformances(const String &p_source_file, const Vector<Conformance> &p_candidates,
			const Vector<ClassTraitBinding> &p_trait_bindings = Vector<ClassTraitBinding>(),
			const HashSet<String> &p_loaded_files = HashSet<String>());

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

	// One declaration-side conformance of an engine class to a trait, as a coherence diagnostic needs
	// to see it: which class was extended, which file said so, and what arguments it recorded.
	struct NativeConformanceRecord {
		StringName native_class;
		String source_file;
		Vector<RecordedTypeArgument> trait_type_arguments;
	};

	// Every ClassDB-registered engine class with a declared conformance to `p_trait_name`. Unfiltered by
	// default, like the other cross-file collision diagnostics: a contradiction between two engine
	// declarations is a property of the program, not of what one file happens to load.
	//
	// `p_visible_only` restricts the answer to declarations the installed `Visibility` allows, which is
	// what a *script* class must ask: an engine conformance reaches it the way an import does, so one it
	// never loads must not decide how it may bind a trait. `p_excluded_source_file` drops one declaring
	// file, for a caller that is re-analyzing that file and holds its current declarations itself.
	Vector<NativeConformanceRecord> get_native_conformance_records(const StringName &p_trait_name,
			bool p_visible_only = false, const String &p_excluded_source_file = String()) const;

	// One declaration-side conformance of a Foundry Script class to a trait, plus the engine class its
	// inheritance chain bottoms out at, which is what places it on an engine chain at all.
	struct ScriptConformanceRecord {
		String target_fqcn;
		String target_label;
		StringName target_native_base;
		// See `Conformance::target_script_ancestor_fqcns`. Carried so a caller can decide the script half
		// of the chain relation in both directions: the target may be an ancestor of the class asking, or
		// a descendant of it.
		Vector<String> target_script_ancestor_fqcns;
		String source_file;
		Vector<RecordedTypeArgument> trait_type_arguments;
	};

	// Every script-class conformance to `p_trait_name` whose target resolved a terminal engine class.
	// Filtered the same way `get_native_conformance_records` is.
	Vector<ScriptConformanceRecord> get_script_conformance_records(const StringName &p_trait_name,
			bool p_visible_only = false, const String &p_excluded_source_file = String()) const;

	// Every class-`uses` binding of `p_trait_name` recorded by an analyzed file. Filtered exactly the way
	// `get_script_conformance_records` is, and for the same reason: a binding reaches the class asking the
	// way an import does, so one it never loads must not decide how it may bind the trait.
	Vector<ClassTraitBinding> get_script_trait_binding_records(const StringName &p_trait_name,
			bool p_visible_only = false, const String &p_excluded_source_file = String()) const;

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
