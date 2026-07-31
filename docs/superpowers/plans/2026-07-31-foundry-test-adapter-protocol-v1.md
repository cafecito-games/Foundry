# Foundry Test Adapter Protocol V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the normative Foundry Test Adapter Protocol v1 bundle, reusable conformance validator, fixtures, and
real `project test` transport regressions specified by Foundry#1428.

**Architecture:** A relocatable `uv` project under `tools/foundry-test-adapter/` owns schemas, fixtures, parsing, and
validation without entering the engine runtime. JSON Schema handles individual capabilities/discovery shapes, a
focused Python parser handles the strict TAP13 profile, and semantic passes handle stream lifecycle and selection.
Existing engine behavior is exercised through a checked-in Foundry Script runner and real subprocess tests.

**Tech Stack:** Python 3.8, `uv`, `jsonschema==4.23.0`, `PyYAML==6.0.3`, `unittest`, JSON Schema Draft 2020-12,
Foundry Script, C++ doctest, SCons.

---

## File Map

Create this self-contained tool:

```text
tools/foundry-test-adapter/
├── pyproject.toml                    # uv project, pins, and console entry point
├── uv.lock                           # complete frozen dependency resolution
├── README.md                         # validator and pinned-checkout usage
├── src/foundry_test_adapter/
│   ├── __init__.py                   # public validator version
│   ├── cli.py                        # stable command-line interface
│   ├── diagnostics.py                # result/violation types and rendering
│   ├── json_artifacts.py             # capabilities and discovery validation
│   ├── selection.py                  # runnable-leaf plan reconstruction
│   └── tap13.py                      # focused TAP13 stream parser/validation
├── protocol/v1/
│   ├── README.md                     # normative protocol prose
│   ├── capabilities.schema.json      # complete capabilities document schema
│   ├── discovery-record.schema.json  # per-record event union schema
│   └── fixtures/                     # reusable valid/invalid corpus and manifest
└── tests/                            # unit, CLI, schema, fixture, and parser behavior
```

Create the real transport fixture:

```text
tests/fixtures/foundry_test_adapter_transport/project.foundry
tests/fixtures/foundry_test_adapter_transport/adapter_transport_runner.fs
```

Modify:

```text
tests/core/os/test_foundry_cli_project_test.h
.pre-commit-config.yaml
```

Do not modify the engine parser, `ScriptRunner`, or CLI help unless a failing transport regression proves a concrete
engine defect. If that occurs, stop and amend this plan with the minimum engine file and a dedicated red/green case.

### Task 1: Bootstrap the uv project and validate the schemas

**Files:**

- Create: `tools/foundry-test-adapter/pyproject.toml`
- Create: `tools/foundry-test-adapter/uv.lock`
- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/__init__.py`
- Create: `tools/foundry-test-adapter/protocol/v1/capabilities.schema.json`
- Create: `tools/foundry-test-adapter/protocol/v1/discovery-record.schema.json`
- Create: `tools/foundry-test-adapter/tests/test_schemas.py`

- [ ] **Step 1: Write schema self-validation and representative shape tests**

Create `test_schemas.py` with helpers that load both checked-in schemas, call
`Draft202012Validator.check_schema()`, and validate one minimal object of every record event. Use this exact test
surface:

```python
from __future__ import annotations

import json
import unittest
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "protocol" / "v1"


