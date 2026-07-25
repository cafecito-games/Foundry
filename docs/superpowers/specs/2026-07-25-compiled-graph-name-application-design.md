# Compiled-Graph Name Application Design

## Goal

Issue #797 adds the reusable application stage for the `.fsb` phase-2 name mangler. Given the
complete set of compiled Foundry Script roots for one export and the single project-wide
`FSNameManglerAnalysis::Result::rename_map`, the stage rewrites every serialized project-identifier
occurrence consistently before `FSBytecodeExporter::serialize`.

The stage must preserve runtime behavior, existing `.fsb` format and loader behavior, resource
paths, and every name absent from the map. It must also leave live editor scripts exactly as it
found them after serialization or any failure.

## Approaches Considered

### Validated scoped transaction over the closed compiled graph (selected)

`FSNameManglerApplication::Transaction` snapshots the compiled fields and conformance-registry
entries it will change, validates the complete transformation before mutation, applies the map to
every supplied root, and restores the snapshot explicitly or from its destructor. All roots are
active under the same map while the caller serializes them one by one, so cross-file identities and
dispatch keys agree.

This is the smallest reusable boundary that lets #799 stage cached editor scripts safely without
changing the writer or loader.

### Deep clone through the existing writer and loader

A serialize/load clone would avoid temporary mutation, but cloning itself needs the final renamed
cross-root identities and an external resolver capable of linking mutually dependent roots. Loading
also registers conformance witnesses globally. Building and maintaining that second orchestration
path would duplicate much of #799 before the application stage exists.

### Rename overlay inside `FSBytecodeExporter`

An exporter-owned overlay could translate strings while writing, but it would distribute policy
across every encoding helper, would not expose a directly testable compiled graph, and would not be
the reusable pre-serialization application stage required by #797.

## Atomic Analysis Contract Correction

The #795 design and `fs_name_mangler_analysis.h` define `rename_map` keys as atomic declaration
identifiers. The merged implementation contradicts that contract by also classifying a registered
global name such as `game.actors.Player` as one flat candidate with an unrelated replacement.

#797 corrects that prerequisite:

- a class contributes its atomic `local_name` once;
- `global_name` and `fully_qualified_name` are structured identities, never flat map keys;
- `@keep_name` and a matching class keep rule add evidence to the atomic class name; and
- existing #795/#796 tests and documentation are updated to make atomic candidates plus structured
  reconstruction the only authoritative behavior.

The application stage therefore rejects map sources containing namespace, nesting, path, or generic
syntax. It never replaces an entire FQCN blindly.

## Public API and Lifecycle

The TOOLS-only component lives in
`modules/foundry_script/fs_name_mangler_application.{h,cpp}`:

```cpp
class FSNameManglerApplication {
public:
	struct Diagnostic {
		String surface;
		StringName source_name;
		String message;

		String format() const;
	};

	class Transaction {
	public:
		enum State {
			STATE_UNUSED,
			STATE_ACTIVE,
			STATE_FINISHED,
		};

		Error begin(const Vector<Ref<FoundryScript>> &p_scripts,
				const RBMap<StringName, StringName> &p_rename_map,
				Vector<Diagnostic> &r_diagnostics);
		void rollback();
		bool is_active() const;
		State get_state() const;

		Transaction();
		~Transaction();
		Transaction(const Transaction &) = delete;
		Transaction &operator=(const Transaction &) = delete;
		Transaction(Transaction &&) = delete;
		Transaction &operator=(Transaction &&) = delete;
	};
};
```

A transaction is single-use. `begin()` is valid only in `STATE_UNUSED`; any second call returns
`ERR_ALREADY_IN_USE` with a diagnostic. A failed begin moves directly to `STATE_FINISHED` without
ever becoming active or publishing a partial snapshot. A successful begin moves to `STATE_ACTIVE`.
`rollback()` is idempotent and non-failing, restores the graph and registry when active, clears the
snapshot, and moves to `STATE_FINISHED`. The destructor calls `rollback()`.

The contract deliberately does not expose a commit operation: the live graph is always restored.
The only durable output is the `.fsb` buffer produced while the transaction is active.

## Threading and Active-Lifetime Boundary

The transaction is a synchronous main-thread/editor export primitive. `begin()`, all serialization,
and rollback run on the same thread. It does not lock or re-key editor caches, global-class indexes,
or unrelated script-language state.

While active, the caller may only:

1. synchronously call `FSBytecodeExporter::serialize` for roots indexed by the transaction; or
2. in focused tests, synchronously execute already-compiled code from that same staged graph to
   prove runtime parity.

