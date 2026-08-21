# Foundry Script Type-Representation Census

**Date:** 2026-08-21
**Status:** Inventory complete; family/adapter coverage validation pending #2477/#2478
**Issue:** #2476 (parent epic #2484)
**Design parent:** `docs/superpowers/specs/2026-08-20-foundry-script-type-system-completeness-design.md` ("Representation and walker census")

## Purpose

The census is the machine-readable inventory of every production Foundry Script type
representation, every semantic child slot in each, and the explicit handling policy for every
(child slot, surface) pair. It exists so that a change to a type representation fails a named
census cell instead of silently diverging on a neighboring surface, and so that the completeness
program can prove coverage per representation child rather than per feature.

The inventory lives in `modules/foundry_script/tests/type_completeness/census/`:

| File | Contents |
| --- | --- |
| `schema.json` | Closed vocabularies: policies, surfaces, axes, owners, transition kinds, witness kinds/statuses. |
| `representations.json` | The eight production representations, their live kind enums (ids and numeric values), and their semantic child slots. |
| `policies.json` | Exactly one policy entry for every (representation, child slot, surface) pair — 120 entries at schema version 1. |
| `transitions.json` | The thirteen erasure/reification/projection/substitution/preservation transitions between representations. |
| `unsupported.json` | Genuinely unsupported configurations, each with a production-anchor rationale and a witness (present or explicitly deferred). |
| `capability_reconciliation.json` | Which census production paths the capability map owns versus which are declared unmapped, with rationale and owning issue. |

The C++ validator `modules/foundry_script/tests/test_type_completeness_census.h`
(suite `[Modules][FoundryScript][TypeCompleteness][Census]`) executes the census against the live
engine: it enumerates the real C++ enums and rejects any divergence from the recorded kind
inventories, proves the policy matrix is exactly complete, resolves every witness reference
against the repository, and reconciles the declared production paths with
`type_completeness/capabilities.json`.

## Representations

### 1. `parser_data_type` — the analyzer's resolved record

`FSParser::DataType` (`modules/foundry_script/fs_parser.h:118`). This is the richest form: it
carries widths (`numeric_type`, `modules/foundry_script/fs_parser.h:166`), callable signatures
(`method_parameter_types`/`method_return_type`/`method_rest_parameter_type`,
`modules/foundry_script/fs_parser.h:214`–221), tagged-union payloads (`enum_case_payloads`,
`modules/foundry_script/fs_parser.h:239`), parameter bounds
(`modules/foundry_script/fs_parser.h:258`), and provenance flags
(`numeric_type_is_carrier_erased`, `is_substituted_self`, `is_receiver_self_contract`,
`is_raw_generic_projection`, `is_unresolved_inference_fallback`,
`modules/foundry_script/fs_parser.h:171`–203) that take no part in type identity.

Live kind enums recorded in the census:

- `FSParser::DataType::Kind` (`modules/foundry_script/fs_parser.h:122`) — 11 values, `BUILTIN`
  through `UNRESOLVED`.
- `FSParser::DataType::TypeParameterScope` (`modules/foundry_script/fs_parser.h:138`) — 4 values;
  note the parser has an `ENUM` scope the runtime form does not.
- `FSParser::DataType::TypeSource` (`modules/foundry_script/fs_parser.h:145`) — 4 values.
- `NumericType` (`core/variant/numeric_type.h:45`) — 9 descriptors; the `MAX` sentinel is a bound,
  not a descriptor, and is excluded.

Semantic child slots (9): `container_element_types`, `union_members`, `type_arguments`,
`type_parameter_bound`, `method_parameter_types`, `method_return_type`,
`method_rest_parameter_type`, `enum_case_payload_field_types`, `enum_case_payload_field_names`.

### 2. `runtime_data_type` — the compiled runtime descriptor

