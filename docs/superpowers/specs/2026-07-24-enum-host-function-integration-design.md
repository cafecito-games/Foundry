# Enum Host Function Integration Validation Design

## Goal

Close issue #1123 with one meaningful end-to-end regression that validates the
remaining cross-surface combination not covered by the earlier #1115 slices:
calls to a namespaced, cross-file `enum_name` host from both text-compiled and
compiled-bytecode callers.

## Audit Result and Scope

The merged parser, analyzer, call metadata, compiler/runtime, bytecode,
formatter, LSP/completion, and documentation changes satisfy their individual
child issues. The final audit also confirms:

- `GRAMMAR.md` matches the named, colon/indented enum-function syntax;
- the temporary `#skip-compiled-bytecode` fixture directive and all four #1120
  runtime sentinels are gone from live fixtures and runner behavior;
- bytecode format version 3 persists root and nested enum-function tables;
- no deprecated Foundry CLI invocation was added by the epic's implementation
  diff.

Existing runtime coverage dispatches across files to an unnamespaced
`enum_name`, while the analyzer metadata tests separately cover a namespaced
global enum. The missing integration is the combination: the qualified enum
identity must survive analyzer metadata, code generation, compiled caller
serialization, VM owner lookup, and source-loaded dependency dispatch.

This slice is test-only unless the new regression exposes a production defect.
Any such defect requires root-cause analysis and a focused test-first fix before
the scope expands.

## Alternatives Considered

### Add a namespaced cross-file runtime fixture

This is the selected approach. It extends the same script-fixture surface used
by #1119 and automatically runs once from source and once with the caller
round-tripped through compiled bytecode. It is small, user-facing, and covers
the qualified owner identity at every layer without duplicating internals.

### Add a large C++ integration test

A C++ test could inspect every scalar field and runtime table directly, but the
child issues already contain that lower-level coverage. Another large AST and
bytecode test would repeat implementation details while providing less
confidence in the normal script-fixture path.

### Publish a validation-only documentation change

A checklist document would record the audit but would not protect the one
remaining combination from regression. The runtime fixture provides durable
value at little maintenance cost.

## Fixture Design

Add a provider under
`modules/foundry_script/tests/scripts/runtime/features/` that declares a
namespace and a global enum:

```fs
namespace issue_1123.enum_integration

enum_name IntegratedStatus:
	UNKNOWN = 0
	READY = 10
	DONE = 20

	func or_else(fallback: Self) -> Self:
		return fallback if self == UNKNOWN else self

	func label(prefix: String = "") -> String:
		return prefix + ("ready" if self == READY else "done")

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE
```

The executable consumer imports the provider namespace, preloads the provider
so the dependency is explicit, and calls:

- the static parser on the imported enum metatype;
- instance methods on returned and literal enum values;
- a method with `Self` parameter and return types;
- a method using its default parameter;
- the enum metatype's existing read-only Dictionary `keys()` fallback.

The expected output locks the dispatch results and dictionary values. The
fixture runner's existing compiled-bytecode pass serializes and restores the
consumer while its external provider resolves through the normal source cache.
That proves the qualified owner path/class/enum tuple remains usable after
caller bytecode round-trip.

## Validation

Run the new fixture first with the current test binary after adding only the
test files. Because this is coverage for already implemented behavior rather
than a behavior change, a passing first run is the expected outcome; any
failure is treated as a discovered defect and debugged systematically before
production code changes.

Then run:

- the focused namespaced enum-host fixture;
- the complete Foundry Script test surface;
- completion and LSP suites;
- a strict `dev_mode=yes` build with warnings as errors;
- the full test suite with a structured JSONL progress file.

No fixture regeneration is needed because the expected output is authored
explicitly and no language or formatter behavior changes.
