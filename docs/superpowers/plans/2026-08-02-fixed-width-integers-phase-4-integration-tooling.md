# Fixed-Width Integers Phase 4: Integration and Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `int`, `uint`, `long`, and `ulong` release-ready across native APIs, reflection, language tooling,
exact-value editor controls, persistence, debugging, and migration documentation.

**Architecture:** Preserve `NumericType` until the last typed boundary, while generic engine APIs continue to expose the
`INT` or `UINT` carrier. Route every user-facing type name through the FoundryScript numeric registry. Replace the
double-backed integer inspector path with an exact decimal editor, retaining sliders only as optional controls for
exactly representable ranges.

**Tech Stack:** FoundryScript analyzer and editor tooling, native binding metadata, editor inspector controls, editor
automation MCP, doctest, script fixtures, TextMate grammar generation, review gallery.

**Prerequisite:** Phases 1 through 3 complete; all four source types, checked operations, typed containers, and bytecode
descriptors pass strict validation.

---

## File map

- `core/object/method_bind.h/.cpp`: expose integer metadata wherever FoundryScript source is analyzed.
- `core/variant/type_info.h`: authoritative native integer metadata and Foundry-specific type descriptors.
- `modules/foundry_script/fs_analyzer.h/.cpp`: map native metadata to `NumericType` without carrier-only inference.
- `modules/foundry_script/fs_analyzer_call_validation.cpp`: validate native calls against exact integer widths.
- `modules/foundry_script/foundry_script.h/.cpp`: retain exact property and method descriptors for reflection.
- `modules/foundry_script/fs_reflection.cpp`: expose exact FoundryScript signatures while preserving generic erasure.
- `modules/foundry_script/editor/fs_refactoring.cpp`: preserve exact types in refactor edits and generated annotations.
- `modules/foundry_script/fs_editor.cpp`: numeric completion, hover, and signature rendering.
- `modules/foundry_script/editor/fs_docgen.cpp`: render source numeric names instead of Variant carrier names.
- `modules/foundry_script/editor/fs_highlighter.cpp`: recognize canonical integer suffixes and built-in type names.
- `modules/foundry_script/grammar/patterns/lexical.json`: TextMate numeric literal suffix patterns.
- `modules/foundry_script/grammar/patterns/keyword_scopes.json`: TextMate built-in numeric type scopes.
- `editor/inspector/editor_properties.h/.cpp`: exact signed and unsigned integer property editor.
- `modules/foundry_script/tests/test_foundry_script_type.h`: native metadata and reflection tests.
- `modules/foundry_script/tests/test_completion.h`: completion, hover, and signature tests.
- `modules/foundry_script/tests/test_format.h`: canonical suffix formatting tests.
- `modules/foundry_script/tests/scripts/format/`: formatter fixtures for fixed-width integers.
- `tests/editor/test_editor_exact_integer_property.h`: exact inspector unit tests.
- `tests/test_main.cpp`: register the editor property test header.
- `tests/fixtures/editor_exact_integer_property/`: editor automation acceptance project.
- `doc/classes/`: Variant and editor-facing API reference changes caused by the new carrier.
- `docs/foundry_script/`: source migration guide and numeric reference.

### Task 1: Preserve native integer metadata through analysis

**Files:**
- Modify: `core/object/method_bind.h:90-110`
- Modify: `core/object/method_bind.cpp`
- Modify: `core/variant/type_info.h`
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp:11420-11480,13020-13080`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`

- [ ] **Step 1: Add behavioral tests for every native integer width**

Bind fixture methods and properties using signed and unsigned 8-, 16-, 32-, and 64-bit C++ types. Analyze a script that
calls each method at its minimum, maximum, and one invalid boundary. Assert on the resulting `DataType::numeric_type`
and emitted call diagnostic, not on source text.

Expected mapping:

| Native metadata | FoundryScript type | Public source name |
|---|---|---|
| `INT8`, `INT16`, `INT32` | signed 8/16/32 constraint | `int` |
| `UINT8`, `UINT16`, `UINT32` | unsigned 8/16/32 constraint | `uint` |
| `INT64` or missing signed width | signed 64 | `long` |
| `UINT64` or missing unsigned width | unsigned 64 | `ulong` |

- [ ] **Step 2: Run the focused tests and capture carrier-only failures**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*Native*Integer*"`

Expected: metadata smaller than 64 bits collapses to `INT`, and unsigned metadata cannot describe the upper half of
`uint64_t`.

- [ ] **Step 3: Make binding metadata available to the analyzer**

Move the read-only `MethodBind::get_argument_meta()` access out of `DEBUG_ENABLED` without changing debug-only
validation or diagnostic storage. Keep one metadata source in `GetTypeInfo<T>`; do not reconstruct width from a carrier
value.

- [ ] **Step 4: Add one metadata-aware conversion helper**

