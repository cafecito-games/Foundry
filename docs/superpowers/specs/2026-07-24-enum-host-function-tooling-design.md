# Enum Host Function Tooling Design

## Goal

Expose static and instance functions declared on Foundry Script enums through editor tooling:
document symbols, completion, resolved completion details, hover, go-to-definition, signature help,
and generated script documentation.

## Slice Boundary

This change owns issue #1122 only:

- enum functions become children of `enum_name` and nested enum symbols;
- enum metatypes complete values, static functions, and existing read-only Dictionary methods;
- enum values complete instance functions;
- enum function references resolve to their declarations for hover, definition, and signature help;
- generated `enum_name` docs contain enum function method docs;
- nested enum functions are represented where the existing `DocData` model permits.

The parser/analyzer/call metadata and formatter foundations are already merged. Runtime compilation
and dispatch (#1119) and bytecode persistence/verifier changes (#1120) remain out of scope. Tooling
tests may analyze enum calls, but must not require executing them.

## Alternatives Considered

### Reuse enum declarations and existing function renderers

This is the selected approach. Enum datatypes already identify their owner class, script path, and
enum name. Tooling maps that datatype back to the `EnumNode`, then reuses the same function symbol
and method-doc builders as ordinary class methods. This keeps signatures, defaults, docs, and
static/async qualifiers consistent while preserving enum functions as their own surface.

### Flatten enum functions into class members

This would make existing class tooling discover them automatically, but it would give enum
functions the wrong ownership and completion scope. It also conflicts with the language contract
that enum functions are not methods of the containing class.

### Build a separate enum-function index

A dedicated workspace index could power lookup and completion, but it would duplicate parser and
analyzer ownership data and be fragile for partial buffers and dependency parsers. The existing
datatype-to-declaration identity is sufficient.

## Symbols and Presentation

`ExtendFSParser` gets one helper that appends enum value and function children. Values remain
`EnumMember`. Each function is rendered with `parse_function_symbol()`, so static functions use the
LSP `Function` kind, instance functions use `Method`, and details preserve the canonical forms:

```text
func name(prefix: String = "") -> String
static async func parse(text: String) -> LogLevel
```

The same child construction is used for root `enum_name` symbols and nested named enum symbols.
Function parameters stay local children, so they are omitted from document-symbol JSON but remain
available to signature help.

The parser's existing recursive symbol search can then map analyzer lookup locations to enum
function children. That makes hover, definition, resolved completion, and signature help use the
same declaration symbol and documentation without adding separate protocol paths.

## Completion and Lookup

`fs_editor.cpp` gains a small enum-declaration resolver for tooling. It accepts only script enum
datatypes with an owner class and returns:

- `class_type->enum_file_decl` for an `enum_name` file; or
- the named `ENUM` member matching `enum_type` for a class/nested enum.

Completion adds only receiver-compatible functions:

- enum metatype: enum values and static functions;
- enum value: instance functions;
- enum metatype fallback: existing const Dictionary methods.

Enum value receivers must not fall through to `int` methods. Enum metatypes keep their current
Dictionary fallback and mutation filter. Function completion uses the existing function option
shape and brace behavior, including the ellipsis display for functions with parameters.

Lookup checks enum functions before Dictionary fallback. A receiver-compatible function returns a
script location at the function identifier line using the enum datatype's `script_path`; this feeds
workspace symbol resolution. Enum values retain current lookup. Wrong-kind enum functions remain
unresolved, matching analyzer semantics.

The editor's expression and method-return guessing also recognize enum function declarations so
chained completion uses the resolved function return type.

## Generated Documentation

`FSDocGen` extracts the existing class-method construction block into one local builder. The builder
preserves description, deprecation/experimental flags, vararg/noreturn/static/async qualifiers,
return type, arguments, enum defaults, and rest parameters.

For an `enum_name` file, enum functions are appended to the class document's `methods` collection.
That class document is already marked `is_enum`, so the existing help model presents them as methods
of the global enum type.

`DocData::EnumDoc` has no method collection, owner field, or qualified-member representation.
Therefore nested enum values and enum descriptions remain documented, while nested enum functions
are not flattened into the containing class's methods. Flattening would falsely present them as
class methods. Their editor symbols, completion, hover, definition, and signature help are still
fully supported.

## Testing

Completion fixtures cover:

- same-file nested enum metatype completion;
- typed nested enum value completion;
- global `enum_name` metatype completion;
- global `enum_name` value completion;
- static/instance filtering and retained Dictionary methods;
- async/static function option kinds and brace insertion.

LSP tests cover:

- root and nested enum function symbol children and qualifiers;
- resolved completion detail and documentation;
- hover and definition for static and instance calls;
- signature help labels, docs, and parameters.

Docgen tests prove `enum_name` methods preserve static/async qualifiers, signatures, defaults, and
documentation, and prove nested enum functions are not misrepresented as containing-class methods.

All production edits follow failing focused tests. Verification includes Completion, LSP, docgen,
the broader Foundry Script suite, a strict warnings-as-errors build, and the full test suite.
