# Design: reified type-argument bindings through inheritance

Tracking issue: cafecito-games/godot#324 (part of epic #125).
Coordinates the implementation of #255, #294, #305; cross-checked against #240, #242, #182.

## Purpose

#255, #294, and #305 look like three different bugs but share one root cause in the
runtime reified-generics layer. This note fixes a **single resolution rule** and the
**instance representation** so those three issues implement and test their respective
cases against one model rather than three independently chosen mechanisms that would
conflict. It stops short of writing the patches; each issue lands its own change and
fixtures against this rule.

## The shared root cause

Runtime member-type validation indexes the **leaf instance's** `type_arguments` by a flat
`MemberInfo::type_parameter_index` (`modules/gdscript/gdscript.cpp:1637`), and the
`OPCODE_GET_TYPE_PARAMETER` path for `create_proxy[T]` does the same
(`modules/gdscript/gdscript_vm.cpp:1423`). Both assume the member's declaring class *is*
the leaf class. Two facts make that assumption wrong:

- A subclass seeds `member_indices = base->member_indices`
  (`modules/gdscript/gdscript_compiler.cpp:3226`), copying each inherited member's
  `type_parameter_index` — an ordinal relative to the member's **declaring** class — with
  no record of which class declared it.
- The instance's `type_arguments` is only ever populated by `_new_specialized`
  (`gdscript.cpp:166`–`176`) with the **leaf** class's arguments.

So an inherited member's ordinal is indexed into the wrong argument vector. The mapping
from a declaring class's parameter, through each `extends Base[...]` application, down to
the leaf is **purely static** — it depends on the class hierarchy, not on runtime
arguments — yet today it is never computed.

The single omission surfaces as three facets:

- **#305 / #255 (concrete specialization):** a non-generic subclass of a specialized base
  (`class Mock extends Base[Greeter]`, `class IntBox extends Box[int]`) builds an instance
  whose `type_arguments` is **empty** → inherited-member validation is silently skipped and
  `create_proxy[T]` materializes a null script.
- **#294 (fixed intermediate base):** a generic leaf with a fixed base
  (`class C[T] extends B[int]`, `B[K] extends A[K]`, `C[float].new()`) *has* args `[float]`,
  but inherited `a_value`/`b_value` are fixed to `int` by `extends B[int]` — yet
  flat-indexing the leaf args reifies them as `float`. The leaf args must **not** win here.
- **#255 (reordered / multi-parameter bases):** `class Derived[A, B] extends Base[B]` would
  index a `Base`-relative ordinal into the leaf's `type_arguments`, validating against the
  wrong argument. Currently unreachable (multi-arg specialized construction does not parse
  yet) but a latent footgun.

## 1. The resolution rule

A type-parameter member's reified argument is resolved **per declaring level** and mapped
down the `extends` chain to the leaf, via a pure compile-time function over the hierarchy:

> **`resolve(L, D, i)`** — for leaf class `L`, the binding of declaring class `D`'s type
> parameter at ordinal `i`:
>
> - If `D == L` → **`OPEN(i)`**.
> - Otherwise let `C` be the child of `D` in `L`'s chain (the class whose
>   `extends D[args]` applies arguments to `D`), and `expr = args[i]`:
>   - `expr` is a concrete type → **`FIXED(reify(expr))`**.
>   - `expr` references `C`'s own parameter `j` → **`resolve(L, C, j)`** (recurse).
>
> The recursion bottoms out at `FIXED(...)` or `OPEN(leaf ordinal)`.

`L`'s inheritance chain is linear (single class inheritance; trait members are flattened
into the implementer at compile time, so they carry no separate parameter scope here).
`reify(expr)` produces the same `ContainerType` the analyzer already resolves for an
`extends Base[...]` specialization argument.

Worked against each facet:

