# Name Mangler Keep Escape Hatches

## Goal

Issue #796 adds two explicit ways to keep compiled Foundry Script declaration names out of the
whole-program rename map from #795:

- a built-in `@keep_name` annotation on supported declarations; and
- a project-level, ProGuard-style keep-rules file with deterministic glob matching.

This stage classifies names and emits typed `FSNameManglerAnalysis::KEEP_RULE` evidence. It does not
mutate compiled graphs, rename `.fsb` contents, rewrite resources, add export-preset settings, or
orchestrate export. The later #797 and #799 stages consume the API and classification produced here.

## `@keep_name`

`@keep_name` is a zero-argument built-in annotation valid on:

- classes and traits, including the implicit root class;
- member variables, including static variables;
- methods, including enum-hosted methods;
- signals;
- constants; and
- named enum declarations, which use the parser's existing constant-like declaration surface.

The parser registers the annotation for `CLASS | VARIABLE | FUNCTION | SIGNAL | CONSTANT`. Enum
declarations accept that existing `CONSTANT` target solely so a valid annotation can be attached to
their existing `Node::annotations` list. This is not a general expansion of enum annotations: enum
values remain unsupported targets, custom-annotation targeting is unchanged, and no other parser
behavior changes.

The compiler's existing passive annotation pipeline stores the built-in usage as
`FoundryScript::AnnotationUsage { name = "keep_name", qualified_name = "keep_name",
is_builtin = true }`. Named enum usages are stored in `constant_annotations` under the enum name;
the other targets already have annotation metadata tables. The existing bytecode writer and loader
serialize these tables, so no new `.fsb` section or parallel metadata channel is needed.

`FSNameManglerAnalysis` reads the compiled metadata while collecting each declaration. A matching
built-in usage adds `KEEP_RULE` evidence with a stable detail naming `@keep_name` and the qualified
declaration. A class usage adds evidence only for that class's atomic `local_name`; its registered
global and fully-qualified spellings remain structured match/diagnostic identities and never become
flat analysis-map keys. The analysis map is intentionally atomic and project-wide: evidence on one
qualified declaration therefore keeps the same local-name spelling wherever it occurs in the closed
input graph.

The built-in registration automatically feeds the existing annotation completion enumeration. The
normative grammar's built-in annotation table and `@FoundryScript` class documentation describe the
new target set and purpose.

## Keep-rules file

### Supported syntax

The format is a documented, deliberately small ProGuard-style subset:

```text
# Keep one class declaration.
-keep class game.Player

# Keep the class and selected members.
-keep class game.** {
    Dynamic*;
    State?;
}

# Keep selected members, but not the class declaration itself.
-keepclassmembers class res://actors/** {
    external_*;
}
```

Blank lines and `#` comments, including trailing comments, are ignored. A directive header occupies
one line. An opening `{`, when present, must end that header. Each member glob occupies one line and
ends in `;`. A closing `}` occupies its own line.

The supported directives are:

- `-keep class <class-glob>`: keep matching class declaration identifiers.
- `-keep class <class-glob> { ... }`: keep matching class identifiers and members selected by the
  block.
- `-keepclassmembers class <class-glob> { ... }`: keep only members selected by the block.

Member entries are atomic declaration-name globs. Types, return types, argument lists, modifiers,
negation, comma-separated entries, inline one-line blocks, and other ProGuard directives are not
supported. The parser reports those forms as actionable errors rather than accepting or silently
ignoring them.

### Canonical class identities

Class globs match `FoundryScript::fully_qualified_name`, which has three tested forms:

- an anonymous root script: canonical resource path, for example `res://actors/player.fs`;
- a namespace-qualified global class: dotted name, for example `game.actors.Player`; and
- a nested class: its owner's identity plus `::`, for example
  `game.actors.Player::Inventory`, or `res://actors/player.fs::Inventory` for an anonymous root.

Member globs match only an atomic member, method, signal, constant, or named-enum identifier, never a
concatenated `Class.member` string.

### Glob semantics

Matching is case-sensitive and covers the complete string.

- `?` matches exactly one character other than a class-identity separator.
- `*` matches zero or more characters other than a class-identity separator.
- `**` matches zero or more characters, including separators.

The class-identity separators are `/`, `.`, and `:`. Treating `:` as a separator makes both
characters of `::` boundaries; it also prevents a single `*` from crossing the `:` characters in
the `res://` scheme. Member names are atomic and cannot contain these separators, so `*` and `**`
have the same reach in member patterns while retaining the documented syntax.

