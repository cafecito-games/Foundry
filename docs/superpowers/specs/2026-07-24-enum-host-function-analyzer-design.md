# Enum Host Function Analyzer Design

## Goal

Analyze the enum-hosted functions parsed by #1116 without extending call dispatch. Named class enums
and `enum_name` files must expose resolved function signatures and bodies, exact enum-value `self`
typing, unqualified access to their own values, and deterministic namespace diagnostics.

## Slice Boundary

This change owns declaration and body semantics only:

- resolve enum values before enum function signatures;
- resolve enum function bodies in their owning enum context;
- type instance `self` and receiver-relative `Self` as the exact containing enum value type;
- keep `self` unavailable in static enum functions through the existing parser rule;
- diagnose value/function, function/function, and static-function/Dictionary-method conflicts;
- prevent enum function bodies from binding to containing-class instance state;
- preserve existing enum-metatype Dictionary behavior.

The change does not resolve calls to enum functions or add call metadata (#1118), compile or dispatch
enum functions (#1119), persist them in bytecode (#1120), format them (#1121), or expose them through
LSP/completion/docs (#1122).

## Analyzer Structure

Add shared enum helpers to `FSAnalyzer` instead of duplicating the member-enum and `enum_name` paths.
The value helper receives the enum node, its owning class, and the canonical enum metatype. It reduces
explicit value expressions, fills `enum_values`, freezes the Dictionary value, and installs the final
datatype on the enum node.

The interface helper calls value resolution first, validates the enum member namespace, then resolves
every stored function signature with `parser->current_class`, `parser->current_function`, and
`current_enum` set to the enum's owning scope. The body helper uses the same scope to resolve every
function body after the class interface is complete. Both helpers are idempotent through the existing
enum datatype and per-function `resolved_signature`/`resolved_body` flags.

Named class enums invoke the interface helper from `resolve_class_member()`. `enum_name` files invoke
the same helper during `resolve_class_interface()`. `resolve_class_body()` invokes the shared body
helper for both forms.

## Exact Enum `self` and `Self`

For an instance enum function, the source of truth is
`type_from_metatype(function->owner_enum->get_datatype())`. This produces a non-meta
`DataType::ENUM` with `Variant::INT` storage while retaining the enum name, owner class, script path,
native identity, and resolved value map.

`reduce_self()`, `Self` type resolution, and explicit `Self` type arguments use that concrete enum
datatype when `parser->current_function->owner_enum` is set. Enums cannot be inherited, so the
receiver-relative class `@Self` type parameter is neither needed nor correct here. Ordinary class and
trait `Self` behavior remains unchanged.

Function names such as `_init` are ordinary enum function names. Enum signatures do not participate
in containing-class constructor or override checks because enum functions are not class methods.

## Namespace and Isolation Diagnostics

The interface helper validates one shared enum member namespace:

- a function cannot reuse an enum value name;
- static and instance functions cannot reuse one another's names;
- a static function cannot use any name reported by
  `Variant::has_builtin_method(Variant::DICTIONARY, name)`.

Using the Variant Dictionary method registry keeps the diagnostic aligned with the same runtime method
surface used by analyzer call validation and avoids a stale hard-coded list. Instance enum functions
may use Dictionary-like names because enum values do not expose the metatype Dictionary surface.

While resolving an enum function body, bare containing-class instance variables, functions, and
signals are rejected. Qualified access through enum `self` naturally resolves against the enum
datatype and therefore cannot expose the outer class. Static containing-class members remain
qualified class concerns and are not made into enum members.

## Testing

Add C++ analyzer tests that inspect the resolved enum function metadata and the datatype stored on a
`self` expression. Cover both a named class enum and an `enum_name` file.

Add analyzer fixtures for:

- instance `self`/`Self`, unqualified values, qualified values, parameters, and returns;
- static `self` rejection;
- value/function conflict;
- duplicate static/instance function name;
- static Dictionary-method conflict;
- rejection of containing-class instance state;
- unchanged `keys()` support and unchanged non-const `clear()` diagnostics on enum metatypes.

The red proof runs new tests against the unchanged analyzer. Focused green verification rebuilds and
runs the enum-host analyzer tests plus the Foundry Script fixture suite. Final verification uses a
strict `dev_mode=yes` build and the full command-first test suite.
