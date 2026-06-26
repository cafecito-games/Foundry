# GDScript `abstract` Keyword — Design

**Status:** Approved design, ready for implementation planning
**Date:** 2026-06-26
**Author:** Christian Sueiras

## Summary

Replace the `@abstract` annotation with a real `abstract` **keyword**. The keyword
is a fully reserved tokenizer keyword used as a leading declaration modifier, in
the same slot `static` occupies today. It applies to the three targets the
annotation supported: the whole-file (implicit) class, inner classes/traits, and
functions.

This is a **hard replace**: the `@abstract` annotation is removed entirely. After
this change `@abstract` is no longer a recognized annotation and falls through to
the existing generic "Unrecognized annotation" error — no migration shim, no
deprecation alias.

The change is implemented on top of a new **unified declaration-modifier
collector** that folds today's scattered `static`/`async`/`@abstract` handling
into one mechanism. The collector is deliberately structured so the upcoming
`final` keyword (see `2026-06-25-gdscript-final-keyword-design.md`) plugs in as
one more modifier field rather than yet another ad-hoc special case.

## Goals

- `abstract` is a fully reserved keyword (`Token::ABSTRACT`); it can no longer be
  used as an identifier anywhere.
- `abstract class`, `abstract trait`, and `abstract func` mark inner
  classes/traits and functions abstract.
- `abstract` prefixing the top-level class header (`abstract class_name Foo` /
  `abstract extends Bar`) marks the whole-file implicit class abstract.
- The `@abstract` annotation, its handler, and its head special-case are deleted.
- A single declaration-modifier collector replaces the per-modifier special
  cases for `static`, `async`, and `abstract`, with `final` able to fold in later.
- The analyzer/compiler/runtime continue to consume the existing
  `ClassNode::is_abstract` / `FunctionNode::is_abstract` flags unchanged.

## Non-Goals

- Converting `async` into a real reserved keyword. `async` stays a **contextual
  identifier** (it remains a valid ordinary identifier outside the modifier
  position); the collector recognizes the `async` identifier token specially,
  exactly as the parser does today.
- Any migration tooling for existing `@abstract` source (no friendly error, no
  wizard rewrite). Chosen explicitly.
- Implementing the `final` keyword. This design only makes the collector ready to
  host it.
- Changing abstract **semantics** (instantiation rejection, abstract-method
  implementation requirements, trait abstract requirements). Only the surface
  syntax and the parsing path change.

## Surface Syntax

`abstract` is a real keyword parsed as a leading modifier.

```gdscript
abstract class_name Shape   # whole-file implicit class is abstract
extends Node

@abstract                   # REMOVED — now an "Unrecognized annotation" error

abstract class Inner:       # abstract inner class
    abstract func draw()    # abstract method, no body

abstract trait Drawable:    # abstract trait
    abstract func area() -> float
```

### Reserved-word consequence

Because `abstract` is fully reserved, existing code using it as an identifier
becomes a syntax error:

```gdscript
var abstract = 1     # syntax error: `abstract` is a reserved keyword
func abstract():     # syntax error
```

### Top-level (whole-file) abstract

There is no `class`/`func` token for the modifier to lead at file scope, so
`abstract` prefixes the class header line, parsed in the
`class_name`/`extends`/`trait_name` loop:

- A top-level `abstract` must be immediately followed by `class_name`,
  `trait_name`, or `extends`. It sets `head->is_abstract = true` directly.
- It binds to the file's implicit class regardless of which of those follows, so
  `abstract class_name Foo` and `abstract extends Bar` both mark the head class
  abstract.
- `abstract` may appear once at top level; a second one is an error.
- **Edge case — file with neither `class_name` nor `extends`:** under the
  prefix-only rule there is nothing to attach to. A file that wants to be an
  abstract base without a name and without an explicit parent must add an explicit
  `extends`, e.g. `abstract extends RefCounted`. This is documented; we do not
  reintroduce a standalone `abstract` statement.

Top level uses a dedicated `case ABSTRACT:`, not the full body collector, since
`static`/`async` are not valid modifiers there anyway.

## Unified Declaration-Modifier Collector (the core mechanism)

Today the class-body loop handles modifiers as independent special cases:
`static` (`gdscript_parser.cpp:1670`), the contextual `async` identifier
(`:1754`), and `@abstract` via the annotation system. B1 replaces all three with
one collector.

**Data structure** — a small struct carrying both the resolved flags and the
source position of each modifier (for precise diagnostics and duplicate
detection):

```cpp
struct DeclarationModifiers {
    bool is_abstract = false;
    bool is_static = false;
    bool is_async = false;
    // source token / position of each modifier seen, for error reporting
    // (room for `is_final` to join later)
};
```

**Collection.** At the start of each class-body iteration, consume the leading
run of modifier tokens (`ABSTRACT`, `STATIC`, and the contextual `async`
identifier) into a `DeclarationModifiers`, then dispatch on the declaration token
that follows (`func`/`var`/`class`/`trait`). The collected flags are threaded
into `parse_class` / `parse_trait` / `parse_function_class_member` /
`parse_variable`, which set the existing `is_abstract` / `is_static` flags on the
resulting nodes (replacing the `p_is_static` / `next_is_static` / `next_is_async`
plumbing).

**Validation** runs once over the collected set against the following member:

*Target validity (which modifier may attach to what):*

- `abstract` → `class`, `trait`, `func` only. On `var`/`const`/`signal`/`enum` →
  error.
- `static` → `func`, `var` only (unchanged).
- `async` → `func` only (unchanged).

*Duplicates:* any modifier appearing twice (`abstract abstract func`) → error.
This preserves today's "`@abstract` can only be used once" as a generic rule
across all modifiers.

*Combinations:*