```cpp
FSParser::DataType type_from_property(
		const PropertyInfo &p_property,
		bool p_is_arg = false,
		bool p_is_readonly = false,
		FoundryTypeInfo::Metadata p_metadata = FoundryTypeInfo::METADATA_NONE) const;
```

Map metadata through `NumericType`; use the carrier default only when metadata is absent. Apply this helper to method
parameters, return values, getters, setters, signals, and native properties.

- [ ] **Step 5: Validate calls using the descriptor range**

For constants, report an analyzer error at the argument. For dynamic values, leave a checked conversion at the native
call boundary. Preserve the existing diagnostic category and add expected and actual source type names.

- [ ] **Step 6: Re-run the native metadata tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*Native*Integer*"`

Expected: all widths map consistently, constants fail during analysis, and dynamic out-of-range calls fail before
invoking native code.

### Task 2: Complete reflection and cross-script type retention

**Files:**
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_reflection.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/fixed_width_integer_reflection.fs`

- [ ] **Step 1: Add a cross-script reflection test**

Load a producer script with properties, arguments, and return values of all four public types from a consumer script.
Assert that FoundryScript reflection returns exact names and descriptors, while generic `PropertyInfo` returns only
`INT` or `UINT`.

- [ ] **Step 2: Run the test and verify width erasure**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*fixed_width_integer_reflection*"`

Expected: signedness survives through `UINT`, but 32- and 64-bit members sharing a carrier are indistinguishable.

- [ ] **Step 3: Store rich descriptors beside generic metadata**

Add `NumericType` to FoundryScript property and method metadata. Keep the generic engine property list ABI unchanged:
exact width is a FoundryScript reflection detail, and generic consumers see the carrier's full range.

- [ ] **Step 4: Use rich descriptors for cross-script analysis**

When the analyzer resolves another FoundryScript resource, read the rich descriptor before falling back to
`PropertyInfo`. Apply the same checked assignment and call rules used within one script.

- [ ] **Step 5: Re-run the type and reflection tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*Type*" --case "*fixed_width_integer_reflection*"`

Expected: exact types survive cross-script use without changing generic reflection behavior.

### Task 3: Update completion, hover, documentation, and refactoring

**Files:**
- Modify: `modules/foundry_script/fs_editor.cpp:1320-1370,4470-4560`
- Modify: `modules/foundry_script/editor/fs_docgen.cpp:80-130`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp:6570-6630`
- Modify: `modules/foundry_script/tests/test_completion.h`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Create: `modules/foundry_script/tests/scripts/completion/features/fixed_width_integer_types.fs`

- [ ] **Step 1: Add tooling behavior tests**

Assert that type-position completion lists `int`, `uint`, `long`, and `ulong` exactly once; hover and signatures render
the declared width; generated method stubs use native metadata; and a rename/refactor preserves annotations and literal
suffixes.

- [ ] **Step 2: Run focused tooling tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Completion*Fixed*Width*" --case "*Refactor*Fixed*Width*"`

Expected: tooling derived from `Variant::get_type_name()` emits carrier names or omits the new source types.

- [ ] **Step 3: Route source names through the numeric registry**

Replace Variant-only type enumeration and rendering with the Phase 2 descriptor registry. Keep `INT` and `UINT` as
engine carrier names only; FoundryScript UI always uses the declared or inferred source type.

- [ ] **Step 4: Preserve descriptor metadata in generated edits**

Use the analyzer's `DataType` when generating annotations, overrides, code actions, and refactor replacements. Never
infer `int` versus `long` by inspecting the current value.

- [ ] **Step 5: Re-run the tooling tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Completion*Fixed*Width*" --case "*Refactor*Fixed*Width*"`

Expected: all four types are stable across completion, hover, signatures, doc generation, and refactoring.

### Task 4: Update formatting and syntax highlighting

**Files:**
- Modify: `modules/foundry_script/editor/fs_highlighter.cpp`
- Modify: `modules/foundry_script/grammar/patterns/lexical.json`
- Modify: `modules/foundry_script/grammar/patterns/keyword_scopes.json`
- Modify: `modules/foundry_script/tests/test_format.h`
- Create: `modules/foundry_script/tests/scripts/format/fixed_width_integer_suffixes/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/fixed_width_integer_suffixes/expected.fs`

- [ ] **Step 1: Add a formatter fixture for every literal form**

Cover decimal, hexadecimal, binary, underscores, unary minus, and `U`, `L`, and `UL`. The expected fixture preserves
uppercase canonical suffixes byte-for-byte.

- [ ] **Step 2: Add highlighter and TextMate grammar tests**

Assert that the suffix is part of the numeric token and that all four public types use the built-in type scope. Invalid
lowercase spellings remain parser errors; the formatter does not silently canonicalize invalid source.

