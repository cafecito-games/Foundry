# Foundry Test Adapter Protocol v1

Status: normative. This document defines version 1 of the Foundry Test Adapter Protocol. The JSON
Schemas in `foundry_test_adapter/schemas/`, the fixtures in `foundry_test_adapter/fixtures/v1/`, and
the conformance validator in `foundry_test_adapter/` are part of the same normative surface: where
prose and the validator disagree, that disagreement is a defect in one of them and must be resolved,
not worked around.

The key words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are used in their usual specification
sense.

## 1. Scope and ownership

Foundry owns the protocol, its transport guarantees, and this conformance tooling. Test frameworks
own discovery, test identity, selection semantics, and result semantics. The protocol is
framework-neutral: nothing in it names or privileges a particular framework.

Foundry guarantees exactly two things about the transport:

1. every argument after the engine's `--` separator reaches the runner unchanged and in order; and
2. the runner's return value propagates to the process exit code, unless an uncaught script failure
   overrides it (section 7).

Foundry does not define assertions, mocks, fixtures, snapshots, or a per-test result model, and the
engine never parses adapter artifacts. `ScriptRunner.run(args) -> int` is unchanged by this protocol.

## 2. Invocation

```sh
foundry --headless --no-header project test \
  --project <project> \
  --runner <runner> -- \
  adapter <capabilities|discover|run> [options]
```

`adapter` is a positional runner subcommand carried in the runner's own argument vector. It is not an
engine option, and the engine attaches no meaning to it. Project paths use `--project <dir>`.

A runner that does not implement the protocol MUST NOT be required to recognize these arguments or to
fail in any particular way. Clients detect support solely by the capabilities artifact (section 3).

Every operation writes its artifact to a caller-provided path. Callers MUST provide unique scratch
paths; runners MUST NOT write protocol artifacts into tracked fixture directories. Artifacts are
UTF-8 encoded and use LF line endings. JSON artifacts are strict RFC 8259 JSON: the non-standard
`NaN`, `Infinity`, and `-Infinity` literals are syntax errors, not ignorable values.

## 3. Capabilities

```sh
... -- adapter capabilities --output <file>
```

The runner writes one complete JSON document:

```json
{
  "protocol": "foundry-test-adapter",
  "supported_versions": [1],
  "framework": {
    "id": "example.framework",
    "name": "Example Framework",
    "version": "0.1.0"
  },
  "extensions": []
}
```

- `protocol` MUST be the string `foundry-test-adapter`.
- `supported_versions` MUST be a non-empty array of unique positive integers in strictly ascending
  order.
- `framework.id`, `framework.name`, and `framework.version` MUST be present and non-empty strings.
  They describe the implementation and are never fixed to a particular framework.
- `extensions` MUST be present and MUST be an array of unique non-empty strings. Version 1 defines
  no extensions and no required-extension mechanism. Clients MUST ignore unknown extension names.
- Unknown top-level fields MUST be ignored, so later versions can add metadata additively.

The client selects the highest version present in both `supported_versions` and its own supported
set, and passes it to every subsequent operation with `--protocol-version`. Version 1 mandates
discovery, exact selection, streaming TAP13, and report-file output; those are not optional features
and are never advertised as extensions.

If the capabilities file is missing, unreadable, or invalid, the runner is unsupported, regardless of
its process exit code. A conforming adapter that is asked for a version it does not support MUST exit
2 (section 7).

The document is complete when the process exits.

## 4. Discovery

```sh
... -- adapter discover --protocol-version 1 --output <file>
```

The runner writes newline-delimited JSON, flushing each record as it is produced. Empty lines carry
no meaning and MUST be ignored by readers. Every record MUST contain `protocol`
(`foundry-test-adapter`), `version` (`1`), and `event`.

Version 1 defines five events.

### 4.1 `discovery_start`

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_start","root":"res://tests"}
```

`root` is the effective `res://` discovery root and MUST be a non-empty string. `discovery_start`
MUST be the first record and MUST appear exactly once.

### 4.2 `suite` and `test`

```json
{
  "protocol": "foundry-test-adapter",
  "version": 1,
  "event": "test",
  "id": "res://tests/math_tests.fs::MathTests::adds_numbers",
  "label": "adds numbers",
  "parent_id": "res://tests/math_tests.fs::MathTests",
  "path": "res://tests/math_tests.fs",
  "range": {"start": {"line": 4, "character": 0}, "end": {"line": 6, "character": 1}},
  "runnable": true,
  "skipped": false,
  "skip_reason": null,
  "case_key": null
}
```

