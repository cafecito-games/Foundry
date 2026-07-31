# FoundryScript Editor Tooling Protocols Design

**Status:** Approved design for GitHub issue specification

**Date:** 2026-07-31

**Umbrella issue:** [cafecito-games/Foundry#1418](https://github.com/cafecito-games/Foundry/issues/1418)

**Extension design:** `cafecito-games/Foundry-Scripting`,
`docs/superpowers/specs/2026-07-31-foundryscript-vscode-design.md`

## Summary

Foundry issue #1418 should become an epic for the engine-owned artifacts and protocols needed by editor tooling.
The work is broader than a VS Code integration but remains driven by the FoundryScript extension. It comprises five
independent or sequentially separable workstreams:

1. generate and publish a complete TextMate grammar;
2. add the Language Server Protocol semantic-token transport;
3. classify FoundryScript symbols for semantic tokens;
4. investigate and specify a command-first headless Debug Adapter Protocol tooling host; and
5. specify a framework-neutral Foundry Test Adapter Protocol.

FoundryLib will be the first test framework to implement the test adapter protocol, but neither Foundry nor the
extension will depend on FoundryLib. Other frameworks can integrate by implementing the same runner-side contract.

## Evidence and Corrections to the Original Issue

The investigation established several facts that change the original issue's proposed solutions.

### TextMate and semantic highlighting are complementary

VS Code applies TextMate tokenization immediately and semantic highlighting later. Semantic highlighting may also be
disabled by the theme or user. The generated TextMate grammar must therefore remain useful on its own, including
careful positional heuristics for contextual words. Semantic tokens refine those classifications; they do not replace
the TextMate layer.

`GRAMMAR.md` section 2.5 has separate representations for reserved words, numeric keyword constants, and contextual
keywords. The fenced reserved-word block does not contain `INF`, `NAN`, `PI`, or `TAU`, while the tokenizer keyword
macro does. A drift check must compare the appropriate sets explicitly instead of assuming one fenced block is the
complete tokenizer table.

### Semantic tokens require classification and position work

The language server currently has no semantic-token request types, capability advertisement, request handler, or
encoder. The analyzed AST retains much of the required symbol information, but some contextual token positions are
not retained as durable AST fields. The implementation needs an AST/token correlation strategy rather than only an
LSP method registration.

LSP positions default to UTF-16 code units. Existing shared FoundryScript position conversion counts Godot `String`
characters, while formatting added its own explicit UTF-16 handling. Semantic-token v1 uses the protocol-default
UTF-16 encoding and does not negotiate alternatives. Token starts and lengths must be correct after non-BMP
characters, tabs, and same-line tokens.

### The DAP server already runs in a headless editor host

The existing debug adapter is an `EditorPlugin` coupled to `EditorNode`, `EditorDebuggerNode`, `EditorRunBar`, and
`ScriptEditor`. A live headless editor launch was observed listening on both the default LSP port 6005 and DAP port
6006 from the same Foundry process. The existing `lsp serve` command also enters editor mode; it is not a lightweight
editor-independent daemon.

A command-first DAP service is therefore feasible as a headless full-editor tooling host. A genuinely
editor-independent DAP daemon is a substantially larger refactor and is not required by this epic unless the spike
finds that the tooling-host model cannot meet the extension's lifecycle needs.

The extension design's sample `lsp serve --path <project>` invocation must use the supported `--project <dir>` form.

### `project test` does not own test semantics

`project test` invokes an arbitrary `ScriptRunner.run(args) -> int`. The engine observes only runner completion and
one exit code. `ScriptTestExecutionResult` describes guarded function execution and deliberately does not infer test
pass or failure. It does not represent discovered cases, stable IDs, hierarchy, source ranges, skip state, or
per-case results.

The earlier project-test epic, #831, intentionally kept assertion and reporting policy in framework/addon space.
Consequently, adding `project test --format=json` would either produce insufficient data or force a test framework
into the engine. The replacement is a runner-side adapter protocol carried through the existing `--` argument
boundary.

## Epic Scope and Ownership

Rename #1418 to **Epic: Engine support for FoundryScript editor tooling** and add the `epic` label. The issue remains
the umbrella for the Foundry-owned deliverables and links cross-repository dependencies.

### Foundry owns

- the authoritative generated TextMate artifact and release pipeline;
- the LSP semantic-token wire protocol and FoundryScript classifications;
- the DAP/tooling-host topology, selected command surface, and lifecycle contract;
- the normative Foundry Test Adapter Protocol specification;
- a reusable protocol validator plus executable conformance fixtures for engine-owned transport behavior; and
- compatibility guarantees for `project test` argument pass-through and exit propagation.

### FoundryLib owns

- the first conforming implementation of Foundry Test Adapter Protocol v1;
- stable IDs for FoundryLib suites, methods, and parameterized cases;
- discovery metadata and exact selection;
- streaming TAP13 result production; and
- compatibility with its existing human-readable and TAP CLI modes.

### The FoundryScript extension owns

- downloading and packaging the released TextMate grammar;
- consuming the server-provided semantic-token legend and contributing the custom `final` modifier;
- launching and supervising the LSP and DAP tooling host;
- configuring a test runner and negotiating its adapter capabilities;
- translating discovery records into VS Code `TestItem` objects; and
- translating TAP test points into VS Code test run events.

## Foundry Child Issues

### 1. Generate and release the FoundryScript TextMate grammar

Produce a complete deterministic `source.foundryscript` TextMate grammar for `.fs` files. The generator combines
machine-extracted language facts with checked-in pattern definitions; section 2.5 alone is not a complete syntax
highlighter.

Required lexical coverage:

- reserved words and boolean/null literals;
- `INF`, `NAN`, `PI`, and `TAU`;
- decimal, hexadecimal, binary, integer, and floating-point literals;
- short and triple-quoted strings, including raw, StringName, and NodePath prefixes;
- comments and `##` documentation comments, with longer forms matched first;
- qualified annotations;
- `$` and `%` node references without confusing `%` modulo expressions; and
- conservative contextual/type-position patterns that do not globally reserve contextual identifiers.

Acceptance criteria:

- the generator emits valid, byte-deterministic `foundryscript.tmLanguage.json`;
- the grammar uses the stable scope name `source.foundryscript` and file type `.fs`;
- a behavioral `misc/checks/check_*.py` check compares parsed grammar and tokenizer keyword sets, including numeric
  constants as a separately defined set;
- tests exercise the generator through an Oniguruma/TextMate-compatible tokenizer rather than assuming Python `re`
  proves VS Code behavior;
- the pre-commit trigger covers `GRAMMAR.md`, the tokenizer, generator, pattern inputs, and check;
- release automation publishes `foundryscript-tmlanguage-<version>.json`; and
- release workflow tests parse the workflow graph and prove the artifact reaches packaging and publication.

The generated JSON need not be committed to Foundry. Generator inputs and tests are committed; release automation
produces the versioned artifact. The extension may commit a fetched copy according to its own reproducibility policy.

### 2. Add FoundryScript semantic-token protocol support

Implement the protocol and encoding foundation independently of full AST classification.

The server advertises `semanticTokensProvider` with full-document support only. It registers the exact request method
`textDocument/semanticTokens/full`; it does not advertise range requests or full/delta refreshes in v1. The server
uses the LSP default UTF-16 position encoding and does not select or advertise an alternative encoding in v1.

Stable token type legend, in order:

1. `namespace`
2. `class`
3. `interface`
4. `struct`
5. `enum`
6. `enumMember`
7. `event`
8. `type`
9. `typeParameter`
10. `function`
11. `method`
12. `property`
13. `variable`
14. `parameter`
15. `decorator`
16. `keyword`

Stable modifier legend, in order:

1. `declaration`
2. `static`
3. `abstract`
4. `final`
5. `async`
6. `readonly`
7. `defaultLibrary`

`final` is a declared custom modifier; the extension must register the identical custom modifier. Themes are not
expected to style it unless they opt in.

Acceptance criteria:

- initialize returns the exact stable legends and full-only capability;
- the full request dispatches through the normal protocol action path;
- records are sorted, non-overlapping, single-line, and encoded as valid five-integer delta records;
- line, start, and length values use UTF-16 code units, including tokens that start after or contain astral characters;
- unsaved `didOpen` and `didChange` text is used instead of stale disk content;
- empty, incomplete, and invalid documents return a safe result without crashing; and
- tests decode the returned stream rather than only comparing opaque integer arrays.

### 3. Classify FoundryScript symbols for semantic tokens

Use the analyzed document cache and AST/token source spans to classify declarations and references. Semantic
`keyword` tokens principally cover contextual constructs whose role is known from parsing or analysis. TextMate
continues to provide the lexical fallback.

Required classification coverage:

- namespaces and qualified namespace segments;
- project classes, native classes, traits, tuples, enums, and enum members;
- type parameters and type references;
- global functions, class methods, static methods, lambdas, and native utility functions;
- properties, members, locals, constants, and parameters;
- annotation declarations and uses; and
- contextual `extend`, `async`, `annotation`, `targets`, `get`, and `set` occurrences without coloring ordinary
  identifiers of the same spelling.

Policy decisions:

- traits map to `interface` and tuples map to `struct`;
- signals map to the standard `event` type;
- instance and static fields map to `property`, while local storage maps to `variable`;
- class declarations and references map to `class`, annotation declarations and uses map to `decorator`, and lambdas
  map to `function`;
- `type` is reserved for built-in or analyzer-resolved type symbols without a more specific class, interface, struct,
  or enum classification;
- constants receive `readonly`;
- `defaultLibrary` applies to resolved native classes, built-in types, native members, native utility functions, and
  built-in constants; same-spelled project symbols do not receive it;
- `declaration` applies only to declarations; `abstract`, `final`, and `async` apply only where declared; `static`,
  `readonly`, and `defaultLibrary` follow the resolved symbol at declarations and references;
- punctuation such as generic brackets is not emitted because the legend contains no operator or punctuation type.

Exact identifier classification wins over broader contextual or type-node candidates. Identical spans with the same
type merge modifiers. Conflicting or partially overlapping lower-priority candidates are discarded and covered by
debug assertions and tests.

Acceptance criteria include declaration and use-site coverage, same-spelled native/project symbols, contextual words
used as ordinary identifiers, generics versus comparisons, tabs, Unicode, parse recovery, and a new full request after
`didChange` reflecting the managed buffer. Server-initiated `workspace/semanticTokens/refresh` is out of scope.

### 4. Investigate and specify a command-first headless DAP tooling host

The investigation must choose the service topology before implementing a new command. One possible surface is:

```sh
foundry --headless dap serve --project <dir> --port <port>
```

For this epic, a DAP service may start a hidden full editor host and its editor plugins. It need not be a lightweight
standalone debug daemon. The lifecycle must be explicit so the extension does not accidentally start separate LSP and
DAP hosts that each bind both plugins' default ports.

Spike acceptance criteria:

- use the existing headless editor, LSP host, and legacy port override to prototype the service without first landing
  parser or help changes;
- choose one combined tooling-host process, dedicated commands that suppress the unrequested plugin, or documented
  reuse of one existing host with a second host forbidden;
- specify loopback-only listening and a readiness line beginning `FOUNDRY_TOOLING ` followed by JSON containing the
  canonical project path, process ID, local-only flag, and actual enabled LSP/DAP ports;
- decide whether port `0` requests ephemeral allocation; if retained, readiness reports the actual bound port;
- DAP initialize/configuration handshake;
- launch of the requested Foundry project;
- breakpoint synchronization and hit confirmation;
- threads, stack traces, scopes, variables, evaluate, continue, and terminate;
- reject a DAP launch project that conflicts with the host's fixed `--project` identity;
- define behavior for attach without a session and port conflicts;
- distinguish debuggee termination from tooling-host shutdown: DAP `terminate` stops the debuggee, while the extension
  that launched the host owns and terminates the host process;
- determine whether DAP can launch `project test` with a configured runner, adapter protocol version, and exact
  selected test IDs; and
- record the chosen command surface and create a separate implementation child with parser, help, readiness, service,
  and end-to-end acceptance tests.

If the full-editor tooling host works, an editor-independent DAP refactor is out of scope. If it fails, the spike must
record the concrete coupling that blocks it and propose a separate refactor epic. If selected-test debugging is not
feasible in v1, the decision and extension limitation must be explicit; the extension must not advertise a Test
Explorer debug profile without an accepted runner-debug launch contract.

### 5. Specify Foundry Test Adapter Protocol v1

The protocol operates entirely in runner arguments after the existing engine `--` boundary:

```sh
foundry --headless --no-header project test \
  --project <project> \
  --runner <runner> -- \
  adapter <capabilities|discover|run> [options]
```

`adapter` is a positional runner subcommand, not an engine option. It matches the command-first CLI style and avoids
embedding a framework name in the protocol surface.

#### Capabilities

```sh
... -- adapter capabilities --output <file>
```

Writes one JSON document:

```json
{
  "protocol": "foundry-test-adapter",
  "supported_versions": [1],
  "framework": {
    "id": "foundrylib.testlib",
    "name": "FoundryLib TestLib",
    "version": "<framework-version>"
  },
  "extensions": []
}
```

The framework fields describe the implementation and are not fixed to FoundryLib. Version 1 requires discovery,
exact selection, TAP13, and report-file output, so those are not optional feature flags. A client selects the highest
mutually supported version and passes it to subsequent operations. Unknown capability fields and optional extensions
are ignored; an unknown selected protocol version is rejected.

`protocol`, `supported_versions`, `framework`, and `extensions` are required. `supported_versions` is a non-empty,
ascending array of unique positive integers. `framework.id`, `framework.name`, and `framework.version` are non-empty
strings. `extensions` is an array of unique names; v1 defines none. Clients ignore unknown optional extension names
and v1 has no required-extension mechanism.

An arbitrary legacy runner cannot be required to recognize adapter arguments or return a particular error. If the
capabilities file is missing or invalid, the client treats the runner as unsupported regardless of process exit code.
A conforming adapter returns exit 2 for an unsupported selected version.

#### Discovery

```sh
... -- adapter discover --protocol-version 1 --output <file>
```

Writes and flushes newline-delimited JSON records. Every record contains `protocol`, `version`, and `event`.
Supported v1 events are:

- `discovery_start`;
- `suite`;
- `test`;
- `discovery_error`; and
- `discovery_end`.

The exact v1 record shapes are shown below. Emitted JSONL records use the same fields on one line.

`discovery_start`:

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_start","root":"res://tests"}
```

`suite`:

```json
{
  "protocol": "foundry-test-adapter",
  "version": 1,
  "event": "suite",
  "id": "suite-id",
  "label": "MathTests",
  "parent_id": null,
  "path": "res://tests/math_tests.fs",
  "range": {"start":{"line":0,"character":0},"end":{"line":20,"character":0}},
  "runnable": true,
  "skipped": false,
  "skip_reason": null
}
```

`test`:

```json
{
  "protocol": "foundry-test-adapter",
  "version": 1,
  "event": "test",
  "id": "test-id",
  "label": "adds numbers",
  "parent_id": "suite-id",
  "path": "res://tests/math_tests.fs",
  "range": {"start":{"line":4,"character":0},"end":{"line":6,"character":1}},
  "runnable": true,
  "skipped": false,
  "skip_reason": null,
  "case_key": null
}
```

`discovery_error`:

```json
{
  "protocol": "foundry-test-adapter",
  "version": 1,
  "event": "discovery_error",
  "id": "error-id",
  "label": "BrokenTests discovery",
  "parent_id": null,
  "message": "Unable to index suite",
  "path": "res://tests/broken_tests.fs",
  "range": null
}
```

`discovery_end`:

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_end","suite_count":1,"test_count":1,"error_count":1}
```

Required field rules:

- `discovery_start.root` is the effective `res://` discovery root;
- `suite` and `test` require `id`, `label`, `parent_id`, `path`, `range`, `runnable`, `skipped`, and `skip_reason`;
- `parent_id`, `path`, `range`, and `skip_reason` are nullable but present; a skipped item has a non-empty skip reason;
- `runnable` says whether an item may be selected directly; `skipped` says a selected test will report TAP `# SKIP`;
- `test.case_key` is null for an ordinary test and is a framework-stable string for a parameterized case;
- `discovery_error` requires stable `id`, `label`, nullable `parent_id`, `message`, nullable `path`, and nullable
  `range`;
- a range is zero-based, end-exclusive, and counts UTF-16 code units, with `start` and `end` objects containing
  non-negative integer `line` and `character` fields; and
- `discovery_end` requires non-negative `suite_count`, `test_count`, and `error_count` matching preceding records.

IDs are protocol identity, not display labels. They remain stable while a framework's logical test identity is
unchanged. Duplicate IDs, missing parents, invalid ranges, unknown events, and malformed records are conformance
errors. Unknown fields are ignored for forward-compatible additive metadata. Ordering is deterministic, parents
precede children, and `discovery_end` is the final record.

Recoverable discovery errors may be interleaved with successfully discovered items; the adapter still writes
`discovery_end` and exits 1. A crash or infrastructure failure may leave a truncated stream without `discovery_end`;
the client treats that as an adapter failure rather than a valid partial discovery result.

#### Execution

```sh
... -- adapter run --protocol-version 1 --report <file> [--select <stable-id>]...
```

No selection runs the discovered suite. `--select` is repeatable and exact. Selecting a suite selects its descendants;
repeated IDs and overlapping suite/descendant selections are deduplicated so each leaf runs once. Unknown IDs are
protocol errors. The extension can expand exclusions into an explicit set of leaf IDs, so v1 does not need a separate
exclusion option.

The report is streaming TAP13:

```tap
TAP version 13
# foundry-test-adapter: 1
1..1
ok 1 - MathTests.adds_numbers
  ---
  _foundry:
    id: "res://tests/math_tests.fs::MathTests::adds_numbers"
    duration_ms: 2
    status_detail: ""
  ...
```

Each test point has a YAML diagnostic block with mandatory `_foundry.id`. `_foundry.duration_ms` and
`_foundry.status_detail` preserve structured timing and failure-kind data. `duration_ms` is a required non-negative
integer. `status_detail` is one of `""`, `discovery_error`, `runtime_error`, `timed_out`, `aborted`, or `setup_error`.
Failures also use standard TAP diagnostic fields such as `message` and this one-based location shape:

```yaml
at:
  fileName: "res://tests/math_tests.fs"
  lineNumber: 5
  columnNumber: 1
```

Skips use the standard `# SKIP` directive.

The runner creates or truncates a caller-provided report path and flushes every complete test point as it finishes.
Application logs remain on ordinary stdout/stderr and cannot corrupt the report. Catastrophic infrastructure failure
detected by the adapter uses TAP `Bail out!` when a report has started. A process crash or forced kill cannot be
required to append a terminal marker.

The TAP plan appears before test points and declares the deduplicated number of selected leaf tests. Test points are
emitted in deterministic discovery order. A normal report is complete only when its points satisfy the plan and the
process exits with the corresponding adapter status.

#### Exit and lifecycle rules

- exit 0: the operation succeeded and all selected tests passed;
- exit 1: discovery or test failures were represented normally;
- exit 2: a conforming adapter detected invalid invocation, unsupported selected version, malformed selection, or
  runner infrastructure failure.

An uncaught engine-level script failure may override a runner return value and exit 1. In that case the missing,
invalid, bailed-out, or incomplete protocol artifact tells the client this was infrastructure failure rather than a
normal test failure.

Cancellation is external process cancellation in v1. The extension requests normal child-process termination using
the platform mechanism and may force termination after a grace period. Only already flushed complete TAP points are
guaranteed; no terminal record or portable exit code is required. The extension's cancellation state takes precedence,
and an unsatisfied plan remains incomplete rather than being interpreted as success. A future protocol version may add
cooperative cancellation.

Capabilities and discovery output files are complete when the process exits. The execution report is intentionally
tail-able while the process is alive. Callers provide unique scratch paths; runners do not write protocol artifacts
into tracked fixture directories.

#### Compatibility and conformance

`ScriptRunner.run(args) -> int` remains unchanged. Ordinary runner modes remain valid. The adapter subcommand is an
additive opt-in contract.

Foundry owns checked-in normative JSON schemas, TAP profile examples, and a reusable conformance validator. The
validator accepts arbitrary adapter artifacts and reports all schema/profile violations; it is not a parser used only
by test scaffolding. FoundryLib runs the validator against live reference-runner output, and the extension tests its
consumer against the same normative fixtures.

Foundry engine tests exercise observable host behavior through real subprocesses: runner argument pass-through,
report-file creation and incremental flushing, uncaught runner failure, and exit propagation. Protocol validator tests
cover positive and negative capabilities, discovery, and TAP artifacts, including Unicode ranges, hierarchy,
duplicate and unknown IDs, incomplete streams, selection metadata, pass/fail/skip, failure details, and malformed
output. Tests must not assert on protocol documentation prose or implementation source text.

## FoundryLib Reference-Implementation Issue

Open a FoundryLib enhancement titled **Implement Foundry Test Adapter Protocol v1** and link it to Foundry #1418 and
the Foundry protocol child issue.

Required changes:

- add command-first `adapter capabilities`, `adapter discover`, and `adapter run` modes;
- advertise supported protocol versions and require an explicitly selected version for discovery and execution;
- preserve the existing human CLI, `--list`, `--filter`, `--format pretty`, and `--format tap` behavior;
- generate stable IDs for suites, methods, parameterized cases, and discovery failures;
- retain normal-test source ranges, not only discovery-error locations;
- emit deterministic JSONL discovery events with suite hierarchy;
- implement repeatable exact-ID set selection with overlap deduplication while retaining substring filtering for
  humans;
- stream TAP13 points instead of constructing the entire report before printing;
- add `_foundry` YAML metadata to every point, including passing and skipped cases;
- write adapter TAP to a report file so user output cannot corrupt it;
- map passed, failed, skipped, discovery error, runtime error, timeout, abort, and setup error states;
- flush results incrementally and preserve completed results under external process cancellation; and
- run the Foundry conformance validator against live capabilities, discovery, and execution artifacts, with additional
  behavioral tests for selection, repeated-discovery ID stability, output isolation, compatibility, and exits.

## Extension Follow-up Issues

The FoundryScript extension should track two downstream issues if equivalents do not already exist:

1. **Integrate VS Code Test Explorer with Foundry Test Adapter Protocol v1**
   - configurable runner path and arguments;
   - capabilities negotiation;
   - discovery-to-`TestItem` hierarchy;
   - TAP-to-test-run translation;
   - run selection, cancellation, refresh, and source navigation; and
   - helpful unsupported-runner and malformed-report diagnostics.
2. **Integrate VS Code debugging with the Foundry DAP tooling host**
   - host launch and readiness;
   - port/project lifecycle;
   - launch and attach configurations;
   - breakpoint synchronization; and
   - Test Explorer debug profiles only after adapter selection and a runner-debug DAP launch contract are stable.

Existing syntax and LSP epics remain the downstream owners for TextMate and semantic-token consumption.

The approved extension design and plan must be amended or superseded where they still prescribe `lsp serve --path`,
engine-owned `project test --format=json`, or semantic tokens replacing rather than augmenting TextMate contextual
patterns.

## Tracking Relationships

- The test adapter protocol child is related to existing Foundry project-test epic #831 and must preserve its decision
  to keep framework policy outside the engine.
- The TextMate child links Foundry-Scripting #5 and syntax-highlighting epic #14.
- Semantic classification is blocked by semantic-token protocol support; both link Foundry-Scripting LSP epic #15 and
  a semantic-token consumption follow-up.
- The DAP investigation and later implementation link the proposed extension debugging issue.
- The test adapter protocol links the FoundryLib reference-implementation issue and proposed extension Test Explorer
  issue.

FoundryLib and extension issues are cross-repository tracked dependencies, not Foundry child issues. Epic completion
may depend on them, but their work and status remain owned by their respective repositories.

## Delivery Order

1. Generate and release the TextMate grammar.
2. Add semantic-token protocol support.
3. Add semantic classification.
4. Complete the DAP tooling-host investigation and create the chosen implementation child if needed.
5. Specify and land Foundry Test Adapter Protocol v1 and conformance fixtures.
6. Implement the protocol in FoundryLib.
7. Integrate Test Explorer and debugging in the extension.

The test protocol specification and FoundryLib implementation may overlap once command syntax, schemas, TAP metadata,
and exit semantics are frozen.

## Non-Goals

- making Foundry depend on FoundryLib;
- defining assertion, mocking, fixture, or snapshot policy in the engine;
- changing `ScriptRunner.run(args) -> int`;
- adding `project test --format=json` without runner participation;
- semantic-token range requests or delta refreshes in v1;
- semantic-token position encodings other than UTF-16 in v1;
- server-initiated semantic-token refresh notifications in v1;
- replacing TextMate contextual heuristics with semantic highlighting;
- refactoring DAP into a lightweight editor-independent daemon unless the spike proves it necessary; or
- implementing extension UI in the Foundry repository.

## Epic Completion Criteria

#1418 can close when:

- all five Foundry workstreams have completed or recorded an explicit accepted outcome;
- the TextMate artifact is available from a release and consumed by the extension;
- semantic tokens are advertised, encoded, classified, and consumed by the extension;
- the extension has a documented supported DAP host lifecycle, even if the investigation rejects a dedicated command;
- selected-test debugging either has an accepted runner-debug DAP launch contract or is explicitly deferred without
  an advertised Test Explorer debug profile;
- Foundry Test Adapter Protocol v1 is normative and covered by executable conformance fixtures;
- FoundryLib has a conforming reference implementation; and
- the extension can discover and run individual FoundryLib tests through the protocol.
