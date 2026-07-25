# Scene and Resource Binding Safety Design

## Goal

Issue #798 adds the resource-owned input to the compiled Foundry Script name-mangling pipeline.
Scenes and resources serialize script-facing identities as names: persistent connection signals and
methods, stored script-property keys, AnimationMixer track targets, and scripted Resource class
identities. The collector identifies those names before `FSNameManglerAnalysis` builds its
project-wide map and supplies `KEEP_SCENE_OR_RESOURCE` evidence for each proven binding.

The selected policy keeps every proven scene/resource-referenced declaration unchanged. It does not
rewrite a source or exported resource. This is the conservative strategy recommended by the issue:
it gives the later export orchestrator a deterministic safety input without coupling the collector
to export presets, project discovery, file staging, or the `.fsb` writer.

## Approaches Considered

### Compose loaded resources semantically without instantiation (selected)

Load resources through `ResourceLoader`, inspect generic `Resource` storage properties, and compose
`PackedScene::get_state()` rows into a virtual scene. The virtual scene expands inherited and
instanced `PackedScene` states, overlays sparse local rows, and resolves connections and animations
against the resulting node/script ownership index.

This approach works identically for text and binary serialization because both formats load into the
same `PackedScene`/`SceneState` representation. It does not parse serialized text, does not depend on
private writer syntax, does not create `ScriptInstance`s, and does not execute scene scripts.

### Instantiate a transient PackedScene

A temporary scene instance would make inherited paths and AnimationMixer targets easy to resolve,
but `SceneState::instantiate()` creates nodes and calls `set_script()` while applying the `script`
property. That constructs script instances and can run user initialization. Even without entering a
SceneTree, it is not a read-only analysis operation and is rejected.

### Scrape `.tscn`/`.tres` text and add a separate binary decoder

Text scraping directly exposes connection and track spellings, but it is format-coupled, cannot
cover `.scn`/`.res` without a second decoder, and cannot reliably distinguish script declarations
from node names, native names, paths, and ordinary strings. It is rejected.

Rewriting scene/resource bindings is also rejected for this issue. It expands the mutation and
rollback surface and is unnecessary under the selected keep policy.

## Public Boundary

A TOOLS-only `FSNameManglerBindingSafety` component lives in
`modules/foundry_script/fs_name_mangler_binding_safety.{h,cpp}`.

Its caller-facing model is:

```cpp
class FSNameManglerBindingSafety {
public:
    enum BindingKind {
        BINDING_CONNECTION_SIGNAL,
        BINDING_CONNECTION_METHOD,
        BINDING_SERIALIZED_PROPERTY,
        BINDING_ANIMATION_PROPERTY,
        BINDING_ANIMATION_METHOD,
        BINDING_RESOURCE_SCRIPT_CLASS,
        BINDING_TYPED_CONTAINER_SCRIPT_CLASS,
    };

    struct ResourceRoot {
        Ref<Resource> resource;
        String source;
    };

    struct Input {
        Vector<ResourceRoot> resources;
        void add_resource(const Ref<Resource> &p_resource,
                const String &p_source = String());
    };

    struct Evidence {
        StringName name;
        BindingKind kind;
        String source;
        String owner;

        String detail() const;
    };

    struct Diagnostic {
        String source;
        String context;
        String message;

        String format() const;
    };

    struct Result {
        Error error = OK;
        bool complete = true;
        Vector<Evidence> evidence;
        Vector<Diagnostic> diagnostics;

        Error apply_to_input(FSNameManglerAnalysis::Input &r_input) const;
    };

    static Result collect(const Input &p_input,
            const FSNameManglerAnalysis::Input &p_analysis_input);
};
```

The caller supplies already-loaded resource roots. A root source defaults to the resource path; an
in-memory root must provide a nonempty stable source. Project scanning, dependency enumeration, and
deciding which `.tscn`, `.scn`, `.tres`, or `.res` files belong to an export are #799 concerns.

`collect()` receives the analysis input only to establish the in-domain compiled-script ownership
set. It does not change it. `Result::apply_to_input()` is transactional: it refuses an error or
incomplete result and otherwise appends sorted/deduplicated `KEEP_SCENE_OR_RESOURCE` records. A
future caller can abort export on an incomplete result or explicitly choose the existing
`complete_project_graph = false` keep-all fallback. The collector never makes that policy decision
silently.

## Script Ownership Domain

The collector recursively indexes every compiled root and nested `FoundryScript` in
`FSNameManglerAnalysis::Input::scripts`. An exact object pointer identifies in-memory fixtures.
Loaded resources are matched by canonical `(script path, fully-qualified name)` so a separately
loaded representation of the same compiled class still has the correct owner.

Bindings add evidence only for declarations owned by this domain:

- properties are matched against `Script::get_script_property_list()`, including inherited script
  properties;
- methods are matched against `Script::get_script_method_list()`, including inherited methods;
- signals are matched against `Script::get_script_signal_list()`/`has_script_signal()`; and
- registered class identities keep the owning `FoundryScript::local_name`, because the application
  pass rebuilds the global/fully-qualified identity from that atomic component.

