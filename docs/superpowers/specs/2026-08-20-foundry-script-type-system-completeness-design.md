# Foundry Script Type-System Completeness Program

**Date:** 2026-08-20
**Revised:** 2026-08-20
**Status:** Revised — review requested

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
surface has been covered. Its headline success measure is the share of findings discovered during reconnaissance or
scheduled exploration instead of during implementation: the program succeeds when discovery moves before the fix.
It must not promise the impossible claim that no future bug can exist.

## Goals

- Define a finite, auditable completeness contract for Foundry Script typing.
- Cover language semantics, runtime enforcement, diagnostics, editor/LSP behavior, serialization, reload, lifetime,
  cache invalidation, and concurrent analysis where these affect correctness.
- Give every in-scope type-system combination a declared or relation-derived executable expected disposition.
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
- Do not promise deterministic replay of real-thread concurrency without a separately designed interleaving harness.
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
Whether an expectation is declared directly or derived from a declared anchor does not change this two-disposition
rule.

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

### How expectations attach to cells

Expectations attach in two ways:

- A **declared disposition** is a hand-authored anchor with concrete coordinates, witnesses, and expected observable
  behavior. Every rule family has at least one declared anchor for each applicable surface, and every distinct
  outcome class in the family has at least one declared witness.
- A **derived disposition** applies a named relation to one or more declared anchors. It may preserve or apply a
  fixed transformation to selected observable dimensions, such as preserving static acceptance while adding a
  runtime membership obligation.

Relations use bounded predicates over normalized axis coordinates and anchor results. A predicate may compare an
axis to named values, test membership in a named equivalence class, and combine those checks with conjunction or
disjunction. It may not inspect generated source or ASTs, call production type-relation code, recurse over arbitrary
types, or synthesize a new outcome algorithm. This is deliberately a small, scoped decision procedure rather than a
second type checker.

Manifest validation rejects a cell with no matching expectation, more than one equally specific expectation, an
unanchored derived expectation, or incompatible outcomes from overlapping relations. The report distinguishes
declared and derived coverage so a large derived region cannot hide a shortage of independent anchors.

### Metamorphic exceptions

An exception is a first-class manifest record, never prose appended to a relation. It carries its own stable
identity, parent relation, axis predicate, rationale, fixed outcome transformation, declared witnesses, and affected
surfaces. Exceptions participate in overlap validation, coverage reports, review, case-ID migration, and scheduled
mutation validation exactly like ordinary rules.

Every exception has at least one positive witness proving that the exceptional behavior occurs and one boundary
witness proving that a neighboring ordinary cell still follows the parent relation. Adding or widening an exception
is a specification change. Free-form clauses such as "unless union semantics intervene" have no executable meaning
and are forbidden.

## Architecture

### Capability manifest

A source-controlled, declarative capability manifest is the program's semantic inventory. It is stored as one JSON
file per rule family under a dedicated test-matrix directory so parallel closure packets do not edit one central
file. Each rule records:

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

The manifest describes declared anchors, bounded relations, first-class exceptions, and expected outcomes. Its
coordinate predicates implement the limited decision procedure defined above; it does not contain an algorithm
capable of deciding arbitrary Foundry Script types. Independent anchors, relation cross-checks, and scheduled
mutation campaigns keep the scoped predicates honest.

Stable case IDs are derived from semantic coordinates, not generation order. Reports, permanent regressions, GitHub
issues, and pull requests use those IDs so a case remains traceable when the generator changes.

A repartition may split or merge semantic coordinates, so it ships an ID-migration file beside the rule-family
files. Migrations map an old ID to one or more replacement IDs and record the reason. Validation rejects alias cycles,
orphaned ledger references, a removed ID with no migration, and a replacement ID that does not exist. Reports resolve
old IDs while displaying their current replacements; migration records remain permanent so external issue and pull
request references do not rot.

### Worked rule before framework expansion

Before general generator tooling is built, Phase 1 implements one real rule-family file and runs it end to end. The
initial family models the union destination membership work from #2469 and the provable numeric-constant parity from
#2471. The following JSON is illustrative of the required semantics; the implementation plan may refine field names
without weakening the declared/derived/exception constraints:

