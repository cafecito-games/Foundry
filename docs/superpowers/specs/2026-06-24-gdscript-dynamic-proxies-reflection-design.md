# GDScript Dynamic Proxies & Reflection — Design

**Status:** Approved (spec only; implementation tracked via GitHub epic)
**Date:** 2026-06-24
**Depends on:** Epic #125 (reified generics), traits + abstract methods (landed), GDScript namespaces (`2026-06-22-gdscript-namespaces-design.md`)

## 1. Summary

Add a runtime capability to GDScript for creating **dynamic proxies**: synthetic
objects that statically satisfy a trait or abstract type `T` and route every call
to `T`'s contract through a user-supplied handler `Callable`. The same primitive
serves two families of use cases with no special-casing:

- **Mocking** — the handler records calls and returns stubbed values instead of
  executing real behavior.
- **Interception / AOP (Spring-style)** — the handler logs, times, or otherwise
  wraps a call and then "proceeds" by forwarding to a real target object.

Alongside the proxy primitive, expose a small, read-only **reflection API** under a
`godot.reflection` namespace for introspecting types (methods, properties, trait
conformance, reified type arguments).

Mocking and AOP are deliberately *not* built into the engine. They are userland
patterns expressible on top of the primitive. The mock library is **out of scope**
for this epic; the test suite includes a mock-like fixture purely to validate the
primitive end-to-end.

## 2. Motivation

GDScript has no method-interception hook. Method dispatch is a direct lookup in the
script's method table (walking the base chain), with `_get`/`_set` fallback for
*properties only*. There is no `_call`/`method_missing` analog. This makes it
impossible today to write a stand-in object that satisfies a typed interface and
intercepts arbitrary calls — the foundation of mocking frameworks and AOP.

