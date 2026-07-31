# Foundry Test Adapter Protocol, version 1

This document, the schemas beside it, and the fixtures under `fixtures/` are the normative
definition of protocol version 1. They are peers: any contradiction between prose, schema, and
fixture is a defect that must be resolved before merge.

The protocol is a framework-neutral runner-side contract carried through the existing
`project test --runner ... -- <runner args>` boundary. Foundry owns the wire protocol, the
artifacts, the conformance validator, and the observable engine transport guarantees. A conforming
framework owns discovery, stable test identity, selection, execution, assertions, and reporting
policy.

Capabilities are JSON, discovery is JSONL, and execution uses a strict streaming TAP13 profile.
Every artifact is written to a caller-provided absolute file path so application stdout and stderr
cannot corrupt it.

## Command grammar

```sh
foundry --headless --no-header project test \
  --project <project> \
  --runner <runner> -- \
  adapter <capabilities|discover|run> <reserved-options> [-- <framework-args>...]
```

`adapter` must be runner argument zero. The first `--` belongs to the engine and begins runner
arguments. The optional second `--` belongs to the adapter and ends reserved adapter parsing.

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
`--protocol-version` are required exactly once where shown. `--select` is the only repeatable
reserved option. Unknown options, prohibited duplicates, missing values, extra positionals,
malformed versions, unsupported versions, and selection errors exit `2`.

Every reserved option consumes its required next token unconditionally, even when that token is `--`
or begins with `-`. Consequently `--select --` selects the opaque ID `--`; it does not begin
framework arguments. The second separator is recognized only while the parser is expecting a new
reserved option. An implementation may not delegate this distinction to a parser that automatically
treats option-looking values as options or separators.

A protocol version is an ASCII positive decimal integer. The second separator may be omitted when
there are no framework arguments. After it, every argument is opaque framework input, including
arguments named `--output`, `--report`, `--protocol-version`, or `--select`. The adapter must
preserve those arguments exactly and must not reinterpret, reorder, or normalize them.

## Common artifact rules

- Artifact paths are absolute native filesystem paths whose parent directory already exists.
- A valid operation creates or truncates its artifact. An adapter-detected open or write failure
  exits `2`.
- Adapters call `FileAccess.flush()` after each completed unit. The binding returns no error, so v1
  cannot require detection of a delayed flush or storage failure. A missing, truncated, or invalid
  artifact remains infrastructure failure regardless of the process exit.
- Clients provide unique paths and never trust a pre-existing artifact.
- Invalid invocation, version, or selection is resolved before writing a success-shaped artifact.
- Files are UTF-8 without a byte-order mark and use LF line endings.
- Capabilities and discovery files are consumed after process exit; execution reports may be tailed
  while the process is alive.
- Application stdout and stderr remain outside protocol artifacts.

## Capabilities

The file contains exactly one JSON object and ends with LF, not CRLF. JSON whitespace within the
document and before the terminal LF is allowed. A missing terminal LF is a conformance violation even
when the JSON value is parseable. See `fixtures/valid/capabilities/minimal.json`.

All fields shown in `capabilities.schema.json` are required. `supported_versions` is a non-empty
ascending array of unique positive integers, and a conforming v1 adapter includes `1`. Framework
values are non-empty strings. Extension names are unique non-empty opaque strings. Unknown additive
fields are ignored. V1 defines no required extension and no extension-selection handshake.

A client chooses the highest mutually supported version and passes it to discovery and execution. A
missing, invalid, or incompatible capabilities artifact means unsupported regardless of a legacy
runner's exit code. A conforming capabilities operation exits `0`; an invalid invocation or an
adapter-detected write failure exits `2`.

The normative schema is `capabilities.schema.json`, Draft 2020-12, `$id`
`urn:foundry:test-adapter:v1:capabilities`.

## Discovery

Discovery is LF-terminated JSONL. Every non-empty line contains exactly one complete record and is
flushed after its LF. Blank lines, malformed UTF-8 or JSON, multiple values on one line, and a
missing final LF are violations. A record boundary is the LF character alone; U+2028 and U+2029
inside a JSON string are ordinary characters.

Every record requires `protocol: "foundry-test-adapter"`, `version: 1`, and one of the recognized
events: `discovery_start`, `suite`, `test`, `discovery_error`, and `discovery_end`. The complete wire
shapes and JSON types are normative and defined by `discovery-record.schema.json`, Draft 2020-12,
`$id` `urn:foundry:test-adapter:v1:discovery-record`. See `fixtures/valid/discovery/nested.jsonl`.

Cross-record rules belong to the validator:

- IDs and labels are non-empty strings without Unicode control characters.
- IDs are opaque, compared exactly, and never parsed or normalized by consumers.
- IDs are unique across suites, tests, and discovery errors.
- `parent_id` is null or identifies a previously emitted suite; tests and errors cannot be parents.
- `root` and non-null `path` values are canonical `res://` paths using `/`, without backslashes, dot
  segments, or empty internal segments.