- [ ] **Step 3: Run the focused tests and grammar generator**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Format*Fixed*Width*" --case "*Highlighter*Fixed*Width*"`

Run: `python3 modules/foundry_script/grammar/tmlanguage_builder.py --output /tmp/foundryscript.tmLanguage.json`

Expected: both commands succeed and the generated grammar recognizes the three suffix alternatives after each integer
base.

- [ ] **Step 4: Implement descriptor-aware highlighting**

Use the tokenizer's suffix boundary in the editor highlighter. Update the declarative grammar patterns to match
uppercase-only suffixes and add `uint`, `long`, and `ulong` to built-in type scopes.

- [ ] **Step 5: Re-run formatting and grammar checks**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Format*Fixed*Width*" --case "*Highlighter*Fixed*Width*"`

Run: `pre-commit run --files modules/foundry_script/grammar/patterns/lexical.json modules/foundry_script/grammar/patterns/keyword_scopes.json`

Expected: formatting is stable, highlighting is canonical, and generated grammar checks pass.

### Task 5: Replace lossy integer inspector editing

**Files:**
- Modify: `editor/inspector/editor_properties.h:45-65,395-420`
- Modify: `editor/inspector/editor_properties.cpp:1580-1635,3810-3855,3910-4140`
- Create: `tests/editor/test_editor_exact_integer_property.h`
- Modify: `tests/test_main.cpp`
- Create: `tests/fixtures/editor_exact_integer_property/project.foundry`
- Create: `tests/fixtures/editor_exact_integer_property/exact_integer_property.fs`
- Create: `tests/fixtures/editor_exact_integer_property/exact_integer_property.tscn`

- [ ] **Step 1: Add exact signed and unsigned control tests**

Instantiate the integer editor through `EditorInspectorDefaultPlugin` for `Variant::INT` and `Variant::UINT`. Set and
commit `INT64_MIN`, `INT64_MAX`, `0`, and `UINT64_MAX`; assert on the emitted Variant type and exact value. Add invalid
text, negative unsigned, and out-of-range cases that keep the previous property unchanged.

- [ ] **Step 2: Prove the current double-backed loss**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*EditorProperty*Exact*Integer*"`

Expected: values above 2^53 round through `EditorSpinSlider`, and no `UINT` editor path exists.

- [ ] **Step 3: Preserve exact range hint text**

Extend `EditorPropertyRangeHint` with optional integer min, max, and step strings parsed by signedness. Keep its
existing doubles for float/vector editors. Integer bounds must not pass through `String::to_float()`.

- [ ] **Step 4: Make decimal text the source of truth**

Refactor `EditorPropertyInteger` to maintain a `Variant` integer value and an exact decimal `LineEdit`. Parse with
checked signed or unsigned helpers selected during `setup`; reject incomplete, negative unsigned, and out-of-range input
without emitting a change.

- [ ] **Step 5: Retain sliders only when they are exact**

An `EditorSpinSlider` may remain as a secondary control only if the current value, bounds, and step are all within the
exactly representable integer range of `double`. Hide or disable it otherwise. Synchronize slider changes back through
the same checked integer path.

- [ ] **Step 6: Register both carrier types**

Route `Variant::INT` and `Variant::UINT` through `EditorPropertyInteger`, passing signedness explicitly. Preserve enum,
flags, layers, and object-ID editors only for the carrier contracts they support; do not reinterpret object IDs as
ordinary unsigned script values.

- [ ] **Step 7: Re-run exact editor tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*EditorProperty*Exact*Integer*"`

Expected: every boundary round-trips exactly, rejected edits are atomic, and safe small ranges retain slider behavior.

### Task 6: Verify the real editor workflow and build a review gallery

**Files:**
- Modify: `tests/fixtures/editor_exact_integer_property/exact_integer_property.fs`
- Modify: `tests/fixtures/editor_exact_integer_property/exact_integer_property.tscn`
- Create: `tests/editor/test_editor_exact_integer_property_workflow.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add an automation acceptance workflow**

Launch the fixture project, select a node exporting `long` and `ulong`, find the properties semantically, enter both
64-bit maxima, save, reload, and assert the exact displayed and stored values. Use the Foundry editor MCP bridge and
semantic selectors; do not use pixel coordinates.

- [ ] **Step 2: Run the acceptance test with a display**

Run: `DISPLAY=:1 python3 scripts/agent_build.py --backend ninja --test --case "*Exact*Integer*Workflow*"`

Expected: the workflow passes without rounding warnings or value changes after reload.

- [ ] **Step 3: Capture a proof gallery**

Capture the inspector showing `9223372036854775807` and `18446744073709551615`, then a post-reload view showing the same
values. Create a `proof` board with captions that ask reviewers to confirm exact preservation and the absence of lossy
slider controls.

- [ ] **Step 4: Poll editor diagnostics**

Use `foundry_poll_events` and `foundry_read_editor_log` to verify there are no parser, inspector, serialization, or
resource reload errors before disconnecting.

### Task 7: Close persistence, debugger, and RPC integration gaps

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.h`
- Create: `modules/foundry_script/tests/scripts/runtime/features/fixed_width_integer_persistence.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/fixed_width_integer_rpc.fs`
- Modify: `modules/foundry_script/language_server/fs_language_server.cpp`
- Modify: `editor/debugger/script_editor_debugger.cpp`
- Modify: `tests/core/io/test_resource.h`

