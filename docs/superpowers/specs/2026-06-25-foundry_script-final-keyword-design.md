# GDScript `final` Keyword — Design

**Status:** Approved design, ready for implementation planning
**Date:** 2026-06-25
**Author:** Christian Sueiras

## Summary

Add a `final` keyword to GDScript, modeled on Java's `final`. It applies to four
targets: classes (cannot be extended), methods (cannot be overridden), member
variables (write-once), and local variables (write-once). `final` is a true
tokenizer keyword used as a leading modifier, not an annotation.

The headline capability is **write-once-at-runtime** variables: unlike `const`
(which requires a compile-time-constant value), a `final var` may be assigned a
runtime-computed value exactly once — in its declaration initializer or, for a
blank final member, in `_init()` — and is read-only thereafter. Enforcement is
**Java-strict definite assignment**, computed by a static analysis pass.

## Goals

- `final class` — reject any attempt to extend the class (all extend forms).
- `final func` — reject any attempt to override the method in a subclass.
- `final var` member — write-once: assigned exactly once via declaration
  initializer or `_init()`, read-only afterward; blank finals must be definitely
  assigned on every `_init()` exit path.
- `final var` local — write-once within its enclosing function/block.
- `final static var` — write-once static, scoped to static initialization.
- Precise, source-located diagnostics for every violation.

## Non-Goals

- Tracking assignment through arbitrary helper methods, captured lambdas, or
  closures. The single assignment slot is bounded to the declaration initializer
  and `_init()` (members) or the enclosing block (locals).
- A per-write **runtime guard**. Enforcement is static-only (the chosen model).
  Dynamic write paths such as `Object.set("id", v)` or reflection are therefore
  **not** caught — a known and accepted gap. (The one runtime check we do keep is
  rejecting extension of a `final` base at script load, see "Runtime".)
- Multiple-constructor definite-assignment (GDScript has a single `_init`).

## Surface Syntax

`final` is a real keyword (`Token::FINAL`) parsed as a leading modifier, in the
same slot `static` occupies today.

```gdscript
final class Weapon:              # cannot be extended
    final var id: int                # blank final member (assigned in _init)
    final var name := "?"            # initialized final member
    final static var REGISTRY := {}  # write-once static
    final func fire(): ...           # cannot be overridden

    func _init(p_id: int):
        id = p_id                    # the one allowed assignment of `id`

func _ready():
    final var roll := randi()        # write-once local
```

### Grammar / parsing rules

- `final` may combine with `static` (`final static var`) and with a type hint.
  Modifier order is `final` then `static`, mirroring how modifiers stack.
- `final` + `const` → **parse error** (redundant and contradictory; `const` is
  already stricter).
- `final` + `abstract` on a **class** → **parse error** (a class cannot be both
  un-extendable and require-extension).
- `final` + `abstract` on a **method** → **parse error** (an abstract method
  must be overridden; `final` forbids it).
- A `final` modifier on the top-level script class marks the whole script final,
  consistent with how `abstract` marks a script.

## Semantics

### Final classes

- `is_final` is stored on `ClassNode` and copied to the runtime `GDScript`.
- During class-resolution in the analyzer (where the `extends` chain is
  resolved), if a class's resolved base has `is_final` set:
  **"Cannot extend final class 'X'."**
- Covers all extend forms: `extends Base`, inner `class Foo extends Base`, and
  cross-file `extends "res://base.gd"` / `extends BaseClassName`.

### Final methods

- `is_final` is stored on `FunctionNode`.
- During function-override validation (the same place signature compatibility
  against the parent is checked), if a matching parent method is `final`:
  **"Cannot override final function 'foo()' declared in 'Base'."**
- A `final func` may itself override a non-final parent method — finality only
  blocks *further* overriding downstream.
- `final static func` is allowed (static methods aren't virtual; the marker documents
  intent and blocks same-name redeclaration in subclasses).
