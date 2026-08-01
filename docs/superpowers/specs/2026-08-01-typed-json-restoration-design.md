# Typed JSON Restoration Design

## Scope

Restore the typed JSON decode path that PR #1458 accidentally removed, then integrate the #1453
return-type inference fix against the current `origin/develop`. Current `develop` remains
authoritative for every unrelated change, including #1466, #1472, and #1474.

## Architecture

Core continues to own JSON text parsing while `JSONObjectMarshaller` supplies language-owned tree
and result values through `lift_variant`, `make_parse_success`, and `make_parse_failure`.
`JSON.parse_to_node` returns the marshaller's opaque result without teaching core about Foundry
Script types.

The Foundry Script marshaller restores the decode-side helpers that call the builtin `JsonNode` and
`JsonResult` scripts. Those helpers are merged into the newer #1466 implementation: retroactive
native `JsonSerializable` witnesses remain the encoding path, while parsing remains independent of
receiver conformance dispatch.

The analyzer keeps ClassDB's runtime `Variant` binding and applies a module-owned static type hint
for the resolved `JSON.parse_to_node` method. It resolves the hint as `JsonResult[JsonNode]` through
the existing builtin global-type system, and builtin source dependencies remain valid during build
task bootstrap and stripped-bytecode export.

## Reconciliation Rules

- Restore only the JSON decode hunks removed by merge commit `31946646cb` and the four deleted
  runtime fixtures; do not revert the tooling-host commit.
- Preserve #1466's conformance-registry lookup, native witness dispatch, tests, fixture, and updated
  documentation. Reinsert decode helpers around that implementation.
- Preserve #1472's exported builtin bytecode path mapping and deterministic builtin enumeration.
- Preserve #1474's tooling host launch/shutdown changes without modification.
- Combine the #1454 `parse_to_node` documentation with #1466's corrected statement that native
  retroactive conformance is supported.
- Do not change `GRAMMAR.md`: this work restores a native API and analyzer metadata but does not add
  or alter Foundry Script syntax, tokens, precedence, annotations, or grammar productions.

## Observable Behavior and Tests

Core doctests prove parsing delegates success, syntax failure, unrepresentable trees, and result
construction failures through the marshaller. Foundry Script doctests prove Variant trees lift to
the builtin wire contract and runtime fixtures prove round-trip and failure behavior. The fixtures
infer `JSON.parse_to_node` locals with `:=`, so strict analysis must expose `JsonResult[JsonNode]`
without explicit annotations. Existing #1466 tests continue proving native retroactive conformance.

Verification requires a strict macOS `dev_mode=yes` build, focused core/marshal/analyzer/bootstrap
and script tests, the full structured-progress suite, touched pre-commit hooks, and supervised Codex
review convergence against the then-current `origin/develop`.
