# Foundry Test Adapter Protocol V1 Design

**Status:** Approved implementation specification

**Date:** 2026-07-31

**Issue:** [cafecito-games/Foundry#1428](https://github.com/cafecito-games/Foundry/issues/1428)

**Parent epic:** [cafecito-games/Foundry#1418](https://github.com/cafecito-games/Foundry/issues/1418)

## Summary

Foundry Test Adapter Protocol v1 is a framework-neutral runner-side contract carried through the existing
`project test --runner ... -- <runner args>` boundary. Foundry owns the wire protocol, normative artifacts,
conformance validator, and observable engine transport guarantees. Frameworks own discovery, stable test identity,
selection, execution, assertions, and reporting policy.

The protocol uses JSON for capabilities, JSONL for discovery, and a strict streaming TAP13 profile for execution.
Every artifact is written to a caller-provided absolute file path so application stdout and stderr cannot corrupt it.
The engine does not learn adapter options or test semantics, and `ScriptRunner.run(args) -> int` remains unchanged.

For v1, downstream repositories may consume the schemas, fixtures, and validator from an immutable pinned Foundry
checkout. Independent publication is not required. The checked-in bundle is nevertheless relocatable and versioned
so it can be packaged later without changing the protocol or validator interface.

## Feasibility Evidence

A throwaway project under `.test_scratch` exercised the current macOS development editor binary through the real
command-first CLI. No engine or fixture source was modified.

The probe established that:

- runner arguments arrived unchanged and in order after both the engine separator and a runner-level separator,
  including opaque arguments named `--output` and `--select`;
- a runner-created TAP report was isolated from deliberate stdout output;
- a flushed first TAP point was observable while the runner process remained alive;
- external termination left one complete point against an unsatisfied two-point plan;
- returning `2` from `ScriptRunner.run()` propagated as process exit `2`; and
- an uncaught Foundry Script error forced process exit `1` and left the report incomplete.

The existing focused project-test baseline also passed: 3 doctest cases and 24 assertions. These results validate the
runner-side adapter topology. They do not replace the checked-in transport regressions required by this design.

## Goals

- Define one normative v1 command grammar after the existing engine argument boundary.
- Define complete capabilities, discovery, selection, execution, exit, and cancellation semantics.
- Check in versioned Draft 2020-12 schemas and reusable positive and negative fixtures.
- Provide a reusable Python 3.8-compatible validator that reports all independent violations.
- Prove engine pass-through, artifact isolation, incremental flushing, exit propagation, uncaught failure, and
  cancellation with real subprocess tests.
- Keep the implementation sufficiently explicit that work is test-first and largely mechanical.

## Non-goals

- Adding engine-owned `project test --format=json` or adapter options to the engine parser.
- Changing `ScriptRunner.run(args) -> int`.
- Defining assertions, mocks, fixtures, snapshots, parameterization, or framework lifecycle policy.
- Inferring per-test results from `ScriptTestExecutionResult`.
- Making Foundry depend on FoundryLib or any other test framework.
- Publishing a separate v1 protocol artifact or Python package.
- Cooperative cancellation in v1.

## Ownership

Foundry owns the protocol, schemas, fixtures, validator, and transport tests. A conforming framework owns its adapter
argument parser, discovery graph, stable IDs, exact selection, and TAP production. FoundryLib is the first reference
implementation, not a dependency. Editor integrations negotiate and consume the same protocol without recognizing
framework-specific IDs, labels, or arguments.

## Checked-in Bundle

All reusable assets live together:

```text
tools/foundry-test-adapter/
├── pyproject.toml
├── uv.lock
├── README.md
├── src/foundry_test_adapter/
│   ├── __init__.py
│   ├── cli.py
│   ├── diagnostics.py
│   ├── json_artifacts.py
│   ├── selection.py
│   └── tap13.py
├── protocol/v1/
│   ├── README.md
│   ├── capabilities.schema.json
│   ├── discovery-record.schema.json
│   └── fixtures/
│       ├── manifest.json
│       ├── valid/
│       └── invalid/
└── tests/
```

The nested `pyproject.toml` defines an installable project named `foundry-test-adapter`, version `1.0.0`, with a
`foundry-test-adapter` console entry point and `requires-python = ">=3.8"`. Direct dependencies are pinned to
`jsonschema==4.23.0` and `PyYAML==6.0.3`; the committed `uv.lock` pins the complete resolution. `jsonschema` validates
capabilities and discovery records. `yaml.safe_load` parses TAP diagnostic mappings. TAP structure remains a focused
in-repository parser.

Normal commands use the frozen project:

```sh
uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter <operation> ...
```

Pre-commit and CI use this same project and lock rather than maintaining a second dependency list. The stable CLI and
JSON output are public interfaces. Internal Python modules are not.

## Runner Command Grammar

The engine invocation is:

```sh
foundry --headless --no-header project test \
  --project <project> \
  --runner <runner> -- \
  adapter <capabilities|discover|run> <reserved-options> [-- <framework-args>...]
```

`adapter` must be runner argument zero. The first `--` belongs to the engine and begins runner arguments. The optional
second `--` belongs to the adapter and ends reserved adapter parsing.

Exact operation grammars are:

```text
adapter capabilities --output <absolute-file>
    [-- <framework-args>...]

adapter discover --protocol-version <positive-integer>
    --output <absolute-file>
    [-- <framework-args>...]

adapter run --protocol-version <positive-integer>
    --report <absolute-file>
    [--select <stable-id>]...
    [-- <framework-args>...]
```

Reserved options may appear in any order before the second separator. `--output`, `--report`, and
`--protocol-version` are required exactly once where shown. `--select` is the only repeatable reserved option. Unknown
options, prohibited duplicates, missing values, extra positionals, malformed versions, unsupported versions, or
selection errors exit `2`.

Every reserved option consumes its required next token unconditionally, even when that token is `--` or begins with
`-`. Consequently `--select --` selects the opaque ID `--`; it does not begin framework arguments. The second
separator is recognized only while the parser is expecting a new reserved option. This keeps control-free opaque IDs
transportable without interpreting their spelling. Implementations may not delegate this distinction to a parser that
automatically treats option-looking values as options or separators.

A protocol version is an ASCII positive decimal integer. The second separator may be omitted when there are no
framework arguments. After it, every argument is opaque framework input, including arguments named `--output`,
`--report`, `--protocol-version`, or `--select`. The adapter must preserve those arguments exactly and must not
reinterpret, reorder, or normalize them.

## Common Artifact Rules

- Artifact paths are absolute native filesystem paths whose parent directory already exists.
- A valid operation creates or truncates its artifact. An adapter-detected open or write failure exits `2`.
- The Foundry Script `FileAccess.flush()` binding returns no error, so v1 requires adapters to call it but cannot
  require detection of a delayed flush/storage failure. A missing, truncated, or invalid artifact remains
  infrastructure failure regardless of the process exit.
- Clients provide unique paths and never trust a pre-existing artifact.
- Invalid invocation, version, or selection is resolved before writing a success-shaped artifact.
- Files are UTF-8 without a byte-order mark and use LF line endings.
- Capabilities and discovery files are consumed after process exit.
- Execution reports may be tailed while the process is alive.
- Application stdout and stderr remain outside protocol artifacts.

## Capabilities

Invocation:

```sh
... -- adapter capabilities --output <absolute-file> [-- <framework-args>...]
```

The file contains exactly one JSON object and ends with LF, not CRLF. JSON whitespace within the document and before
the terminal LF is allowed. A missing terminal LF is a conformance violation even when the JSON value is parseable.

```json
{
  "protocol": "foundry-test-adapter",
  "supported_versions": [1],
  "framework": {
    "id": "framework-id",
    "name": "Framework name",
    "version": "framework-version"
  },
  "extensions": []
}
```

All shown fields are required. Supported versions are a non-empty ascending array of unique positive integers and a
conforming v1 adapter includes `1`.
Framework values are non-empty strings. Extension names are unique non-empty opaque strings. Unknown additive fields
are ignored. V1 defines no required extension and no extension-selection handshake.

A client chooses the highest mutually supported version and passes it to discovery and execution. A missing, invalid,
or incompatible capabilities artifact means unsupported regardless of a legacy runner's exit code. A conforming
capabilities operation exits `0`; invalid invocation or adapter-detected write failure exits `2`.

The normative schema is Draft 2020-12 with `$id` `urn:foundry:test-adapter:v1:capabilities`.

## Discovery

Invocation:

```sh
... -- adapter discover --protocol-version 1 --output <absolute-file> [-- <framework-args>...]
```

Discovery is LF-terminated JSONL. Every non-empty line contains exactly one complete record and is flushed after its
LF. Blank lines, malformed UTF-8 or JSON, multiple values on one line, or a missing final LF are violations.

Every record requires `protocol: "foundry-test-adapter"`, `version: 1`, and one recognized event. The event records are:

- `discovery_start`, with the effective canonical `res://` discovery `root`;
- `suite`, with identity, hierarchy, location, runnable, and skip fields;
- `test`, with the suite fields plus nullable stable `case_key`;
- `discovery_error`, with stable identity, hierarchy, optional location, and non-empty `message`; and
- `discovery_end`, with suite, test, and error counts.

The complete v1 wire shapes are:

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_start","root":"res://tests"}
```

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

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_end","suite_count":1,"test_count":1,"error_count":1}
```

`suite` and `test` require every shown identity, location, runnable, and skip field. `test.case_key` is null for an
ordinary test or a non-empty framework-stable string for a parameterized case. `discovery_error` requires every shown
field; its `parent_id`, `path`, and `range` are nullable. `discovery_end` counts are non-negative integers. These exact
property names and JSON types are normative.

The normative per-record schema is Draft 2020-12 with `$id`
`urn:foundry:test-adapter:v1:discovery-record`. Cross-record rules belong to the validator.

### Identity, paths, and ranges

- IDs and labels are non-empty strings without Unicode control characters.
- IDs are opaque, compared exactly, and never parsed or normalized by consumers.
- IDs are unique across suites, tests, and discovery errors.
- `parent_id` is null or identifies a previously emitted suite; tests and errors cannot be parents.
- `root` and non-null `path` values are canonical `res://` paths using `/`, without backslashes, dot segments, or
  empty internal segments.
- `range` is null when `path` is null.
- A non-null range is zero-based, end-exclusive, and measured in UTF-16 code units. Start and end contain
  non-negative integer line and character values, and start does not follow end lexicographically.
- Static artifact validation checks range structure and ordering. Live framework tests prove correspondence to source
  text, including astral characters.
- Nullable fields are present as JSON `null`, not omitted.

### Runnable and skipped items

`runnable` means the exact item ID may be selected. A run without selections includes every runnable test leaf.
Selecting a runnable suite includes all runnable descendant test leaves. Selecting a non-runnable suite or test, an
unknown ID, or a standalone discovery-error ID is a protocol error and exits `2`.

Repeated selections and overlapping suite/test selections are deduplicated by test ID while retaining deterministic
discovery order. A non-runnable test is excluded from every plan and cannot be marked skipped.

`skipped: true` requires a non-empty `skip_reason`; otherwise `skip_reason` is null. A runnable skipped test produces
one TAP skip point. A skipped suite explicitly marks each runnable descendant test skipped; skip state is not inherited
implicitly.

Labels and non-null skip reasons are single-line strings without Unicode control characters so they are representable
on a TAP point line. A non-null `case_key` is non-empty; clients retain but do not parse it.

### Stream lifecycle

Exactly one `discovery_start` appears first. Parents precede descendants. Exactly one `discovery_end` appears last,
and its counts match all preceding records. Unknown events are violations.

A complete stream with zero discovery errors exits `0`. A complete stream containing recoverable discovery errors
exits `1` while preserving valid items. Unsupported version, invalid invocation, adapter-detected write failure, or
adapter-detected inability to complete the stream exits `2`. A stream without final `discovery_end` is infrastructure
failure, never valid partial discovery.

## Execution and Exact Selection

Invocation:

```sh
... -- adapter run --protocol-version 1 --report <absolute-file> \
  [--select <stable-id>]... [-- <framework-args>...]
```

The adapter determines the complete ordered runnable-leaf plan before emitting it. A run-all operation over no
runnable leaves uses `1..0`. An explicitly selected suite with no runnable descendants is a selection error and exits
`2`.

The report uses this strict TAP13 profile:

```tap
TAP version 13
# foundry-test-adapter: 1
1..N
<point 1 and YAML block>
...
<point N and YAML block>
```

The first three lines are exact once the plan is known. V1 permits no late plan, `TODO`, pragmas, nested TAP, or
unrelated top-level content. Point numbers are contiguous from `1` through `N`. Labels are non-empty single-line
display text, may collide, and never establish identity. Labels cannot end in text that parses as a TAP directive.

A skipped point is `ok N - label # SKIP reason` with a non-empty reason. Other successes use `ok`; failures use
`not ok`.

Every point is immediately followed by a YAML diagnostic block with exactly two-space TAP indentation and explicit
`---` and `...` markers:

```tap
not ok 1 - MathTests.adds_numbers
  ---
  message: "expected 4, got 5"
  at:
    fileName: "res://tests/math_tests.fs"
    lineNumber: 5
    columnNumber: 1
  _foundry:
    id: "opaque-stable-id"
    duration_ms: 2
    status_detail: ""
  ...
```

The validator removes the TAP indentation and parses the block with `yaml.safe_load`. Its top level and `_foundry`
value are mappings. Every point requires `_foundry.id`, a non-boolean non-negative integer `duration_ms`, and
`status_detail` from this closed set:

- `""`
- `discovery_error`
- `runtime_error`
- `timed_out`
- `aborted`
- `setup_error`

Passing and skipped points require the empty detail. Non-empty details require `not ok`. Every `not ok` point has a
non-empty standard `message`. `at` is optional because framework-generated tests may not have source locations. When
present it has a canonical `res://` `fileName` and positive one-based line and column integers. Unknown additive YAML
keys are ignored.

`_foundry.id` is a string obeying the same non-empty, control-free identity rule as discovery IDs. Artifact-only
report validation enforces that constraint even without discovery context.

Every planned runnable leaf produces exactly one point, including leaves represented as timed out or aborted. Report
IDs are unique. Complete points appear in planned discovery order.

`status_detail: discovery_error` is only a `not ok` point carrying the selected runnable leaf's ID when that planned
leaf cannot be rediscovered or loaded. It never carries a standalone discovery-error record ID. Once the complete
leaf plan is known, failure to reload one leaf produces its point and leads to process exit `1`. Discovery errors
unrelated to an exact selected set do not become execution points.

If the adapter cannot establish the complete plan, it does not invent a count. If output began, it emits
`Bail out! <message>` and exits `2`. After a plan exists, an adapter-detected catastrophe appends one bailout, emits
nothing afterward, and exits `2`. A bailout is legal only before all planned points have been emitted. A bailout after
the plan is already satisfied is invalid trailing content, not a conforming infrastructure lifecycle.

The adapter assembles each complete point and YAML block before writing it, writes through the terminating `...\n`,
and flushes. A complete report exits `0` when all points pass or skip and `1` when any point is `not ok`. Unknown
selection, unsupported version, malformed invocation, or adapter-detected write failure exits `2` and never produces
a falsely complete report.

## Cancellation and Uncaught Failures

V1 cancellation is external termination of the owned adapter child process. No cooperative message, terminal record,
or portable child exit code is required. Only complete flushed points are guaranteed.

Under `--cancelled`, the validator first finds the longest completed protocol prefix. It ignores bytes after the final
LF and discards a trailing point/YAML candidate that lacks its terminating `...\n`; those bytes were not a guaranteed
flushed point. It then accepts any of these valid prefixes:

- missing or empty output;
- only `TAP version 13\n`;
- the version line plus the exact adapter comment; or
- the complete version/comment/unsatisfied-plan preamble followed by zero or more complete point/YAML blocks.

Every completed unit in the retained prefix is validated normally. A completed malformed preamble or point remains a
violation; cancellation does not hide it. Bailout, a satisfied plan, or a supplied child exit is invalid cancellation.
An unsatisfied or not-yet-emitted plan is incomplete and never success.

An uncaught engine script error may override the runner return and exit `1`. A missing, malformed, bailed-out, or
incomplete artifact distinguishes infrastructure failure from represented discovery or test failures.

## Validator Interface

Stable operations are:

```sh
foundry-test-adapter capabilities <file> [--exit-code <n>] [--format text|json]
foundry-test-adapter discovery <file> [--exit-code <n>] [--format text|json]
foundry-test-adapter report <file> [--discovery <file>] [--select <id>]... \
  [--exit-code <n> | --cancelled] [--format text|json]
foundry-test-adapter fixtures <manifest>
```

Omitting process context performs artifact-only validation. Supplying it also validates artifact/process lifecycle
consistency. `--cancelled` validates a cancellation prefix without inventing an exit code. Report selections require
discovery context.

With discovery context, the report validator reconstructs the runnable-leaf plan and verifies deduplication, IDs,
order, skip state, and selection. Without it, the validator checks only self-contained TAP structure, metadata,
completeness, and optional exit consistency.

When discovery context is present, a skipped point's TAP reason equals the discovery `skip_reason` exactly, without
Unicode normalization. A non-skipped discovered leaf cannot produce a skip directive.

Validator exit codes are:

- `0`: the artifact conforms to the supplied lifecycle, including represented failures, a well-formed bailout, or a
  valid incomplete cancellation report;
- `1`: one or more conformance violations; and
- `2`: invalid validator invocation or an I/O failure that prevents examining a requested path. A nonexistent path is
  an observable missing artifact, not a validator I/O failure; it produces a validation result and validator exit
  `1`.

Under `--format json`, an I/O failure after argument parsing emits one result with `artifact.read`, `valid: false`,
`complete: false` for a primary artifact (or primary-derived completeness for context I/O), and classification
`invalid`, then exits `2`. Invalid CLI syntax emits usage to stderr, no JSON document, and exit `2` because no artifact
operation was established. Text mode reports the same conditions to stderr.

A missing discovery context reports `artifact.missing`, suppresses cross-artifact selection checks, retains the
report's self-contained completeness, classifies the result `invalid`, and exits `1`. A BOM or invalid UTF-8 context
reports `artifact.encoding`, suppresses selection checks, continues self-contained report validation, classifies
`invalid`, and exits `1`. A readable UTF-8 but otherwise nonconforming or incomplete discovery context reports only
`report.selection` in the report result; detailed discovery violations are obtained by running the discovery
operation. Another unreadable context reports `artifact.read`, continues self-contained report validation, classifies
`invalid`, and exits `2`.

### JSON result

`--format json` writes one document to stdout:

```json
{
  "validator": "foundry-test-adapter",
  "validator_version": "1.0.0",
  "protocol_version": 1,
  "artifact": "discovery",
  "valid": false,
  "complete": false,
  "classification": "invalid",
  "violations": [
    {
      "code": "discovery.duplicate_id",
      "message": "ID 'suite-a' was already emitted on line 2",
      "file": "/tmp/discovery.jsonl",
      "line": 4,
      "column": null,
      "path": "$.id"
    }
  ]
}
```

Classification is one of `conforming`, `discovery_failures`, `test_failures`, `infrastructure_failure`, `cancelled`,
`unsupported`, or `invalid`. `valid` describes conformance to supplied lifecycle context. `complete` describes
protocol completeness independently.

### Lifecycle result table

The result fields are determined by this table. An omitted process exit accepts the artifact-only row; supplying an
exit requires the shown value.

| Operation | Observable state | Required exit | `valid` | `complete` | Classification |
| --- | --- | ---: | --- | --- | --- |
| Capabilities | Complete JSON, schema/semantics valid, includes v1 | `0` | true | true | `conforming` |
| Capabilities | Complete JSON but schema/semantics invalid or lacks v1 | any | false | true | `unsupported` |
| Capabilities | Parseable JSON without terminal LF | any | false | false | `unsupported` |
| Capabilities | Missing, empty, truncated, or malformed JSON | any | false | false | `unsupported` |
| Capabilities | Valid artifact with a supplied nonzero exit | nonzero | false | true | `invalid` |
| Discovery | Valid final end, zero errors | `0` | true | true | `conforming` |
| Discovery | Valid final end, one or more represented errors | `1` | true | true | `discovery_failures` |
| Discovery | Final end exists but any record/semantic rule is invalid | any | false | true | `invalid` |
| Discovery | Missing final end or truncated record | any | false | false | `infrastructure_failure` |
| Discovery | Otherwise-valid artifact with the wrong supplied exit | wrong | false | true | `invalid` |
| Report | Satisfied plan, all `ok`/skip points | `0` | true | true | `conforming` |
| Report | Satisfied plan with one or more `not ok` points | `1` | true | true | `test_failures` |
| Report | Bailout before plan satisfaction, no trailing content | `2` | true | false | `infrastructure_failure` |
| Report | Cancelled with a valid completed protocol prefix | none | true | false | `cancelled` |
| Report | Unsatisfied plan without conforming bailout/cancellation | any | false | false | `infrastructure_failure` |
| Report | Satisfied plan followed by bailout or other trailing content | any | false | true | `invalid` |
| Report | Structurally/semantically invalid; plan satisfaction still known | any | false | derived | `invalid` |
| Report | Otherwise-valid artifact with the wrong supplied exit | wrong | false | derived | `invalid` |

A missing primary artifact has `valid: false`, `complete: false`, and one `artifact.missing` violation, except that a
report validated with `--cancelled` treats missing or empty output as a valid incomplete cancellation.
Capabilities classify it as `unsupported`; discovery and report classify it as `infrastructure_failure`. A primary
I/O failure uses the same validity/completeness but classification `invalid`, code `artifact.read`, and validator exit
`2`.

A primary BOM or invalid UTF-8 artifact has `valid: false`, `complete: false`, one `artifact.encoding` violation, and
validator exit `1`. Capabilities classify it as `unsupported`; discovery and report classify it as
`infrastructure_failure` because their lifecycle cannot be recovered.

For the two `derived` entries, `complete` is true only when a valid leading plan is satisfied by complete point/YAML
blocks; otherwise it is false. Structural invalidity takes classification precedence over infrastructure failure.
`--cancelled` is invalid when the retained prefix contains a completed malformed unit, the plan is satisfied, a
bailout exists, or a child exit is also supplied.

Diagnostic codes are stable; messages may improve. Diagnostics are ordered by artifact location and then code. Text
mode writes human diagnostics to stderr. V1 uses this closed code registry:

- Common: `artifact.missing`, `artifact.encoding`, `artifact.read`.
- Capabilities: `capabilities.line_ending`, `capabilities.json`, `capabilities.schema`,
  `capabilities.version_order`, `capabilities.exit`.
- Discovery: `discovery.line_ending`, `discovery.json`, `discovery.schema`, `discovery.start`, `discovery.end`,
  `discovery.ordering`, `discovery.duplicate_id`, `discovery.parent`, `discovery.path`, `discovery.range`,
  `discovery.skip`, `discovery.counts`, `discovery.incomplete`, and `discovery.exit`.
- Report: `report.line_ending`, `report.header`, `report.adapter_version`, `report.plan`, `report.point`,
  `report.directive`, `report.yaml`, `report.metadata`, `report.duplicate_id`, `report.selection`, `report.order`,
  `report.skip`, `report.status`, `report.location`, `report.bailout`, `report.incomplete`, `report.cancellation`, and
  `report.exit`.

One code may occur multiple times at different locations. Fixture manifests compare the set of codes while unit tests
also verify multiplicity and locations where relevant.

Cascade suppression is deterministic:

1. `artifact.missing`, `artifact.read`, or `artifact.encoding` stops validation of that artifact. A failure in optional
   discovery context stops only context/selection checks; self-contained report checks continue.
2. Capabilities JSON failure suppresses schema, version-order, and exit checks. A line-ending violation does not.
3. A malformed discovery line reports `discovery.json`; a schema-invalid parsed line reports `discovery.schema`.
   Semantic checks for that record are skipped. Global ordering/end-presence checks continue, but parent/count checks
   that depend on a rejected record are suppressed.
4. A report header or leading-plan failure stops point parsing. YAML parse failure reports `report.yaml`, suppresses
   metadata/status/location checks for that point, and resumes only after an explicit closing `...` marker.
5. Selection checks run only with a valid complete discovery model and successfully parsed plan/points.
6. Exit consistency runs only when the artifact lifecycle classification is otherwise determinable.

All other checks are independent and are reported together.

The exact condition-to-code allocation is:

- A missing path emits only `artifact.missing`; the sole exception is a missing/empty report under `--cancelled`,
  which is valid cancellation and emits no violation. A decoding failure emits only `artifact.encoding`; another I/O
  failure emits only `artifact.read` for that artifact.
- Capabilities without terminal LF or containing CRLF emit `capabilities.line_ending` and are incomplete. JSON
  syntax/type failure emits
  `capabilities.json`; schema failure emits `capabilities.schema`; a schema-valid version array that is unsorted emits
  `capabilities.version_order`; a fully valid artifact with the wrong supplied exit emits `capabilities.exit`.
- Discovery without terminal LF or containing CRLF emits `discovery.line_ending` and `discovery.incomplete`. A blank
  or malformed line
  emits `discovery.json`; a
  parsed record that fails its event schema emits `discovery.schema`. Missing/duplicate start or end records emit
  `discovery.start` or `discovery.end`. A unique start/end in the wrong position, or content after end, emits
  `discovery.ordering`. Missing final end additionally emits `discovery.incomplete`. Identity, parent, path, range,
  skip, and count conditions emit only their same-named registry code. Count checks run only when all counted item
  records are accepted. A fully valid stream with the wrong supplied exit emits `discovery.exit`.
- A report without terminal LF or containing CRLF emits `report.line_ending` and `report.incomplete`. A bad first TAP
  line emits
  `report.header` and stops. A bad adapter comment emits `report.adapter_version` and
  stops. A missing/invalid/non-leading plan emits `report.plan`; when no conforming bailout or cancellation explains
  the missing plan, it also emits `report.incomplete`. An otherwise valid plan with too few points emits only
  `report.incomplete`. Bad point syntax/number/count emits `report.point`; bad directives emit `report.directive`;
  malformed YAML emits `report.yaml`; missing/invalid `_foundry` fields emit `report.metadata`. Duplicate IDs,
  cross-artifact selection, discovery order, skip-reason/state, status/detail, and source location conditions emit
  only their same-named registry code.
- Illegal bailout position or trailing content emits `report.bailout`. Cancellation with a satisfied plan, bailout,
  supplied child exit, or completed malformed protocol unit emits `report.cancellation` plus that unit's structural
  code. An incomplete trailing byte/point/YAML suffix is discarded and emits no violation. A fully valid lifecycle
  with the wrong supplied exit emits `report.exit`.

## Normative Schemas and Fixtures

The normative authorities are:

```text
tools/foundry-test-adapter/protocol/v1/README.md
tools/foundry-test-adapter/protocol/v1/*.schema.json
tools/foundry-test-adapter/protocol/v1/fixtures/
```

The README defines cross-record and TAP semantics that JSON Schema cannot express. Schemas permit unknown additive
properties and are self-validated as Draft 2020-12. Fixtures are normative peers. Any contradiction is an
implementation defect that must be resolved before merge.

The fixture manifest records a stable fixture ID, operation, artifact paths, optional exit/cancellation context,
optional discovery/selections, expected validator exit, validity, completeness, classification, and exact diagnostic
code set.

The minimum matrix covers:

- capabilities: minimal, additive, and multi-version valid documents; malformed JSON; wrong protocol; missing fields;
  empty, duplicate, unsorted, or invalid versions; bad framework metadata; and duplicate extensions;
- discovery: empty and nested streams, parameterized cases, colliding labels, astral-character ranges, recoverable
  errors, additive fields, malformed lines, wrong protocol/version, bad start/end ordering, trailing records, unknown
  events, duplicate IDs, bad parents, invalid paths/ranges/skip state, count mismatch, and truncation; and
- reports: empty, pass, fail, skip, every non-empty status, colliding labels, exact/suite/overlap selection, bailout
  before and after a plan, cancellation, malformed headers/plans/points/directives/YAML, bad metadata, duplicate or
  unexpected IDs, wrong order/count, standalone discovery-error identity, partial cancellation blocks, trailing
  content after bailout, and exit/result mismatch.

Tests execute schemas, parsers, semantic validation, selection reconstruction, the public CLI, and every manifest
entry. They assert parsed behavior and diagnostic codes, never implementation source or documentation prose.

## Engine Transport Regression

Add a real fixture project:

```text
tests/fixtures/foundry_test_adapter_transport/
├── project.foundry
└── adapter_transport_runner.fs
```

`tests/core/os/test_foundry_cli_project_test.h` stages it beneath a unique `FOUNDRY_TEST_SCRATCH` directory and runs
the actual Foundry executable. The synthetic runner implements only enough adapter behavior to prove transport.

Required cases prove:

1. arguments after both separators, including reserved-looking framework names, arrive unchanged and ordered;
2. valid operations create or truncate caller files under scratch;
3. deliberate stdout and stderr do not enter protocol artifacts;
4. a first point is visible while the child waits on a continuation-file handshake, then the second point completes;
5. runner returns `0`, `1`, and `2` propagate;
6. an uncaught script failure exits `1` and leaves an incomplete artifact; and
7. terminating the delayed child preserves only complete points against an unsatisfied plan.

The subprocess helper uses bounded condition polling, captures stdout/stderr and current artifact content on timeout,
terminates when required, and always reaps its child. Tests never use fixed sleeps for synchronization.

No engine change is expected. If these tests expose a transport defect, implementation may add only the smallest
compatibility-preserving engine fix needed for the stated guarantees.

## Versioning and Extensions

Once merged, v1 field meanings, events, status values, selection semantics, exit rules, and TAP profile are stable.
Compatible optional metadata may be added. Removing or renaming fields/events, tightening previously valid values,
changing selection/exit/completeness, or changing required TAP structure requires a new version directory and
advertised protocol version.

Unknown extension names and metadata are ignored. An optional extension cannot change v1 core validity or meaning.
Anything stronger requires a new protocol version.

## Compatibility

- Ordinary runner modes remain valid because adapter mode is opt-in at runner argument zero.
- The engine keeps the supported command-first CLI and existing argument pass-through boundary.
- `ScriptRunner.run(args) -> int` remains unchanged.
- Legacy runners remain unsupported unless they produce valid capabilities.
- FoundryLib remains a reference implementation rather than a dependency.
- All assertion and test-lifecycle policy remains framework-owned.

## Verification Gates

```sh
uv lock --check --project tools/foundry-test-adapter

uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest discover -s tools/foundry-test-adapter/tests

uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter fixtures \
  tools/foundry-test-adapter/protocol/v1/fixtures/manifest.json

pre-commit run foundry-test-adapter --all-files

scons platform=macos target=editor dev_mode=yes tests=yes

./bin/foundry.* --headless test run \
  --case "*FoundryCLI*Adapter*" --force-colors
```

Linux uses `python3 scripts/agent_build.py` and the same focused case. Final implementation validation includes a
CI-style build and full headless suite. These tests do not require an editor GUI display.

## Downstream Coordination

FoundryLib#11 and Foundry-Scripting testing issues cite the checked-in v1 directory as authority. Their command
examples adopt the second runner-level separator for framework arguments. FoundryLib runs the validator against live
outputs. The extension exercises its consumer against the same normative fixtures.