| Case | Member | Resolves to |
|------|--------|-------------|
| **#294** `C[T] extends B[int]`, `B[K] extends A[K]`, `C[float]` | `a_value` (A's `T`) → via B's `K` → `extends B[int]` | **FIXED(int)** |
| | `b_value` (B's `K`) → `extends B[int]` | **FIXED(int)** |
| | `c_value` (C's `T`) | **OPEN(0)** → `float` |
| **#305** `Mock extends Base[Greeter]` | Base's `T` (in `create_proxy[T]`) | **FIXED(Greeter)** |
| **#255** `Derived[A, B] extends Base[B]` | Base's param 0 | **OPEN(1)** — Derived's 2nd arg |
| regression `Derived[U] extends Base[U]`, `Derived[int]` | Base's param 0 | **OPEN(0)** → `int` |

This replaces the flat-index assumption at both `gdscript.cpp:1637` and
`gdscript_vm.cpp:1423`.

## 2. Instance representation

**The instance stores only the leaf class's own open arguments** — exactly what
`_new_specialized` already places in `instance->type_arguments`. A non-generic leaf
(`Mock`, `IntBox`) carries an **empty** `type_arguments`, and that is correct: all of its
inherited bindings are `FIXED` and resolved statically. **No per-level argument table is
reified onto instances.** This is the explicit answer to #305's open question about the
representation for multi-level and mixed generic/non-generic inheritance: nothing
hierarchy-shaped lives on the instance.

Everything hierarchy-shaped is precomputed once at compile time as a single binding value:

```
TypeArgumentBinding {
    enum { NONE, FIXED, OPEN } tag = NONE;
    ContainerType fixed;   // valid when tag == FIXED
    int leaf_ordinal = -1; // valid when tag == OPEN
};
```

One `resolve(L, D, i)` computation feeds two consumers:

### Member-write validation (`GDScriptInstance::set`)

`MemberInfo`'s bare `type_parameter_index` is replaced by a resolved
`TypeArgumentBinding`. When a subclass copies `base->member_indices`
(`gdscript_compiler.cpp:3226`), each inherited type-parameter member's binding is
**re-resolved against the subclass's chain** with `resolve(L, D, i)` — this is the step
the current blind copy omits. The declaring class `D` and its ordinal `i` are recovered
from the analyzer-resolved member datatype (the same `TYPE_PARAMETER` information the
compiler reads today at `gdscript_compiler.cpp:3314`).

Runtime `set()` then branches on the tag:

- `FIXED` → validate the write against the baked `ContainerType`.
- `OPEN` → if `leaf_ordinal < instance->type_arguments.size()`, validate against
  `type_arguments[leaf_ordinal]`; otherwise skip (the existing untyped-slot behavior for an
  unspecialized generic leaf, preserved unchanged).
- `NONE` → not a type-parameter member; existing non-parameter validation path.

### `create_proxy[T]` (`OPCODE_GET_TYPE_PARAMETER`)

The leaf script `L` holds a per-ancestor binding table — keyed by declaring `GDScript*`,
each entry a `Vector<TypeArgumentBinding>` indexed by that ancestor's parameter ordinal,
populated at compile time from `resolve(L, D, i)`. The opcode already knows the declaring
`_script` and the ordinal; it looks up `L`'s table for `_script` and branches:

- `FIXED` → the baked script (`fixed.script`).
- `OPEN` → `instance->type_arguments[leaf_ordinal].script` when present.

This replaces the current conservative `p_instance->script.ptr() == _script` null-guard
(`gdscript_vm.cpp:1423`), which deliberately resolves to null for derived instances.

## 3. Ordinal mapping for reordered / multi-parameter bases

Handled intrinsically by the rule: `OPEN(j)` carries the **leaf** ordinal `j`, which the
recursion derives by following each `extends Base[...]` application. So
`Derived[A, B] extends Base[B]` maps Base's param 0 → Derived's param 1, and a Base member
typed as Base's param 0 validates against the leaf's *second* argument rather than its
first. The model is correct now; its **test** is deferred until `Pair[int, String].new()`
multi-argument construction parses (per #255's framing), and is listed as pending in the
matrix.

## 4. Interaction with #240 and #242

The representation adds **no competing per-instance structure** — it only refines how the
existing `type_arguments` vector plus the static bindings are read. Therefore:

- **#242** (stored/aliased specialized handle, `const IntBox = Box[int]; IntBox.new()`):
  whatever vehicle carries the reified arguments feeds the same `_new_specialized`
  `type_arguments` vector for the leaf's `OPEN` parameters; `FIXED` parameters need no
  propagation. This design recommends #242's "specialized class-handle representation"
  route precisely because it funnels into the one existing vehicle rather than introducing
  a second.
- **#240** (typed-container element metadata): a `Box[int]` element descriptor supplies its
  arguments through the identical `ContainerType.type_arguments` channel. `FIXED` bindings
  *are* `ContainerType`s, so element-metadata tracking and instance bindings compose
  without conflict.

## 5. Open considerations (preserved behavior, documented)

- **Unspecialized generic leaf** (`C.new()` with no arguments): `OPEN` bindings have no
  instance argument to index; validation is skipped (untyped slot), exactly as today.
- **Bounded type parameters** (`T: Node`): when `OPEN` and unspecialized, validation
  against the parameter's declared bound is **out of scope**; the skip behavior is kept.
- **Trait members:** flattened into the implementer at compile time, so they resolve under
  the implementer's own parameter scope and need no separate handling in the rule.

## 6. Test matrix

The deliverable for #324; #255/#294/#305 each contribute their rows.

1. **Empty-arg concrete subclass** — `IntBox extends Box[int]` (#255) and
   `Mock extends Base[Greeter]` (#305): a wrong-typed inherited write is rejected, a
   correct write accepted, and `create_proxy[Greeter]` materializes a non-null proxy.
2. **Fixed intermediate base** (#294) — `class C[T] extends B[int]`, `B[K] extends A[K]`,
   `C[float].new()` prints `1 2 3.5`; `a_value`/`b_value` reify and validate as `int`;
   `c_value` reifies as `float`; a wrong-typed write to `a_value` (e.g. `1.5`) is rejected.
3. **Reordered / multi-parameter base** (#255 footgun) —
   `class Derived[A, B] extends Base[B]`: a Base member resolves to Derived's 2nd argument.
   **Pending** until multi-argument specialized construction parses.
4. **Multi-level mixed** generic / non-generic chain — e.g. a non-generic leaf over a
   `… extends M[int]`, `M[K] extends N[K]` chain: both member validation and
   `create_proxy[T]` resolve correctly across every level.
5. **Regression** — aligned single-parameter `Derived[U] extends Base[U]`,
   `Derived[int].new()` still validates a wrong-typed inherited write (already passing;
   guards against regression from the rule change).
6. **Cross-checks** with #242 alias handle and #240 typed-container element — instances
   built through an aliased handle or held as a typed-container element bind and validate
   identically to the direct form. These may live in #242/#240's own fixtures.

Fixtures land under `modules/gdscript/tests/scripts/runtime/` (runtime reification and
rejection) alongside the existing analyzer fixtures
(`analyzer/errors/generic_inherited_member_declaring_scope`).