The caller must not pump the editor event loop, reload/recompile scripts, mutate caches or the
compiled graph, invoke unrelated project callbacks, hand the graph to another thread, or nest
another name-application transaction. #799 must keep this scope narrow around serialization and
must roll back before returning to normal editor work.

## Closed-Graph Preflight

`begin()` first indexes every supplied root and nested class without mutation. Root order is
semantically irrelevant. Null roots, duplicate roots, inner classes supplied as roots, or duplicate
class pointers are invalid.

The supplied roots must form a closed Foundry Script graph. Preflight traverses:

- Foundry Script bases and nested classes;
- Foundry Script references in constants, static/default values, containers, and specialized class
  handles;
- script references in `FSDataType`, member type-argument bindings, and ancestor-binding keys;
- witness target scripts and each conformance witness function's target script;
- known trait identities and conformance target/trait identities; and
- any other Foundry Script object reference that the existing `.fsb` writer will serialize.

Every referenced Foundry Script root must be represented in `p_scripts`. A non-Foundry Script
resource remains an external dependency and is not renamed. An omitted Foundry Script base,
preload, trait, conformance target, or other cross-file dependency returns
`ERR_INVALID_PARAMETER`. This forces #799 to pass the same closed graph that #795 analyzed and
prevents one file from serializing a renamed reference to an unrenamed target.

Analysis and application use the same conservative boundary for identities that are not proven to
belong to that graph. They recursively inspect `ContainerType`, `FSDataType`, `PropertyInfo`, and
`Variant` surfaces, including typed Array/Dictionary metadata, nested generic arguments,
`PackedStringArray` values, external Script metadata, Resource paths, and specialized class
handles. Numeric and geometric packed arrays are explicitly non-textual. A container cycle is
visited once; exceeding the engine recursion limit makes the surface unscannable and protects every
candidate rather than silently omitting evidence.

Resource paths identify files, not declarations. `res://`, `uid://`, and other serialized resource
paths remain byte-for-byte unchanged even when a path segment happens to equal a mapped name.

## Map and Collision Validation

Validation completes before any graph or registry mutation. Diagnostics are produced in stable map
and canonical-class order and identify the source name and exact surface.

Preflight rejects:

- an empty source or replacement;
- a non-Unicode-identifier source or replacement;
- a composite source containing namespace, nesting, path, nullable, or generic syntax;
- identity mappings and duplicate replacement values;
- a replacement that equals any observed serialized project identifier;
- a source that does not occur on a project declaration/dispatch surface in the closed graph;
- a source that appears on an RPC, native, Variant, ClassDB, builtin, utility, named-global,
  global-store/autoload, resource-path, or other protected surface;
- any transformed map/set/dictionary that would contain duplicate keys;
- any two transformed classes that would share a local sibling key, registered global identity, or
  fully-qualified identity; and
- any transformed annotation/parameter/conformance table with ambiguous keys.

Protected-name occurrence wins even if the same spelling also appears on an otherwise manglable
project declaration. The stage fails loudly rather than guessing which occurrence the map meant.
Maps produced by the conservative #795 analysis avoid those ambiguous spellings.

The preflight computes the complete transformed graph and registry key plan. After it succeeds, the
mutation phase consists only of assignments and collision-free container rebuilds; it has no
recoverable error path.

## Structured Identity Rewriting

The application rewrites atomic mapped class segments while preserving surrounding syntax:

```text
game.actors.Player                  -> game.actors._fsb_4
game.actors.Player::Inventory       -> game.actors._fsb_4::_fsb_7
res://actors/player.fs::Inventory   -> res://actors/player.fs::_fsb_7
game.Box[game.items.Item]           -> game._fsb_2[game.items._fsb_9]
res://actors/player.fs              -> res://actors/player.fs
```

Known input-class aliases drive identity rewriting. External native/builtin identities and external
project identities are either protected or rejected by closed-graph validation; they are never
opportunistically token-replaced.

For every included nested class, the alias plan contains its local/global/fully-qualified spellings
and, when the fully-qualified identity begins with that script's exact path plus `::`, the
path-stripped relative spelling. Exact whole-identity aliases are tried before path handling.
Remaining aliases are ordered longest-first and applied in one left-to-right pass, so a replacement
is never interpreted as new input. A foreign path-qualified identity is not stripped against an
included script path even when its terminal `Outer::Inner::Leaf` spelling is identical; its
components remain protected as external evidence.

Compiled enum value dictionaries authoritatively add their exact `Owner.Enum` and `Owner::Enum`
aliases for local, global, fully-qualified, and path-stripped relative owners. Analysis recognizes
only those exact owned aliases as internal. An enum identity owned by an omitted or foreign class
therefore remains external evidence even when its terminal enum name matches an included enum.