- `final func` inside a `final class` is redundant-but-allowed (no error), to
  keep refactoring painless.

### Final variables (write-once)

`is_final` is stored on `VariableNode` (members, static vars, locals). Each `final`
variable has exactly **one assignment slot** that must be filled exactly once.

**Member variables (instance):**

- *Initialized final* (`final var name := "?"`): the declaration initializer
  fills the slot. Any later assignment anywhere →
  **"Cannot assign to final variable 'name'; it is already assigned."**
- *Blank final* (`final var id: int`): the slot is empty at declaration and must
  be definitely assigned on **every** return path of `_init()`. The declaration
  initializer and an `_init` assignment are mutually exclusive — together they
  are a double-assign error.
- Assignment is legal **only** in the declaration initializer or lexically
  within `_init`. Assigning a `final` member in any other method →
  **"Final variable 'id' can only be assigned in its declaration or in
  _init()."** (This bounds the flow analysis to `_init`.)
- Reading a blank final before it is definitely assigned within `_init` →
  **"Final variable 'id' may be used before assignment."**

**Static finals** (`final static var`): identical, but the single slot is filled
by the declaration initializer or by `static func _static_init()` if present;
otherwise an initializer is required. Scoped to static init rather than `_init`.

**Local variables** (`final var x` in a body): assigned exactly once before use
along every path; reassignment → "already assigned"; use-before-assign → "may be
used before assignment". Same engine, scoped to the enclosing block/function.

## Definite-Assignment Engine (the core analysis)

A structural recursion over the AST following the Java Language Spec
definite-assignment model. No control-flow graph and no fixpoint are needed —
GDScript's control flow is fully structured (no `goto`), so a single pass over
the AST is sufficient.

**State.** Thread a set `assigned` (the `final` targets definitely assigned so
far) through statement traversal. For each construct, compute the `assigned` set
flowing *out* from the set flowing *in*:

- **Assignment to a final target `T`:** if `T ∈ assigned` → "already assigned"
  error; otherwise add `T`. A read of a final `T ∉ assigned` → "may be used
  before assignment."
- **Sequence** `s1; s2`: thread `out(s1)` into `in(s2)`.
- **`if/elif/else`:** analyze each arm from the same incoming set; the result is
  the **intersection** of the arms' out-sets (a variable counts as assigned only
  if assigned on *every* arm). A missing `else` contributes the incoming set
  unchanged, so anything assigned only inside `if` is *not* definitely assigned
  afterward.
- **`match`:** intersection across all branches; only contributes to "definitely
  assigned" if there is a wildcard `_` branch (otherwise the no-match path is
  open, like a missing `else`).
- **`while` / `for`:** the body may execute zero times, so nothing the body
  assigns is definitely-assigned after the loop. Assigning a final inside a loop
  body is also flagged as a potential double-assign across iterations.
- **Terminators** (`return`, `break`, `continue`, fatal aborts): mark the path
  **unreachable**. An unreachable path's out-set is the universal set (⊤), which
  is neutral under intersection at the join. This is what lets
  `if cond: x = a else: <fatal>` count `x` as definitely assigned afterward
  (e.g. for a local, or after a branch that aborts via `@noreturn`/`push_fatal`).
- **`return` from `_init()`/`_static_init()` is an escape point.** Unlike a fatal
  abort, a `return` still produces a fully constructed object (or loaded class),
  so a blank final left unassigned on that path would escape at its default value.
  Following Java's blank-final-in-constructor rule, every blank final member must
  be definitely assigned *before* each `return` that exits initialization; a
  `return` that leaves one unassigned is an error at the `return`. (Join
  neutrality above still governs the *after-state* used for locals and for reads
  past the join — it is not a license to skip this escape-point check.)

**Where it runs.** A focused traversal invoked from the analyzer once a class's
members and `_init` are resolved: seed `assigned` with all initialized finals,
walk `_init`'s body, then require every blank final ∈ `assigned` at each `_init`
exit. Locals run the same walk over their enclosing function with an empty seed.
The walk reuses the analyzer's existing per-statement visiting structure rather
than introducing a parallel traversal.

