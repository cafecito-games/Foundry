# Foundry Test Adapter Protocol tooling

This directory is a self-contained [uv](https://docs.astral.sh/uv/) project holding the normative
Foundry Test Adapter Protocol v1 assets and the conformance validator that checks artifacts against
them.

- `protocol/v1/README.md` — the normative protocol specification.
- `protocol/v1/*.schema.json` — Draft 2020-12 schemas for the capabilities document and discovery records.
- `protocol/v1/fixtures/` — the reusable positive and negative corpus plus `manifest.json`.
- `src/foundry_test_adapter/` — the validator implementation.
- `tests/` — the validator's own test suite.

## Running the validator

Every command runs through the frozen project so the pinned `jsonschema` and `PyYAML` resolutions in
`uv.lock` are used:

```sh
uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter capabilities /path/to/capabilities.json --exit-code 0 --format json
```

The four stable operations are:

```sh
foundry-test-adapter capabilities <file> [--exit-code <n>] [--format text|json]
foundry-test-adapter discovery <file> [--exit-code <n>] [--format text|json]
foundry-test-adapter report <file> [--discovery <file>] [--select <id>]... \
    [--exit-code <n> | --cancelled] [--format text|json]
foundry-test-adapter fixtures <manifest>
```

Omitting the process context validates the artifact alone. Supplying `--exit-code` also checks that
the observed process lifecycle matches the artifact, and `--cancelled` validates a cancellation
prefix without inventing an exit code. `--select` requires `--discovery`, and each selection value is
opaque: `--select --` selects the identifier `--`.

Validator exit codes are `0` for a conforming lifecycle (including represented failures, a
well-formed bailout, and a valid cancellation prefix), `1` for conformance violations, and `2` for an
invalid invocation or an I/O failure that prevents examining a requested path. A nonexistent path is
an observable missing artifact, so it produces a result document and exit `1`.

Under `--format json` exactly one result document is written to stdout. Text mode writes the same
information to stderr. The command surface and the JSON document are the public interfaces; the
Python modules behind them are not.

## Development

```sh
uv lock --check --project tools/foundry-test-adapter
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest discover -s tools/foundry-test-adapter/tests
uv run --frozen --project tools/foundry-test-adapter \
  foundry-test-adapter fixtures tools/foundry-test-adapter/protocol/v1/fixtures/manifest.json
pre-commit run foundry-test-adapter --all-files
```

Dependencies are pinned exactly in `pyproject.toml` and resolved completely in `uv.lock`. Refresh the
lock with `uv lock --project tools/foundry-test-adapter` and commit the result in the same change as
the dependency edit. Pre-commit and CI consume this same project and lock rather than a second
dependency list.

Fixtures are byte-exact conformance inputs. `protocol/v1/fixtures/.gitattributes` disables end-of-line
normalization so the deliberate CRLF, missing-terminal-LF, and non-UTF-8 cases survive checkout.

## Downstream consumption

For v1 there is no separately published package. A downstream repository consumes the schemas,
fixtures, and validator from an immutable pinned Foundry checkout:

```sh
git clone --depth 1 --branch <pinned-tag> <foundry-remote> foundry
uv run --frozen --project foundry/tools/foundry-test-adapter \
  foundry-test-adapter report build/report.tap --discovery build/discovery.jsonl --exit-code 1
```

The bundle is relocatable and versioned so it can be packaged later without changing the protocol or
this command surface. A future protocol version adds a sibling `protocol/v2/` directory rather than
editing `protocol/v1/`.