`FSDataType` (`modules/foundry_script/fs_function.h:78`). What every runtime boundary — `is`
tests (`is_type`, `modules/foundry_script/fs_function.h:230`), typed stores, member slots,
operands — actually asks. Live kind enums: `FSDataType::Kind`
(`modules/foundry_script/fs_function.h:85`, 8 values, with the documented wire constraint that
`UNION` must stay last, `modules/foundry_script/fs_function.h:82`),
`FSDataType::TypeParameterScope` (`modules/foundry_script/fs_function.h:106`), and `NumericType`.

Child slots (3): `container_element_types` (`:80`), `type_arguments` (`:133`),
`union_alternatives` (`:140`). Everything else the parser knew — enum identity, callable
signatures, bounds, payload field names — is erased here by design, and the census records those
erasures as explicit `erase` policies rather than as absence.

### 3. `container_descriptor` — the engine-level currency

`ContainerType` (`core/variant/container_type_validate.h:38`) and its validating twin
`ContainerTypeValidate` (`:59`). Child slots (2): `element_types` (`:45`) and `type_arguments`
(`:49`), deliberately separate vectors because element types describe container contents while
type arguments carry a script handle's reified generic arguments.

### 4. `bytecode_serialized_type` — the .fsb record

Written by `FSBytecodeExporter::encode_data_type` (`modules/foundry_script/fs_bytecode_export.cpp:340`),
rebuilt by `FSBytecodeLoader::decode_data_type` (`modules/foundry_script/fs_bytecode_loader.h:158`).
Child slots (3): the serialized element, type-argument, and union-alternative vectors
(`modules/foundry_script/fs_bytecode_export.cpp:380`–400). Script identity travels as
(path, fully qualified class name), never as a pointer.

### 5. `member_binding_descriptor` — runtime type-parameter resolution

`FoundryScript::TypeArgumentBinding` (`modules/foundry_script/foundry_script.h:169`) with kinds
`NONE`/`FIXED`/`OPEN` (`:170`), plus `MemberInfo::tuple_slot_shape` (`:207`). Child slots (2):
`fixed` (`:187`) — the only runtime form that keeps `TYPE_PARAMETER` nodes at any depth — and
`tuple_slot_shape`, the only input a reflective write has for a tuple member
(`modules/foundry_script/foundry_script.h:236`).

### 6. `reflection_property_info` — the PropertyInfo projection

`FSParser::DataType::to_property_info` (`modules/foundry_script/fs_parser.h:356`) and its inverse
`FSAnalyzer::type_from_property` (`modules/foundry_script/fs_analyzer.h:1010`); also serialized
per member (`modules/foundry_script/fs_bytecode_export.cpp:404`). It has no recursive type
children — it is the terminal projection most `project` policies point at, and the erasure
boundary most `erase` policies name.

### 7. `rendered_source_name` — the renderer

`FSDataType::get_source_type_name` (`modules/foundry_script/fs_function.h:149`) and the
analyzer-side `to_string`/`to_string_diagnostic` (`modules/foundry_script/fs_parser.h:333`,
`:338`). Lossless for widths, containers, generic arguments, tuples, and nullability by
construction; also no recursive children of its own.

### 8. `specialization_evidence` — what a value knows about itself

`FSRuntimeSpecializationEvidence` (`modules/foundry_script/fs_function.h:65`), filled from an
instance (`:71`) or a class handle (`:75`). Child slot (1): `evidence_type_arguments` (`:67`).

## Handling policies

Every (child slot, surface) pair carries exactly one of six policies, from the design doc's
census contract: `traverse`, `preserve`, `substitute`, `project`, `erase`, `not_applicable`
(the last requires a rationale). Surfaces distinguish analyzer, text runtime, bytecode runtime,
bytecode serialization/reload, reflection/proxy, and tools-only applicability, matching the
issue's requirement that those be told apart.

Two structural facts fall out of the matrix and are worth naming:

