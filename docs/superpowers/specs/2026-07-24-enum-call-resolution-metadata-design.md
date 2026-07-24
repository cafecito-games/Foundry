# Enum Call Resolution Metadata Design

## Goal

Resolve static and instance functions declared on Foundry Script enums and record an explicit,
pointer-free dispatch identity on each successful call. The compiler/runtime child issue must be
able to consume that identity without inferring enum dispatch from syntax or from the enum's runtime
`Variant` representation.

## Slice Boundary

This change owns analyzer call and attribute resolution only:

- resolve instance enum functions on enum-value receivers;
- resolve static enum functions on enum-metatype receivers;
- expose the same functions as precisely typed first-class `Callable` attributes;
- reject static-on-value and instance-on-metatype calls with targeted diagnostics;
- preserve existing Dictionary methods on enum metatypes when no enum static function matches;
- record stable enum-call metadata for same-file, nested, and cross-file calls.

The change does not compile or dispatch enum calls (#1119), persist enum functions in bytecode
(#1120), change formatting (#1121), or add completion/refactor/docs surfaces (#1122).

## Enum Declaration Lookup

Script enum datatypes already carry the information needed to find their declaration:
`script_path`, the owning `class_type`, and `enum_type`. A shared analyzer helper first resolves the
owner class interface, then maps the datatype to:

- `class_type->enum_file_decl` for an `enum_name` file; or
- the owning class member whose name equals `enum_type` for a named enum.

Native, builtin, and global engine enums do not carry a Foundry Script `class_type`, so they never
enter enum-host-function lookup and keep their current behavior.

`get_function_signature()` performs this lookup before the existing enum-to-Dictionary fallback. If
the enum declares the requested name, the helper returns the resolved Foundry Script function
signature. If it does not, an enum metatype continues through the Dictionary builtin path, while an
enum value reports a missing enum function.

## Static and Instance Rules

Function-name lookup is intentionally independent from receiver kind so the analyzer can distinguish
a wrong receiver from a missing function:

- a static enum function called on the enum metatype succeeds;
- an instance enum function called on an enum value succeeds;
- a static function called on an enum value emits a static-on-value error;
- an instance function called on the enum metatype emits an instance-on-type error.

The resolved enum function participates in the existing named-argument, parameter, return,
coroutine, and first-class `Callable` typing paths. Enum functions do not use class inheritance or
virtual dispatch, so their exact enum receiver type is used for `Self` substitution.

Attribute lookup follows the same receiver-kind rule. Enum values remain the first metatype match.
After that, a matching static function becomes a typed `Callable`; on an enum value, a matching
instance function becomes a typed `Callable`. Wrong-kind attributes remain unresolved so call
resolution can produce the targeted receiver diagnostic.

## Stable Call Metadata

`FSParser::CallNode` gains a three-state enum call kind:

```cpp
enum EnumCallKind {
	ENUM_CALL_NONE,
	ENUM_CALL_STATIC,
	ENUM_CALL_INSTANCE,
};
```

A successful enum function call records:

- `enum_call_kind`;
- `enum_call_owner_script_path`;
- `enum_call_owner_class`;
- `enum_call_enum_type`;
- `enum_call_function`.

The owner path plus owner class FQCN locates the parser/class across files and nested classes. The
enum type selects either the named enum member or the `enum_name` declaration, and the function name
selects the declaration. These scalar fields remain valid independently of parser-node addresses.
Ordinary calls, invalid enum calls, and Dictionary fallback calls retain `ENUM_CALL_NONE` and empty
identity fields.

Storing the full analyzer `DataType` would carry transient parser pointers and unrelated state.
Storing an `EnumNode *` would be even less stable across dependency parsers. The scalar tuple is the
smallest contract that is both explicit and sufficient for the later compiler lookup.

## Diagnostics

The call site reports receiver-specific errors:

- `Cannot call static enum function "parse()" on enum value "Status".`
- `Cannot call instance enum function "label()" on enum type "Status".`
- `Function "label()" does not exist for enum value "Other".`
- `Function "missing()" does not exist for enum "Status" or its Dictionary methods.`

Existing non-const Dictionary diagnostics remain unchanged. In particular, a missing enum static
function named `clear` still reaches the Dictionary path and reports that non-const Dictionary
methods cannot be called on an enum.

## Testing

C++ analyzer tests inspect the AST and prove:

- literal, typed-variable, and static calls carry the correct kind and identity;
- Dictionary fallback carries no enum-call metadata;
- first-class enum function attributes have precise callable signatures;
- nested enum calls record the nested owner FQCN;
- a cross-file `enum_name` call records the dependency path and owner identity.

Analyzer fixtures cover successful calls, receiver-kind errors, cross-enum isolation, preserved
`keys()` support, and preserved invalid Dictionary-call diagnostics. Success fixtures use `.norun.fs`
because runtime lowering belongs to #1119.