- [ ] **Step 1: Add end-to-end boundary tests**

Cover scene/resource save and reload, project settings, debugger values, language-server evaluation, and local RPC
encoding for all four types. Assert exact carrier and value after each round trip. Explicitly document that untyped JSON
numbers cannot guarantee 64-bit integer precision; typed encodings must use the Variant text/binary representation from
Phase 1.

- [ ] **Step 2: Run the focused tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Fixed*Width*Persistence*" --case "*Fixed*Width*RPC*" --case "*Debugger*Integer*"`

Expected: any carrier-only switch omissions fail visibly; JSON tests verify the documented limitation rather than
promising impossible recovery after rounding.

- [ ] **Step 3: Add missing UINT dispatch cases**

Use shared Variant text/binary codecs instead of one-off decimal or signed casts. Send FoundryScript rich descriptors
only where both ends understand them; generic engine endpoints exchange the `INT` or `UINT` carrier.

- [ ] **Step 4: Re-run the integration tests**

Run: `python3 scripts/agent_build.py --backend ninja --test --case "*Fixed*Width*Persistence*" --case "*Fixed*Width*RPC*" --case "*Debugger*Integer*"`

Expected: every exact transport round-trips `UINT64_MAX`, and unsupported JSON paths state their precision boundary.

### Task 8: Document migration and perform release validation

**Files:**
- Modify: `doc/classes/@GlobalScope.xml`
- Modify: `doc/classes/Variant.xml`
- Create: `docs/foundry_script/fixed_width_integers.md`
- Create: `docs/foundry_script/int_to_long_migration.md`
- Modify: `modules/foundry_script/GRAMMAR.md`

- [ ] **Step 1: Write the numeric reference**

Document ranges, carriers, uppercase suffixes, literal inference, promotion, checked arithmetic, casts, type tests,
native mapping, typed-container retention, reflection erasure, persistence, and JSON precision limits.

- [ ] **Step 2: Write the breaking-change migration guide**

Tell existing code that relied on 64-bit `int` to annotate `long` and add `L` where inference matters. Include
diagnostics users will encounter and mechanical before/after examples. Do not introduce compatibility aliases or an
implicit legacy mode.

- [ ] **Step 3: Audit Variant and numeric switches**

Run: `rg -n "Variant::INT|Variant::FLOAT|VARIANT_MAX|get_type_name\(" core editor modules scene servers tests`

Classify every hit that can receive arbitrary Variant values or render FoundryScript source names. Add a behavioral
regression test for each missing `UINT` case before fixing it.

- [ ] **Step 4: Regenerate intentional fixtures and class reference output**

Run: `./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts`

Run: `./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format`

Run: `pre-commit run --all-files`

Review generated changes and keep only outputs caused by the feature.

- [ ] **Step 5: Run strict native validation**

Run: `python3 scripts/agent_build.py`

Expected: the strict `dev_mode=yes dev_build=yes tests=yes` native build completes without warnings.

- [ ] **Step 6: Run the full suite with structured progress**

Run: `DISPLAY=:1 ./bin/foundry.* --headless test run --progress-format=jsonl --progress-file /tmp/fixed-width-integer-tests.jsonl --progress-heartbeat-seconds 30 --force-colors`

Expected: the final doctest summary reports success, including GUI-dependent editor automation tests.

- [ ] **Step 7: Review the complete contract**

Confirm the public surface is exactly `int`, `uint`, `long`, and `ulong`; suffixes are exactly `U`, `L`, and `UL`;
runtime failure is checked and atomic; typed slots retain width; untyped boundaries erase width only; and no
float/double or small-width source syntax leaked into this release.

## Phase 4 completion gate

- Exact native metadata reaches analysis, calls, cross-script reflection, and generated source signatures.
- Completion, hover, doc generation, refactoring, formatting, and syntax highlighting use the four source names.
- Signed and unsigned 64-bit inspector values survive entry, save, and reload without a `double` conversion.
- Persistence, debugger, language-server evaluation, and RPC paths preserve carrier and exact value.
- Migration and numeric reference documentation are published; float widths and small integers remain follow-ups.
- The strict native build, full suite, editor automation workflow, pre-commit checks, and proof gallery pass review.
