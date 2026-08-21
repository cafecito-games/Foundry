# Foundry Script Type-System Completeness Program

**Date:** 2026-08-20
**Status:** Approved

## Problem

Foundry Script's type system has reached a point where fixes repeatedly expose adjacent gaps. A recent snapshot of
the last 100 merged pull requests found 63 whose titles matched type-system themes and at least 15 that explicitly
mentioned follow-up work. The exact counts are only a point-in-time heuristic, but the failures form a consistent
pattern: a rule works for one type spelling, composition, value boundary, representation, or tooling surface and
diverges in a neighboring one.

Recent examples span unions, `Self`, generic traits, tuple and enum payloads, numeric widths, callable rendering,
runtime stores, reflection, proxies, conformance lifetime, diagnostics, and editor inference. Individual fixes are
well tested, but the tests usually begin from one reproduction. Agents discover the surrounding semantic
neighborhood during implementation or review and file more issues because no finite inventory says which adjacent
cases belong to the same rule.

The project needs an executable definition of type-system completeness. It must find gaps deliberately, turn them
into bounded work, prevent new features from recreating missing cells, and provide evidence that the known language
surface has been covered. It must not promise the impossible claim that no future bug can exist.

## Goals

- Define a finite, auditable completeness contract for Foundry Script typing.
- Cover language semantics, runtime enforcement, diagnostics, editor/LSP behavior, serialization, reload, lifetime,
  cache invalidation, and concurrent analysis where these affect correctness.
- Give every in-scope type-system combination an executable expected disposition.
- Discover families of adjacent defects before implementation begins rather than during unrelated pull requests.
- Replace one-reproduction issues with closure packets that own a complete semantic neighborhood.
- Detect missing recursion, erasure, projection, or reification across every type representation.
- Use independent semantic, metamorphic, and parity oracles so the test system does not reproduce one implementation's
  mistakes as its expected result.
- Produce stable case identities and structured failure artifacts that agents can reproduce and GitHub issues can
  reference.
- Gate new type features on extending and passing the completeness model.
- Permit deliberate unsupported combinations only when their rejection is specified, rationalized, and tested.

## Non-goals

- Do not implement a second Foundry Script type checker as the test oracle.
- Do not enumerate arbitrary programs or unbounded recursive type depth.
- Do not require every syntactically expressible type combination to become supported.
- Do not begin with a big-bang rewrite of analyzer, compiler, VM, editor, or LSP typing code.
- Do not treat optimization or performance as completeness requirements unless they change observable correctness or
  make a required gate impractical.
- Do not replace focused handwritten unit tests and integration fixtures.
- Do not use source-text assertions or function-body matching to prove that production code traverses a type field.
- Do not turn every generated matrix case into a checked-in fixture.

## Completeness Contract

Completeness is defined over declared equivalence classes and bounded compositions. Every generated cell must have
one of two dispositions:

1. Supported, with executable expectations for every applicable surface.
2. Intentionally unsupported, with a language rationale and an executable, consistent rejection expectation.

An unimplemented case, a crash, an unclassified diagnostic, or a link to a future issue is not a disposition.

The contract has five axes:

- **Type shape:** Primitive and native types, numeric widths, `Variant`, nullable values, metatypes, `Self`, type
  parameters, specialized classes, traits, tuples, tagged unions, ordinary unions, callables, signals, arrays, and
  dictionaries.
- **Composition:** Direct use, container element, generic argument, union alternative, tuple or payload field,
  callable parameter/return/rest slot, nullable layer, metatype/type-handle layer, and substituted or erased position.
- **Value boundary:** Declaration initialization, assignment, argument binding, return, member/property store,
  container mutation, tuple/payload construction, cast, `is` test, pattern, reflective write, proxy crossing, signal
  emission, and native/script boundary.
- **Execution and lifecycle:** Analyzer-only, text execution, bytecode execution, serialization/deserialization,
  reload, dependency replacement or freeing, cache invalidation, repeated analysis, and concurrent LSP analysis.