If the matching script belongs outside the input domain, the binding is ignored by this collector.
The name map cannot mutate that script. Native methods, signals, and properties are also ignored
after validation through `ClassDB`.

This prevents unrelated strings from globally keeping an internal declaration that happens to share
the spelling. Node names, owner paths, native node/resource types, native properties/methods/signals,
animation and library names, transform/bone/blend-shape path segments, resource paths, and ordinary
`String`/`StringName` values are not evidence.

## Virtual Scene Composition

`SceneState` is a sparse serialization state, not a ready-made resolved scene. The collector builds
an ordered virtual index keyed by canonical scene-relative NodePath components.

For one `PackedScene` mounted at a canonical prefix:

1. Detect active recursion by PackedScene/SceneState identity. The check uses the active expansion
   stack rather than a global visited set because the same scene may be validly instanced at several
   paths.
2. Expand `get_base_scene_state()` at the same mount. The root's `get_node_instance(0)` identifies
   the base PackedScene and is not mounted a second time as a child.
3. Iterate current state rows in serialization order. Compute the row's absolute virtual path from
   `get_node_path(i)`, with `.` mapping to the current mount.
4. For a non-root row whose `get_node_instance(i)` is a PackedScene, expand the instance at that
   row's virtual path before applying the row.
5. Overlay the current row. A nonempty `get_node_type(i)` supplies the concrete native type.
   `TYPE_INSTANTIATED` appears as an empty type and must resolve an existing base/instance node;
   otherwise the state is incomplete. Enumerated stored properties overlay inherited/instanced
   values, including an explicit NIL `script` that removes an inherited script.
6. Append local connection records, with source and target paths adjusted by the mount. Connections
   are resolved only after all nodes are composed because a serialized endpoint can point into a
   base or instance without having a row in the current state.

Base connections are unioned at the same mount; instance connections are mounted under the instance
root; current connections are added last. The state writer already excludes nonpersistent
connections. Hand-authored text scenes may store `flags=0`, which instantiation upgrades with
`CONNECT_PERSIST`, so the collector does not require the serialized flags to contain that bit.

The collector intentionally does not call `SceneState::find_node_by_path()` or
`get_property_value()`. A normally loaded state has no `node_path_cache`; `find_node_by_path()`
requires that cache, and base fallback in `get_property_value()` depends on remaps created by the
cache lookup. Neither helper composes child instances. Row enumeration and explicit overlay are the
format-independent source of truth.

Instance placeholders are not expanded implicitly. Their referenced graph is not present in the
loaded state and loading it would make the collector a project-discovery stage. A placeholder that
must participate in ownership or path resolution makes the result incomplete with an actionable
diagnostic. #799 can ensure placeholder targets are supplied as independent roots, but cross-root
path resolution remains unsupported until a complete loaded graph is available.

## Serialized Properties and Generic Resources

Every stored SceneState node property is checked against the node's final effective script:

- `script` establishes ownership and is never itself treated as a script declaration name;
- a property key owned by an in-domain script produces `BINDING_SERIALIZED_PROPERTY` evidence;
- a property key owned by the native type or an external script is ignored; and
- a key with no provable script/native owner is a stale or ambiguous serialized binding and makes
  the result incomplete.

This emits explicit scene/resource evidence for exported overrides even though the merged analysis
layer independently keeps all `@export` members. It also covers storage-only script properties and
inherited exported properties.

Generic non-PackedScene roots are traversed through their `PROPERTY_USAGE_STORAGE` property list.
The traversal:

- validates and records script-owned stored property keys;
- recursively visits `Resource`, Array, and Dictionary values using identity-based cycle
  protection;
- inspects typed Array/Dictionary script types as semantic class identities; and
- never treats an ordinary string, dictionary key, or NodePath segment as a declaration identity.

The collector never calls `set`, saves, duplicates, or instantiates a source resource. Reading a
stored generic Resource property uses the same `Object::get` surface the resource writer consumes.

Both text and binary resource writers serialize the global script class in the header of a top-level
non-PackedScene scripted Resource. Therefore, when such a root has an in-domain script with a
nonempty global name, the collector emits `BINDING_RESOURCE_SCRIPT_CLASS` for its atomic local class
name. An embedded subresource does not receive its own file header; its class identity is handled
only if it appears through a typed container or the subresource is also supplied as a top-level root.

## Connections

For each composed persistent connection:

- resolve the source and target against the virtual node index;
- validate the source signal against the source script and native class;
- validate the target method against the target script and native class;
- emit signal and/or method evidence only when the declaration belongs to an in-domain script.

A native signal connected to an in-domain script method keeps only the method. An in-domain script
signal connected to a native method keeps only the signal. Cross-script connections keep the exact
declaration on each endpoint owner.

Unresolved endpoints or spellings that belong to neither the effective script nor native class are
not silently ignored. They indicate stale or unsupported serialized state and produce an incomplete
result.

## Animation Bindings

