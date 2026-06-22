# GDScript Async Function Contracts

Date: 2026-06-22

## Summary

Add `async` as a reflected GDScript function contract for methods that must be awaited by callers. The feature lets abstract classes declare coroutine requirements directly:

```gdscript
@abstract
class_name MyBaseClass
extends Node

@abstract
async func download_data() -> String
```

The first implementation focuses on method contracts, reflection, diagnostics, and editor/LSP surfaces. It does not introduce a first-class `Coroutine` type or source-level async callable syntax.

## Goals

- Allow `async func` declarations in GDScript.
- Allow `@abstract async func` declarations with no body.
- Reflect coroutine methods through `MethodInfo` using a new `METHOD_FLAG_ASYNC`.
- Preserve compatibility for existing functions whose coroutine status is inferred from `await` in the body.
- Enforce async-ness as part of override signature compatibility.
- Reuse existing await/coroutine runtime behavior.
- Surface `async` in docs, completion, LSP, and method-list tooling.

## Non-Goals

- Do not add a `Coroutine` type in this epic.
- Do not add syntax such as `Callable[async [String], int]`.
- Do not require all existing coroutine bodies to be explicitly declared with `async`.
- Do not change VM scheduling or `GDScriptFunctionState` semantics.

## User-Facing Semantics

The accepted syntax is:

```gdscript
async func load() -> String
static async func make() -> String
@abstract async func download_data() -> String
```

`async` marks the function as a coroutine contract even if the body does not contain `await`. Callers should use `await`, and the existing missing-await diagnostic path applies.

Existing functions that contain `await` remain valid. They continue to be treated as coroutines and should also reflect as async through `METHOD_FLAG_ASYNC`.

`async` is invariant for overrides:

- async parent + async/coroutine child: valid
- async parent + sync child: error
- sync parent + async/coroutine child: error
- sync parent + sync child: valid

## Parser Design

Treat `async` as a contextual modifier in class/function declaration positions, not as a globally reserved keyword. This minimizes compatibility risk for existing identifiers named `async`.

Valid modifier order is:

```gdscript
async func name()
static async func name()
@annotation async func name()
@annotation static async func name()
```

Invalid forms should produce targeted parser errors:

```gdscript
async static func name() # use static async func
func async name()        # async belongs before func
async var value          # async only applies to functions
```

Add `FunctionNode::is_declared_async` and keep the existing `FunctionNode::is_coroutine`.

- `is_declared_async` records source syntax.
- `is_coroutine` records the effective behavior contract.
- Parsing `async func` sets both.
- Parsing an `await` expression in a body continues to set `is_coroutine`.

## Reflection Design

Add a core method flag:

```cpp
METHOD_FLAG_ASYNC = 256
```

Bind the flag in core constants so it is visible to GDScript reflection. Update method documentation generation and method-list formatting to render `async` when this flag is present.

GDScript analyzer/compiler method metadata should set `METHOD_FLAG_ASYNC` when `FunctionNode::is_coroutine` is true. This intentionally reflects both explicit `async func` declarations and legacy body-inferred coroutines.

## Analyzer Design

The analyzer should:

- Treat `async func` as coroutine even when the body has no `await`.
- Preserve current call-site behavior for coroutine return types.
- Use the existing missing-await warning/error path for root async calls.
- Keep non-root async calls without `await` as errors.
- Include async-ness in parent signature compatibility checks.
- Require abstract async methods to be implemented by async/coroutine overrides.
- Propagate async method flags through `get_function_signature()` and `function_signature_from_info()`.

The parent signature error should make the async mismatch clear rather than only reporting a generic signature mismatch when practical.

## Runtime Design

No VM behavior change is required for this first pass.

The VM already supports:

- awaiting a real suspended GDScript function state;
- awaiting a signal;
- awaiting a synchronous value, which returns immediately.

That makes explicit async functions without internal suspension safe:

```gdscript
async func cached_value() -> String:
	return "cached"

func test() -> void:
	var value := await cached_value()
```

## Tooling Design

Update GDScript tooling to surface async contracts consistently:

- syntax highlighting recognizes contextual `async` as a declaration modifier;
- completion suggests `async func` and `static async func` where appropriate;
- override completion includes `async` for async GDScript methods;
- method argument/signature hints display `async` for reflected async methods;
- docs generation adds `async` to method qualifiers;
- LSP document symbols, completion details, and signature help include `async`;
- test helper output for reflected method signatures includes `async`.

## Compatibility

This is a compatibility-first design:

- Existing coroutine bodies remain valid without an explicit `async` modifier.
- Existing identifiers named `async` should keep working outside declaration modifier positions.
- Existing missing-await settings continue to control root call diagnostics.

A future warning can encourage explicit declarations:

```text
Function contains await but is not declared async.
```

That warning is intentionally outside this first implementation.

## Testing Plan

Add or update tests for:

- parser accepts `async func`;
- parser accepts `static async func`;
- parser accepts `@abstract async func`;
- parser rejects invalid modifier order and unsupported async targets;
- async abstract method implementation requirements;
- async override invariance in both directions;
- explicit async function without internal `await` requires await at call sites;
- explicit async function without internal `await` runs correctly when awaited;
- legacy body-inferred coroutine reflects with `METHOD_FLAG_ASYNC`;
- `get_method_list()` and helper formatting include `async`;
- docs, LSP, and completion surfaces include `async`;
- no `Coroutine` source type is introduced in this epic.

## GitHub Epic Breakdown

Create one tracking epic with native subissues:

1. Core reflection: add `METHOD_FLAG_ASYNC` and docs/constants plumbing.
2. GDScript parser/AST: contextual `async` modifier and signature capture.
3. Analyzer: async propagation, call enforcement, and override invariance.
4. Compiler/runtime metadata: set reflected async flags without VM behavior changes.
5. Editor/LSP/docs: render async in completions, signatures, docs, and symbols.
6. Tests: parser, analyzer, runtime, reflection, and tooling coverage.
7. Follow-up research: first-class async callable or `Coroutine` type design.

## Open Decisions

All currently known language-design decisions for this epic are resolved. The `Coroutine` type question is deferred to a follow-up research issue.
