# Name Mangler End-to-End Acceptance Design

## Context

Issues #795 through #799 and follow-up #1229 implemented the whole-program
Foundry Script name mangler: analysis and deterministic rename-map generation,
keep escapes, compiled-graph application, serialized scene/resource binding
safety, and export-preset integration. Their focused suites cover the individual
components thoroughly. Issue #799 also added a real command-first pack export
smoke test covering a scene connection, exported properties, animation
bindings, a keep rule, cross-file dispatch, and one private method leak marker.

Issue #800 is the final integrated acceptance gate for epic #786. It must prove
that the merged stack preserves behavior with mangling enabled while actually
removing private dispatch names. The current suite does not yet compare
mangling ON against OFF at the real export/runtime boundary, exercise the full
acceptance matrix in one exported project, prove an unescaped computed dispatch
fails explicitly, scan all emitted scripts for both member and method markers,
or compare repeated real exports for determinism.

## Goals

The new acceptance suite will prove:

1. A canonical semantic transcript is identical when the same project is
   exported and run with mangling OFF and ON.
2. The project exercises:
   - a declared signal connected in script;
   - a signal connected by the serialized scene;
   - an inherited exported property overridden by the scene;
   - a scripted Resource with a persisted exported property;
   - an RPC-annotated method and its runtime RPC configuration;
   - the `_ready` engine virtual;
   - cross-file generic inheritance;
   - generic trait conformance and trait-typed dispatch; and
   - `get_method_list()` reflection.
3. `@keep_name` and a project keep rule preserve computed dynamic dispatch.
4. A computed dynamic dispatch without either escape produces a runtime error
   that explicitly names the reconstructed target and never invokes that
   target.
5. Mangled `.fsb` files contain none of the chosen private member or method
   markers, while intentional scene, export, RPC, virtual, reflection, and
   dynamic-escape names remain.
6. Two real mangled exports produce identical sorted
   `res://path.fsb -> bytes` maps.

## Non-Goals

- Re-running the entire Foundry Script fixture corpus through the export
  pipeline; phase 1 already runs that corpus through compiled-bytecode
  round-tripping.
- Replacing #799's smaller real-pack smoke test.
- Comparing whole `.pck` files, whose container metadata or entry ordering is
  not the name mangler's determinism contract.
- Adding a production API that exposes the internal rename map solely for
  tests.
- Rewriting serialized scene/resource bindings; the merged implementation
  intentionally keeps externally bound names.
- Changing mangling policy, `.fsb` layout, or format version unless the
  acceptance test first exposes a concrete defect.

## Considered Approaches

### 1. Dedicated integrated export acceptance suite

Create a focused acceptance header that reuses the real export subprocess and
pack-mount infrastructure from #799. Generate a rich safe project and a small
unsafe-dispatch project under the shared test scratch root.

This is the selected approach. It closes the missing cross-layer contracts
without weakening component test isolation or turning #799's smoke test into a
large final corpus.

### 2. Expand the #799 smoke test in place

This would minimize helper changes, but a single test would mix preset
integration smoke coverage with final epic parity, leak, failure, and
determinism proof. Failures would be harder to diagnose and the test's original
purpose would become unclear.

### 3. Add lower-level graph tests plus one pack smoke

This would run faster, but the existing lower-level suites already contain most
of that coverage. It would still fail to prove that real presets, manifests,
pack output, bytecode loading, scene loading, and runtime behavior compose
correctly.

## Test Infrastructure

Move the subprocess result type, command runner, and scoped PCK mount currently
local to `test_name_mangler_export.h` into a small test-only helper header. The
existing #799 pack test and the new #800 acceptance tests will both use the
same implementation. The extraction must be behavior-neutral and the existing
pack case must pass before adding the new acceptance behavior.

The helper keeps these guarantees:

- it launches the current Foundry executable with the supported command-first
  CLI;
- it pumps stdout and stderr while the subprocess runs;
- it has a bounded deadline and kills a stuck child;
- it records process launch status, exit code, and combined output; and
- its pack mount refuses to coexist with another mounted pack and always clears
  `PackedData` on scope exit.

The acceptance header will add local helpers that:

- invoke `foundry project export --project ... --preset ... --output ...
  --mode pack`;
- run a mounted pack from an isolated runtime directory;
- extract the one canonical transcript line from process output;
- mount a pack and return a sorted map of every `.fsb` path to its bytes; and
- scan those byte maps for exact marker presence or absence.

All projects, packs, runtime directories, logs, and inspection data are created
by `TemporaryProjectTree` beneath `FOUNDRY_TEST_SCRATCH` and removed by RAII.

## Safe Parity Project

The safe scratch project has two export presets over the same manifest:

- `Unmangled`: compiled bytecode with name mangling disabled.
- `Mangled`: compiled bytecode with name mangling enabled and a keep-rules
  file.

The test exports `Unmangled` once and `Mangled` twice to three separate packs.
It runs the unmangled pack and the first mangled pack.

The project is split into small scripts:

- a generic `Node` base defines the declared signal, an inherited exported
  property, a distinctive private member, and a distinctive private method;
- a generic trait declares a typed witness requirement;
- a derived script specializes the generic base, conforms to the trait,
  handles both signals, declares an RPC method, and supplies annotated and
  rule-kept dynamic targets;
