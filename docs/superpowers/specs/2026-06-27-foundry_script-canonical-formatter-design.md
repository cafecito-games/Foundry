# Canonical Foundry Script Formatter — Design

**Date:** 2026-06-27
**Status:** Approved design, ready for implementation planning
**Scope:** Formatter first. Linting is explicitly out of scope for this spec (a later phase
may surface the analyzer's existing warning system via CLI).

## Goal

Provide a **canonical, built-in formatter** for this fork's dialect of Foundry Script — `gofmt`/`rustfmt`
style: one official style, no configuration, shipped with the toolchain. The formatter is usable from
CI/CD pipelines and precommit hooks so that all committed Foundry Script looks identical.

The formatter must structurally match the fork's evolving dialect (`final`, `abstract`, generics /
type parameters, traits, `AsyncCallable[[Args], Return]`, stricter typing). It achieves this by
**reusing the engine's real tokenizer and parser** as the single source of truth, rather than
maintaining a separate grammar that would inevitably drift.

## Non-Goals

- No linting / analyzer-warning surfacing in this scope (separate later phase).
- No configuration surface — there is exactly one canonical style (the `gofmt` philosophy).
- No auto-wrapping or auto-joining of long logical lines in v1 (see Style Rules).
- No declaration reordering (we never move members around).
- No standalone stripped binary in v1 — the formatter ships as a subcommand of the existing
  headless engine binary. Extraction into a smaller artifact can be revisited later.

## Architecture

One reusable **formatter core** with thin front-ends. All new code lives in `modules/foundry_script/`.

```
gdscript_format.h / .cpp        ← GDScriptFormatter core (the only place style lives)
   ├─ input:  source text (+ filename for diagnostics)
   ├─ step 1: GDScriptTokenizerText  → tokens + comment map  (reused, unchanged)
   ├─ step 2: GDScriptParser         → parse tree            (reused, unchanged)
   ├─ step 3: GDScriptPrinter        → canonical text        (NEW: walks tree, emits)
   └─ output: formatted String  |  ParseError (refuse to format)

main/main.cpp                   ← --gdscript-format CLI handler
                                   (mirrors the existing --gdscript-generate-tests hook)
   └─ drives GDScriptFormatter over files/dirs/stdin; applies --check/--write/--diff modes

(later, out of scope) LSP textDocument/formatting and editor format-on-save call the same core.
```

### Boundaries (these guarantee fidelity)

- **Text in, text out.** The core needs no editor, no analyzer, no project context. This keeps it
  fast, single-file, and trivially testable.
- **Parser/tokenizer consumed read-only and unmodified.** The formatter is a *new consumer* of the
  existing parse tree, not a fork of it. The formatter literally cannot accept syntax the real parser
  rejects, and cannot disagree with it about structure.
- **Style is centralized in the printer only.** No config, no flags affecting style.
- **Round-trip caveat (resolve in implementation):** confirm each literal/expression node retains
  enough information to round-trip (e.g. string contents, numeric literal forms). Where the AST has
  normalized something away, the printer picks the canonical form deterministically.

## Formatter Core Internals

### Printer
A recursive walk over the parse tree emitting text into a small buffer abstraction that tracks
current indent depth and column. Each node type has a print routine (`print_class`, `print_function`,
`print_if`, `print_binary_op`, …). **Indentation is structural** — derived from the tree, never from
the input's whitespace — which is what makes output deterministic.

**Node coverage must be exhaustive.** In dev builds, the printer asserts loudly on any unhandled node
type, so a newly added language construct that lacks a print routine fails fast rather than silently
dropping code.

### Comment reattachment (the core challenge)
Comments live in the tokenizer's line-keyed map (`HashMap<int, CommentData>`), not on AST nodes. Each
`CommentData` records the text and a `new_line` flag (full-line vs inline). Every parse node carries
`start_line` / `end_line`. Strategy:

- Maintain a cursor over the sorted comment lines.
- **Before** emitting a node, flush any full-line comments whose line precedes the node's start line,
  at the node's indent.
- **After** emitting a statement, if a comment shares its source line, emit it as a trailing inline
  comment (two spaces + `#`).
- Use `CommentData.new_line` to choose full-line vs inline placement.

### Blank lines
`gofmt` rule: collapse runs of blank lines to at most one inside blocks; enforce the style's
structural blanks (two between top-level definitions, one between methods) regardless of input. We
preserve *intent* (was there a gap?) but normalize *amount*.

### Safety properties (each backed by tests)
1. **Refuse-on-error.** If the file has parse errors, emit nothing, report the error, leave the file
   untouched, exit nonzero. We never format code we can't fully parse.
2. **Semantic preservation.** Formatting must not change behavior — verified by a harness that
   tokenizes+parses before/after and asserts structural equivalence (modulo trivia) over the corpus.
3. **Idempotency.** `format(format(x)) == format(x)` — verified by re-running the formatter on its own
   output across the whole corpus.

## CLI Surface & Integration

Modeled on the existing `--gdscript-generate-tests` handler in `main/main.cpp`, invoked on the
headless binary.

```
godot --headless --gdscript-format [MODE] [paths...]
```

- `paths` — files or directories (recursed for `*.fs`). No path or `-` reads stdin → writes stdout.

