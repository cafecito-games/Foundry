# Named function-call arguments in Foundry Script — Design

**Status:** Approved design, pending implementation
**Date:** 2026-06-26
**Scope:** Foundry Script module (`modules/foundry_script/`)

## Summary

Allow Foundry Script function calls to pass arguments by parameter name, so argument
order does not matter:

```gdscript
func print_greeting(name, greeting):
    print("%s, %s" % [greeting, name])

print_greeting(greeting = "Hello friend!", name = "Bob")
```

The feature is resolved **entirely at compile time**. The parser learns to
attach a name to a call argument; the analyzer maps names to parameter positions
and rewrites each call into canonical positional order; the VM is untouched,
because by the time bytecode is emitted every call is already positional. Codegen
emits the existing call opcodes unchanged, but evaluates the argument expressions
in **source order** (see "Evaluation order") rather than the rewritten positional
order, so side effects run left to right as written. The runtime never learns
that named arguments exist, so there is no bytecode-format or compiled-script
binary-compatibility impact.

## Goals

- Pass arguments to Foundry Script-defined functions by name, in any order.
- Mix positional and named arguments under the Python rule (positional first).
- Skip optional (defaulted) parameters in the middle of the signature when the
  skipped parameter's default value is a compile-time constant.
- Produce precise compile-time diagnostics for every misuse.

## Non-goals

- Named arguments for engine/native methods, built-in utility functions, or any
  call whose target the analyzer cannot statically resolve to a Foundry Script
  function. Parameter names are only reliably available for Foundry Script-defined
  functions; everything else stays positional-only.
- Skipping a middle defaulted parameter whose default is a **non-constant**
  expression. This is a precise compile error, not silent or wrong behavior.
- Any runtime/ABI change (no presence bitmask, no calling-convention change).

## Background: the current call pipeline

Findings from the existing code that shape the design:

- **Parser.** `CallNode` (`gdscript_parser.h`) stores arguments as a flat
  `Vector<ExpressionNode *> arguments` with no names. Arguments are parsed in a
  simple comma loop in `parse_call` (`gdscript_parser.cpp` ~4017–4051).
- **Analyzer.** Call arguments are matched to parameters strictly by index in
  `validate_call_arg` (`gdscript_analyzer.cpp` ~10511–10562). Parameter names
  are available: Foundry Script functions expose
  `FunctionNode.parameters_indices: HashMap<StringName, int>`.
- **Codegen / VM.** Every call opcode pushes arguments positionally
  (`gdscript_byte_codegen.cpp` `write_call`). The VM never sees argument names.
- **Default values.** A Foundry Script default value can be an **arbitrary runtime
  expression** evaluated in the **callee's scope**, with access to `self`, class
  members, and earlier parameters (e.g. `func f(a, b = a + 1)`). The analyzer
  stores a constant-folded `reduced_value` in `FunctionNode.default_arg_values`
  only when `initializer->is_constant`; otherwise it stores a placeholder
  `Variant()`. Non-constant defaults are evaluated by callee bytecode reached
  through the `OPCODE_JUMP_TO_DEF_ARGUMENT` jump table, which only fills a
  **trailing contiguous** block of omitted parameters (`defarg = _argument_count
  - p_argcount`).

The default-value mechanism is the reason named arguments cannot be a purely
naive call-site rewrite: a skipped *middle* default that is non-constant can
only be computed correctly by the callee, which can only fill trailing
omissions. Constants, by contrast, are scope-independent and safe to inline at
the call site.

## Semantics (compile-time rules)

1. **Ordering.** Positional arguments come first; once a named argument appears,
   every following argument must be named. A positional argument after a named
   argument is an error.
2. **Eligibility.** Named arguments are allowed only when the analyzer
   statically resolves the callee to a known Foundry Script function/method: self
   calls `f(...)`, instance calls `obj.method(...)`, static calls
   `Type.method(...)`, and the constructor `_init`. On an unresolved /
   `Callable` / untyped target, a named argument is an error.
3. **Unknown name.** Naming a parameter the function does not declare is an
   error.
4. **Duplicate.** Supplying a parameter both positionally and by name, or naming
   the same parameter twice, is an error.
5. **Rest parameter.** Named arguments may target the declared fixed parameters
   only. The rest parameter cannot be named (error). It still collects trailing
   *positional* arguments, which by rule 1 cannot appear after a named argument.
6. **Missing required.** After resolution, every non-defaulted parameter must be
   filled, reusing the existing "too few arguments" diagnostic, worded via the
   missing parameter name.
7. **Gap fill.** An omitted parameter positioned *before* the last filled
   argument slot must have a compile-time-constant default; the compiler inlines
   that constant, resolved against the **statically known** callee signature (see
   "Inlined defaults bind to the static type"). If its default is a non-constant
   expression, it is an error. Omitted *trailing* parameters are handled by the
   callee's existing default mechanism, regardless of default kind.
8. **Evaluation order.** Argument *expressions* are evaluated in source
   (left-to-right written) order, matching Python, C#, and Kotlin; the parameter
   name only chooses which parameter each value *binds* to. `f(b = g(), a = h())`
   evaluates `g()` before `h()` even though it binds `a = h()` first. Synthesized
   constant gap-fills have no side effects and are immaterial to this order.