- **Tooling surface:** Diagnostic rendering and attribution, completion, hover, signature help, semantic lookup,
  refactoring, documentation/type rendering, and migration tooling.

The catalog partitions each axis into semantic equivalence classes. Small finite domains are expanded exhaustively.
Large domains use deterministic pairwise coverage by default, targeted higher-order combinations for historically
dangerous interactions, explicit depth-two composition, and selected depth-three composition. Handwritten seeds
cover cases whose behavior depends on a specific ownership, lifetime, ordering, aliasing, or concurrency shape.

Changing an equivalence partition is a specification change. If scheduled exploration finds a failure that cannot
be expressed by the current axes or rules, the model is incomplete: the catalog is extended and the closure soak is
reset.

## Architecture

### Capability manifest

A source-controlled, declarative capability manifest is the program's semantic inventory. Each rule records:

- A stable rule identity.
- The participating type-shape and composition classes.
- Applicable value boundaries, execution modes, lifecycle states, and tooling surfaces.
- Preconditions and excluded equivalence classes.
- Expected outcome class: accept, reject, accept with conversion, accept with runtime check, warning, or other
  explicitly defined behavior.
- Observable runtime result where applicable.
- Metamorphic and parity relations that should hold.
- Diagnostic meaning or structured fields when rejection or warning is expected.
- Any intentional unsupported disposition and its rationale.
- Links to historical regression families and permanent seed cases.

The manifest describes rules and expected relations. It does not contain an algorithm capable of deciding arbitrary
Foundry Script types, which would create a second implementation likely to share or introduce defects.

Stable case IDs are derived from semantic coordinates, not generation order. Reports, permanent regressions, GitHub
issues, and pull requests use those IDs so a case remains traceable when the generator changes.

### Type-shape generator

The generator expands the manifest into valid Foundry Script programs and direct C++ type-relation probes. It
supports:

- Exhaustive expansion of small partitions.
- Deterministic pairwise and configured higher-order expansion.
- Direct, wrapped, nested, substituted, and erased variants.
- Positive, negative, and boundary-value witnesses.
- Multi-file projects for ownership, inheritance, traits, namespaces, and reload.
- Reproducible random exploration with an explicit seed.
- Automatic reduction that preserves the failing semantic coordinates.

Generated projects and artifacts are written under the shared Foundry test scratch space. The checked-in fixture
corpus receives only minimized, high-value regressions that improve readability, protect a subtle invariant, or
exercise infrastructure unavailable to the generated runner.

### Independent oracles

The harness combines several oracles. A cell need not use every oracle, but no broad rule family should rely on only
one.

#### Explicit semantic expectations

The manifest declares observable acceptance, rejection, conversion, runtime-check, warning, and execution results.
These expectations are appropriate for settled language rules and intentional exclusions.

#### Metamorphic relations

Metamorphic rules compare related programs without needing an independent full type checker. Examples include:

- Wrapping a destination `T` as `T | U` preserves acceptance unless union semantics explicitly change the rule.
- Replacing a type parameter with a concrete argument produces the same result as spelling that concrete type
  directly where ownership semantics do not intervene.
- Moving the same declared slot between equivalent value boundaries preserves static and runtime enforcement.
- A type renderer followed by its supported parse/serialization round trip preserves semantic identity.
- Adding an unrelated union alternative or conformance does not change which existing alternative or witness owns a
  value.

#### Cross-surface parity

Parity comparisons include text versus bytecode, ordinary versus reflective writes, direct versus proxy crossings,
parameter versus return enforcement, initial load versus reload, analyzer diagnostics versus editor/LSP type
information, and equivalent native/script boundaries.

#### Historical regression families

Recent and older type-system fixes become permanent seed families, not merely individual examples. Each family is
expanded across the axes that the original defect plausibly affected. Historical cases guide coverage but do not
define the full catalog.

### Representation and walker census

Foundry Script has multiple type representations, including parser `DataType`, runtime `FSDataType`, container and
weak forms, serialized descriptors, and editor-facing forms. The census records every semantic child slot in each
representation and every transformation, predicate, comparator, renderer, serializer, or projection that may need
to visit it.