- an emitter declares and emits the scene-connected signal;
- a reflector enumerates `get_method_list()` and reports a semantic count
  without printing reflected method spellings;
- a scripted Resource carries a persisted exported value; and
- the main script's `_ready` method drives all checks and exits.

The scene assigns the inherited exported property and serializes the
emitter-to-derived signal connection. The runtime also connects the generic
base's declared signal in script. The derived instance is widened to the
generic trait before witness dispatch. RPC coverage checks both direct method
behavior and the size/presence semantics of `get_node_rpc_config()` without
embedding the RPC method's complete spelling as a string literal.

Reflection checks a count using a harmless prefix fragment rather than
embedding the complete reflected method name. Dynamic dispatch builds target
names from separate prefix and suffix literals. One target is protected by
`@keep_name`; the other is protected by the preset's keep-rules file.

The reflector is an independent script with no inheritance, preload,
annotation, or typed reference to the generic base that owns the private leak
controls. Before relying on the controls, the focused acceptance test must
prove the reflection policy is scoped: the reflector's method is retained
while the base's private member and method remain rename candidates. If that
precondition fails, the test must stop and the implementation must be
investigated rather than weakening either assertion.

The main script prints exactly one normalized line:

```text
NAME_MANGLER_ACCEPTANCE|declared=<int>|scene=<int>|scene_export=<int>|resource_export=<int>|rpc=<int>|rpc_config=<int>|trait=<int>|reflection=<int>|annotated=<int>|rule=<int>|ready=<int>
```

Only semantic values and stable labels appear. No mangled spelling, private
marker, complete reflected method name, or diagnostic text is included. The
test extracts this line from each process's surrounding engine output and
requires byte-for-byte equality between OFF and ON.

## Leak and Keep Proof

The safe project uses distinctive private member and method identifiers only
as declarations and direct compiled references. Their complete spellings
never appear in a string literal, transcript, error, resource field, or scene
binding.

Both controls are owned by the generic base, which is outside the independent
reflector script's reflection-wide keep closure. No reflective enumeration is
performed on the base, its derived instance, or a type that inherits either
control. The acceptance therefore never asks one identifier to be both
reflection-kept and mangled away.

The unmangled pack is first required to contain both controls, proving the
markers are observable in ordinary `.fsb` output and the negative checks are
meaningful. Every `.fsb` entry in the mangled pack is then scanned, and neither
complete private spelling may appear anywhere.

The owning mangled bytecode entries are also required to retain the complete
spellings that external behavior depends on:

- serialized scene signal and handler;
- exported scene and Resource properties;
- RPC method;
- `_ready`;
- reflection-enumerated method;
- `@keep_name` target; and
- keep-rule target.

The C++ assertions may contain those spellings; the generated project may not
contain a complete private marker as data.

## Determinism Proof

The two `Mangled` exports are mounted one at a time. For each pack, the test:

1. enumerates every packed path;
2. selects `.fsb` entries;
3. reads their bytes; and
4. inserts them into an ordered map keyed by normalized `res://` path.

The ordered maps must have the same path set and byte-identical value for
every script. This proves the effective project-wide rename choices and
serialized outputs are deterministic while avoiding irrelevant whole-PCK
container details.

## Unsafe Dynamic-Dispatch Project

The second project has one mangled compiled-bytecode preset and no keep
escapes. Its main script declares a distinct target method, constructs that
method name from two separate string fragments, schedules a bounded exit, and
calls the computed name.

The export itself must succeed. The runtime result is accepted only when all
of these are true:

- output contains an explicit missing/nonexistent-method diagnostic;
- the diagnostic contains the exact reconstructed target spelling;
- the target's body sentinel is absent; and
- the emitted `.fsb` does not contain the target's original complete spelling.

A generic nonzero exit code is insufficient evidence. A timeout, launch
failure, or unrelated runtime error fails the test.

## Failure Handling and TDD

Implementation follows strict red-green-refactor:

1. Extract and verify the existing process/pack helpers without behavior
   change.
2. Add the safe acceptance case and build it into the test binary.
3. Run the focused case and capture every unmet acceptance assertion as RED.
4. If merged production behavior already satisfies it, retain the new test as
   the issue's deliverable. If it exposes a production defect, investigate the
   root cause before changing code, add the narrowest assertion that
   reproduces it, and implement only the required fix.
5. Add and run the unsafe case independently, following the same process.
6. Refactor only after both cases are green.

Tests use `REQUIRE` before indexing outputs or maps so one failed external
operation cannot cascade into an out-of-bounds crash.

## Verification

Verification proceeds from narrow to broad:

1. existing #799 real-pack smoke after helper extraction;
2. safe parity/leak/determinism acceptance;
3. unsafe computed-dispatch acceptance;
4. all `*NameMangler*` suites;
5. relevant bytecode/export suites;
6. repeated real command-first export/runtime execution;
7. strict optimized macOS editor build with tests and warnings-as-errors;
8. exact full test suite with structured JSONL progress; and
9. diff/status hygiene and formatting checks.

An independent read-only acceptance-matrix reviewer will compare the
implementation and evidence against issue #800 and epic #786. Fresh Cursor
reviews against fetched `origin/develop` must then converge to `RESULT: clean`
before the PR is eligible for auto-merge.