- **The union alternative set is the only child that both recurses and degrades.** It traverses
  on its own membership test on every runtime and serialization surface
  (`modules/foundry_script/fs_function.h:372`) and erases on reflection — "a union stays an
  unconstrained node everywhere except its own check" (`modules/foundry_script/fs_function.h:138`).
  This is the `union_destination_membership` family the completeness foundation already models.
- **`erase` is a policy, not a gap.** Fourteen cells are deliberate erasures (callable signatures at
  runtime, tagged-union payload names, bounds, widths across PropertyInfo). Each records the
  production anchor that justifies it, so a future change that makes one of these observable is a
  census change, not a silent behavior change.

## Transitions

The thirteen transitions in `transitions.json` make erasure and reification explicit and
bidirectional where the engine is bidirectional — notably
`runtime_type_to_property_info` (erase) paired with `property_info_to_parser_type` (reify, with
the `numeric_type_is_carrier_erased` provenance rule at `modules/foundry_script/fs_parser.h:171`
that keeps a reconstructed width from becoming a constraint), and
`runtime_type_to_serialized_record` paired with `serialized_record_to_runtime_type`.

## Unsupported configurations

Six entries in `unsupported.json`, each with a production-anchor rationale. Four have present
executable witnesses (union collapse normalization, static variables typed by open class type
parameters, bare generic extends, out-of-range/carrier-inconsistent width descriptors on the wire).
Two are explicitly `deferred` — the serialized kind-above-UNION rejection and the over-bound
Callable invocation — because a dedicated negative witness needs the #2477 adapters; the census
counts them as uncovered, never as passes.

## Capability reconciliation

The capability map (`type_completeness/capabilities.json`) currently lists six production paths.
Five census production paths are mapped; six are declared unmapped in
`capability_reconciliation.json` with rationale and owning issue (#2477 for selector mapping,
#2478 for serialization ownership). The validator fails in both directions: if a declared mapped
path drops out of the map, or a declared-unmapped path silently appears in it.

## Validation

`test_type_completeness_census.h` asserts, by executing code rather than reading source text:

1. **Live enum identity** — for every recorded `live_enum`, an exhaustive C++ enumeration of the
   real enum values matches the census ids and numeric values exactly, in both directions. A
   renumbered or appended enum value fails the census. The one exception is
   `FoundryScript::TypeArgumentBinding::Kind`, a private nested type the test cannot name; its
   integers are pinned by the bytecode-script accessor suite, so the census check for it is
   structural (unique ids, contiguous values from zero).
2. **Policy-matrix completeness** — the set of (representation, child slot, surface) entries in
   `policies.json` equals the cartesian product of representations' child slots and the surface
   vocabulary: no uncovered child, no orphaned entry, one policy each, all from the closed
   vocabulary, every `not_applicable` and every `erase` carrying a rationale.
3. **Reference resolution** — transition endpoints are inventoried representations; family
   references resolve against `type_completeness/rules/`; `fixture` witnesses exist on disk and
   `doctest_case` witnesses carry suite-qualified names (binding them to a `TEST_CASE`
   registration is executed by the adapters, not by source-text scanning).
4. **Capability reconciliation** — mapped declarations match `capabilities.json`; unmapped
   declarations do not.
5. **Surface applicability** — every representation distinguishes analyzer, text runtime, bytecode
   runtime, serialization, reflection, and tools-only applicability.

## Scope and deferrals

This change delivers the **inventory** portion of #2476: the complete representation, kind,
child-slot, policy, transition, unsupported, and capability-reconciliation data plus the
validator that keeps it honest against the live engine.

Deferred, per the issue's rollout dependencies:

- Final family/adapter coverage validation (every census cell observed behaviorally through the
  adapter coordinator) depends on gate-safe evidence from **#2477** and multi-family
  registration/declarative witnesses from **#2478**; neither is merged.
- The two `deferred` unsupported witnesses need #2477's negative adapters.
- Behavioral census coverage (constructed values with one distinctive child per slot, round-trip
  proof per policy) is Phase 2 work in the design's rollout and lands with the adapters.