For every operation/child pair, the census declares one policy:

- Traverse recursively.
- Preserve without traversal.
- Substitute or rebind.
- Project into another representation.
- Deliberately erase or degrade.
- Not applicable, with rationale.

Coverage must execute behavior. It may use shared visitor registries, constructed values with one distinctive child
per slot, compile-time exhaustiveness checks, and round-trip tests. It must not grep production source or assert that
a function body contains a field name.

The census is also an architectural feedback loop. When several operations repeat the same child recursion or
several representations repeat the same projection, the closure packet may introduce a shared visitor or an
explicitly named projection API. Consolidation is evidence-driven and local rather than a prerequisite rewrite.

### Surface adapters

One test-only coordinator drives adapters for the analyzer, fixture runner, bytecode serializer/loader, runtime
boundaries, reflection/proxies, reload/lifetime scenarios, completion, LSP, and refactoring. Adapters return a common
structured result containing outcome class, diagnostics, value/result identity, and relevant lifecycle events.

The existing `FSVerificationHarness` is not the coordinator. It is a `TOOLS_ENABLED` migration/refactoring facility
that stages editor source overrides and validates proposed edits. The completeness harness may call existing
refactoring or test utilities through an adapter, but its core belongs under `modules/foundry_script/tests/` and
must run in the normal test build.

### Findings ledger

A source-controlled findings ledger maps stable case IDs and rule families to their disposition, permanent tests,
GitHub issue, closure packet, and pull request. GitHub provides the workflow view; the repository data remains the
versioned source of truth from which coverage reports are generated.

Every failure has exactly one classification:

- Product defect.
- Missing or incorrect specification.
- Harness or generator defect.
- Intentional unsupported case requiring a rejection rule.
- Duplicate of an already catalogued finding.

Failures remain blocking until classified. A harness defect is fixed in the harness; it does not silently suppress
the affected cells. An intentional exclusion requires review of its rationale and a negative executable case.

## Execution Flow

For a deterministic run:

1. Load and validate the capability manifest and findings ledger.
2. Select the requested rule families and coverage strength.
3. Generate stable cases and scratch projects.
4. Execute the applicable adapters.
5. Evaluate explicit, metamorphic, and parity expectations.
6. Reduce each unexpected result when reduction is supported.
7. Emit human-readable failures and a structured JSON artifact.
8. Reconcile every failure against the ledger.
9. Emit coverage and disposition summaries.

Each unexpected result includes:

- Stable case ID and random seed, if any.
- Rule family and complete axis coordinates.
- Expanded source and project layout.
- Declared expectations and actual adapter results.
- Parser/analyzer diagnostics.
- Text and bytecode execution results where applicable.
- Lifecycle or tooling events involved.
- The smallest known reproducer and reduction history.

Fatal harness initialization errors fail the run without classifying product cells. Individual product crashes,
timeouts, malformed bytecode outcomes, or editor/LSP failures are case failures with retained artifacts. Scheduled
concurrency and lifecycle campaigns record their schedule or event sequence so they can be replayed.

## Audit and Burn-Down Workflow

### Closure packets

The unit of work is a semantic neighborhood, not a single reproduction. Examples include "runtime enforcement of
reified type parameters at every write boundary" and "`Self` traversal through every composite child slot."

Before implementation begins, reconnaissance expands a finding across:

- The same type feature at every applicable boundary.
- Adjacent type features at the failing boundary.
- Direct, wrapped, nested, and substituted forms.
- Text, bytecode, reload, lifetime, and tooling variants.
- Acceptance, rejection, runtime enforcement, and diagnostic behavior.

The issue records the resulting matrix slice and acceptance criteria. A defect discovered inside that slice expands
the current packet. A genuinely independent finding may be scheduled separately, but it must map to an existing
catalog entry or extend the catalog before the current pull request merges. Filing an unclassified follow-up is not
evidence that the current semantic family is closed.

Each closure packet requires:

