# Foundry Test Adapter Protocol assets and conformance validator

This directory holds everything Foundry owns for the Foundry Test Adapter Protocol:

- `PROTOCOL.md` — the normative specification of version 1.
- `foundry_test_adapter/schemas/` — normative JSON Schemas for the capabilities document and the
  discovery records.
- `foundry_test_adapter/fixtures/v1/` — normative conformance fixtures plus `expectations.json`, the
  hand-written manifest stating what each fixture proves.
- `foundry_test_adapter/` — a stdlib-only, framework-neutral conformance validator.
- `tests/` — the validator's own test suite.

The validator accepts artifacts from any runner. It is not FoundryLib-shaped and has no knowledge of
any particular test framework.

## Command line

```sh
python3 -m foundry_test_adapter \
  --capabilities /path/to/capabilities.json \
  --discovery /path/to/discovery.jsonl \
  --report /path/to/report.tap \
  --select some::stable::id
```

Every artifact is optional, but at least one is required. When both `--discovery` and `--report` are
given, the validator additionally correlates the report against the selection: it resolves the
selection to the deduplicated leaf identifiers the run must answer and checks the report against
them.

Exit codes:

- `0` — no violations.
- `1` — at least one violation; every violation is printed, not just the first.
- `2` — usage error, including an unreadable artifact.

`--json` emits a machine-readable document with per-artifact violations plus the facts a client needs
(negotiable versions, discovery counts and completeness, plan, point count, bail-out state).

## Library

```python
from foundry_test_adapter import (
    validate_capabilities_document,
    validate_discovery_stream,
    validate_tap_report,
    validate_run,
)

capabilities = validate_capabilities_document(capabilities_text)
version = capabilities.negotiate((1,))

discovery = validate_discovery_stream(discovery_text)
report = validate_tap_report(report_text)
violations = validate_run(discovery, report, ["some::stable::id"])
```

Each `validate_*` call returns every violation it found. Results also expose parsed protocol facts —
discovered items, errors, TAP points, plan, and completeness — so a client can build UI from the same
objects it validates with.

`foundry_test_adapter.utf16_length` and `foundry_test_adapter.utf16_offset_to_index` implement the
UTF-16 position encoding that discovery ranges use.

## Running the tests

```sh
python3 -m unittest discover -s tools/foundry-test-adapter/tests -t tools/foundry-test-adapter
```

## Consuming these assets downstream

For version 1, downstream repositories consume the schemas, fixtures, and validator from an immutable
pinned Foundry commit or release tag. Pinning a mutable branch such as `develop` is not sufficient,
because the assets are only normative at a fixed revision.

Publishing an independent package is not required for version 1. The layout nevertheless keeps that
option open without a protocol change: `foundry_test_adapter/` is a self-contained importable package
that carries its own schemas and fixtures as package data, resolved through
`foundry_test_adapter.schemas_root()`, `foundry_test_adapter.schema_path()`,
`foundry_test_adapter.fixtures_root()`, and `foundry_test_adapter.expectations_path()` rather than
through paths relative to this repository. Nothing outside `tests/` depends on the checkout layout.

Fixtures are versioned by directory (`fixtures/v1/`), so a future version 2 adds a sibling directory
instead of rewriting version 1 inputs that downstream implementations already assert against.