### Evaluation order

Canonicalization rewrites `arguments` into parameter order so the callee binds by
position, which would otherwise make codegen evaluate the arguments in
parameter-declaration order. To keep observable side effects in written order,
the analyzer records a source-order evaluation list (`CallNode.argument_evaluation_order`,
a permutation of the canonical argument indices) whenever the rewrite reorders the
arguments. The compiler evaluates the argument expressions in that order into
registers, then passes the register addresses to the existing call opcode in
positional order. This needs no new opcode and no VM/ABI change: it only reorders
which existing argument expression is evaluated first. An ordinary positional call
leaves the list empty and is evaluated front to back as before.

### Worked examples

Given `func f(a, b = 1, c = 2)`:

| Call            | Result                                                                 |
| --------------- | ---------------------------------------------------------------------- |
| `f(0, c = 5)`   | `b` skipped (middle), default `1` is constant → inlined → `f(0, 1, 5)`. |
| `f(0, b = 5)`   | `c` omitted (trailing) → callee fills via jump table. OK.               |
| `f(c = 5, a = 0)` | Reordered to `f(0, 1, 5)`; `b` inlined.                              |
| `f(0, 1, b = 5)` | Error: `b` supplied positionally and by name (rule 4).                |
| `f(b = 5, 0)`   | Error: positional after named (rule 1).                                |
| `f(0, d = 5)`   | Error: no parameter named `d` (rule 3).                                |

