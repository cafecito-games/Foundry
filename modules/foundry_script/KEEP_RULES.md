# Foundry Script name-mangler keep rules

Keep rules preserve declaration names that whole-program analysis cannot prove are reached, such as
methods assembled from dynamic strings. They add explicit keep evidence to
`FSNameManglerAnalysis`; they do not rename bytecode or select an export configuration by
themselves.

## Syntax

The file format is a deliberately small, ProGuard-style subset:

```text
# Keep a matching class declaration.
-keep class game.Player

# Keep each matching class and the selected members.
-keep class game.** {
    Dynamic*;
    State?;
}

# Keep selected members without keeping their class declaration.
-keepclassmembers class res://actors/** {
    external_*;
}
```

Blank lines and `#` comments are ignored. A trailing `#` starts a comment after a directive or
member entry.

Each directive header occupies one line. An opening `{`, when present, must be the last item on that
line. Each member glob occupies one line and ends in `;`. The closing `}` occupies its own line.
`-keepclassmembers` always requires a non-empty member block. A `-keep` directive may omit its block;
with a block, it keeps both the class declaration and matching members.

Member entries are atomic declaration-name globs. Types, return types, argument lists, modifiers,
negation, comma-separated entries, inline blocks, and other ProGuard directives are unsupported and
produce errors.

## Class identities

Class globs match the complete `FoundryScript::fully_qualified_name`:

- Anonymous root script: `res://actors/player.fs`
- Namespace-qualified global class: `game.actors.Player`
- Nested global class: `game.actors.Player::Inventory`
- Nested anonymous-root class: `res://actors/player.fs::Inventory`

Member globs match only an atomic member, method, signal, constant, named-enum, or enum-method name.
They never match a concatenated `Class.member` spelling.

## Tuples

A tuple value is a positional read-only `Array` at runtime, so a tuple declaration contributes no
runtime identity. Its field names are analyzer-only and are never mangled and never matched by a
member glob: writing a rule for a tuple field name matches nothing and produces the usual
unmatched-rule warning. Field access compiles to an index, so renaming a field in source is safe and
invisible to keep rules.

A whole-file `tuple_name` declaration registers a global class, so its type name is a class identity
and follows the class rules above; a class-body `tuple` declaration has no identity to keep.

## Glob matching

Matching is case-sensitive and covers the complete string:

- `?` matches exactly one character other than a class-identity separator.
- `*` matches zero or more characters other than a class-identity separator.
- `**` matches zero or more characters, including separators.

The class-identity separators are `/`, `.`, and `:`. Consequently, `*` cannot cross path,
namespace, resource-scheme, or `::` nested-class boundaries; `**` can. Member names are atomic and
cannot contain those separators, so `*` and `**` have the same reach in member entries.

## Diagnostics

Every diagnostic includes its source path and one-based line number. Malformed syntax is an error,
and parsing is transactional: no partial rule set is published. Replacing an existing rule object
with malformed text, an unreadable file, or invalid UTF-8 leaves its last valid rules unchanged.

An exact duplicate rule is retained once and produces a warning at the duplicate line that names
the original line. After rules are applied to a complete project graph, a retained rule that matched
no declarations produces a warning. Partial or incremental graphs do not produce unmatched-rule
warnings because the omitted declaration may exist elsewhere.

Parsing and duplicate diagnostics follow source order. Classes and members are matched in sorted
canonical order, and unmatched warnings follow retained rule order, so output is deterministic.

## Export integration

Keep rules supplement the exporter's automatic static-analysis, scene/resource, RPC, and
serialized-binding evidence. They are needed only for names reached through patterns the complete
graph cannot prove, such as a dynamically assembled method name.

In an export preset's **Scripts** section, configure:

```text
FoundryScript Export Mode = Compiled bytecode
Mangle names = On
Keep-rules file = res://path/to/mangling.keep
```

The path may be empty, in which case no manual rules are loaded. A nonempty path is resolved as a
project resource path. A missing or unreadable file, invalid UTF-8, malformed syntax, or rule
application error aborts the export; no partially mangled or ordinary-bytecode fallback is emitted.
Parser and application diagnostics retain their source path and line information in the
**Compiled Script Export** message category.

The keep-rules path is retained when **Mangle names** is turned off, but it is ignored and ordinary
compiled-bytecode output is preserved. Name mangling itself is valid only with the **Compiled
bytecode** script export mode.

The exporter seals the complete effective source manifest before applying these rules. A later
plugin-generated or plugin-customized script, native scene, or native resource is outside that graph
and aborts before export save. For the full preset, manifest, built-in-script, and diagnostics
contract, see [Name-mangled compiled-bytecode exports](README.md#name-mangled-compiled-bytecode-exports).
