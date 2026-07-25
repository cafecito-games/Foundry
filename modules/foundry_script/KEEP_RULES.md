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
and parsing is transactional: no partial rule set is published.

An exact duplicate rule is retained once and produces a warning at the duplicate line that names
the original line. After rules are applied to a complete project graph, a retained rule that matched
no declarations produces a warning. Partial or incremental graphs do not produce unmatched-rule
warnings because the omitted declaration may exist elsewhere.

Parsing and duplicate diagnostics follow source order. Classes and members are matched in sorted
canonical order, and unmatched warnings follow retained rule order, so output is deterministic.

## Export integration

The parser and matcher accept any caller-supplied text or file path. Project and export-preset
settings that select the keep-rules path are intentionally deferred to issue #799.