- A failing executable case before the production fix.
- A neighborhood-expansion report.
- Text and bytecode coverage wherever runtime behavior applies.
- Census updates when a representation or recursive child policy changes.
- No unexplained generated failures in the owned slice.
- Ledger classification for every independently scoped finding.

### Workstreams

One umbrella epic owns five workstreams:

1. Type algebra and representation census.
2. Static semantic laws.
3. Runtime boundary enforcement.
4. Serialization, reload, lifetime, cache, and concurrency correctness.
5. Diagnostics and editor/LSP/refactoring parity.

Each workstream publishes its dependencies. Runtime boundary enforcement may depend on a settled semantic rule, but
it does not wait for unrelated completion support. This keeps closure packets parallelizable without allowing
surfaces to disappear from the program.

### Review contract

Every type-system pull request states the semantic law it establishes. Review verifies:

- Adjacent type shapes and value boundaries.
- Recursive child slots and projections.
- Static admission and runtime enforcement.
- Text/bytecode and direct/reflective/proxy parity.
- Serialization, reload, lifetime, or concurrency impact.
- Diagnostic and tooling consumers.
- Manifest, census, seed-family, and ledger changes.

The checklist is scoped by applicability; a pull request does not manufacture irrelevant tests merely to mark every
item complete.

## Test and Automation Strategy

### Presubmit gate

Pull requests run deterministic cases selected from the capabilities they change, all directly dependent
metamorphic relations, historical seeds for those families, and a compact cross-feature parity set. The selector is
based on manifest dependencies and registered capabilities, not source-text matching.

### Strict validation gate

The native SCons validation build runs the complete bounded deterministic matrix. This remains part of the required
pre-handoff validation through `scripts/agent_build.py`.

### Scheduled exploration

Scheduled campaigns run deeper compositions, configured higher-order combinations, randomized metamorphic cases,
lifecycle stress, and replayable concurrency schedules. Seeds and artifacts are retained. A newly minimized failure
is promoted to the deterministic suite when it represents a stable semantic class.

### Release gate

Release validation runs the deterministic matrix, all historical regression families, and every promoted scheduled
reproducer. Release reports include disposition and parity coverage rather than only pass/fail totals.

### Harness validation

The harness is tested by controlled fault injection or mutation. Representative faults include:

- Omit one composite child from a traversal.
- Skip a runtime boundary check.
- Erase a callable parameter, return, or rest slot.
- Perturb `Self` ownership during substitution.
- Reorder or collapse union alternatives incorrectly.
- Lose a type argument during serialization or reload.

The matrix must detect every historical escape class represented by the injected faults. These checks validate the
audit mechanism; ordinary source line coverage is secondary.

## Coverage Reporting

The generated report tracks:

- Matrix disposition coverage.
- Static/runtime boundary parity.
- Text/bytecode parity.
- Representation-child policy and behavioral coverage.
- Lifecycle and tooling adapter coverage.
- Historical regression-family coverage.
- Unclassified failures.
- Findings discovered during reconnaissance or scheduled exploration versus during implementation.
- Closure packets that generated in-slice follow-ups after implementation began.

The last two measures show whether discovery is moving earlier. A falling raw issue count is not sufficient if new
features bypass the model or failures remain unclassified.

## Feature Admission Gate

New type features and changes to existing type semantics may proceed during the stabilization program, but they must:

- Add or revise their capability-manifest rules.
- Extend the representation census for new child slots or projections.
- Declare applicable boundaries, execution/lifecycle modes, and tooling surfaces.
- Add explicit and metamorphic expectations.
- Pass presubmit and strict deterministic gates.
- Classify every newly exposed failure before merge.

The gate begins as an explicit review requirement and becomes mechanically enforced where compiled registries,
manifest dependencies, and behavioral tests can do so without brittle source inspection.

## Rollout

### Phase 1: Baseline and historical mining

- Create the umbrella epic, workstreams, catalog skeleton, and findings ledger.
- Convert recent type-system pull requests, open issues, deliberate scope notes, review findings, and existing tests
  into regression families and stable seeds.
