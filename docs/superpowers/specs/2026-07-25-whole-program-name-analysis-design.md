# Whole-Program Name Analysis Design

## Goal

Build the analysis-only contract for the `.fsb` phase-2 name mangler. Given the complete set of
compiled Foundry Script roots for an export, the pass classifies every surviving project identifier
as either safe to mangle or required to stay unchanged, explains every keep decision, and produces
one deterministic project-wide `old_name -> new_name` map.

This issue does not mutate a `FoundryScript`, rewrite an exported resource, parse a keep-rules file,
or expose an export-preset option. Those consumers arrive in #796-#799.

## Approaches Considered

### Compiled-graph analysis with typed external evidence (selected)

Walk `FoundryScript` and `FSFunction` objects after compilation and accept typed `KeepEvidence`
records for facts owned by another stage. The compiled graph is the same graph the `.fsb` writer
will serialize, so the pass sees the exact names that survive. A typed evidence seam lets #796 feed
annotation/file-rule matches and lets #798 feed scene/resource/animation references without
coupling their parsers to the core analysis.

This keeps the output directly reusable by #797 and makes the conservative boundary explicit.

### Parser/AST analysis

Walking every cached parse tree would distinguish enum declarations and source literals directly,
but it would introduce a second graph beside the compiled graph, require source-to-compiled identity
matching, and fail for compiled dependencies. It also couples the pass to front-end state that the
exporter otherwise does not serialize.

### Export-plugin analysis

Building the map inside `EditorExportFoundryScript` would make resource discovery convenient, but it
would mix policy, project traversal, compilation, diagnostics, mutation, and serialization in one
stateful plugin. It would also be hard to unit test without an end-to-end export.

## Public Contract

`FSNameManglerAnalysis` is a `TOOLS_ENABLED` utility in
`modules/foundry_script/fs_name_mangler_analysis.{h,cpp}`.

Its `Input` contains:

- `scripts`: the complete set of compiled root scripts selected for one export. Nested classes are
  discovered recursively. Root order is semantically irrelevant.
- `keep_evidence`: ordered records containing a name, a `KeepReason`, and a diagnostic detail.
  `SCENE_OR_RESOURCE` and `KEEP_RULE` are intentionally public reasons. #796 will add matches here;
  it will not need to modify the classifier.
- `complete_project_graph`: defaults to `true`. A caller that could not establish a closed export
  graph sets it to `false`; the pass then keeps every candidate as unprovable.

Its `Result` contains:

- classifications sorted by original name;
- each name's sorted identifier kinds, sorted/deduplicated keep evidence, and optional replacement;
- an `RBMap<StringName, StringName>` containing only mangled names;
- stable human-readable keep-log lines, one per kept name/reason pair.

The pass returns an empty successful result for an empty project. A null script root is invalid
input and returns `ERR_INVALID_PARAMETER` without a partial map.

## Candidate Collection

The walker visits each compiled class once, including nested classes, and collects these names:

- non-empty local/global class identifiers;
- current-class instance and static members, excluding compiler-only names beginning with `@`;
- member methods and enum-host methods, excluding compiler-only names beginning with `@`;
- declared signals;
- named enum/constant keys and enum-host table keys.

The compiled representation intentionally stores enum declarations in the class constant table.
The analysis therefore uses one `ENUM_OR_CONSTANT` kind for these surviving keys. This is the exact
surface #797 must rewrite; the distinction no longer changes the rename policy.

Path-bearing and composite identity records are not additional flat candidates. Script paths,
`fully_qualified_name` values, conformance target aliases, trait references, and generic
type-parameter names are observed so replacement allocation cannot collide with them. The later
#797 application pass must rewrite the mapped local/global class-name components inside structured
identities while preserving their path, namespace, nesting, and generic syntax. In particular, it
must not serialize an original terminal class segment merely because the complete FQCN is absent
from `rename_map`. Abstract trait-requirement keys and conformance witness-map keys are method
declarations, so they are `METHOD` candidates; unknown trait/target identity references instead
provide conservative external-boundary evidence.

If the same spelling appears in several classes or categories, it receives one aggregate
classification. A global keep decision wins over all candidate occurrences because the output map
is project-wide, not class-scoped.

## Keep Analysis

The pass records a candidate as kept when any of these facts applies:

- a typed input record identifies a scene/resource/animation reference;
- a typed input record identifies a future annotation or keep-rules match;
- the class RPC configuration contains the method name;
- the method overrides a virtual method on its native base;
- the name collides with another native or built-in API surface that a name-dispatched operation
  could target;
- an exact `String` or `StringName` constant contains the name;
- a `NodePath` constant contains the name as a property/subname segment. Ordinary node-name path
  segments do not by themselves identify script declarations and are not treated as name evidence;
- a `Callable`/`Signal` constant names it;
- any recursively nested Array, Dictionary, or packed-string constant contains it;
- reflection enumeration is used (`get_method_list`, `get_property_list`, `get_signal_list`, or the
  equivalent Foundry reflection enumeration APIs). Each API keeps the relevant declaration set
  rather than merely keeping the reflection API's own name: method enumeration keeps methods,
  property enumeration keeps members, and signal enumeration keeps signals;
- an external Foundry Script boundary exposes the same spelling;
- the caller reports an incomplete whole-project graph.

String and reflection discovery covers class constants, current static/default member values,
function constants/default arguments, annotation arguments, and nested lambdas.

External bases and script constants are not silently pulled into the rename domain. Their public
surfaces become external evidence; only scripts explicitly present in `Input.scripts` can contribute
mangle candidates.

## Deterministic Mapping

Candidate names are sorted by Unicode string order before allocation. Kept names never enter the
map. Remaining names receive `_fsb_<base36 ordinal>`, starting at zero. The allocator skips any
generated spelling already observed anywhere in the compiled program or input evidence.

Consequently, repeated analysis of the same program produces byte-for-byte equivalent ordered
classifications, logs, and maps regardless of hash-table or input-root iteration order.

## Diagnostics

Keep evidence is sorted first by `KeepReason` and then by detail. Log lines use stable reason labels
and include the detail when present, for example:

`Keeping "scene_handler": scene/resource reference (res://main.tscn connection method).`

The pass returns diagnostics instead of printing directly. #799 can route them through export
messages without making the reusable analysis noisy in tests or editor sessions.

## Testing

Focused doctest coverage compiles real Foundry Script fixtures and proves:

1. private class/member/method/signal/enum-or-constant names are candidates while scene/resource,
   RPC, native-virtual, and string-literal names are kept with exact reasons;
2. root order and repeated runs produce the same collision-free mapping;
3. a keep-rules evidence record keeps every occurrence of a shared spelling, proving the #796 seam;
4. reflection enumeration and external/incomplete graph boundaries conservatively keep names;
5. recursive Variant constants and nested lambdas contribute string evidence.

The relevant bytecode tests and the full Foundry Script test family run after the focused suite.

## Non-Goals

- `@keep_name` grammar or metadata;
- keep-rules file parsing or glob matching;
- mutating compiled classes/functions or `.fsb` buffers;
- rewriting `.tscn`, `.tres`, `.res`, animation, or connection data;
- export preset settings or editor UI;
- the final integrated mangling corpus and leak assertions.