def load_json(name: str) -> dict[str, Any]:
    value = json.loads((PROTOCOL / name).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError(f"{name} must contain an object")
    return value


class SchemaTests(unittest.TestCase):
    def test_schemas_are_valid_draft_2020_12(self) -> None:
        for name in ("capabilities.schema.json", "discovery-record.schema.json"):
            Draft202012Validator.check_schema(load_json(name))

    def test_minimal_capabilities_validate(self) -> None:
        validator = Draft202012Validator(load_json("capabilities.schema.json"))
        document = {
            "protocol": "foundry-test-adapter",
            "supported_versions": [1],
            "framework": {"id": "fixture", "name": "Fixture", "version": "1.0.0"},
            "extensions": [],
        }
        self.assertEqual([], list(validator.iter_errors(document)))
```

Add one subtest each for `discovery_start`, `suite`, `test`, `discovery_error`, and `discovery_end`, using all required
nullable fields explicitly.

- [ ] **Step 2: Run the test and verify RED**

Run:

```sh
uv run --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_schemas.py -v
```

Expected: failure because the nested project and schemas do not exist.

- [ ] **Step 3: Create the exact project metadata**

Create `pyproject.toml`:

```toml
[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[project]
name = "foundry-test-adapter"
version = "1.0.0"
description = "Foundry Test Adapter Protocol conformance validator"
requires-python = ">=3.8"
dependencies = [
  "jsonschema==4.23.0",
  "PyYAML==6.0.3",
]

[project.scripts]
foundry-test-adapter = "foundry_test_adapter.cli:main"

[tool.hatch.build.targets.wheel]
packages = ["src/foundry_test_adapter"]
```

Create `__init__.py`:

```python
from __future__ import annotations

__version__ = "1.0.0"
```

Run `uv lock --python 3.8 --project tools/foundry-test-adapter` and commit the resulting lockfile.

- [ ] **Step 4: Implement the two complete schemas**

Use Draft 2020-12 and these exact IDs:

```json
"$id": "urn:foundry:test-adapter:v1:capabilities"
```

```json
"$id": "urn:foundry:test-adapter:v1:discovery-record"
```

The capabilities schema requires the protocol constant, unique positive versions, the three non-empty framework
strings, and unique non-empty extension strings. The discovery schema uses `$defs` for positions, ranges, common
identity/location fields, and one `oneOf` branch for every event. Every object permits unknown additive properties.
Keep ordering, sorted-version, parent, range-order, count, and lifecycle checks out of the schemas; later semantic
passes own them.

- [ ] **Step 5: Run schema tests and lock verification for GREEN**

Run:

```sh
uv lock --check --project tools/foundry-test-adapter
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_schemas.py -v
```

Expected: all schema tests pass.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Add test adapter protocol schemas"
```

### Task 2: Add deterministic diagnostics and capabilities validation

**Files:**

- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/diagnostics.py`
- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/json_artifacts.py`
- Create: `tools/foundry-test-adapter/tests/test_capabilities.py`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/valid/capabilities/`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/invalid/capabilities/`

- [ ] **Step 1: Write failing capabilities behavior tests**

Tests must exercise valid minimal/additive/multi-version documents and malformed JSON, wrong protocol, missing fields,
empty/duplicate/unsorted/invalid versions, empty framework values, and duplicate extensions. Assert stable codes such
as `capabilities.line_ending`, `capabilities.json`, `capabilities.schema`, `capabilities.version_order`, and
`capabilities.exit`. Include missing terminal LF, CRLF, valid LF, BOM, and invalid UTF-8 cases. Encoding cases expect
`valid: false`, `complete: false`, classification `unsupported`, validator exit `1`, and only `artifact.encoding`.

Use this internal unit-test surface; only the CLI and JSON result are stable public interfaces:

```python
result = validate_capabilities(path, process_exit=0)
self.assertTrue(result.valid)
self.assertTrue(result.complete)
self.assertEqual("conforming", result.classification)
self.assertEqual((), result.violations)
```

- [ ] **Step 2: Run the test and verify RED**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_capabilities.py -v
```

Expected: import failure for the missing validator modules.

- [ ] **Step 3: Implement the result model**

Define frozen dataclasses with these fields:

```python
@dataclass(frozen=True)
class Violation:
    code: str
    message: str
    file: str
    line: Optional[int] = None
    column: Optional[int] = None
    path: Optional[str] = None


@dataclass(frozen=True)
class ValidationResult:
    artifact: str
    valid: bool
    complete: bool
    classification: str
    violations: tuple[Violation, ...]
```

Provide deterministic sorting by `(file, line-or-zero, column-or-zero, path-or-empty, code)` and a JSON renderer that
adds validator name/version and protocol version.

- [ ] **Step 4: Implement capabilities validation**

Read bytes, reject BOM/invalid UTF-8, require terminal LF without CRLF, parse exactly one object, apply the checked-in
schema with
`Draft202012Validator.iter_errors()`, then apply ascending-version and optional process-exit rules. Convert every
independent schema error to a stable `capabilities.schema` violation with its JSON path. Missing or invalid
capabilities, including a document whose `supported_versions` omits `1`, classify as `unsupported`; a valid document
that includes v1 plus exit `0` classifies as `conforming`.

- [ ] **Step 5: Run capabilities and schema tests for GREEN**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest \
  tools/foundry-test-adapter/tests/test_schemas.py \
  tools/foundry-test-adapter/tests/test_capabilities.py -v
```

Expected: all tests pass with deterministic diagnostic ordering.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Validate test adapter capabilities"
```

### Task 3: Validate discovery JSONL and lifecycle semantics

**Files:**

- Modify: `tools/foundry-test-adapter/src/foundry_test_adapter/json_artifacts.py`
- Create: `tools/foundry-test-adapter/tests/test_discovery.py`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/valid/discovery/`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/invalid/discovery/`

- [ ] **Step 1: Write failing stream tests**

Cover empty and nested valid streams, parameterized/colliding labels, astral-character positions, recoverable errors,
and additive fields. Negative cases cover malformed/blank/non-LF lines, wrong protocol/version, start/end placement,
records after end, unknown events, duplicate IDs, missing/late/non-suite parents, invalid canonical paths, range order,
range without path, skip invariants, count mismatch, and truncation.

Add BOM and invalid UTF-8 primary artifacts. They expect `valid: false`, `complete: false`, classification
`infrastructure_failure`, validator exit `1`, and only `artifact.encoding`. CRLF uses `discovery.line_ending` plus
`discovery.incomplete`.

Use this parsed surface:

```python
result, discovery = validate_discovery(path, process_exit=0)
self.assertTrue(result.valid)
self.assertEqual(["suite-a", "test-a"], [item.id for item in discovery.items])
```

- [ ] **Step 2: Run the test and verify RED**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_discovery.py -v
```

Expected: failure because discovery validation is absent.

- [ ] **Step 3: Implement line parsing and schema validation**

Decode UTF-8 once, require final LF, and split only on the literal `"\n"` byte-decoded character. Remove the one final
empty segment created by the required terminal LF, reject every other empty segment, parse each object, and run the
discovery-record schema. Do not use `str.splitlines()`, which would incorrectly treat raw U+2028/U+2029 characters
inside JSON strings as record boundaries. Preserve physical LF line numbers. Continue after independent malformed
lines when the next LF provides a safe resynchronization point.

- [ ] **Step 4: Implement the semantic pass**

Track event state, global IDs, preceding suite IDs, counts, and parsed items. Enforce canonical `res://` paths,
range/path coupling, lexicographic range order, runnable/skip combinations, parent-before-child, one first start, one
final end, and exact counts. Reconcile process exit: complete/no-error is `0`, complete/with-errors is `1`, and
incomplete is infrastructure failure regardless of an uncaught engine exit `1`.

- [ ] **Step 5: Run discovery regression tests for GREEN**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_discovery.py -v
```

Expected: all discovery cases pass and each negative fixture reports the expected independent code set.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Validate test adapter discovery streams"
```

### Task 4: Parse and validate the strict TAP13 report profile

**Files:**

- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/tap13.py`
- Create: `tools/foundry-test-adapter/tests/test_tap13.py`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/valid/report/`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/invalid/report/`

- [ ] **Step 1: Write focused parser tests before parser code**

Valid cases cover `1..0`, pass, fail, skip, all non-empty status details, colliding labels, bailout before/after a plan,
and cancellation with missing output, empty output, a zero-point unsatisfied plan, or a prefix after complete points.
Invalid cases cover headers, plan position, numbering/count, `TODO`, malformed directives,
missing/misplaced/malformed YAML, bad `_foundry`, boolean/negative duration, status/result mismatch, missing failure
message, malformed location, duplicate IDs, completed malformed cancellation units, content after bailout, and exit
mismatches. Add valid cancellation cases after the version line, after the adapter comment, during the plan line, and
inside an incomplete trailing point/YAML block; the validator discards only the incomplete suffix.

Add BOM and invalid UTF-8 reports with only `artifact.encoding`, incomplete infrastructure classification, and
validator exit `1`. CRLF reports use `report.line_ending` plus `report.incomplete`.

- [ ] **Step 2: Run the test and verify RED**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_tap13.py -v
```

Expected: import failure for `foundry_test_adapter.tap13`.

- [ ] **Step 3: Implement the line-state parser**

Use explicit states `HEADER`, `ADAPTER_COMMENT`, `PLAN_OR_BAILOUT`, `POINT`, `YAML`, `DONE`, and `BAILED_OUT`.
Represent parsed points with:

```python
@dataclass(frozen=True)
class TapPoint:
    number: int
    ok: bool
    label: str
    skip_reason: Optional[str]
    test_id: str
    duration_ms: int
    status_detail: str
    message: Optional[str]
    location: Optional[SourceLocation]
```

Parse TAP structure directly. Strip exactly two leading spaces from the explicit YAML document, then call
`yaml.safe_load`. Reject YAML sequences/scalars and invalid `_foundry` mappings. Do not use a general TAP package.

- [ ] **Step 4: Implement report lifecycle validation**

Enforce contiguous numbering, leading plan, unique IDs, exact point count, status/detail combinations, optional
one-based location, no post-bailout content, and process-exit mapping. Under `cancelled=True`, operate on bytes: ignore
an unterminated suffix after the final LF, then discard a trailing point/YAML candidate without its closing `...\n`.
Accept missing/empty output, valid one- or two-line preamble prefixes, or a complete
header/comment/unsatisfied-plan prefix followed by zero or more complete points. Validate every retained completed
unit. Reject a completed malformed unit, bailout, supplied child exit, or satisfied plan as cancellation.

- [ ] **Step 5: Run TAP tests for GREEN**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_tap13.py -v
```

Expected: all strict-profile and lifecycle cases pass.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Validate streaming TAP13 adapter reports"
```

### Task 5: Reconstruct exact selections and cross-check reports

**Files:**

- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/selection.py`
- Modify: `tools/foundry-test-adapter/src/foundry_test_adapter/tap13.py`
- Create: `tools/foundry-test-adapter/tests/test_selection.py`
- Add: selection-context fixtures under `tools/foundry-test-adapter/protocol/v1/fixtures/`

- [ ] **Step 1: Write failing plan reconstruction tests**

Cover run-all, exact test, nested suite, repeated IDs, overlapping suite/test, skipped leaves, non-runnable items,
unknown IDs, empty selected suites, standalone discovery-error IDs, report ID mismatch, and wrong discovery order.

Exercise the internal plan-construction function directly in unit tests:

```python
plan = build_leaf_plan(discovery, selections)
self.assertEqual(("test-a", "test-b"), tuple(item.id for item in plan))
```

- [ ] **Step 2: Run the test and verify RED**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest tools/foundry-test-adapter/tests/test_selection.py -v
```

Expected: import failure for the missing selection module.

- [ ] **Step 3: Implement plan construction**

Index items without interpreting opaque IDs. With no selections, return all runnable tests in discovery order. For
each selection in caller order, reject unknown/non-runnable/error IDs, expand runnable suites to runnable descendant
tests, deduplicate by ID, then restore global discovery order. Reject an explicitly selected suite whose expansion is
empty.

- [ ] **Step 4: Cross-check TAP points**

When discovery context is supplied, require report plan length, IDs, order, and skip directives to match the rebuilt
plan. A skipped point reason must equal the discovery `skip_reason` exactly, without Unicode normalization. Permit
`status_detail: discovery_error` only on a `not ok` point carrying the planned test ID. Never accept a standalone
discovery-error record ID.

- [ ] **Step 5: Run selection plus report tests for GREEN**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest \
  tools/foundry-test-adapter/tests/test_selection.py \
  tools/foundry-test-adapter/tests/test_tap13.py -v
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Validate exact adapter selections"
```

### Task 6: Expose the stable CLI and normative fixture manifest

**Files:**

- Create: `tools/foundry-test-adapter/src/foundry_test_adapter/cli.py`
- Create: `tools/foundry-test-adapter/protocol/v1/fixtures/manifest.json`
- Create: `tools/foundry-test-adapter/tests/test_cli.py`
- Create: `tools/foundry-test-adapter/tests/test_fixture_manifest.py`

- [ ] **Step 1: Write CLI and manifest tests first**

Execute the console entry point in subprocesses. Cover all four operations, text/JSON output, optional exits,
`--cancelled` exclusivity, selection IDs equal to `--`, `--select`, and `--report`, discovery-context requirements,
unreadable inputs, bad invocation, and every manifest entry. Include at least one fixture for every row of the design's
lifecycle result table. Assert exact result keys, classifications, validity, completeness, violation code sets from
the closed registry, and validator exits.

- [ ] **Step 2: Run the tests and verify RED**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest \
  tools/foundry-test-adapter/tests/test_cli.py \
  tools/foundry-test-adapter/tests/test_fixture_manifest.py -v
```

Expected: console entry-point import failure because `cli.py` is absent.

- [ ] **Step 3: Implement argument parsing and rendering**

Build `argparse` subcommands matching the design. Before passing report arguments to `argparse`, scan each `--select`
and consume its next token unconditionally, even when it is `--` or begins with `-`; preserve the resulting opaque
values separately. JSON mode writes only one JSON document to stdout. Text mode writes diagnostics to stderr. Return
`0` for conforming lifecycle results, `1` for violations, and `2` only for invalid validator invocation or an I/O
failure that prevents examining the requested path. A missing artifact returns a normal validation result and exit
`1`.

Implement the design's invalid-context rules exactly: missing discovery context returns `artifact.missing` and exit
`1`; BOM/invalid UTF-8 context returns `artifact.encoding` and exit `1`; readable UTF-8 invalid context returns only
`report.selection` in the report result; another unreadable context returns `artifact.read` and exit `2`. Invalid CLI
syntax always writes usage to stderr, leaves stdout empty even when the raw arguments contained `--format json`, and
exits `2`. Post-parse I/O failures in JSON mode emit one result document.

- [ ] **Step 4: Complete the manifest**

Each entry contains:

```json
{
  "id": "discovery.invalid.duplicate-id",
  "operation": "discovery",
  "artifact": "invalid/discovery/duplicate-id.jsonl",
  "exit_code": 0,
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

The `fixtures` command resolves paths relative to the manifest, runs all entries, reports every mismatch, and exits
non-zero when any expected result differs.

- [ ] **Step 5: Run the full Python gate for GREEN**

```sh
uv lock --check --project tools/foundry-test-adapter
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest discover -s tools/foundry-test-adapter/tests
uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter fixtures \
  tools/foundry-test-adapter/protocol/v1/fixtures/manifest.json
```

Expected: all unit tests and manifest entries pass.

- [ ] **Step 6: Commit**

```sh
git add tools/foundry-test-adapter
git commit -m "Add test adapter conformance CLI"
```

### Task 7: Add the real project-test transport fixture

**Files:**

- Create: `tests/fixtures/foundry_test_adapter_transport/project.foundry`
- Create: `tests/fixtures/foundry_test_adapter_transport/adapter_transport_runner.fs`
- Modify: `tests/core/os/test_foundry_cli_project_test.h`

- [ ] **Step 1: Write the first failing subprocess test before the runner fixture**

Stage the fixture directory under a PID/test-specific `FOUNDRY_TEST_SCRATCH` path, invoke the absent runner through
the real executable, and assert the expected capabilities artifact. Run the focused case and observe failure because
the fixture runner does not exist.

```sh
./bin/foundry.* --headless test run \
  --case "*FoundryCLI*Adapter*" --force-colors
```

- [ ] **Step 2: Add the minimal fixture project and runner**

The runner parses `adapter <operation> <reserved-options> [-- <framework-args>...]`, writes minimal conforming
artifacts, records opaque framework args in additive fixture metadata, prints deliberate process noise, and exposes
fixture-only modes `return-code`, `delayed-report`, and `uncaught-error`. It writes only beneath caller-provided paths.

- [ ] **Step 3: Make pass-through, truncation, isolation, and exit tests GREEN**

Add cases proving reserved-looking framework args remain opaque, pre-seeded files are truncated, process output does
not enter artifacts, and returns `0`, `1`, and `2` propagate. Parse produced JSON/TAP; do not assert fixture source.

- [ ] **Step 4: Add the failing incremental-flush test**

Launch asynchronously. Wait with a bounded condition loop for one complete `...\n`, verify the child remains alive
and the second point is absent, create a scratch continuation file, then wait for a complete two-point report.

- [ ] **Step 5: Implement the delayed-report handshake and make it GREEN**

The runner assembles and flushes the first complete point, yields process frames while waiting for the continuation
file, then assembles and flushes the second. The helper captures stdout/stderr and artifact contents on timeout and
always reaps the child.

- [ ] **Step 6: Add uncaught-error and cancellation regressions**

For uncaught error, assert process exit `1` and an incomplete report. For cancellation, terminate after observing the
first point and assert exactly one complete YAML block against an unsatisfied plan. Do not use fixed sleeps.

- [ ] **Step 7: Build and run the focused suite**

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
./bin/foundry.* --headless test run \
  --case "*FoundryCLI*Adapter*" --force-colors
```

Expected: every adapter transport case passes with no tracked scratch artifacts.

- [ ] **Step 8: Commit**

```sh
git add tests/core/os/test_foundry_cli_project_test.h \
  tests/fixtures/foundry_test_adapter_transport
git commit -m "Prove test adapter runner transport"
```

### Task 8: Finish normative docs, pre-commit, and downstream handoff

**Files:**

- Create: `tools/foundry-test-adapter/README.md`
- Create: `tools/foundry-test-adapter/protocol/v1/README.md`
- Modify: `.pre-commit-config.yaml`

- [ ] **Step 1: Write the two documentation surfaces**

The tool README documents frozen `uv` commands, stable CLI/JSON output, lock maintenance, pinned-checkout use, and
future packaging boundary. The protocol README contains the complete command grammar, schemas, cross-record rules,
strict TAP profile, exits, bailout, cancellation, compatibility, versioning, and fixture manifest contract from the
approved design. Reference checked-in examples; do not duplicate fixture bytes in prose.

- [ ] **Step 2: Add the focused downstream pre-commit hook**

Place it below the repository's downstream marker. Use `language: system`, the frozen `uv` project, no filename
passing, and a `files:` regex limited to `.pre-commit-config.yaml` plus `tools/foundry-test-adapter/`:

```yaml
      - id: foundry-test-adapter
        name: Foundry test adapter protocol
        language: system
        entry: >-
          uv run --frozen --project tools/foundry-test-adapter
          python -m unittest discover -s tools/foundry-test-adapter/tests
        pass_filenames: false
        files: |
          (?x)^(
            \.pre-commit-config\.yaml|
            tools/foundry-test-adapter/.*
          )$
```

- [ ] **Step 3: Run documentation and Python gates**

```sh
git diff --check
uv lock --check --project tools/foundry-test-adapter
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest discover -s tools/foundry-test-adapter/tests
uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter fixtures \
  tools/foundry-test-adapter/protocol/v1/fixtures/manifest.json
pre-commit run foundry-test-adapter --all-files
```

Expected: every command exits zero.

- [ ] **Step 4: Run final engine verification**

On macOS:

```sh
scons platform=macos target=editor dev_mode=yes tests=yes
./bin/foundry.* --headless test run \
  --case "*FoundryCLI*Adapter*" --force-colors
./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-test-adapter-full-progress.jsonl \
  --force-colors
```

On Linux, use `python3 scripts/agent_build.py`, then the focused and full command-first test runs with
`DISPLAY=:1` for the full suite as required by repository instructions.

- [ ] **Step 5: Update linked issue contracts**

Update FoundryLib#11 and Foundry-Scripting#19/#20/#22 to cite
`tools/foundry-test-adapter/protocol/v1/` in an immutable Foundry checkout and use the second runner-level `--` before
framework arguments. Do not duplicate protocol rules that can drift from the normative README.

- [ ] **Step 6: Commit**

```sh
git add .pre-commit-config.yaml tools/foundry-test-adapter
git commit -m "Document test adapter protocol v1"
```

## Final Acceptance Checklist

- [ ] `uv.lock` is current and all Python/fixture gates pass from a clean checkout.
- [ ] Every invalid fixture reports the exact expected independent diagnostic codes.
- [ ] All JSON schemas pass Draft 2020-12 self-validation.
- [ ] The report parser accepts only the approved strict TAP13 profile.
- [ ] Every report point maps to one planned runnable leaf.
- [ ] Standalone discovery-error IDs are rejected from selection and TAP.
- [ ] Cancellation accepts valid completed prefixes, discards only an incomplete suffix, and never reports success.
- [ ] Real subprocess tests prove pass-through, isolation, flushing, exits, uncaught failure, and cancellation.
- [ ] Existing ordinary `project test` cases remain green.
- [ ] No generated or aborted-test files appear outside `FOUNDRY_TEST_SCRATCH`.
- [ ] FoundryLib and extension issues point to the same immutable v1 authority.