With reified generics (Epic #125) a type parameter `T` survives to runtime as a
descriptor, and with traits/abstract methods there is now a first-class notion of a
method contract. Together these make a type-safe dynamic proxy feasible:

```gdscript
class Mock[T]:
    var calls: Dictionary[StringName, Array] = {}
    var stubbed_returns: Dictionary[StringName, Variant] = {}

    func get_mock_object() -> T:
        return godot.reflection.create_proxy[T](func(method: StringName, args: Array):
            calls[method] = args
            return stubbed_returns.get(method, null)
        )
```

## 3. Scope

### In scope
- A C++ `ProxyScriptInstance` that intercepts all calls to `T`'s contract.
- `godot.reflection.create_proxy[T](handler) -> T` (typed) and a dynamic value form.
- Auto-backing property store on proxies.
- `is` / trait-conformance correctness for synthetic proxy instances.
- Handler contract, return-value coercion/validation, and error paths.
- A read-only `godot.reflection` introspection API.
- A GDScript stdlib delegation helper for the "intercept some, pass the rest through"
  AOP pattern.
- Documentation and tests (including a mock-like validation fixture).

### Out of scope
- A general-purpose mocking library (`Mock[T]` with matchers/verification). It is a
  downstream consumer; only a minimal mock-like *test fixture* is built here.
- A general GDScript `_call` / method-missing hook on ordinary classes. The proxy is
  a dedicated `ScriptInstance`, not a language-level fallback. (Could be a future,
  separate feature.)
- Proxying **concrete classes that have no trait or abstract supertype** (the
  CGLIB-style "subclass a concrete class" case). Proxy targets must be a trait or an
  abstract type. Programming-to-a-trait covers the idiomatic AOP/mock case.

## 4. Constraints from the generics design

Epic #125 uses **reified, single-script generics** (not monomorphization). A generic
class such as `class Mock[T]` compiles to one script; `T` is a runtime descriptor
carried on the instance. Therefore a proxy cannot be a per-`T` forwarder class
generated at compile time — it must be constructed from a **runtime type
descriptor**. This is why the primitive is a runtime `ScriptInstance` rather than
codegen, and it fits the reified model directly.

## 5. Architecture

### 5.1 `ProxyScriptInstance` (C++, `modules/gdscript/`)

A custom implementation of the `ScriptInstance` interface
(`core/object/script_instance.h`), hosted on a `RefCounted` Object so its lifetime is
managed. It is bound at construction to:

- the **target script** `T` (trait or abstract type) — used for contract scanning,
  `get_script()`, and `is`/trait checks;
- the **handler** `Callable`;
- an internal **property backing store** (`HashMap`/`Dictionary`).

#### Call dispatch (`callp`)

Let *contract* = every method declared anywhere in `T`'s script-level hierarchy
(the class/trait/abstract chain), including both abstract methods and any concrete
methods, but **excluding** native `RefCounted`/`Object` methods.

1. If `method_name` ∈ *contract* → invoke `handler.call(method_name, args_array)`
   and return its (coerced) result.
2. Otherwise → return `GDScriptFunction::CALL_ERROR_INVALID_METHOD` so
   `Object::callp` falls through to `ClassDB` native methods. This keeps
   `get_instance_id`, `connect`, `free`, reference counting, etc. working normally —
   **native built-ins are never intercepted**.

The proxy **never executes `T`'s real method bodies**. Even concrete methods declared
on an abstract base are routed to the handler; "proceeding" to real behavior is the
handler's job (it calls a captured target). This gives one simple, predictable rule:
*everything in `T`'s script contract is intercepted; everything below `RefCounted` is
native.*

#### Type identity

`get_script()` returns `T`'s script. `is` checks and trait-conformance checks consult
the script (and its base/trait chain), so `proxy is T` and `proxy is <any base
trait/abstract of T>` are true, and the static `-> T` return type is sound at runtime.

#### Instantiation bypass

Traits and abstract types normally cannot be instantiated. Proxies are constructed
through a dedicated C++ path that sidesteps the abstract-instantiation guard; there is
no public `new()` on the proxy.

### 5.2 Static typing & the `create_proxy` API

Primary, type-safe form:

```gdscript
static func create_proxy[T](handler: Callable) -> T
```

The caller supplies `T` as a type argument (explicit or inferred). Inside a generic
class the enclosing parameter is forwarded:

```gdscript
return godot.reflection.create_proxy[T](handler)
```

The analyzer types the result as `T`; at runtime the call receives `T`'s reified
descriptor, which the C++ implementation uses to scan the contract and build the
property list.

This relies on three generics-epic capabilities, which are explicit dependencies:

1. generic **functions/methods** with their own type parameters;
2. **forwarding** an enclosing class's type parameter `T` into a generic call;
3. **reification** of the type argument at the call site (so C++ can read the bound
   script descriptor).

Dynamic (untyped) form for fully runtime cases where `T` is not statically known:

```gdscript
static func create_proxy_dynamic(type: Script, handler: Callable) -> Object
```

Returns an `Object` the caller must cast/duck-type. Useful for tooling and generic
infrastructure; not the common path.

### 5.3 Property model

Auto-backing store by default:

- `set(name, value)` / `get(name)` read and write an internal dictionary for any
  property declared in `T`'s contract; defaults are the zero value of the declared
  type. Properties behave as plain data slots — exactly what mock setup wants.
- `get_property_list()` is synthesized from `T`'s declared `var`s, so properties are
  visible to reflection and the editor **without any user-written accessors**.

The core does **not** route property access through the handler. Spying on or
delegating property access is the delegation helper's job (§5.5).

### 5.4 Handler contract

```gdscript
func(method_name: StringName, args: Array) -> Variant
```

- `args` is a plain `Array` of the call's arguments (the proxy is varargs at the
  dispatch layer).
- The return value is **coerced** to the intercepted method's declared return type;
  in debug builds a mismatch raises an error. For `void` methods the return value is
  ignored.
- Error paths:
  - null/invalid handler at construction → error, no proxy created;
  - handler raises → propagated to the caller as a normal call error;
  - method not in contract → standard "invalid method" (falls through to native).

### 5.5 Delegation helper (GDScript stdlib)

