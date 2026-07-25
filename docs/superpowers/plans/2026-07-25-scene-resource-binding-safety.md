# Scene and Resource Binding Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collect deterministic, ownership-aware keep evidence for Foundry Script names serialized by scenes, resources, connections, and animations without instantiating or mutating those resources.

**Architecture:** Add a TOOLS-only `FSNameManglerBindingSafety` component. It builds a pure virtual scene by composing sparse `SceneState` base/instance/local rows, traverses generic Resource storage and typed containers, resolves connection and AnimationMixer bindings against in-domain compiled scripts, and returns a structured transactional result that feeds `FSNameManglerAnalysis::Input`.

**Tech Stack:** Godot/Foundry C++17, `Resource`/`PackedScene`/`SceneState`, `Animation`/`AnimationLibrary`, Foundry Script compiled metadata, doctest, SCons macOS editor tests.

---

## File Structure

| File | Responsibility |
|---|---|
| `modules/foundry_script/fs_name_mangler_binding_safety.h` | Public resource-root, evidence, diagnostic, result, and collection API. |
| `modules/foundry_script/fs_name_mangler_binding_safety.cpp` | Script-domain index, generic Variant/Resource traversal, virtual SceneState composition, connection/animation ownership, sorting, and failure behavior. |
| `modules/foundry_script/tests/test_name_mangler_binding_safety.h` | Focused unit, serialization-format parity, scratch fixture, application, load, and runtime acceptance tests. |
| `docs/superpowers/specs/2026-07-25-scene-resource-binding-safety-design.md` | Approved scope and behavior contract. |
| `docs/superpowers/plans/2026-07-25-scene-resource-binding-safety.md` | TDD execution and verification checklist. |

No parser/tokenizer file changes, grammar changes, export-plugin changes, or export-preset changes are planned.

### Task 1: Public result contract and first RED

**Files:**
- Create: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`
- Create: `modules/foundry_script/fs_name_mangler_binding_safety.h`
- Create: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`

- [ ] **Step 1: Write the failing public-contract test**

Create the test header with a case that compiles two real Foundry Script declarations, constructs a
`FSNameManglerBindingSafety::Result` containing sorted evidence, applies it to an analysis input,
and asserts that analysis reports exact `KEEP_SCENE_OR_RESOURCE` reasons while an incomplete result
refuses application without changing the input.

The wished-for calls are:

```cpp
FSNameManglerBindingSafety::Result result;
result.evidence.push_back({
        SNAME("scene_method"),
        FSNameManglerBindingSafety::BINDING_CONNECTION_METHOD,
        "res://main.tscn",
        "res://receiver.fs",
});
CHECK_EQ(result.apply_to_input(input), OK);

FSNameManglerBindingSafety::Result incomplete;
incomplete.error = ERR_INVALID_DATA;
incomplete.complete = false;
const int previous_count = input.keep_evidence.size();
CHECK_EQ(incomplete.apply_to_input(input), ERR_INVALID_DATA);
CHECK_EQ(input.keep_evidence.size(), previous_count);
```

- [ ] **Step 2: Run the strict incremental build and record RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
```

Expected: compilation fails because `fs_name_mangler_binding_safety.h` or its declared API does not
exist. Record the exact first error as RED evidence before adding production code.

- [ ] **Step 3: Add the minimal public model**

Declare:

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
        BindingKind kind = BINDING_SERIALIZED_PROPERTY;
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

Implement stable labels/details, sorting/deduplication helpers, and transactional
`apply_to_input()`. Do not implement traversal yet.

- [ ] **Step 4: Build and run the focused contract case**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*Public*" --force-colors
```

Expected: build succeeds and the public-contract case passes with zero failures.

- [ ] **Step 5: Commit the contract**

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.* \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Add scene binding safety result contract"
```

### Task 2: Pure SceneState composition

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Write failing virtual-scene tests**

Build `PackedScene`/`SceneState` fixtures with public build APIs and real saved/loaded resources.
Cover:

```text
base root .
  BaseChild (script A, base_value)
instance mount Instance
  . (script B)
  Nested (script C, nested_value)
derived/local overlays:
  BaseChild (TYPE_INSTANTIATED, local property override)
  Instance/Nested (TYPE_INSTANTIATED, explicit NIL script)
  LocalChild parented below BaseChild
```

Add the same nested scene twice under different mounts to prove the expansion stack permits repeated
DAG instances. Assert collected serialized-property evidence uses the canonical mounted paths and
final effective scripts.

- [ ] **Step 2: Run focused tests and record behavioral RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*SceneState*" --force-colors
```

Expected: cases fail because `collect()` does not yet compose scene nodes or emit stored-property
evidence.

- [ ] **Step 3: Implement canonical paths and sparse overlays**

Add private implementation types equivalent to:

```cpp
struct VirtualNode {
    String path;
    StringName native_type;
    Ref<Script> script;
    RBMap<StringName, SerializedProperty> properties;
};

struct VirtualConnection {
    String source_path;
    StringName signal;
    String target_path;
    StringName method;
    String source;
};
```

