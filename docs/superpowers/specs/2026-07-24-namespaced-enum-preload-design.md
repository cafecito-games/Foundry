# Namespaced Global Enum Preload Design

## Goal

Allow `preload()` to resolve a namespaced global `enum_name` script as a script
metatype while preserving existing namespace, enum identity, class, and nested
class resolution behavior.

## Root Cause

`FSAnalyzer::type_from_variant()` raises the preloaded script's parser and asks
`FSParser::find_class()` for the runtime script's `fully_qualified_name`.
`enum_name` assigns both the parser head `fqcn` and runtime script identity the
dotted global name, such as `enum_preload.repro.Status`.

`find_class()` recognizes the parser head through its short identifier or
script path and recognizes nested classes through `::` segments. It does not
recognize the parser head's exact `fqcn`. A namespaced global name therefore
falls through even though it identifies the raised parser's head exactly.

## Selected Design

Treat an exact `p_qualified_name == head->fqcn` match as the parser head at the
start of `FSParser::find_class()`. Keep the existing short-name, script-path,
and nested-class branches unchanged.

This is the canonical identity boundary: the parser owns the mapping from a
class identity to its AST node. It avoids an enum-only exception in
`type_from_variant()` and naturally applies to any parser head whose global
identity includes a namespace.

## Alternatives Rejected

- Special-case `enum_file_decl` in `type_from_variant()`. This would fix the
  current symptom but duplicate parser identity knowledge in the analyzer.
- Reconstruct the lookup from `ScriptServer` namespace metadata. This adds
  registry coupling even though the raised parser already has the exact
  identity needed for the lookup.

## Regression Coverage

Add an isolated runtime fixture whose provider declares a namespaced global
enum and whose consumer imports the namespace, preloads the provider, and uses
the resulting script metatype together with the imported enum type.

The existing unnamespaced external enum-host fixture remains the comparison
case and already preloads its provider. The fixture runner executes both source
compilation and compiled-bytecode caller round trips. Existing ordinary class
preload fixtures remain in the focused Foundry Script suite as guards against
unrelated lookup regressions.

No grammar or bytecode format changes are required.

## Error Handling

Only exact equality with the parser head `fqcn` gains a new success path.
Unknown dotted identities and malformed nested paths continue to return
`nullptr`, preserving the analyzer's existing `Could not resolve script`
diagnostic.

## Validation

- Demonstrate the new namespaced fixture fails with the current binary because
  the preload cannot resolve the provider script.
- Rebuild after the one-line lookup fix and run the namespaced and unnamespaced
  enum-host fixtures through source and compiled-bytecode paths.
- Run focused parser/analyzer/runtime tests, a strict `dev_mode=yes` build, and
  the full structured suite.