```json
{
  "family": "union_destination_membership",
  "anchors": [
    {
      "id": "plain_static_member",
      "coordinates": { "destination": "plain", "source_proof": "static_member" },
      "expect": { "analysis": "accept", "runtime_obligation": "none" },
      "surfaces": ["text", "bytecode"]
    },
    {
      "id": "plain_unproven",
      "coordinates": { "destination": "plain", "source_proof": "unproven" },
      "expect": { "analysis": "accept", "runtime_obligation": "typed_destination_check" },
      "surfaces": ["text", "bytecode"]
    },
    {
      "id": "union_numeric_constant",
      "coordinates": { "destination": "union", "source_proof": "numeric_constant" },
      "expect": { "analysis": "accept", "stored_carrier": "admitting_alternative" },
      "historical_issue": 2442
    }
  ],
  "relations": [
    {
      "id": "union_wrapper_parity",
      "from": { "destination": "plain" },
      "to": { "destination": "union" },
      "derive": { "analysis": "same", "runtime_obligation": "same" }
    }
  ],
  "exceptions": [
    {
      "id": "unproven_source_requires_membership",
      "parent": "union_wrapper_parity",
      "when": { "source_proof": ["gradual", "erased", "variant"] },
      "derive": { "analysis": "same", "runtime_obligation": "union_membership_check" },
      "rationale": "A union destination must prove membership when the source cannot.",
      "witnesses": {
        "positive": ["text_gradual_parameter", "bytecode_erased_reflective_write"],
        "boundary": ["text_static_member_parameter"]
      }
    }
  ]
}
```

This example makes the pilot precise: wrapper parity applies to static admission, while the union-specific runtime
membership obligation is an audited transformation with its own text and bytecode witnesses. The numeric-constant
anchor proves that #2471 restores admission parity and preserves the carrier chosen by the admitting alternative.

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

Every matrix invocation owns a process-unique scratch root, and every generated case gets a child named by stable
case ID plus an invocation nonce. The harness relies on the process-unique `user://` policy that closed #2100, but it
still gives spawned tools and editor/LSP subprocesses distinct child roots. A case must not reuse another case's
`project.foundry`, autoload index, editor cache, FSCache entries, global-class registrations, or conformance state.
Successful cases clean their children; failing cases retain them and report their exact paths. No generated case may
write into the tracked fixture tree.

### Independent oracles

The harness combines several oracles. A cell need not use every oracle, but no broad rule family should rely on only
one.

#### Explicit semantic expectations

The manifest declares observable acceptance, rejection, conversion, runtime-check, warning, and execution results.
These expectations are appropriate for settled language rules and intentional exclusions.

#### Metamorphic relations

Metamorphic rules compare related programs without needing an independent full type checker. Examples include:

- Wrapping a destination `T` as `T | U` preserves declared observable dimensions; any changed dimension, such as an
  added runtime membership obligation, is a first-class exception with executable witnesses.
- Replacing a type parameter with a concrete argument produces the same result as spelling that concrete type
  directly across the relation's declared ownership coordinates; owner-changing coordinates use named exceptions.
- Moving the same declared slot between equivalent value boundaries preserves static and runtime enforcement.
- A census-declared lossless renderer followed by its supported parse/serialization round trip preserves semantic
  identity. A census-declared lossy renderer instead obeys monotonicity: it may erase declared information but never
  invent type information.
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

The strict editor build exercises every tooling adapter. Template and non-tools builds exercise only their applicable
parser, runtime, bytecode, and serialization adapters; tools-only cells are reported as not covered by that
configuration, never as passes or silent skips. Coverage artifacts name the build configuration and enabled adapters
so a green non-tools run cannot be mistaken for tooling parity evidence.

### Findings ledger

A source-controlled findings-ledger directory maps stable case IDs and rule families to their disposition, permanent
tests, GitHub issue, closure packet, and pull request. Each finding is a separate JSON file named by finding ID; no
closure packet edits a shared ledger file. The report generator validates and aggregates the directory. GitHub
provides the workflow view; the repository data remains the versioned source of truth.

Every failure has exactly one classification:

- Product defect.
- Missing or incorrect specification.
- Harness or generator defect.
- Intentional unsupported case requiring a rejection rule.
- Duplicate of an already catalogued finding.

Classification is an execution status, not a third expected disposition. A product-defect entry records the intended
supported or intentionally unsupported disposition and the observed mismatch while the fix is pending. A harness
defect is fixed in the harness; it does not silently suppress affected cells. An intentional exclusion requires
review of its rationale and a negative executable case.

Blocking scope is decided by the gate that found the failure:

- **Presubmit:** A failure introduced or worsened by the branch blocks that pull request. An unclassified failure in
  the changed capability slice blocks only until it is classified. If it reproduces unchanged on `develop` and lies
  outside the pull request's closure packet, automation opens a provisional finding with a two-business-day
  classification deadline and the pull request may proceed; if it lies inside the owned packet, the packet expands.
  A failure outside the changed slice does not transfer ownership to the pull-request author.
- **Strict handoff:** Branch-introduced or worsened deterministic failures block handoff. Known `develop` mismatches
  remain visible in the report but do not make unrelated branches newly responsible for them.
- **Scheduled exploration:** New failures do not stop daily merges. They require provisional ledger entries and
  classification within two business days, and they block the release gate and epic closure until classified.
- **Release:** Unclassified failures and confirmed soundness or semantic-correctness mismatches block release.
  Named cosmetic-quality deferrals follow the closure-severity policy below.
- **Epic closure:** No unclassified failure or unresolved in-scope mismatch may remain.

Baseline comparison never turns a known mismatch into a passing cell; it only scopes which workflow is blocked while
the stabilization campaign repairs it.

## Execution Flow

For a deterministic run:

1. Load and validate all rule-family, finding, and ID-migration files; reject aliases, references, or mappings that do
   not resolve.
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
concurrency campaigns retain seeds, inputs, timing, thread/event traces, logs, and scratch state for best-effort
reproduction; they do not promise deterministic schedule replay. Any future controlled-interleaving or cooperative
scheduler harness requires its own approved design before it becomes a completeness dependency.

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
- Declared anchors, derived predicates, and every added or widened exception.
- Manifest, census, seed-family, and ledger changes.

The checklist is scoped by applicability; a pull request does not manufacture irrelevant tests merely to mark every
item complete.

## Test and Automation Strategy

### Presubmit gate

Pull requests run deterministic cases selected from the capabilities they change, all directly dependent
metamorphic relations, historical seeds for those families, and a compact cross-feature parity set. The selector is
based on manifest dependencies and registered capabilities, not source-text matching. The matrix portion targets a
90-second p95 wall time after binary launch on the reference four-job agent environment.

The selector owns an explicit production-path-to-capability map. An unmapped production change under
`modules/foundry_script/` runs the broad core presubmit slice and fails selector validation until the path is mapped or
declared irrelevant. Manifest validation also proves that every rule family is reachable from at least one path
mapping. This fail-closed fallback makes mapping rot noisy rather than silently reducing coverage.

### Strict validation gate

The native SCons validation build runs a budgeted deterministic matrix as part of required pre-handoff validation
through `scripts/agent_build.py`. The matrix portion has a five-minute p95 wall-time budget after binary launch on
the reference four-job agent environment, separate from build time. Reports record total and per-rule/adapter
durations.

The strict tier always retains, in order:

1. Every declared anchor and exception witness.
2. Every historical soundness regression.
3. At least one case for each rule family, applicable surface, and expected outcome class.
4. The complete changed-capability slice for the branch under validation.

If the budget is exceeded, only redundant pairwise/higher-order combinations move to scheduled shards. Unique
semantic coverage above may not be demoted. A persistent budget breach is a gate failure requiring harness
optimization, additional deterministic CI shards, or an explicit design amendment; it is not solved by silently
dropping a rule family.

### Scheduled exploration

Scheduled campaigns run the complete bounded deterministic matrix in workstream shards, deeper compositions,
configured higher-order combinations, randomized metamorphic cases, lifecycle stress, and concurrency stress. Each
shard targets at most 30 minutes. Seeds and artifacts are retained. A newly minimized failure is promoted to the
strict tier when it represents a distinct semantic class; redundant combinations remain scheduled.

