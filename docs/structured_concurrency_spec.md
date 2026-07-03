# Foundry Script Structured Concurrency — Specification

Source-level structured-concurrency typing for Foundry Script. Builds on the reflected
`async func` contracts from epic #54 and the typed-callable work, adding two
first-class, statically-typed concepts so async work can be passed around and
held as typed values.

Tracking epic: #332.

## The three lifecycle types

```
AsyncCallable[[String], String]  --.call(x)-->  Coroutine[String]  --await-->  String
```

- **`AsyncCallable[[Args], Return]`** — a typed reference to an async callable.
  Still `Variant::CALLABLE` at runtime; async-ness comes from the target
  method's `METHOD_FLAG_ASYNC` and is carried on the signature metadata via
  `DataType::signature_is_async`.
- **`Coroutine[T]`** — a typed handle to an in-flight async computation that
  will eventually produce a `T`. This is what `AsyncCallable.call(...)` returns
  before it is awaited, and what an unawaited async call evaluates to. Maps onto
  the existing `GDScriptFunctionState` (RefCounted, `completed` signal,
  `resume`, `is_valid`); `T` is a phantom type parameter (the VM carries a
  `Variant` result).
- **`await`** consumes a `Coroutine[T]` and produces `T`.

## Substrate in place

- `METHOD_FLAG_ASYNC = 256` on `MethodInfo.flags` (`core/object/object.h`).
- `DataType::is_coroutine` (`gdscript_parser.h`), set from the flag and cleared
  by `await` (`gdscript_analyzer.cpp`).
- `DataType::signature_is_async` distinguishing `AsyncCallable` from `Callable`.
- Typed callables/signals with rich signature metadata
  (`method_parameter_types`, `method_return_type`,
  `has_explicit_method_signature`).
- Runtime awaitable `GDScriptFunctionState`.
- Missing-await enforcement (error in expression position / warning at root).

---

# Phase 2 design note — `Coroutine[T]`

Resolves the design decisions in #338, sufficient to implement #339 (parser)
and #340 (analyzer threading).

## Representation: honest typing, not a flag over `T`

Today an unawaited async call is represented as **`T` decorated with an
`is_coroutine` flag** — the `DataType` *is* the return type. The flag is set in
five places in `gdscript_analyzer.cpp` (the AsyncCallable `.call`/`.callv`
sites, the Foundry Script async-function call site, and the native
`METHOD_FLAG_ASYNC` site) and cleared in exactly one (`reduce_await`). That
representation is unsound: `var x = coro()` statically claims to be the return
type, but at runtime `x` is a `GDScriptFunctionState`.

`Coroutine[T]` becomes an honest type whose **principal identity is "Coroutine"**
and which carries `T` in `container_element_types[0]` (the slot `Array[T]`
uses), while **keeping `is_coroutine = true`** as the discriminator so the
existing await / missing-await pipeline is reused unchanged. The change from the
status quo is that the `DataType`'s surface identity is no longer `T`, so it is
**not** assignment-compatible with `T`.

The five flag-set sites collapse into one helper — `make_coroutine_type(T)` —
that wraps a result type into `Coroutine[T]`.