- Record the initial equivalence partitions and intentional exclusions.

### Phase 2: Representation census

- Inventory semantic child slots across parser, runtime, weak/container, serialized, and editor representations.
- Classify the operations that traverse, substitute, project, render, serialize, or erase them.
- Add behavioral census coverage and identify targeted shared visitors or projection APIs.

### Phase 3: Destination-wrapper parity pilot

Prove the harness with this law:

> If a value is accepted by destination `T`, wrapping that destination as `T | U` preserves the result unless an
> explicit union rule changes it.

The pilot covers numeric conversions, `Self`, reified parameters, tuples, callables, arguments, returns,
assignments, reflective writes, proxies, and text/bytecode execution. It directly absorbs the newest regression
family while exercising every major harness concept.

### Phase 4: Soundness closure

- Audit assignability, equality, invariance, substitution, inference, narrowing, conversion, reification, receiver
  identity, and conformance.
- Apply the settled laws across all value-entry and value-exit boundaries.
- Prioritize silent admission holes, missing runtime checks, crashes, and corruption over diagnostic polish.

### Phase 5: Lifecycle and tooling closure

- Complete bytecode, serialization, reload, dependency lifetime, cache, proxy/reflection, and concurrency adapters.
- Close diagnostic rendering, attribution, completion, hover, signature, refactoring, documentation, and migration
  parity.

### Phase 6: Closure run and soak

- Regenerate the full bounded deterministic matrix from a clean state.
- Reconcile all failures and known issues with the ledger.
- Run fault-injection checks and scheduled exploration.
- Activate the permanent feature-admission gate.
- Begin a two-week soak under ordinary feature work.

A new defect during the soak does not automatically reset it. A finding that maps to an existing cell counts as an
implementation regression only if the existing deterministic or scheduled expectation already reproduces it. The
soak resets when expressing or detecting the finding requires a new axis, equivalence class, semantic rule, witness,
or oracle. That distinction tests the completeness of the model rather than claiming the implementation can never
regress.

## Closure Criteria

The stabilization epic closes only when:

- Every in-scope deterministic matrix cell has an executable expected disposition.
- Every semantic child slot in every inventoried representation has an explicit traversal, projection, preservation,
  substitution, erasure, or not-applicable policy.
- All deterministic presubmit, strict-validation, and release gates pass.
- Scheduled exploration has no unclassified failures.
- Every known in-scope typing issue and closure packet is closed; every excluded issue has an explicit program-boundary
  rationale.
- Fault injection demonstrates that the harness detects the historical omission and divergence classes.
- The feature-admission gate is active and documented.
- The two-week soak completes without requiring a new completeness axis, equivalence class, semantic rule, witness,
  or oracle.

After closure, the manifest, census, scheduled exploration, and admission gate remain permanent. The program ends;
the executable completeness contract does not.

## Risks and Mitigations

### Combinatorial explosion

Use semantic equivalence classes, bounded depth, deterministic pairwise coverage, and targeted higher-order
interactions. Promote only demonstrated high-risk combinations rather than increasing global strength blindly.

### Oracle duplication

Keep the manifest declarative, combine independent oracles, and validate the harness with injected faults. Do not
encode a general assignability algorithm in test data.

### False confidence from a green matrix

Report the declared boundaries and partition strength, retain randomized exploration, and reset the soak when a new
axis, equivalence class, semantic rule, witness, or oracle is required. Completeness claims always refer to the
versioned contract.

### Slow presubmit feedback

Select presubmit cases through manifest dependencies, run the full deterministic matrix in strict validation, and
reserve deep/random stress for scheduled campaigns. Generated cases use shared test infrastructure and scratch
space.

### A second stale tracking system

Keep the manifest and ledger in the repository, generate reports from them, and make GitHub issues reference stable
case IDs. Do not maintain an unrelated prose spreadsheet.

### Audit work blocking feature development

Allow new features behind the admission gate. Workstreams and closure packets remain independently schedulable, and
consolidation happens only where audit evidence justifies it.