Given `func g(a, b = [], c = 2)` (`b`'s default is a non-constant array literal):

| Call          | Result                                                                  |
| ------------- | ----------------------------------------------------------------------- |
| `g(0, c = 5)` | Error: cannot skip `b`; its default is not a constant expression (r. 7). |
| `g(0, b = 5)` | OK: `c` omitted is trailing.                                            |

### Inlined defaults bind to the static type

A middle gap is filled by inlining the skipped parameter's default, resolved
against the **statically resolved** callee signature — the same signature the
call is type-checked against. When a subclass overrides the method with a
*different* default and a base-typed receiver is dispatched at runtime to the
override, the inlined gap still carries the **base** (static) default, while a
trailing omission on that same receiver picks up the **override's** (runtime)
default through the callee's default mechanism:

```gdscript
class Base:
    func f(a: int, b: int = 10, c: int = 0) -> int: return a * 100 + b * 10 + c
class Derived extends Base:
    func f(a: int, b: int = 99, c: int = 0) -> int: return a * 100 + b * 10 + c

var x: Base = Derived.new()
x.f(1, c = 5)  # 205  — middle gap inlines Base's b = 10 (static type)
x.f(1)         # 1090 — trailing omit uses Derived's b = 99 (runtime dispatch)
```

This is **intended** behavior, not a defect, and is the direct and unavoidable
consequence of the "no ABI change / no presence bitmask" non-goal:

- A non-trailing gap has no ABI-free representation other than a materialized
  call-site value, so it must be inlined at compile time. Compile-time inlining
  can only see the static signature.
- A trailing omission *does* have an ABI-free representation — the existing
  `OPCODE_JUMP_TO_DEF_ARGUMENT` callee mechanism — which is required anyway for
  non-constant defaults and matches long-standing positional-call behavior. It
  therefore honors the override's default.

The two paths consequently diverge precisely when an override changes a constant
default's value. Resolving the inlined default from the static type is also the
coherent choice for a compile-time feature and mirrors C#, where an optional
argument's default is likewise determined by the **compile-time** receiver type
rather than its runtime type. Making the middle-gap path honor the runtime
override would require the presence-bitmask calling convention this design rules
out (see Follow-ups). This behavior is locked in by the runtime fixture
`named_call_argument_middle_gap_static_default.fs`.

## Architecture

### Parser

`CallNode` gains an array of argument names parallel to `arguments`:

```cpp
struct CallNode : public ExpressionNode {
    ExpressionNode *callee = nullptr;
    Vector<ExpressionNode *> arguments;
    Vector<StringName> argument_names; // parallel to `arguments`; empty = positional
    ...
};
```

In the argument loop of `parse_call`, before each argument: if the current token
is `IDENTIFIER` and the immediately following token is `EQUAL`, consume both and
record the name; then parse the value expression. Otherwise parse positionally
and push an empty `StringName`. The two vectors are kept exactly aligned in
length.

Disambiguation is unambiguous: Foundry Script assignment is a statement, never an
expression, so `IDENTIFIER` followed by `EQUAL` inside an argument can only be a
named argument. `f(a == b)` uses `EQUAL_EQUAL`; `f(a)` has no following `=`.
Neither is affected.

The parser records surface syntax only; it performs **no** semantic validation
(it does not know the callee). All rules in the Semantics section are enforced
by the analyzer.

### Analyzer

A canonicalization pass runs during call resolution, after the callee signature
is known. For any resolved-Foundry Script call that has at least one named argument:

1. **Validate ordering** (rule 1); reject named arguments on unresolved targets
   (rule 2) and the rest parameter (rule 5).
2. **Resolve names to indices** via `FunctionNode.parameters_indices`; unknown
   name → error (rule 3).
3. **Build a slot table** sized to the parameter count. Place positional
   arguments into slots `0..k-1`; place each named argument into its resolved
   slot. A collision is a duplicate error (rule 4).
4. **Fill gaps.** For any empty slot whose index is `< max_filled_index`, the
   parameter must be defaulted. If its default `initializer->is_constant`,
   synthesize a constant argument node from `default_arg_values[i]` into that
   slot. Otherwise → error (rule 7).
5. **Emit canonical positional order.** Rewrite the `CallNode`'s `arguments`
   into pure positional order over slots `0..max_filled_index`, and clear
   `argument_names`. Trailing unfilled slots stay omitted for the VM default
   mechanism.
6. **Reuse existing validation.** Run the current `validate_call_arg`
   type-checking on the now-canonical positional arguments, so type errors,
   conversions, default counts, and the `TOOLS_ENABLED resolved_parameter_types`
   cache all reuse existing logic unchanged.

After this pass, the remainder of the analyzer, all of codegen, and the LSP /
refactor surfaces see an ordinary positional call.

### Codegen / VM

No VM/ABI changes. The `CallNode` reaching the compiler holds a pure positional
`arguments` vector (middle gaps materialized as constants, trailing omissions
left to the callee). Existing call opcodes, the `OPCODE_JUMP_TO_DEF_ARGUMENT`
jump table, and rest-parameter packing operate exactly as today.

The one codegen refinement is evaluation order: when `CallNode.argument_evaluation_order`
is set, the compiler evaluates the argument expressions in that source order
(allocating each result into a temporary as usual) and then references the
resulting addresses positionally in the call. Because every argument is already
evaluated into a register before the call opcode is emitted, this is a pure
reordering of the argument-evaluation loop — the emitted opcode, the argument
register layout, and the temporary stack discipline are unchanged.

Synthesized constant gap-fill arguments flow through the normal
constant-argument path, so the callee's `use_conversion_assign` behavior for
that parameter is preserved (identical result for a constant value).

## Diagnostics

All are analyzer compile errors, each naming the offending parameter:

- Positional argument after named argument → "Positional argument cannot follow
  a named argument."
- Named argument on an unresolved / `Callable` target → "Named arguments require
  a statically known function; the callee here is dynamic."
- Unknown name → "Function 'f' has no parameter named 'x'."
- Duplicate (positional + named, or named twice) → "Parameter 'x' was already
  specified."
- Naming the rest parameter → "The rest parameter '...x' cannot be passed by
  name."
- Non-constant skipped middle default → "Cannot skip parameter 'b': its default
  value is not a constant expression. Pass it explicitly."
- Missing required after resolution → reuse the existing too-few-arguments
  error, worded via the missing parameter name.

## Testing

Following repo fixture conventions under `modules/foundry_script/tests/scripts/`:

**Runtime feature fixtures** (`runtime/features/`):

- Reordered named arguments produce correct values.
- Mixed positional + named.
- Skipping a trailing optional.
- Skipping a constant-default middle optional (`f(0, c = 5)`).
- Named arguments targeting fixed parameters before a rest parameter.
- Named arguments on a static call and on `_init`.

**Analyzer error fixtures** (`analyzer/errors/`): one per diagnostic above —
positional-after-named, unknown name, duplicate, rest-by-name, non-constant
middle skip, named argument on a `Callable` variable, missing required.

Regenerate `.out` files with `--gdscript-generate-tests` after the behavior is
in place, and run the suite headless with `dev_mode=yes` for CI parity.

## Editor / LSP completion

Named arguments are only useful if they are discoverable, so call-site
completion of `name =` is part of delivering the feature (not a follow-up). In
`modules/foundry_script/language_server/` and the completion path in
`gdscript_editor.cpp`:

- When the cursor is inside a call's argument list and the callee resolves to a
  known Foundry Script function, offer the declared parameter names as `name = `
  completion entries.
- Mirror the analyzer's acceptance rules so completion never suggests something
  that would error: only parameters not already supplied (positionally or by
  name), keep offering names once a named argument has appeared
  (positional-then-named), and never offer the rest parameter.
- Scope to statically resolved Foundry Script targets, matching the runtime feature;
  dynamic / `Callable` targets get no name completion.

The resolved parameter list and `FunctionNode.parameters_indices` already
provide names, order, and types, so this is localized. Surfacing the parameter
type/default in the completion detail is a nice-to-have.

## Follow-ups (out of scope for v1)

- **Non-constant middle-gap support** via a presence-bitmask calling convention
  (the rejected "Approach B"). Larger and runtime-affecting; only if real demand
  appears. The analyzer diagnostic and canonicalization pass are structured so
  this restriction can be lifted in a localized way. The same calling convention
  would also let a middle gap honor an override's runtime default instead of the
  static-type default it inlines today (see "Inlined defaults bind to the static
  type"); both are deferred together, and neither is planned absent real demand.