Representation constraints for the analyzer implementer (#340). The encoding
must:
1. stringify as `Coroutine[T]`;
2. be distinct from / incompatible with `T` in assignment and alpha-equality;
3. keep `is_coroutine = true`;
4. carry `T` in `container_element_types[0]`.

### Pinned encoding: NATIVE over `GDScriptFunctionState`

```
kind                    = NATIVE
native_type             = "GDScriptFunctionState"
is_coroutine            = true     // discriminator, reused
container_element_types = [ T ]    // the phantom result type
```

"Coroutine" is a **source-level skin** applied in `to_string()` and at the parse
site — the same approach `Type[T]` (#568) takes by skinning a `CLASS`/`NATIVE`
type rather than introducing a new `Kind` (`gdscript_parser.cpp:6262`).

There is deliberately **no `Variant::COROUTINE` builtin**: `Variant`'s type enum
is a closed engine-wide set, and a coroutine is never a `Variant` value — at
runtime it is an `Object` (`GDScriptFunctionState`, a registered `RefCounted`).

Why NATIVE over `GDScriptFunctionState` rather than a pure synthetic flag
(`BUILTIN`/`OBJECT` with no native class):

- **Runtime-truthful** — the static type *is* the class the value actually has;
  that honesty is the whole premise of this phase.
- **Null / RefCounted semantics, and ClassDB resolution, come for free** (the
  class is registered via `FOUNDRY_CLASS`).
- **Reuses `is_coroutine`** as the discriminator, so the five set-sites and
  `reduce_await` keep working with minimal change.
- A future `is_valid() -> bool` member would resolve through normal native
  member lookup with no special casing.

The one cost the skin imposes: the native name must never leak. Route all
rendering through the `is_coroutine` branch (see `to_string()` below).

### Implementation obligations (#340)

1. **`to_string()`** — add an `is_coroutine` branch *first* (mirroring the
   `is_type_handle_annotation` check at `gdscript_parser.cpp:6262`), rendering
   `Coroutine[%s]` from `container_element_types[0]`. Single choke point, so the
   native name cannot leak into hovers / errors / completion.
2. **`operator==`** (`gdscript_parser.h`, NATIVE branch) — the NATIVE case
   compares `native_type` only; add a `container_element_types` compare (or a
   top-level `is_coroutine` guard). `_datatype_alpha_equal` already recurses
   `container_element_types` for any kind, so it mostly works as-is.
3. **`GDScriptTypeCompatibility::check`** (`gdscript_type.cpp`) — NATIVE→NATIVE
   compat is inheritance-based and ignores element types. Add a branch: if
   either side is `is_coroutine`, require *both* to be and check element[0]
   **invariantly** (clone the `Array` element branch at
   `gdscript_type.cpp:313–322`). This is the one genuinely new piece of
   compatibility code.
4. **Parse-site recognition** — recognize `Coroutine` in type-annotation
   position like the `Type` special-case at `gdscript_analyzer.cpp:1530`, not as
   a real class; route `T` into `container_element_types[0]`.

Invariant to hold: `is_coroutine == true` now *always* means "principal identity
is Coroutine, result type in `container_element_types[0]`." The
`make_coroutine_type(T)` helper consolidation is what enforces this across the
five set-sites and `reduce_await`.

`container_element_types[0]` is used for `T` (not `type_arguments[0]`) because it
reuses the `has_`/`get_container_element_type` plumbing, the `alpha_equal`
recursion, and the array-style invariant compat; `type_arguments` would buy
nothing here.

## `await` operand-dispatch rules

`reduce_await` dispatches on the operand's static type, in this precedence:

```
t = operand.datatype
if t.is_coroutine:        # Coroutine[T] — async calls AND AsyncCallable.call()
    result = t.container_element_types[0]   if present
           = Variant                        if empty (untyped / bare AsyncCallable.call)
elif t is Signal:
    result = Variant      (unchanged; type_source = UNDETECTED)
else:
    result = t            (identity)
    if not t.is_variant(): push REDUNDANT_AWAIT  (DEBUG only)
```

- Check `is_coroutine` **before** Signal (a Signal is never `is_coroutine`, so
  ordering is safe, but be explicit).
- `await` on a plain `Variant` operand → identity `Variant`, **no** warning.
- Single-level unwrap only: `await Coroutine[Coroutine[U]]` → `Coroutine[U]`.
- Delta from current `reduce_await`: instead of "clear the flag and keep the
  type," extract `container_element_types[0]`.

## Type of an unawaited async call + migration

An unawaited async call, `AsyncCallable.call`, and a native `METHOD_FLAG_ASYNC`
call all yield `Coroutine[T]`.

- **Unchanged:** `var x: T = await coro()` — RHS unwraps to `T`.
- **Newly valid (loosening, safe):** `var x = coro()`,
  `jobs.append(downloader.call(f))`, `var jobs: Array[Coroutine[String]] = []`.
  Previously hard errors in expression position; no previously-valid code breaks
  from a loosening.
- **Intended behavioral break (the soundness fix):** at root scope,
  `var x = coro()` today only warns and infers `x : <return type>`, so
  `x.length()` passed analysis and then misbehaved at runtime. Under honest
  typing `x : Coroutine[T]`, so `x.length()` becomes a compile error. This only
  affects code that was already runtime-broken (using the unsound inferred type
  without awaiting). Document in the release notes.

## Root-scope soundness gap

Honest typing closes the gap: `var x = coro()` now has the type its runtime
value actually has. The missing-await rule is **repurposed, not removed**:

- **Discarded coroutine statement** (`coro()` as a bare statement) → keep the
  `MISSING_AWAIT` warning ("you probably forgot `await`").
- **Held / assigned / passed coroutine** → no diagnostic; this is the epic's
  purpose (store-and-await-later, fan-out). Was an error before; now well-typed.

The discarded-statement case stays a **warning** (not strengthened to an error):
fire-and-forget is sometimes intentional, and honest typing already removed the
unsoundness that motivated stronger enforcement.

## Relationship to `AsyncCallable`

`AsyncCallable[[Args], Return].call(args) : Coroutine[Return]`.

The three AsyncCallable sites that today set `is_coroutine = true` route through
`make_coroutine_type(method_return_type)`. The bare/untyped AsyncCallable case
(no signature metadata) yields `Coroutine[Variant]`, which `await` unwraps to
`Variant`.

## Constructibility and value semantics

`Coroutine[T]` is **nameable and holdable, but not source-constructible** —
mirroring `Signal`.

- Usable in annotations, assignable, returnable, storable in containers
  (`Array[Coroutine[String]]`).
- No `Coroutine.new()`, no literal, no source constructor; only the
  analyzer/runtime mints them. Not extendable.
- Assignment is **invariant in `T`** (matching typed `Array`/`Callable`
  alpha-equality).
- The **nullable variant is allowed** (the runtime value is a RefCounted Object
  that can be null).

## Members exposed to source

**None for Phase 2** — `Coroutine[T]` is opaque; its only source operation is
`await`. `GDScriptFunctionState`'s `resume()` / `is_valid()` / `completed` are
not surfaced: `resume()` would let users hand-drive the coroutine (breaking
structured-concurrency invariants and returning `Variant`, defeating the phantom
`T`). `is_valid() -> bool` is the only plausibly-safe future addition, but is out
of scope. Keeping the surface await-only preserves `T` as a true phantom and
matches the epic non-goals (no `gather`/`join`, no VM change).