- `static` + `abstract` on a function → error, **except inside a trait** (this
  preserves the exact carve-out in today's `abstract_annotation()`).
- `abstract` + `async` → error (an abstract function has no body, so
  `async`/`await` is meaningless).

*Ordering — relaxed (behavior change):* a flag set is inherently
order-independent, so modifiers are accepted in **any order**
(`static async func`, `async static func`, `abstract static func` in a trait,
etc.). This **drops** today's *"`static` must appear before `async`"* parse error
(`gdscript_parser.cpp:1757`) and re-baselines its fixture. Any desired stylistic
ordering is left to a future linter, not the parser. The only existing programs
affected are ones that currently receive that stylistic ordering *error*.

## Semantics (unchanged)

The collector's sole job is to set `ClassNode::is_abstract` /
`FunctionNode::is_abstract` — the same flags the rest of the pipeline already
reads. Everything downstream is untouched:

- **Analyzer** (`gdscript_analyzer.cpp`): cannot-construct-abstract-class check,
  abstract-method implementation requirements for non-abstract subclasses,
  abstract-function "must have no body" / non-abstract "must have a body"
  validation, and trait abstract-requirement handling all read `is_abstract` and
  need no change.
- **Compiler** (`gdscript_compiler.cpp`): `p_script->_is_abstract =
  p_class->is_abstract` propagation and trait abstract-requirement collection:
  unchanged.
- **Runtime** (`gdscript.h`): `_is_abstract` storage and `is_abstract()`
  accessor: unchanged.

## Removals

- `register_annotation(MethodInfo("@abstract"), ...)` (`gdscript_parser.cpp:186`).
- The `abstract_annotation()` handler (`gdscript_parser.cpp:5233-5258`) and its
  declaration in `gdscript_parser.h`.
- The `@abstract`-on-head application loop (`gdscript_parser.cpp:916-922`).

## Implementation Map

| Layer | File(s) | Change |
| --- | --- | --- |
| Tokenizer | `gdscript_tokenizer.h` | Add `Token::ABSTRACT` to the enum and its name to the token-names table. |
| Tokenizer | `gdscript_tokenizer.cpp` | Register `KEYWORD("abstract", Token::ABSTRACT)` in the `KEYWORDS` macro (group `'a'`). |
| Tokenizer buffer | `gdscript_tokenizer_buffer.h` | Bump `TOKENIZER_VERSION` (currently 103) — the keyword set / token stream changes. |
| Parser | `gdscript_parser.cpp` | Add the `DeclarationModifiers` collector; fold `static`/`async`/`abstract` collection + validation into it; thread flags into class/trait/func/var parsing; add the top-level `case ABSTRACT:`; delete the annotation registration, handler, and head special-case. |
| Parser | `gdscript_parser.h` | Declare `DeclarationModifiers`; remove the `abstract_annotation` declaration. |
| Reserved words | `gdscript.cpp` | Add `"abstract"` under the "Declarations" group in `get_reserved_words()` (hand-maintained; alphabetical, before `"class"`). `async` stays out, as today. |
| Analyzer / Compiler / Runtime | — | No change (consume existing `is_abstract` flags). |
| Docs | `modules/gdscript/doc_classes/@GDScript.xml` | Remove the `@abstract` annotation entry. |
| Docs | GDScript language docs / class reference | Update abstract-class prose and examples to keyword syntax. |

## Testing Strategy

Primary coverage is script fixtures in `modules/gdscript/tests/scripts/` and
`tests/scripts/` (paired `.gd` + expected-output). Regenerate affected `.out`
files with the headless runner
(`--headless --gdscript-generate-tests modules/gdscript/tests/scripts`) after the
intentional behavior change, then run the full `--headless --test` suite and the
`*Refactor*` suite for parity.

**Fixture migration** — rewrite every `@abstract` fixture to keyword syntax and
regenerate outputs:

- analyzer/errors: `abstract_methods.gd`, `construct_abstract_class.gd`,
  `construct_abstract_script.notest.gd`, `abstract_class_instantiate.gd`,
  `abstract_async_method_implementation.gd`, `trait_base_abstract_required.gd`,
  `trait_abstract_method_requirement.gd`,
  `trait_conflict_deferred_by_abstract.gd`
- `refactor/implement_abstract_*.gd`,
  `completion/common/override_function_abstract.gd`,
  `runtime/features/abstract_methods.gd`

**New fixtures (keyword-specific behavior):**

- `abstract` as a now-reserved word: `var abstract` / `func abstract()` → syntax
  error.
- Any-order modifiers: `async static func`, `abstract static func` inside a
  trait.
- Top-level prefix forms: `abstract class_name Foo`, `abstract extends Bar`, and
  the neither-`class_name`-nor-`extends` edge case.
- Duplicate `abstract` (`abstract abstract func`, second top-level `abstract`).
- `abstract` on invalid targets (`var`/`const`/`signal`/`enum`).
- `abstract` + `async` error; `abstract` + `static` outside a trait error;
  `abstract` + `static` inside a trait allowed.
- Re-baseline the dropped `static`-before-`async` ordering fixture.

**C++ unit tests** (`tests/`, doctest) only where a tokenizer/parser edge needs
isolation; otherwise script fixtures are the primary coverage.

## Known Gaps / Risks

- Relaxing modifier ordering is a deliberate behavior change; the only programs
  affected are those that currently get the stylistic ordering error. Captured in
  a re-baselined fixture.
- Folding `static`/`async` into the collector touches well-trodden parser paths;
  the existing fixture suite is the regression net (the rationale for choosing B1
  over an abstract-only collector).
- Editor/LSP keyword highlighting beyond `get_reserved_words()` is opportunistic;
  follow up if a separate highlight list needs the keyword.