The virtual node's native type identifies `AnimationMixer`/`AnimationPlayer` subclasses through
`ClassDB`. The serialized `root_node` property is used when present; otherwise the native default is
`..`. Animation libraries are taken only from semantic `libraries/<name>` storage properties and
enumerated by their resource APIs in sorted name order.

For each track:

- `TYPE_VALUE` and `TYPE_BEZIER` resolve their target NodePath from the mixer's root. The collector
  follows stored Resource-valued subproperties where possible, then classifies the first remaining
  property segment against the final Node or Resource script/native property surface. An in-domain
  script property emits `BINDING_ANIMATION_PROPERTY`.
- `TYPE_METHOD` resolves the target object, enumerates every method key through
  `method_track_get_name()`, and emits `BINDING_ANIMATION_METHOD` for in-domain script methods.
- transform, rotation, scale, blend-shape, audio, nested-animation, and animation/library names do
  not encode Foundry Script declaration identities and are ignored.

Disabled tracks are still serialized and can later be enabled, so they are analyzed. A value,
Bezier, or method track whose target or ownership cannot be resolved is incomplete rather than
being guessed from raw path text.

## Errors and Conservative Failure

The collector reports an error and `complete = false` for:

- null roots or scripts;
- empty/duplicate root sources that cannot define deterministic provenance;
- active inheritance or scene-instance cycles (generic Resource/container cycles are identity-deduplicated);
- duplicate or colliding virtual node paths;
- missing parents, missing/wrong-type instance dependencies, or unbacked `TYPE_INSTANTIATED` rows;
- placeholder content required for a binding;
- absolute, escaping, or otherwise unsupported scene-relative paths;
- unresolved connection endpoints;
- stale connection/property/method/animation names with no script/native owner;
- unresolved animation objects or resource subpaths; and
- storage properties that cannot be read through their declared property surface.

Diagnostics contain stable source, context, and message fields and are sorted/deduplicated. Evidence
already discovered may remain available for debugging, but `apply_to_input()` refuses the incomplete
result. There is no partially applied keep set.

## Determinism and Immutability

Root resources are sorted by source. Virtual nodes are ordered by canonical path, connections by
their full stable tuple, generic properties by name, animation libraries/animations by name, and
evidence by `(name, kind, source, owner)`. Diagnostics are sorted by `(source, context, message)`.
Hash-map and caller root order do not affect output.

Traversal identity is used only for cycle safety. Pointer values never enter details, diagnostics,
or ordering.

The collector performs no mutation of a PackedScene, SceneState, Animation, generic Resource,
FoundryScript, or analysis input. Tests snapshot bundled scene dictionaries, stored properties, and
animation data before and after collection.

## Testing

TDD begins with a new focused
`[FoundryScript][NameManglerBindingSafety]` doctest family. The initial RED build must fail because
the collector API does not exist.

Unit coverage proves:

1. a hand-built SceneState composes base, nested/repeated instances, sparse descendant overrides,
   local children under inherited/instance parents, script NIL removal, and mounted connections
   without calling `instantiate()` or cache-dependent helpers;
2. active cycles, placeholders needed for a binding, missing dependencies/parents/endpoints,
   unbacked instantiated rows, duplicate paths, and path escapes return stable incomplete results;
3. native names, node names, resource paths, animation names, track node segments, and ordinary
   strings do not become evidence;
4. generic Resource properties, typed containers, and top-level scripted Resource headers produce
   evidence only for in-domain declarations; and
5. root order and repeated collection produce identical evidence and diagnostics.

A real scratch project/resource fixture then writes several `.fs` files plus `project.foundry` under
`FOUNDRY_TEST_SCRATCH`, creates a cross-script scene and scripted Resource, and saves both text and
binary forms:

- `.tscn` and `.scn` contain a declared signal connected to a method on another script, an inherited
  exported-property override, and an AnimationPlayer value track targeting a nonexported script
  property;
- `.tres` and `.res` contain a registered `class_name` scripted Resource and a stored script
  property;
- unrelated private class/member/method/signal names remain candidates.

For each representation the test loads the real resource, collects and applies evidence, analyzes,
and starts an `FSNameManglerApplication` transaction. It asserts exact keep reasons, serializes the
mutated scripts to `.fsb`, verifies unrelated original private names do not leak, remaps the source
script paths to the staged binaries, reloads the serialized scenes/resources, and checks connection,
export override, animation, class identity, and runtime behavior parity. All generated resources,
binary scripts, and remap files remain inside the shared test scratch tree and are removed by the
fixture.

Focused tests run first, followed by the existing analysis/application families, broader
`*FoundryScript*` tests, relevant PackedScene/Animation/Resource tests, a strict optimized macOS
`dev_mode=yes tests=yes` build, formatting, and diff/status checks.

## Out of Scope

- export preset options or a mangling toggle;
- project file/resource discovery;
- export-plugin orchestration and diagnostics routing;
- source or exported scene/resource rewriting;
- choosing abort versus keep-all for an incomplete collector result;
- changing the `.fsb` format or compiled-graph application transaction; and
- final integrated export corpus/preset coverage owned by #799/#800.