- `id` and `label` MUST be present and non-empty. `id` is opaque protocol identity, never a display
  string; `label` is the display string and carries no identity.
- `parent_id`, `path`, `range`, and `skip_reason` MUST be present, and are either a non-empty value
  or JSON `null`. A field that is absent is a violation; a field that does not apply is `null`.
- A non-null `parent_id` MUST identify a record emitted strictly earlier in the stream. A record
  MUST NOT name itself as its parent.
- `runnable` MUST be a boolean and says whether the item may be selected directly.
- `skipped` MUST be a boolean and says a selected test will report TAP `# SKIP`. A skipped item MUST
  carry a non-empty `skip_reason`.
- `test` records MUST additionally carry `case_key`: `null` for an ordinary test, or a
  framework-stable non-empty string identifying one case of a parameterized test.
- `suite` records MUST NOT be required to carry `case_key`, and readers ignore it if present.

Identifiers MUST be unique across every `suite`, `test`, and `discovery_error` record in a stream,
and MUST remain stable while the framework's logical test identity is unchanged. A record whose
`parent_id` is non-null MUST appear after the record that declares that identifier: parents precede
children. Ordering MUST be deterministic for an unchanged project.

### 4.3 Ranges and position encoding

A range is `{"start": {"line": L, "character": C}, "end": {...}}`. Lines and characters are
zero-based, the range is end-exclusive, `character` counts UTF-16 code units, and `end` MUST NOT
precede `start`. A character outside the Basic Multilingual Plane therefore advances `character` by
two. `foundry_test_adapter.utf16_length` and `foundry_test_adapter.utf16_offset_to_index` implement
this encoding for consumers.

### 4.4 `discovery_error`

```json
{
  "protocol": "foundry-test-adapter",
  "version": 1,
  "event": "discovery_error",
  "id": "res://tests/broken_tests.fs::error",
  "label": "BrokenTests discovery",
  "parent_id": null,
  "message": "Unable to index suite",
  "path": "res://tests/broken_tests.fs",
  "range": null
}
```

`id`, `label`, and `message` MUST be present and non-empty; `parent_id`, `path`, and `range` MUST be
present and nullable. `id` MUST be stable so a client can keep an error node in place across runs.

Recoverable discovery errors MAY be interleaved with successfully discovered items. The adapter still
writes `discovery_end` and exits 1 (section 7).

### 4.5 `discovery_end`

```json
{"protocol":"foundry-test-adapter","version":1,"event":"discovery_end","suite_count":1,"test_count":1,"error_count":1}
```

The three counts MUST be non-negative integers and MUST equal the number of `suite`, `test`, and
`discovery_error` records emitted before this record. `discovery_end` MUST be the final record and
MUST appear exactly once.

### 4.6 Conformance failures

Duplicate identifiers, an unknown or not-yet-emitted parent, an invalid range, an unknown event, a
malformed record, mismatched counts, a misplaced `discovery_start`/`discovery_end`, and a stream that
ends without `discovery_end` are all conformance failures. Unknown fields on a known event are
ignored.

A stream that ends without `discovery_end` is an infrastructure failure, never a valid partial
result. Records already flushed remain readable so a client can distinguish a crashed adapter from a
malformed one, but the discovery result as a whole MUST be treated as failed.

## 5. Execution

```sh
... -- adapter run --protocol-version 1 --report <file> [--select <stable-id>]...
```

- No `--select` runs every runnable leaf discovered by section 4.
- `--select` is repeatable and matches identifiers exactly; there is no pattern syntax.
- Selecting a suite selects its descendants.
- Repeated and overlapping selections are deduplicated: each leaf runs at most once.
- A leaf is a discovered item that no other item declares as its parent. Only runnable leaves run.
- An unknown selected identifier is a protocol error and MUST exit 2.
- Clients that need exclusions expand them into an explicit set of leaf identifiers; version 1 has no
  exclusion option.

The runner creates or truncates the report path before writing, and streams TAP13 into it, flushing
every complete test point as it finishes. Application stdout and stderr stay on the ordinary process
streams and MUST NOT be written into the report file.

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

- Physical line 1 MUST be `TAP version 13` and physical line 2 MUST be
  `# foundry-test-adapter: 1`. A blank line before or between them is a violation.
- The plan `1..N` MUST appear exactly once, before any test point, and N MUST equal the deduplicated
  number of selected runnable leaves. An empty selection therefore emits `1..0`. Because the plan is
  written before any point, bailing out later never excuses a plan that disagrees with the
  selection.
- Test points MUST be numbered from 1 without gaps, and MUST appear in the same deterministic order
  as discovery.