### Release gate

Release validation runs the complete bounded deterministic matrix in shards, all historical regression families,
and every promoted scheduled reproducer. Release reports include disposition, declared/derived/exception, parity,
configuration, and runtime coverage rather than only pass/fail totals.

### Harness validation

Phase 1 creates a finite mutation catalog from historical escape classes. Each entry owns an ephemeral patch recipe,
the rule families it should disturb, and the exact case IDs expected to detect it. Scheduled mutation jobs apply one
recipe in a disposable worktree, use the shared compiler cache, build the affected configuration, and run the named
detectors. They do not add production test hooks and do not run in presubmit. A recipe that no longer applies is an
infrastructure failure requiring migration or replacement, not a successful detection.

Representative catalog entries include:

- Omit one composite child from a traversal.
- Skip a runtime boundary check.
- Erase a callable parameter, return, or rest slot.
- Perturb `Self` ownership during substitution.
- Reorder or collapse union alternatives incorrectly.
- Lose a type argument during serialization or reload.

The matrix must detect every historical escape class listed in the Phase 1 mutation catalog. Mutation shards target
at most 30 minutes and run on a schedule and during the final closure campaign. These checks validate the audit
mechanism; ordinary source line coverage is secondary.

## Coverage Reporting

The generated report tracks:

- Matrix disposition coverage.
- Declared-anchor, derived-relation, and first-class-exception coverage.
- Static/runtime boundary parity.
- Text/bytecode parity.
- Representation-child policy and behavioral coverage.
- Lifecycle and tooling adapter coverage.
- Historical regression-family coverage.
- Unclassified failures.
- Findings discovered during reconnaissance or scheduled exploration versus during implementation.
- Closure packets that generated in-slice follow-ups after implementation began.
- Presubmit-selector mapping reachability and conservative-fallback activations.
- Matrix wall time by gate, shard, rule family, and adapter.

The last two measures show whether discovery is moving earlier. A falling raw issue count is not sufficient if new
features bypass the model or failures remain unclassified.

## Feature Admission Gate

New type features and changes to existing type semantics may proceed during the stabilization program, but they must:

- Add or revise their capability-manifest rules.
- Extend the representation census for new child slots or projections.
- Declare applicable boundaries, execution/lifecycle modes, and tooling surfaces.
- Add declared anchors, derived relations, and first-class exception witnesses as applicable.
- Update the production-path capability map and any case-ID migrations.
- Pass presubmit and strict deterministic gates.
- Classify every newly exposed failure before merge.

The gate begins as an explicit review requirement and becomes mechanically enforced where compiled registries,
manifest dependencies, and behavioral tests can do so without brittle source inspection.

## Rollout

### Phase 1: Baseline and historical mining

- Create the umbrella epic, workstreams, per-family rule directory, per-finding ledger directory, ID-migration
  directory, capability map, and report skeleton.
- Implement the union destination membership worked rule end to end before generalizing the manifest or generator.
- Convert recent type-system pull requests, open issues, deliberate scope notes, review findings, and existing tests
  into regression families and stable seeds. Execute every mined witness against current `develop` first and record
  `still_failing`, `fixed`, or `premise_false`; do not transcribe an issue's claimed behavior into the catalog.
- Build the finite mutation catalog and detector mapping from the reverified historical families.
- Record the initial equivalence partitions and intentional exclusions.

### Phase 2: Representation census

- Inventory semantic child slots across parser, runtime, weak/container, serialized, and editor representations.
- Classify the operations that traverse, substitute, project, render, serialize, or erase them.
- Add behavioral census coverage and identify targeted shared visitors or projection APIs.

### Phase 3: Destination-wrapper parity pilot

Prove the harness with this law:

> Wrapping destination `T` as `T | U` preserves each observable dimension named by the wrapper-parity relation; every
> changed dimension is a first-class exception with declared witnesses.

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
- Run the scheduled mutation catalog and exploration campaigns.
- Activate the permanent feature-admission gate.
- Begin a two-week soak under ordinary feature work.