Class/script references stored as pointers remain pointers. Once every root is staged, the existing
writer naturally emits each external Foundry Script reference as its unchanged path plus transformed
FQCN. Local classes continue to use the existing preorder index representation.

## Rewritten Compiled Surfaces

### Classes and members

For every included `FoundryScript`, the transaction rewrites:

- atomic `local_name` and structured `global_name`, `fully_qualified_name`, `trait_type_name`, and
  direct trait identities;
- `subclasses` keys;
- flattened `member_indices`, direct `members`, `static_variables_indices`, and editor/default-value
  maps keyed by those declarations;
- every corresponding `MemberInfo::property_info.name`, project setter/getter name, recursive data
  type, and type-argument binding;
- `constants` keys, including the outer keys that hold named-enum dictionaries;
- `_signals` keys and `MethodInfo` names;
- `member_functions` keys, enum-table keys, enum function-map keys, and compiled function names;
- `abstract_trait_requirements` keys and their `MethodInfo` names;
- project-keyed RPC configuration entries, subject to the protected RPC rule below;
- method, variable, signal, and constant annotation-map keys; and
- method/signal parameter-annotation owner keys and safely renamed parameter keys.

Annotation usage `name` and `qualified_name` fields are serialized identity evidence, not annotation
map keys. Both fields protect matching declaration atoms and reserve their spellings against use as
replacements. They remain byte-for-byte unchanged while an unrelated safe declaration is staged.

Static values, defaults, and constants are not textually rewritten. Script objects nested inside
them observe their staged identities through their referenced `FoundryScript`; arbitrary String,
StringName, NodePath, Callable, Signal, annotation argument, dictionary key, and resource-path values
remain exact. #795 already keeps a declaration whose spelling appears in such dynamic evidence.

### Functions, lambdas, and dispatch operands

For every member, enum-host, implicit, witness, and recursively nested lambda `FSFunction`, the
transaction rewrites:

- a mapped project function name and matching `method_info.name`;
- recursive argument/return `FSDataType` project trait identities;
- safe `MethodInfo` argument names as defined below; and
- `global_names` entries that resolve to mapped project declarations or known included class
  identities.

The bytecode instructions continue to index the same `global_names` slots, so CALL, GET_NAMED,
SET_NAMED, enum-host calls, and cross-class dispatch resolve the transformed spelling without opcode
or format changes. `setup_runtime_pointers()` is called after staging and after rollback so vector
mirror pointers cannot drift.

### Trait conformances and witnesses

The compiler registers one `RuntimeConformance` per declared trait in deterministic `uses` source
order, copying the shared compiled witness-function map into each entry and setting a non-empty
`trait_name`. This makes ordinary (non-mangled) version-3 bytecode self-sufficient for runtime
membership after the parser registry is absent. A multi-trait conformance therefore writes one
existing-format witness entry per trait; the wire layout and format version do not change.

The transaction snapshots every unique `registered_conformance_source` entry, rewrites known project
target aliases and trait identities structurally, rewrites witness-map keys and witness function
metadata, and temporarily re-registers the staged entries so both runtime dispatch and
`FSBytecodeExporter::_write_witness_section` see one coherent view. Correlation still accepts an
empty runtime trait identity defensively and expands it against exact parse target/witness matches,
because legacy version-3 bytecode and manually registered state may contain that old representation.

Rollback re-registers the exact saved entries under the unchanged source path. Validation failure
does not call the registry. Tests compare the full registry entry vectors and live dispatch before,
during, and after staging.

The loader keeps accepting legacy version-3 witness entries with an empty trait-name field. Their
existing witness dispatch remains usable, but runtime `is`/`as`/typed membership cannot be recovered
when no parser registration exists because those bytes contain no trait identity. New compiler and
Transaction exports always populate the existing field.

## MethodInfo Argument Safety

Foundry named call arguments are canonicalized to positional operands during analysis. Original
parameter names are therefore unnecessary for a method whose owning declaration is itself proven
safe to rename, but they remain part of reflection/named-dispatch compatibility for kept methods.

The rule is:

- member, enum-host, abstract-requirement, witness, and signal argument names are renamed only when
  that owning declaration name is in `rename_map`;
- kept/RPC/native-virtual/reflected/external/named-dispatch owners retain every argument name
  byte-for-byte; and
- lambda argument names are always safe to rename because a lambda has no externally
  name-dispatched declaration surface, even when its enclosing method is kept.

Each safe signature receives deterministic `_fsb_arg_<base36 ordinal>` names in argument order. The
allocator skips every original name in that signature and every replacement already selected for
that signature. The matching inner parameter-annotation keys use the same per-signature mapping.
There is no project-wide parameter map because parameter identity is local to one signature.