- A test point line is `ok <n> - <description>` or `not ok <n> - <description>`, optionally followed
  by ` # SKIP <reason>`. A description MUST NOT contain `#`, so the optional directive is
  unambiguous. `# SKIP` is the only directive version 1 defines; any other directive, including
  `# TODO`, is outside the profile.
- Further `#` comment lines carry no protocol meaning and are ignored.

### 5.1 Diagnostic blocks

Every test point MUST be followed by a YAML diagnostic block delimited by `  ---` and `  ...`, using
a restricted YAML subset: block mappings only, two-space indentation steps starting at column 3, and
scalar values that are double-quoted strings, integers, `true`, `false`, or `null`. Anything outside
that subset is a conformance failure, so consumers never need a full YAML implementation.

A block MUST be terminated by its closing `  ...` line. A report truncated part-way through a block
has an incomplete final point rather than a usable result, and that point MUST NOT be consumed.

The block MUST contain a `_foundry` mapping with:

- `id`: the non-empty stable identifier of the test that produced the point;
- `duration_ms`: a non-negative integer; and
- `status_detail`: exactly one of `""`, `discovery_error`, `runtime_error`, `timed_out`, `aborted`,
  or `setup_error`.

Identifiers MUST NOT repeat across points in one report.

A failing point MUST carry a `message` diagnostic. It MAY carry an `at` mapping using standard TAP
field names:

```yaml
at:
  fileName: "res://tests/math_tests.fs"
  lineNumber: 5
  columnNumber: 1
```

`at.lineNumber` and `at.columnNumber` are one-based and MUST be at least 1. This is deliberately a
different convention from the zero-based discovery ranges of section 4.3, because both follow the
conventions of the format they belong to.

A skipped test reports `ok <n> - <description> # SKIP <reason>` and still carries a `_foundry` block.
The reason MUST be non-empty, and a skip MUST use `ok`: `not ok ... # SKIP` is a conformance failure.
Skip state is discovery-owned, so a point's skip state and reason MUST match the discovered
`skipped` and `skip_reason` of the test it reports.

### 5.2 Bail out and completeness

An adapter that detects catastrophic infrastructure failure after starting a report MUST write
`Bail out! <message>` and MUST NOT write further TAP output, including a second `Bail out!` line. A process crash or forced kill cannot be
required to append any terminal marker.

A report is complete only when it declares a plan, is not bailed out, and emitted exactly as many
points as the plan declares. An unsatisfied plan is incomplete, never success. Points already flushed
remain valid and readable.

## 6. Artifact lifecycle

- The capabilities and discovery artifacts are complete when the process exits.
- The execution report is intentionally tail-able while the process is alive; a client MAY read it
  incrementally to drive live UI.
- Callers own the artifact paths and their cleanup.

## 7. Exit codes

- `0`: the operation succeeded and every selected test passed.
- `1`: discovery or test failures were represented normally in the artifact.
- `2`: a conforming adapter detected invalid invocation, an unsupported selected protocol version, a
  malformed or unknown selection, or runner infrastructure failure.

An uncaught engine-level script failure may override the runner's return value and exit 1. A client
therefore MUST NOT infer success from the exit code alone: a missing, invalid, bailed-out, or
incomplete artifact identifies infrastructure failure rather than ordinary test failure.

## 8. Cancellation

Version 1 cancellation is external child-process termination. The client requests normal termination
using the platform mechanism and MAY force termination after a grace period. Only complete points
already flushed are guaranteed. No terminal record and no portable exit code are required. The
client's cancellation state takes precedence over whatever the artifact says, and an unsatisfied plan
remains incomplete rather than success. Cooperative cancellation may be added in a later version.

## 9. Versioning and compatibility

- The protocol version is a single integer, advertised in `supported_versions` and echoed in every
  discovery record and in the report's second line.
- Additive, ignorable metadata (new optional fields, new comment lines) does not require a version
  bump. Any change to required fields, identifiers, ordering, position encoding, TAP structure, or
  exit semantics does.
- An adapter MAY support several versions concurrently; the client picks the highest shared one.
- Consumers MUST reject a selected version they do not implement rather than guessing.

## 10. Conformance tooling

`foundry_test_adapter` validates artifacts produced by any runner and reports every violation it
finds, addressed by a stable code and location. See `README.md` for the command line, the library
interface, and the packaging contract for downstream repositories.

The JSON Schemas describe individual documents and records. Stream-level and report-level rules —
ordering, identifier uniqueness, parents preceding children, counts matching emitted records, plan
satisfaction, and selection correlation — are not expressible in JSON Schema and are enforced only by
the validator, which is normative for them.