- `range` is null when `path` is null.
- A non-null range is zero-based, end-exclusive, and measured in UTF-16 code units. Start and end
  contain non-negative integer line and character values, and start does not follow end
  lexicographically. See `fixtures/valid/discovery/astral-range.jsonl`.
- Nullable fields are present as JSON `null`, not omitted.
- Exactly one `discovery_start` appears first, parents precede descendants, and exactly one
  `discovery_end` appears last with counts matching all preceding records.

`runnable` means the exact item ID may be selected. A run without selections includes every runnable
test leaf. Selecting a runnable suite includes all runnable descendant test leaves. Selecting a
non-runnable suite or test, an unknown ID, or a standalone discovery-error ID is a protocol error and
exits `2`. Repeated and overlapping selections deduplicate by test ID while the plan retains
deterministic discovery order. An explicitly selected suite with no runnable descendants is a
selection error.

`skipped: true` requires a non-empty `skip_reason`; otherwise `skip_reason` is null. A runnable
skipped test produces one TAP skip point. A skipped suite explicitly marks each runnable descendant
test skipped; skip state is never inherited implicitly. A non-runnable test is excluded from every
plan and cannot be marked skipped. Labels and non-null skip reasons are single-line strings without
Unicode control characters so they are representable on a TAP point line. A non-null `case_key` is
non-empty; clients retain but do not parse it.

A complete stream with zero discovery errors exits `0`. A complete stream containing recoverable
discovery errors exits `1` while preserving valid items. An unsupported version, invalid invocation,
adapter-detected write failure, or adapter-detected inability to complete the stream exits `2`. A
stream without a final `discovery_end` is infrastructure failure, never valid partial discovery.

## Execution reports

The adapter determines the complete ordered runnable-leaf plan before emitting it. A run-all
operation over no runnable leaves uses `1..0`.

```tap
TAP version 13
# foundry-test-adapter: 1
1..N
<point 1 and YAML block>
...
<point N and YAML block>
```

The first three lines are exact once the plan is known. V1 permits no late plan, `TODO`, pragmas,
nested TAP, or unrelated top-level content. Point numbers are contiguous from `1` through `N`. Labels
are non-empty single-line display text, may collide, never establish identity, and cannot end in text
that parses as a TAP directive. A skipped point is `ok N - label # SKIP reason` with a non-empty
reason; other successes use `ok` and failures use `not ok`.

Every point is immediately followed by a YAML diagnostic block with exactly two-space TAP indentation
and explicit `---` and `...` markers. The validator removes the TAP indentation and parses the block
with `yaml.safe_load`; its top level and `_foundry` value are mappings. See
`fixtures/valid/report/failure.tap`.

Every point requires `_foundry.id`, a non-boolean non-negative integer `_foundry.duration_ms`, and
`_foundry.status_detail` from this closed set: `""`, `discovery_error`, `runtime_error`, `timed_out`,
`aborted`, and `setup_error`. Passing and skipped points require the empty detail, and a non-empty
detail requires `not ok`. Every `not ok` point has a non-empty standard `message`. `at` is optional
because framework-generated tests may not have source locations; when present it has a canonical
`res://` `fileName` and positive one-based line and column integers. Unknown additive YAML keys are
ignored. `_foundry.id` obeys the same non-empty, control-free identity rule as discovery IDs, and
artifact-only validation enforces it even without discovery context.

Every planned runnable leaf produces exactly one point, including leaves represented as timed out or
aborted. Report IDs are unique and complete points appear in planned discovery order.

`status_detail: discovery_error` is only a `not ok` point carrying the selected runnable leaf's ID
when that planned leaf cannot be rediscovered or loaded. It never carries a standalone
discovery-error record ID. Once the complete leaf plan is known, failure to reload one leaf produces
its point and leads to process exit `1`. Discovery errors unrelated to the exact selected set never
become execution points.

If the adapter cannot establish the complete plan it does not invent a count: if output began it
emits `Bail out! <message>` and exits `2`. After a plan exists, an adapter-detected catastrophe
appends one bailout, emits nothing afterward, and exits `2`. A bailout is legal only before all
planned points have been emitted; a bailout after the plan is already satisfied is invalid trailing
content.

The adapter assembles each complete point and YAML block before writing it, writes through the
terminating `...\n`, and flushes. A complete report exits `0` when all points pass or skip and `1`
when any point is `not ok`. An unknown selection, unsupported version, malformed invocation, or
adapter-detected write failure exits `2` and never produces a falsely complete report.

## Cancellation and uncaught failures

V1 cancellation is external termination of the owned adapter child process. No cooperative message,
terminal record, or portable child exit code is required. Only complete flushed points are
guaranteed.

A cancellation validator first finds the longest completed protocol prefix. It ignores bytes after
the final LF and discards a trailing point/YAML candidate that lacks its terminating `...\n`; those
bytes were not a guaranteed flushed point. It then accepts any of these prefixes:

- missing or empty output;
- only `TAP version 13\n`;
- the version line plus the exact adapter comment; or
- the complete version/comment/unsatisfied-plan preamble followed by zero or more complete
  point/YAML blocks.