## Protected Native and Dynamic Surfaces

The application never rewrites:

- Variant validated setter/getter/builtin-method fixup keys;
- ClassDB `MethodBind` class or method keys;
- Variant/GDS utility names, constructor/operator descriptors, or builtin method-name tables;
- global-store/autoload names or `named_globals`;
- native class names, builtin type names, engine singleton names, or native signals/properties;
- resource and script paths;
- compiler-only names beginning with `@`; or
- arbitrary string-like Variant payloads and annotation values.

The global protected namespace also includes ClassDB classes, methods, virtuals, properties,
accessors, signals, and constants; Variant types, members, methods, and utilities; Foundry utility
functions; engine singletons and language globals; and every discoverable non-Foundry ScriptServer
global plus its external script surface. Analysis keeps any matching source, application rejects a
hand-authored conflicting map, and both reserve these names against replacement allocation.

Application repeats the same protected checks over the compiled graph's export fixups, annotation
identities, recursive type/container metadata, external scripts/resources, and registry entries.
This parity is intentional: a conservative analysis result must stage successfully, while an
unsafe manually supplied map must fail before mutation.

RPC method names are protected because they are named network dispatch. A valid #795 map never
contains them. If a supplied map contains a source found in class or function RPC configuration,
preflight fails atomically instead of renaming the RPC key.

`MemberInfo::setter` and `getter` are different: when they name mapped Foundry Script accessor
methods, they are project dispatch keys and are rewritten. The fixup tables above describe
native/builtin pointers and remain exact.

## Rollback and Determinism

Snapshots contain every mutated class/function container, identity, runtime-pointer backing vector,
and conformance-registry entry. Rollback restores containers by value, restores registry entries,
re-establishes runtime pointers, clears snapshots, and cannot fail.

Tests serialize an unmangled baseline before each case and prove byte-identical output and identical
registry state:

1. after a rejected begin;
2. after explicit rollback;
3. after destructor rollback;
4. after serialization itself returns an error; and
5. across repeated fresh transactions on the same live graph.

Applying the same map to the same graph produces byte-identical staged buffers independent of input
root order.

## Test Strategy

TDD begins with a missing-API test in
`modules/foundry_script/tests/test_name_mangler_application.h`. A real namespaced/nested compiled
graph includes the wished-for header and opens a scoped transaction. The first build must fail
because the application component does not exist. The same RED slice changes the #795 namespaced
analysis test to reject a flat qualified-global key.

Subsequent focused RED/GREEN slices prove:

1. exhaustive class/function/member/signal/enum/annotation/type metadata rewriting while active and
   exact explicit/destructor rollback;
2. safe private method/signal/abstract/witness and always-safe lambda argument renaming, with kept
   reflected/RPC/native argument names unchanged and collision-free deterministic replacements;
3. engine/native/Variant fixup keys and global-store/resource paths remain exact;
4. malformed, colliding, protected, and incomplete-graph maps fail before mutation with actionable
   diagnostics, byte-identical serialization, and unchanged registry state;
5. a multi-file compiled base, subclass, and caller share one map across member/method/signal/class,
   enum, trait/generic/conformance, and cross-file reference surfaces;
6. direct execution while staged and execution after `.fsb` serialization/loading match the
   unmangled behavior;
7. original private marker strings are absent from staged `.fsb` buffers while exported, kept,
   RPC, string-dispatch, and native API names remain; and
8. deep path-qualified and relative class/trait/conformance identities stage, serialize, load, and
   roll back together while a foreign identity with the same terminal segments stays protected;
9. every fixup table and annotation `name`/`qualified_name` field protects both map sources and
   replacement spellings, with a non-colliding control proving the boundary is not over-broad;
10. exported property names, metadata, and defaults remain stable across a staged multi-file graph
    and loaded buffers; and
11. repeated transactions and reversed input-root order produce the same buffers and restore the
    same live editor graph.

Final verification runs the focused name-mangler application/analysis/keep-rules tests, bytecode and
runtime suites, the broader Foundry Script family, and a macOS `dev_mode=yes` warnings-as-errors
build.

## Non-Goals

- rewriting `.tscn`, `.tres`, `.res`, animation tracks, exported-property bindings, or scene
  connections (#798);
- export-preset settings, a mangling toggle, keep-file selection, graph discovery orchestration, or
  export-plugin integration (#799);
- changing the `.fsb` wire layout or format version;
- renaming function-local bytecode/debug variables or generic type-parameter declarations; and
- absorbing the final integrated corpus/leak project owned by #800 beyond focused #797 acceptance
  fixtures.