**Diagnostics** carry source positions from the offending node so errors point at
the bad assignment/use, not the declaration.

## Implementation Map

The existing `@abstract` implementation is the closest template (inheritance-side
checks are the inverse of `abstract`). Note `abstract` is currently an annotation
in this fork; `final` is a true keyword, so it additionally needs tokenizer and
modifier-parsing work.

| Layer | File(s) | Change |
| --- | --- | --- |
| Tokenizer | `gdscript_tokenizer.h/.cpp` | Add `Token::FINAL`; register `"final"` in the keyword map. |
| Parser AST | `gdscript_parser.h` | `is_final` on `ClassNode`, `FunctionNode`, `VariableNode`. |
| Parser | `gdscript_parser.cpp` | Parse `final` as a leading modifier (model on the `static` modifier slot) for class/member/statement; enforce the `const`/`abstract` combination parse errors. |
| Analyzer — inheritance | `gdscript_analyzer.cpp` | Reject extending a final base in class resolution. |
| Analyzer — override | `gdscript_analyzer.cpp` | Reject overriding a final method in override validation. |
| Analyzer — definite assignment | `gdscript_analyzer.cpp` | New structural definite-assignment pass for final members (`_init`), statistics, and locals. |
| Compiler | `gdscript_compiler.cpp` | Copy `is_final` to the runtime script (class-level). |
| Runtime | `gdscript.h/.cpp` | `_is_final` + `is_final()` accessor; consult it when resolving `extends` at script load. |
| Editor/LSP | syntax highlighter, completion | Add `final` to keyword highlighting; don't offer final members as assignment targets or suggest overriding final methods (opportunistic; follow-up if no hook exists). |

Final *methods* and final *vars* are fully enforced at analyze time and emit **no
bytecode changes / no per-write runtime guard**. Only the class-level final flag
is needed at runtime (to reject extension at load).

## Testing Strategy

Primary coverage is script fixtures in `modules/gdscript/tests/scripts/`
(paired `.gd` + `.out`), matching how `abstract` is tested. Generate/verify with
the headless runner.

**Error fixtures (must fail with the right message):**

- Extend a `final class` — each form (`extends Name`, inner `class … extends`,
  cross-file `extends "res://…"`).
- Override a `final func`; `final` + `abstract` (class and method); `final` +
  `const`.
- Reassign an initialized final; double-assign a blank final (initializer +
  `_init`); assign a final member outside `_init`.
- Blank final not assigned on every `_init` path (missing `else`, non-wildcard
  `match`, assignment only inside a loop body, early `return` that leaves it
  unassigned).
- Read a blank final before assignment; local final reassignment and
  use-before-assign.

**Positive fixtures (must compile & run):**

- Blank final assigned once in `_init`; assigned in both arms of `if/else`;
  assigned before an early `return`; assign-or-abort pattern (a branch that ends
  in `@noreturn`/`push_fatal` is exempt); wildcard `match`.
- `final func` overriding a non-final parent; `final static var` with
  initializer; `final` local assigned once then read.

**Runtime fixture:** a `.gd` that dynamically `load()`s and tries to extend a
final base, asserting the load-time rejection (the one path not covered by pure
static analysis).

**C++ unit tests** (`tests/`, doctest) only where a tokenizer/parser edge needs
isolation; otherwise script fixtures are the primary coverage.

## Known Gaps / Risks

- Dynamic writes (`set()`, reflection) bypass static enforcement of write-once
  variables — accepted, per the static-only decision.
- The definite-assignment pass adds analyzer complexity; the structured-AST
  approach keeps it bounded (no CFG), but loop/`match`/terminator merge rules
  must be tested carefully (covered above).
- Editor/LSP completion changes are opportunistic and may land as a follow-up if
  no existing hook is available.