Every completed unit in the retained prefix is validated normally; a completed malformed preamble or
point remains a violation. A bailout, a satisfied plan, or a supplied child exit is invalid
cancellation. An unsatisfied or not-yet-emitted plan is incomplete and never success.

An uncaught engine script error may override the runner return and exit `1`. A missing, malformed,
bailed-out, or incomplete artifact distinguishes infrastructure failure from represented discovery or
test failures.

## Diagnostics and lifecycle results

Diagnostic codes are stable; messages may improve. Diagnostics are ordered by artifact location and
then by code. V1 uses this closed registry:

- Common: `artifact.missing`, `artifact.encoding`, `artifact.read`.
- Capabilities: `capabilities.line_ending`, `capabilities.json`, `capabilities.schema`,
  `capabilities.version_order`, `capabilities.exit`.
- Discovery: `discovery.line_ending`, `discovery.json`, `discovery.schema`, `discovery.start`,
  `discovery.end`, `discovery.ordering`, `discovery.duplicate_id`, `discovery.parent`,
  `discovery.path`, `discovery.range`, `discovery.skip`, `discovery.counts`, `discovery.incomplete`,
  `discovery.exit`.
- Report: `report.line_ending`, `report.header`, `report.adapter_version`, `report.plan`,
  `report.point`, `report.directive`, `report.yaml`, `report.metadata`, `report.duplicate_id`,
  `report.selection`, `report.order`, `report.skip`, `report.status`, `report.location`,
  `report.bailout`, `report.incomplete`, `report.cancellation`, `report.exit`.

`capabilities.version_order` covers `supported_versions` semantics that JSON Schema cannot express:
both a non-ascending array and an array that omits protocol version `1`.

Cascade suppression is deterministic:

1. `artifact.missing`, `artifact.read`, or `artifact.encoding` stops validation of that artifact. A
   failure in optional discovery context stops only context and selection checks; self-contained
   report checks continue.
2. Capabilities JSON failure suppresses schema, version-order, and exit checks. A line-ending
   violation does not.
3. A malformed discovery line reports `discovery.json`; a schema-invalid parsed line reports
   `discovery.schema`. Semantic checks for that record are skipped. Global ordering and
   end-presence checks continue, but parent and count checks that depend on a rejected record are
   suppressed.
4. A report header or leading-plan failure stops point parsing. YAML parse failure reports
   `report.yaml`, suppresses metadata, status, and location checks for that point, and resumes only
   after an explicit closing `...` marker.
5. Selection checks run only with a valid complete discovery model and successfully parsed plan and
   points. An interrupted report is required only to be a prefix of the planned leaves.
6. Exit consistency runs only when the artifact lifecycle classification is otherwise determinable.

All other checks are independent and are reported together.

A result is described by three fields. `valid` describes conformance to the supplied lifecycle
context, `complete` describes protocol completeness independently, and `classification` is one of
`conforming`, `discovery_failures`, `test_failures`, `infrastructure_failure`, `cancelled`,
`unsupported`, and `invalid`. An omitted process exit accepts the artifact-only row; supplying an
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

A missing primary artifact has `valid: false`, `complete: false`, and one `artifact.missing`
violation, except that a report validated as cancelled treats missing or empty output as a valid
incomplete cancellation. Capabilities classify a missing artifact as `unsupported`; discovery and
report classify it as `infrastructure_failure`. A primary I/O failure uses the same validity and
completeness but classification `invalid`, code `artifact.read`, and validator exit `2`. A primary
BOM or invalid UTF-8 artifact has one `artifact.encoding` violation and validator exit `1`.

For the two `derived` entries, `complete` is true only when a valid leading plan is satisfied by
complete point/YAML blocks. Structural invalidity takes classification precedence over infrastructure
failure.

## Fixture manifest

`fixtures/manifest.json` lists every normative fixture. Each entry records a stable fixture ID, the
operation, the artifact path relative to the manifest, optional exit and cancellation context,
optional discovery context and selections, and the expected validator exit, validity, completeness,
classification, and exact diagnostic code set:

```json
{
  "id": "discovery.invalid.duplicate-id",
  "operation": "discovery",
  "artifact": "invalid/discovery/duplicate-id.jsonl",
  "exit_code": null,
  "cancelled": false,
  "discovery": null,
  "selections": [],
  "expected": {
    "validator_exit": 1,
    "valid": false,
    "complete": true,
    "classification": "invalid",
    "codes": ["discovery.duplicate_id"]
  }
}
```

Manifest entries compare the set of codes; unit tests additionally verify multiplicity and locations
where relevant. `foundry-test-adapter fixtures <manifest>` resolves paths relative to the manifest,
runs every entry, reports each mismatch, and exits non-zero when any expected result differs.

## Versioning and extensions

Once merged, v1 field meanings, events, status values, selection semantics, exit rules, and TAP
profile are stable. Compatible optional metadata may be added. Removing or renaming fields or events,
tightening previously valid values, changing selection, exit, or completeness semantics, or changing
required TAP structure requires a new version directory and an advertised protocol version.

Unknown extension names and metadata are ignored. An optional extension cannot change v1 core
validity or meaning; anything stronger requires a new protocol version.