Implement expansion in this order:

1. active-stack cycle check;
2. base state at the same mount;
3. non-root instance state at its row path;
4. current row overlay;
5. current connection collection.

Reject empty-type rows without a base/instance node, duplicate concrete nodes, missing parents,
absolute/escaping/subname scene paths, wrong instance resource types, and placeholders required for
resolution. Do not call `PackedScene::instantiate()`, `SceneState::find_node_by_path()`, or
`SceneState::get_property_value()`.

- [ ] **Step 4: Keep only in-domain stored script properties**

Index compiled roots/nested classes by pointer and `(path, fully-qualified name)`. Build inherited
script property sets using `get_script_property_list()`. A stored node property emits evidence only
when its final effective script is in-domain and owns the key. Validate native properties through
`ClassDB`; reject stale unknown keys.

- [ ] **Step 5: Run focused tests**

Run the Step 2 commands again.

Expected: all SceneState composition and property-ownership cases pass.

- [ ] **Step 6: Commit scene composition**

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.cpp \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Compose scene states for name binding safety"
```

### Task 3: Persistent connection ownership

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Write failing cross-boundary connection tests**

Create declared script signals/methods and connections that cover:

- local source to instanced descendant target;
- base descendant source to local target where the endpoint has no local state row;
- native source signal to script target method;
- script source signal to native target method; and
- inherited and instance-local connections mounted into the composed scene.

Assert only the script-owned endpoints produce evidence and connection flags/binds/unbinds do not
become names.

- [ ] **Step 2: Run the connection cases and record RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*Connection*" --force-colors
```

Expected: no connection evidence exists yet.

- [ ] **Step 3: Resolve and validate connections**

After full scene composition, sort connection tuples and resolve both endpoints. Use the effective
script method/signal lists including bases, then native `ClassDB` validation. Emit
`BINDING_CONNECTION_SIGNAL` and `BINDING_CONNECTION_METHOD` only for in-domain declarations.
Return incomplete diagnostics for missing endpoints or names owned by neither script nor native
surface.

- [ ] **Step 4: Run focused tests and commit**

Run the Step 2 commands, then:

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.cpp \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Keep persistent scene connection names"
```

### Task 4: Generic Resource and class identity traversal

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Write failing Resource ownership tests**

Create scripted Resource roots and nested Array/Dictionary values. Include:

- a top-level registered `class_name` Resource;
- an inherited stored script property;
- typed Array and typed Dictionary script types;
- a cyclic Resource/Array graph;
- ordinary strings equal to private declaration names; and
- an external script with a spelling shared by an in-domain private name.

Assert class/property/typed-container evidence is exact, ordinary strings are ignored, and Resource
cycles terminate deterministically.

- [ ] **Step 2: Run the Resource cases and record RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*Resource*" --force-colors
```

- [ ] **Step 3: Implement generic traversal**

For each non-PackedScene root:

1. keep its in-domain scripted Resource atomic local class when global name is nonempty;
2. enumerate and sort `PROPERTY_USAGE_STORAGE` properties;
3. classify script-owned keys;
4. read and recursively traverse Resource/Array/Dictionary values;
5. inspect typed container scripts/class names semantically; and
6. use object/container identity sets for cycle safety.

Do not inspect ordinary string contents or NodePath segments.

- [ ] **Step 4: Run focused tests and commit**

Run the Step 2 commands, then:

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.cpp \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Collect scripted resource binding evidence"
```

### Task 5: Animation property and method bindings

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Write failing animation tests**

Build a virtual scene containing an AnimationPlayer with semantic `libraries/<name>` properties and
tracks for:

- `TYPE_VALUE` targeting an in-domain script property;
- `TYPE_BEZIER` targeting an inherited script property;
- `TYPE_METHOD` keys targeting an in-domain script method;
- a native transform/property target;
- node names and animation names equal to private declarations; and
- disabled tracks.

Add unresolved target and resource-subpath cases that must return stable incomplete diagnostics.

- [ ] **Step 2: Run animation cases and record RED**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*Animation*" --force-colors
```

- [ ] **Step 3: Implement virtual AnimationMixer resolution**

Identify native AnimationMixer subclasses with `ClassDB`. Resolve serialized `root_node`, defaulting
to `..`. Enumerate `libraries/<name>` values and animation names in sorted order. Resolve
VALUE/BEZIER paths through virtual nodes and stored Resource subproperties, and METHOD names through
`method_track_get_name()`. Classify only the first unresolved property/method identity against its
script/native owner. Analyze disabled tracks and reject unresolved ownership.

- [ ] **Step 4: Run focused tests and commit**

Run the Step 2 commands, then:

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.cpp \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Keep script animation track bindings"
```

### Task 6: Text/binary scratch-project acceptance and application parity

**Files:**
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Write the failing real-resource acceptance fixture**

Use `TemporaryProjectTree` so every generated file is below `FOUNDRY_TEST_SCRATCH`. Write
`project.foundry` plus separate base, emitter, receiver, and scripted-Resource `.fs` files. Build and
save:

```text
main.tscn / main.scn
  Emitter: declared script signal
  Receiver: inherited @export override + nonexported animated property
  AnimationPlayer: value track targeting Receiver:animated_value
  persistent Emitter signal -> Receiver method

