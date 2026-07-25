# Qualified Global Enum Compilation Design

## Context

Foundry Script accepts a fully qualified namespaced `enum_name` in type
annotations and resolves its values and host-function signatures. Compilation
still fails when the qualified enum metatype is needed as a runtime expression,
for example as the receiver of a static enum function:

```fs
var status: qualified_enum.repro.Status = qualified_enum.repro.Status.READY
print(qualified_enum.repro.Status.parse("ready").label())
```

`FSAnalyzer::reduce_subscript()` has a namespace-chain fast path. For ordinary
global classes, it marks the exact class-prefix expression with
`resolved_global_class`, allowing `FSCompiler::_parse_expression()` to emit the
class object without looking up the namespace root as an ordinary identifier.
For global enums, the analyzer resolves only the enum datatype. The prefix
expression is left non-constant, so the compiler recursively reaches
`qualified_enum` and reports `Identifier not found`.

## Decision

The analyzer will materialize the exact fully qualified global-enum prefix as
the same read-only enum `Dictionary` constant already used for bare and
imported global-enum names.

The namespace resolver remains the sole owner of dotted-name and ambiguity
resolution. The compiler will not rediscover namespace chains, and the AST
will not gain a second global-enum identity marker. Existing enum-call metadata
continues to carry the stable owner script, owner class, enum type, function
name, and static/instance kind used by code generation and the VM.

## Analyzer Data Flow

When the namespace-chain fast path identifies a global enum:

1. Resolve the enum datatype from the registered global class path.
2. Find the sub-expression spanning exactly the registered dotted enum name.
3. Annotate that prefix with the enum datatype.
4. Mark the prefix constant and store the existing read-only enum dictionary.
5. Continue reducing any trailing enum value or function member against the
   resolved datatype.

This matches the representation produced by
`set_enum_meta_identifier_constant()` for short names. A fully qualified enum
value can therefore constant-fold to its integer, while a static host call has
a valid dictionary receiver address. Static enum dispatch still uses the
analyzer's enum-call metadata; the dictionary does not become a dispatch key.

## Compatibility and Error Handling

- Imported short-name global enums keep their current path.
- Fully qualified ordinary global classes keep
  `resolved_global_class` emission.
- Local/member/native names continue to shadow namespace roots because the
  existing `is_namespace_chain_root_shadowed()` gate is unchanged.
- Namespace ambiguity remains an analyzer diagnostic from
  `get_namespace_global_class_from_type_chain()`.
- No grammar, bytecode format, opcode, or runtime storage changes are needed.

## Tests

Add an isolated runtime fixture pair:

- a namespaced `enum_name` provider with one instance and one static function;
- a consumer that uses the fully qualified spelling in a type annotation,
  value expression, instance call, and static call;
- the same consumer also imports the namespace and exercises the short enum
  name as a regression guard.

The standard source-runtime fixture pass proves direct compilation and
execution. The compiled-bytecode fixture pass proves the caller compiles,
serializes, loads, and dispatches against the provider. Existing focused tests
for qualified global classes and namespace resolution remain in the regression
set.