A pure-GDScript convenience built on the core, for the common AOP shape of
"intercept a few methods, pass everything else through to a real object":

```gdscript
godot.reflection.create_delegating_proxy(T, target, interceptor)
```

It wraps `create_proxy`: the handler consults `interceptor` for a method; if the
interceptor declines, it forwards to `target.callv(method, args)`. Property reads and
writes likewise forward to `target`. This keeps the trusted C++ surface minimal while
giving ergonomic Spring-style interception in userland.

Pure-core delegation (no helper) is always available, since the handler is an
arbitrary `Callable`:

```gdscript
var real := UserService.new()
var logged: UserService = godot.reflection.create_proxy[UserService](
    func(method: StringName, args: Array):
        var result = real.callv(method, args)   # "proceed"
        print("%s -> %s" % [method, result])
        return result
)
```

### 5.6 `godot.reflection` read-only API

Exposed under the `godot.reflection` namespace (see §6 on exposure). Read-only
introspection plus the proxy constructors:

- `create_proxy[T](handler: Callable) -> T`
- `create_proxy_dynamic(type: Script, handler: Callable) -> Object`
- `create_delegating_proxy(type, target, interceptor)` (stdlib helper)
- `get_methods(type) -> Array` — method descriptors (name, args, return type, flags)
- `get_method_info(type, name: StringName) -> Dictionary`
- `get_properties(type) -> Array` — property descriptors
- `implements_trait(obj_or_type, trait) -> bool`
- `get_type_arguments(obj) -> Array` — reified generic bindings on an instance

The introspection calls largely wrap existing `Script`/`ClassDB` introspection plus
the reified type-argument access added by Epic #125.

## 6. Open implementation questions

- **Namespace exposure.** The intended surface is `godot.reflection.*`. Whether this
  is a true namespace (per `2026-06-22-gdscript-namespaces-design.md`) or a nested
  singleton object (`godot` exposing a `reflection` member) is an implementation
  detail to resolve when namespaces land. The API names above are the contract; the
  binding mechanism is flexible.
- **Type-argument forwarding ergonomics.** Exact syntax/feasibility of
  `create_proxy[T](...)` forwarding the enclosing `T` depends on the final generics
  surface. If type-argument forwarding to free/static generic functions is not
  available at first, the dynamic form (`create_proxy_dynamic`) plus an explicit
  reified descriptor is the fallback.
- **Return-type validation strictness** in release builds (coerce silently vs. skip).

## 7. Testing strategy

- **C++ doctest** (`tests/` / module tests) for `ProxyScriptInstance`: dispatch
  precedence (contract vs. native), `get_script`/`is` identity, property backing,
  handler errors, return coercion, instantiation bypass.
- **GDScript fixtures** under `modules/gdscript/tests/scripts/` for end-to-end
  behavior: proxying a trait, `is`-checks against the trait and its bases, property
  read/write, the delegation helper, and a **mock-like fixture** (record calls, stub
  returns, verify) that demonstrates the primitive without shipping a mock library.
- Regenerate `.out` fixtures after intentional behavior changes per repo workflow.

## 8. Work breakdown (GitHub epic sub-issues)

1. `ProxyScriptInstance` C++ core — interception, dispatch precedence, `get_script`/
   lifetime, instantiation bypass.
2. `create_proxy[T]` static typing — reified type-arg plumbing, analyzer return type,
   dynamic value form.
3. Auto-backing property model — `get`/`set`/`get_property_list` synthesized from `T`.
4. `is` / trait conformance for synthetic proxy instances.
5. Handler contract — `(StringName, Array) -> Variant`, return coercion/validation,
   error paths.
6. `godot.reflection` read-only API — `get_methods`/`get_method_info`/
   `get_properties`/`implements_trait`/`get_type_arguments` and namespace exposure.
7. Delegation stdlib helper — `create_delegating_proxy`, auto-forward unhandled
   methods and properties.
8. Docs & integration test sweep — class reference XML, mocking + AOP examples, and
   the mock-like end-to-end validation fixture.
