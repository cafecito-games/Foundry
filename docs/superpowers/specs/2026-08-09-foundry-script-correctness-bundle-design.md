# Foundry Script Correctness Bundle Design

**Issues:** [#1808](https://github.com/cafecito-games/Foundry/issues/1808),
[#1967](https://github.com/cafecito-games/Foundry/issues/1967),
[#1310](https://github.com/cafecito-games/Foundry/issues/1310),
[#1388](https://github.com/cafecito-games/Foundry/issues/1388),
[#1966](https://github.com/cafecito-games/Foundry/issues/1966), and
[#1811](https://github.com/cafecito-games/Foundry/issues/1811)

**Base branch:** `develop`

**Delivery:** One pull request that closes all six issues.

## Summary

This change fixes six independent correctness defects in one coordinated pull request:

1. Derived receivers fail to find statically typed members supplied by a base class's trait.
2. Global trait signals disappear from the runtime signal table during a state-preserving reload.
3. The formatter loses comments owned by redundant grouping closing delimiters.
4. Nested cross-file analysis borrows the caller's conformance visibility.
5. Class-parameterized container returns remain erased at runtime despite concrete static types.
6. Repository-wide pre-commit mutates deliberate byte-exact fixtures and known spelling debt.

The fixes share a release and verification boundary but do not share a new abstraction. Each defect keeps its existing
runtime, analyzer, formatter, or repository policy model. Where two issues touch the same analyzer functions, one worker
owns or sequences those edits so the combined behavior is designed and tested together.

## Goals

- Restore the static and runtime contracts described by all six issues.
- Reuse existing specialization, trait flattening, erased-container conversion, and conformance visibility models.
- Make behavior deterministic across inheritance depth, generic specialization, reload mode, source-file ordering, and
  formatter nesting.
- Add observable behavioral coverage before implementation and keep existing controls unchanged.
- Deliver one reviewable branch and pull request with strict build, full-suite, pre-commit, and
  adversarial-review proof.

## Non-goals

- No Foundry Script syntax, grammar, bytecode format, or VM representation change.
- No copying trait members into analyzer class member tables.
- No per-specialization compilation of generic class or trait methods.
- No editor or LSP symbol-policy change beyond behavior already supplied by the analyzer.
- No broad formatter trivia rewrite or unrelated formatter normalization.
- No broad pre-commit exemption for documentation or source fixtures.
- No work on the related follow-ups explicitly excluded by the issue bodies, including #1390 and #1490.

## Design

### 1. Base-applied trait member lookup (#1808)

The analyzer will gain one ordered lookup helper for traits applied anywhere along a receiver's Foundry Script
inheritance chain. It will resolve each hierarchy level's trait uses, visit classes from most-derived to oldest base,
preserve each level's resolved-trait order, and de-duplicate trait nodes by first reach.

All ordinary class members keep their current precedence and are searched across the full class/base chain before the
trait fallback begins. Lexical outer-class traversal remains unchanged and does not inherit outer traits.

`reduce_identifier()`, `get_function_signature()`, and `find_generic_method()` will use this one hierarchy-trait policy.
Each consumer retains its current member-kind, static/instance, constructor, and diagnostic checks. When a member is
found, specialization remains anchored at the original receiver datatype and uses `specialize_ancestor_type()` to
project through inheritance and trait-application edges. Shared trait AST nodes are never mutated.

This covers variables, signals, concrete methods, callable method values, explicit generic methods, and bare, `self`,
and explicit receivers. It does not extend flattening to unsupported tuple or inner-class declarations.

### 2. Class-parameterized erased container returns (#1966)

Call classification will inspect the declared, unspecialized top-level `Array` or `Dictionary` return before receiver
specialization removes evidence of class-scoped type parameters. Recursive dependence in array elements and either
dictionary slot counts, including nested containers.

If receiver specialization makes the call's static return concrete while the reusable method body returns an erased
container, the analyzer sets the existing `returns_erased_container` marker. Rich callable signatures retain the
corresponding return-erasure metadata for `call()` and `callv()`.

The compiler will continue using its existing typed-array and typed-dictionary conversion opcodes at typed consumers.
Untyped consumers remain untyped, and incompatible contents still fail conversion. No compiler consumer, VM opcode, or
runtime representation is added unless a behavioral test exposes an existing consumer-position gap; such a gap will be
triaged against the issue's stated scope before extension.

### 3. Global trait signal materialization (#1967)

Compilation already includes direct and flattened trait signals, but `reload(true)` subsequently clears the runtime
signal table and rebuilds it from direct AST members only. The reload export-update path will instead reconstruct
signals from the same compiler-effective direct-plus-flattened member set used during compilation.

The reconstruction must reuse the established class/base shadowing and trait diamond de-duplication policy rather than
maintain an independent flattening loop. Rebuilding from current effective members avoids retaining stale signals after
source changes. Existing script-base traversal provides subclass inheritance; no new signal storage or instance lookup
mechanism is needed.

Tests will exercise a real file-backed/global script through the state-preserving reload path, because ordinary corpus
compilation does not trigger the clobber. Direct implementers, subclasses, trait-typed access, composed/diamond traits,
and signal delivery from a concrete trait method are covered. The inherited unqualified-method route is owned by #1808,
not duplicated here.

### 4. Owner-scoped nested conformance visibility (#1388)

`FSAnalyzer` will gain one private RAII helper that installs the owning analyzer's `ConformanceVisibility` for a nested
foreign-node resolution. The helper replaces the caller scope for the delegated call and restores it on return.

All five foreign-analyzer delegation paths use the helper: class inheritance, class member, class interface, trait uses,
and class body. No call site installs `ScopedVisibility` directly, and the scope is not derived from
`parser->current_class`.

The helper documents the invariant that memoized foreign-node results are owner-defined and therefore independent of
which caller first forces resolution. Invalid dependencies are rejected consistently; a dependency that loads the
needed retroactive conformance itself remains valid.

### 5. Redundant grouping closing-line comments (#1310)

The parser will retain enough closing-delimiter position/ownership metadata on a redundant grouping to distinguish a
comment owned by the close delimiter from trivia belonging to later source on the same line. Line-only ownership is not
sufficient for nested calls, collection elements, postfix operations, or binary continuations.

When a multiline redundant grouping owns an otherwise unconsumed close-line comment, the formatter preserves that
grouping. To keep enclosing syntax safe, the canonical fallback moves the comment to a standalone line immediately
before the synthesized closing `)`:

```foundryscript
var value = foo(
	(
		1 + 2
		# close note
	),
)
```

This placement was explicitly approved for cases where retaining `)  # note` inline could comment out an outer
operator, comma, or delimiter. Single-line groupings and comments belonging to later continuation source keep their
existing behavior. The formatter consumes only the proven delimiter-owned comment and trivia strictly inside the
selected grouping; it does not restore the previous broad span scan.

Golden fixtures cover top-level and nested positions, arrays/dictionaries/tuples, nested redundant groups, content-line
plus close-line comments, postfix/binary continuations, and the corruption/duplication controls from commit
`64db0ea801`. The formatted result must be a fixed point and preserve comment count exactly.

### 6. Repository-wide pre-commit cleanliness (#1811)

The existing file-format hook will exclude the entire
`tools/foundry-test-adapter/protocol/v1/fixtures/` tree. That tree is declared byte-exact by its `.gitattributes`, and a
read-only simulation found deliberate BOM, CRLF, missing-terminal-newline, truncated, and valid partial-report fixtures
that the hook rewrites. Excluding only `fixtures/invalid/` would leave one valid byte-exact fixture vulnerable.

Legitimate codespell findings will be corrected using en-US spelling, including fixture identifiers and their uses.
The valid analyzer term `statics` will be added to the existing codespell ignore configuration instead of rewriting the
term or broadly exempting docs. Fixture output regeneration is unnecessary when an identifier rename does not change
observable output.

No test will assert on YAML text or regex formatting. Existing adapter manifest/validator tests prove fixture bytes and
protocol behavior. The final behavioral gate is `pre-commit run --all-files` followed by a clean worktree check.

## Error handling and compatibility

- #1808 reuses current direct-application diagnostics; it introduces no new wording.
- #1966 preserves strict destination validation and reports incompatible contents through existing runtime errors.
- #1967 removes a false runtime missing-property error without weakening signal typing.
- #1388 intentionally turns false-clean dependent analyses into the same errors their owning files already produce.
- #1310 prefers retaining and safely repositioning a comment over dropping, duplicating, or allowing it to alter syntax.
- #1811 keeps protocol fixtures invalid by design while removing accidental repository mutation.

## Test strategy

Each behavior change follows test-driven development:

1. Add the smallest focused fixture or C++ regression and demonstrate failure against the existing binary/build.
2. Implement the scoped production change.
3. Rebuild through `scripts/agent_build.py` and rerun the focused case or suite.
4. Run neighboring controls and both source-runtime and compiled-bytecode fixture modes where applicable.

Focused coverage includes:

- Analyzer error and positive/runtime fixtures for derived trait members, multiple specializations, inheritance depth,
  cross-file use, precedence, and all supported member routes.
- Generic class, direct trait, forwarded trait, nested array/dictionary, rich callable, untyped target, incompatible
  content, and existing consumer-position coverage for erased returns.
- File-backed state-preserving reload, direct/subclass/trait-typed signal access, diamond safety, and signal delivery.
- Multi-file C++ conformance tests in both analysis orders, foreign interface resolution, and a valid visibility
  control.
- Formatter golden, idempotency, and comment-count tests for close-owned grouping comments.
- Foundry Test Adapter unit tests, affected Foundry Script fixtures, full pre-commit, and clean-diff verification.

Before publication, the branch must pass:

- The relevant focused doctest cases/suites and script corpora.
- `python3 scripts/agent_build.py` using the native strict backend.
- The full headless suite with structured progress and the required display for GUI-dependent subprocess tests.
- `pre-commit run --all-files` with no tracked rewrite.
- A supervised adversarial Codex review against `origin/develop`, repeated until clean or only explicitly triaged
  out-of-scope findings remain.

## Parallel execution and ownership

One shared `issue-1808` worktree keeps the final branch continuously integrated. Parallel work runs in waves:

1. One worker owns #1808 then #1966 because both modify analyzer lookup/signature construction.
2. One worker owns #1967's runtime signal reconstruction and file-backed tests.
3. One worker owns #1310 and #1811, whose formatter/configuration files are disjoint from the analyzer and runtime work.
4. #1388 starts after the #1808/#1966 analyzer worker finishes because it touches the same large analyzer files.

Workers do not stage, commit, regenerate broad fixture sets, run repository-wide mutating hooks, or build concurrently
in the same worktree. The root agent reviews each change set, integrates commits intentionally, runs serialized builds
and final tests, and resolves cross-issue interactions.

## Delivery

The branch will contain focused commits with imperative subjects. After verification and review convergence, it will be
pushed as one pull request targeting `develop`. The pull request description will explain each behavior change, list
verification evidence, and end with closing references for all six issues. Squash auto-merge will be enabled. The
worktree and branch will be removed only after the pull request actually merges.