config.tres / config.res
  registered class_name script
  stored script property override
```

Load each format through `ResourceLoader`, run collection with reversed root orders, and assert
byte-for-byte-equivalent evidence snapshots.

- [ ] **Step 2: Record RED before filling any missing behavior**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
FOUNDRY_TEST_SCRATCH="$PWD/.test_scratch" \
  ./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*Acceptance*" --force-colors
```

Expected: the first missing integration assertion fails; diagnose its actual ownership/path cause
before changing implementation.

- [ ] **Step 3: Complete only the missing collector behavior**

Use the systematic-debugging sequence for each failure: read full diagnostic, reproduce, trace the
virtual path/resource ownership, compare text and binary SceneState rows, state one hypothesis, and
make the smallest implementation change. Do not weaken assertions or add text-format special cases.

- [ ] **Step 4: Prove analysis/application/staging/runtime parity**

For each representation:

1. apply the complete collector result to analysis input;
2. assert exact `KEEP_SCENE_OR_RESOURCE` evidence for signal, method, exported override, animated
   property, and registered Resource class;
3. assert unrelated private names remain in `rename_map`;
4. begin `FSNameManglerApplication::Transaction`;
5. serialize every mutated script to adjacent scratch `.fsb` files;
6. assert unrelated original private names are absent from buffers while kept names remain;
7. write scratch `.remap` sidecars, evict source/resource caches, and reload scene/resource paths;
8. instantiate the reloaded scene only in the runtime acceptance phase;
9. emit the signal, advance the animation, and compare exported/property/resource/class behavior to
   the unmangled baseline; and
10. remove remaps/caches through fixture cleanup.

Snapshot source SceneState dictionaries, resource stored values, and Animation data before
collection and assert they are unchanged afterward.

- [ ] **Step 5: Run acceptance and focused suites**

Run:

```sh
FOUNDRY_TEST_SCRATCH="$PWD/.test_scratch" \
  ./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerAnalysis*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerApplication*" --force-colors
```

Expected: every focused case passes with zero failures and no unexpected diagnostics.

- [ ] **Step 6: Commit acceptance coverage**

```sh
git add modules/foundry_script/tests/test_name_mangler_binding_safety.h \
  modules/foundry_script/fs_name_mangler_binding_safety.*
git commit -m "Verify serialized binding safety end to end"
```

### Task 7: Failure matrix, determinism, and final verification

**Files:**
- Modify: `modules/foundry_script/fs_name_mangler_binding_safety.cpp`
- Modify: `modules/foundry_script/tests/test_name_mangler_binding_safety.h`

- [ ] **Step 1: Add failure/determinism controls before fixes**

Add cases for null/duplicate roots, active base/instance cycles, missing parents, unbacked
TYPE_INSTANTIATED rows, placeholders needed for bindings, absolute/escaping paths, unresolved
connection endpoints, stale script/native names, and unreadable storage properties. Assert sorted
diagnostics and transactional refusal.

- [ ] **Step 2: Run focused cases and capture any RED**

Run the binding-safety focused command from Task 6. For every failure, use systematic debugging and
fix only the proven root cause.

- [ ] **Step 3: Format changed C++ files**

Run:

```sh
clang-format -i \
  modules/foundry_script/fs_name_mangler_binding_safety.h \
  modules/foundry_script/fs_name_mangler_binding_safety.cpp \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git diff --check
```

Expected: `git diff --check` exits zero.

- [ ] **Step 4: Run strict optimized build**

Run:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes -j12
```

Expected: exit zero with `-O2`, strict checks, and warnings-as-errors.

- [ ] **Step 5: Run focused, broader, and relevant engine tests**

Run:

```sh
FOUNDRY_TEST_SCRATCH="$PWD/.test_scratch" \
  ./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*NameManglerBindingSafety*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*FoundryScript*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*PackedScene*" --force-colors
./bin/foundry.macos.editor.arm64 --headless test run \
  --case "*Animation*" --force-colors
```

Record exact test and assertion counts from each doctest summary.

- [ ] **Step 6: Verify requirements and repository state**

Run:

```sh
git diff --check origin/develop...
git status --short
git diff --stat origin/develop...
git log --oneline origin/develop..HEAD
```

Review the design acceptance list line by line. Confirm no export orchestration, preset, resource
rewrite, parser/grammar, or unrelated main-checkout file changed.

- [ ] **Step 7: Commit final corrections**

```sh
git add modules/foundry_script/fs_name_mangler_binding_safety.* \
  modules/foundry_script/tests/test_name_mangler_binding_safety.h
git commit -m "Harden resource binding safety diagnostics"
```

- [ ] **Step 8: Request independent review**

Report the final SHA, exact build/test evidence, and clean worktree status to the parent agent for the
required independent spec gate. Do not run Cursor review, push, open a PR, or merge until explicitly
authorized.