The implementation uses a deterministic matcher rather than platform path-globbing or
`String::match`, whose dot-only wildcard boundary does not cover resource and nested-class
identities.

## API and data flow

A TOOLS-only `FSNameManglerKeepRules` component owns parsing, loading, matching, and evidence
application:

```cpp
class FSNameManglerKeepRules {
public:
    enum DiagnosticSeverity {
        DIAGNOSTIC_WARNING,
        DIAGNOSTIC_ERROR,
    };

    struct Diagnostic {
        DiagnosticSeverity severity;
        String source;
        int line;
        String message;

        String format() const;
    };

    static Error parse(const String &p_text, const String &p_source,
            FSNameManglerKeepRules &r_rules, Vector<Diagnostic> &r_diagnostics);
    static Error load(const String &p_path, FSNameManglerKeepRules &r_rules,
            Vector<Diagnostic> &r_diagnostics);

    Error apply_to_input(FSNameManglerAnalysis::Input &r_input,
            Vector<Diagnostic> &r_diagnostics) const;
};
```

`parse` supports tests and callers that already have file contents. `load` uses `FileAccess` and
preserves the supplied path as the diagnostic source. This API accepts any project- or
preset-resolved path; #796 does not choose, store, or expose that path.

`apply_to_input` walks the compiled roots and nested classes already present in
`FSNameManglerAnalysis::Input::scripts`, matches each rule against the canonical identity, and calls
`Input::add_keep(name, KEEP_RULE, detail)` for each matched atomic name. Details contain the
rule's `source:line`, directive, qualified class identity, and member where applicable. The existing
analysis pass sorts and deduplicates evidence before producing its stable `keep_log`.

Because #795 exposes an explicit `complete_project_graph` contract, unmatched-rule warnings are
emitted only when `Input::complete_project_graph` is true. A partial or incremental input may not
contain the declaration that a valid rule is meant to match and therefore produces no unmatched
warning.

## Diagnostics and determinism

Malformed syntax is an error and makes `parse` or `load` return `ERR_PARSE_ERROR` without publishing
partial rules or replacing a previously valid rule set. Open/read failures and invalid UTF-8 likewise
leave the caller's existing rules unchanged. Examples include an unknown directive, missing `class`,
empty or malformed glob, missing opening/closing brace, missing member semicolon, an empty member
block, and unsupported inline block syntax.

Exact duplicate rules are accepted once and produce a warning pointing to the duplicate
`source:line` and the original `source:line`. A valid rule that matches no declaration produces an
unmatched warning after application to a complete graph. All diagnostics include `source:line`.

Diagnostics are stable:

1. parse errors and duplicate warnings follow source-line order;
2. applied declarations are visited in sorted canonical-identity order;
3. unmatched warnings follow retained rule source order; and
4. `KEEP_RULE` evidence details do not depend on hash-map iteration order.

## Test strategy

TDD begins with a parser/metadata fixture that uses `@keep_name` on every supported declaration.
Before implementation, the existing binary must reject the unknown annotation or its enum target.
If no compatible prebuilt binary is available, the first build runs the new C++ cases and records
the expected failure before production code is added.

Focused C++ coverage proves:

- all supported annotation targets compile to built-in passive metadata, while unsupported targets
  remain rejected;
- named enum metadata and every other supported table survive `.fsb` serialization and loading;
- root path, namespace-qualified global, and nested class identities match the documented globs;
- `?`, `*`, and `**` obey the `/`, `.`, and `:` separator rules;
- comments, blank lines, duplicate warnings, malformed syntax, and load failures have stable,
  actionable diagnostics;
- unmatched warnings occur only for complete whole-program inputs;
- annotation and file-rule matches become typed `KEEP_RULE` evidence with stable details and do not
  appear in `rename_map`; and
- names used only through string-built dispatch remain eligible for mangling unless annotated or
  matched by a file rule.

The bytecode assertion at this stage is metadata round-trip plus classification/map evidence.
Verbatim mutated `.fsb` output is explicitly deferred to #797/#800.

## Out of scope

- Applying the rename map to bytecode or any compiled object graph (#797).
- Rewriting scenes, resources, animations, or serialized references (#798).
- Export-preset properties, a mangling toggle, keep-file path selection, or export orchestration
  (#799).
- The integrated parity/leak corpus and final mutated `.fsb` acceptance (#800).