A new defect during the soak does not automatically reset it. A finding that maps to an existing cell counts as an
implementation regression only if the existing deterministic or scheduled expectation already reproduces it. The
soak resets when expressing or detecting the finding requires a new axis, equivalence class, semantic rule, witness,
or oracle. Every soak-window classification requires review by someone other than the finding or closure-packet
author. A no-reset classification records the pre-existing reproducing case ID; validation rejects the classification
if that case does not reproduce the finding. This tests the completeness of the model rather than claiming the
implementation can never regress.

## Closure Severity Policy

The epic distinguishes correctness from optional polish without weakening tooling parity:

- **Soundness and safety:** Invalid admission, missing runtime enforcement, crashes, corruption, stale/lifetime
  hazards, and unsafe concurrency behavior. Every such issue must close.
- **Semantic correctness and parity:** False rejection, wrong inference/substitution/conformance, wrong type identity,
  inconsistent text/bytecode or boundary behavior, and editor/LSP/refactoring behavior that reports or acts on the
  wrong semantic type. Every such issue must close.
- **Quality polish:** Behavior-preserving diagnostic phrasing or layout, cosmetic tooling presentation, and
  performance that does not affect correctness or gate feasibility. These may be excluded only under the named
  `quality_deferred` category with rationale and aggregate reporting; they do not receive ad hoc per-issue scope
  exceptions.

## Closure Criteria

The stabilization epic closes only when:

- Every in-scope deterministic matrix cell has an executable expected disposition.
- Every semantic child slot in every inventoried representation has an explicit traversal, projection, preservation,
  substitution, erasure, or not-applicable policy.
- All deterministic presubmit, strict-validation, and sharded release gates pass within their declared budgets.
- Scheduled exploration has no unclassified failures.
- Every known soundness, safety, semantic-correctness, and semantic-parity issue and closure packet is closed. Any
  remaining quality-polish issue is reported under `quality_deferred` with its rationale.
- Scheduled mutation jobs demonstrate that the harness detects every entry in the reverified historical mutation
  catalog.
- Every case-ID reference resolves directly or through a validated migration record.
- The feature-admission gate is active and documented.
- The two-week soak completes without requiring a new completeness axis, equivalence class, semantic rule, witness,
  or oracle, with every soak classification independently reviewed.

After closure, the manifest, census, scheduled exploration, and admission gate remain permanent. The program ends;
the executable completeness contract does not.

## Risks and Mitigations

### Combinatorial explosion

Use semantic equivalence classes, bounded depth, deterministic pairwise coverage, and targeted higher-order
interactions. Promote only demonstrated high-risk combinations rather than increasing global strength blindly.

### Oracle duplication

Limit derived predicates to normalized coordinates and fixed outcome transformations, require independent declared
anchors per surface and outcome, combine independent oracles, and validate the harness with scheduled mutations. Do
not encode a general assignability algorithm in test data.

### Exception creep

Represent every exception as a validated manifest entry with boundary witnesses and report exception counts and
coverage separately. An exception cannot be added as prose or used to suppress a failing cell.

### False confidence from a green matrix

Report the declared boundaries and partition strength, retain randomized exploration, and reset the soak when a new
axis, equivalence class, semantic rule, witness, or oracle is required. Completeness claims always refer to the
versioned contract.

### Gate runtime growth

Enforce the 90-second presubmit, five-minute strict, and 30-minute scheduled-shard budgets. Preserve declared anchors,
exceptions, historical soundness cases, and one case per rule/surface/outcome in strict validation; move only
redundant combinatorial depth to scheduled shards. Track runtime as a first-class coverage metric.

### A second stale tracking system

Keep per-family rules, per-finding ledger entries, and ID migrations in the repository; generate reports from them
and make GitHub issues reference stable case IDs. Do not maintain an unrelated prose spreadsheet or central
merge-conflict hotspot.

### Mutation cost and brittleness

Run patch-based mutations only in disposable scheduled worktrees with shared compiler caches and finite historical
detector mappings. Treat a stale patch recipe as infrastructure failure and migrate or replace it explicitly.

### Audit work blocking feature development

Allow new features behind the admission gate. Workstreams and closure packets remain independently schedulable, and
consolidation happens only where audit evidence justifies it.