| Flag                | Behavior                                              | Exit code                          |
|---------------------|-------------------------------------------------------|------------------------------------|
| *(default)*         | write formatted result to **stdout**                  | 0                                  |
| `--write` / `-w`    | rewrite files **in place**                            | 0                                  |
| `--check`           | print files that **would change**; do not modify      | **1 if any file differs**, else 0  |
| `--diff` / `-d`     | print a unified diff per file                         | 1 if any differ                    |

`--check` is the CI/precommit primitive: non-zero exit fails the pipeline; the listed files tell the
developer to run `--write`.

**Diagnostics:** parse errors print `file:line:col: message` to stderr; that file is skipped and the
overall exit is non-zero, so malformed files surface loudly rather than being silently passed through.

**Precommit:** a local hook in `.pre-commit-config.yaml` runs
`godot --headless --gdscript-format --check` on staged `*.fs` files. Formatting is single-file and
syntactic, so it is fast enough for precommit. The hook documents the binary path, consistent with how
the repo already invokes the binary.

**CI:** a job step runs the same `--check` over the tree — the canonical gate keeping all committed
Foundry Script in one style.

## Canonical Style Rules

Based on the official Godot Foundry Script style guide, made stricter where the guide is silent or
permissive. The full rule table lives here; the implementation plan refines edge cases.

### From the official guide (enforced)
- **Tabs** for indentation, one level per block.
- **One statement per line**; no `;`.
- **Two blank lines** between top-level definitions; **one** between methods inside a class. Longer
  runs collapse.
- One space around binary operators and `=`; no space for unary; `,` followed by one space; no space
  inside `()` / `[]` edges.
- Spaces around `:` in type hints (`var x: int`) and around `->` return arrows.
- One space after `#` in comments (`# like this`).
- **Member order left as written** — we do not reorder declarations.

### Opinionated extras (guide silent/permissive → we pick one)
- **Double quotes** canonical for strings; normalize single→double *unless* the string contains a `"`.
- **Trailing commas** in multi-line collection/argument literals; none in single-line.
- **Redundant parentheses** removed where unambiguous; preserved where they aid precedence clarity.
  Conservative default is *preserve*; exact rule pinned during implementation.
- Normalize numeric literal casing (`0xFF`, `1e10`) and leading-zero forms.
- One canonical blank-line convention after the class header (`class_name` / `extends` / class-level
  annotations).

### Fork-specific syntax (new — no upstream precedent)
- `final` / `abstract` modifier placement and spacing on classes, methods, vars.
- Generics: `Array[int]`, type-parameter lists `[T, U]` — no inner padding, `, ` between params.
- `AsyncCallable[[Args], Return]` — canonical spacing of the nested bracket form.
- Trait declarations / trait usage — spacing and ordering.

### Line wrapping
**No auto-wrap or auto-join in v1.** Respect the author's line breaks for long expressions; only
normalize spacing and indentation within them. Auto-wrapping is the riskiest formatter behavior given
Foundry Script's fragile line-continuation rules, and is deferred to a possible later phase.

## Testing Strategy

Tests live under `modules/foundry_script/tests/scripts/` (new `format/` area) plus a C++ harness in `tests/`
following the existing doctest pattern.

1. **Golden-file fixtures (the bulk).** Each case is `input.fs` → `expected.fs`, organized by feature:
   basics (spacing, blanks, indent), comments (full-line, inline, comment-only files, comments in odd
   positions), strings/quotes, collections/trailing commas, and a dedicated **fork-syntax** set
   (`final`, `abstract`, generics, `AsyncCallable`, traits). A runner formats `input.fs` and diffs
   against `expected.fs`. A regen path (mirroring the existing `.out` regeneration workflow) rewrites
   `expected.fs` from current output for intentional style changes.

2. **Property tests over the whole corpus** (run against *all* existing `.fs` test scripts, a large
   real-world set):
   - **Idempotency:** `format(x) == format(format(x))` for every script.
   - **Refuse-on-error:** known-bad scripts (the `analyzer/errors/` set) must be refused, not
     reformatted.

3. **Semantic-preservation harness.** For each formattable script, tokenize+parse before and after and
   assert structural/semantic equivalence (same token/AST shape modulo trivia). Scoped to the parse
   level (not execution) to stay fast and project-free.

4. **Coverage assertion.** In dev builds the printer asserts on any unhandled node type, so a new
   language construct lacking a print routine fails tests loudly.

**Regen workflow** matches existing muscle memory: after an intentional style change, run the
format-test regen command, review the diff, commit. CI runs fixtures + property tests on every PR.

## Implementation Risks & Open Questions

- **Literal round-trip fidelity.** Verify per node type that the AST retains enough to reproduce the
  literal; fall back to reading the source range where it does not.
- **Redundant-paren rule.** Exact removal/preservation rule to be pinned; start conservative
  (preserve).
- **Comment edge cases.** Comments inside multi-line expressions, between decorators/annotations and
  their target, and trailing comments after block-opening lines need explicit fixtures.
- **Binary availability for hooks.** Precommit/CI need a built binary on PATH; document the expected
  path consistent with current repo conventions.

## Future Phases (not in this scope)

- Lint CLI surfacing the analyzer's existing warning system with CI exit codes.
- LSP `textDocument/formatting` and editor format-on-save calling the same core.
- Optional standalone/stripped formatter artifact for lighter CI installs.
- Optional published tree-sitter grammar for editor syntax-highlighting only (decoupled from canonical
  formatting).
- Possible auto-wrapping phase.
