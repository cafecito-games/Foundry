# GDScript Custom Annotations Design

Date: 2026-06-26
Status: Approved brainstorm - pending implementation plan
Fork: CafecitoGames / Godot Engine

## Purpose

GDScript should support first-class, user-declared, passive custom annotations that attach
static metadata to classes, methods, and member variables. Runtime code should be able to
read that metadata through `godot.reflection` without executing annotation code or adding
per-call overhead.

This is primarily motivated by test libraries that want discovery and per-test
configuration to live on the declaration itself:

```gdscript
import cafecito.test

@suite(name = "Combat System")
@tags("gameplay")
class CombatTests:
	@fixture
	var world: TestWorld

	@test
	@timeout(10.0)
	@cases(provider = "crit_rows")
	func crit_table(row: Dictionary) -> void:
		pass
```

The feature is deliberately metadata-only. It does not introduce decorators,
interceptors, AST transforms, code generation, annotation processors, or runtime-valued
annotation arguments.

## Goals

- Let libraries declare annotation symbols in GDScript.
- Validate annotation use statically: unknown names, ambiguous imports, wrong targets,
  wrong arity, wrong argument names, wrong argument types, and non-constant arguments are
  errors.
- Support annotation targets needed in v1: root/inner classes, methods, and member
  variables.
- Support marker annotations, positional arguments, named arguments, defaults, variadic
  positional arguments, stacking, and repeated annotation uses.
- Store resolved custom annotation metadata on compiled GDScript scripts.
- Expose annotation metadata at runtime through structured reflection objects, not raw
  dictionaries.
- Preserve the current behavior of built-in annotations while keeping built-in annotation
  reflection out of v1.
- Match existing effective reflection views for inherited and trait-flattened members.

## Non-Goals

- Fully qualified annotation usage such as `@cafecito.test.timeout(...)`.
- Annotation targets for signals, constants, parameters, local variables, statements, or
  expressions.
- Reflection over built-in engine annotations such as `@export`, `@rpc`, `@abstract`, or
  `@onready`.
- Annotation execution, AST transforms, method wrapping, code generation, or annotation
  processors.
- Runtime-valued annotation arguments.
- Inner-class annotation declarations.
- Structured replacements for every existing method/property descriptor dictionary.

## Language Surface

V1 adds a contextual root-level declaration:

```gdscript
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation tags(...names: String) targets METHOD, CLASS
annotation skip(reason: String = "") targets METHOD, CLASS
annotation fixture targets VARIABLE
annotation cases(provider: String) targets METHOD
annotation suite(name: String = "") targets CLASS
```

`annotation` is contextual. It is treated as an annotation declaration only where a
root-level declaration is valid. This avoids reserving `annotation` as a keyword in all
identifier positions.

Annotation declarations are root-only in v1. A file can declare them in the script root,
usually inside a namespace:

```gdscript
namespace cafecito.test

annotation test targets METHOD
annotation fixture targets VARIABLE
annotation timeout(seconds: float) targets METHOD
```

Declarations inside inner classes, traits, functions, or local scopes are not supported.
Annotation declarations do not become instance members, static variables, constants, or
runtime callables.

Annotation declarations are root body declarations. The top-level order remains:

1. Existing script-level built-in annotations such as `@tool`.
2. Optional `namespace`.
3. Zero or more `import` declarations.
4. Optional `class_name`, `trait_name`, `extends`, and `uses` declarations.
5. Root body declarations, including `annotation` declarations.

An annotation-only library file does not need a `class_name`. Its annotation declarations
are still indexed under the file's namespace for import resolution.

### Targets

V1 target names:

| Target | Meaning |
| --- | --- |
| `CLASS` | Root classes, `class_name` scripts, and inner classes. |
| `METHOD` | Script methods, including static methods and abstract/trait methods. |
| `VARIABLE` | Member `var` declarations only, including static member variables. |

The implementation can reserve internal target bits for future targets, but the parser
and docs should expose only the v1 target names.

### Parameters

Annotation parameters are function-like but intentionally stricter:

- Every parameter must declare a type.
- Default values must be constant expressions.
- A variadic parameter is written with the existing ellipsis style and must be final:
  `...names: String`.
- Variadic arguments collect only extra positional arguments.
- Parameter names define valid named arguments.

The declaration syntax does not support generic annotation parameters in v1.

### Usage

Annotation use keeps the existing `@identifier` spelling:

```gdscript
@test
@timeout(10.0)
@tags("integration", "slow")
@skip("flaky on CI")
@cases(provider = "crit_rows")
func crit_table(row: Dictionary) -> void:
	pass
```

Rules:

- The annotation declaration must be visible through the current namespace or explicit
  `import`.
- Multiple annotations can stack on one declaration.
- Repeating the same annotation is legal and preserved in source order.
- Positional and named arguments can be mixed only with positional arguments first.
- Named arguments bind by declaration parameter name.
- Default values are used for validation but are not injected into reflection output in
  v1.
- Extra positional arguments are accepted only when the declaration has a variadic
  parameter.

Fully qualified annotation usage is intentionally deferred because the current tokenizer
turns only `@identifier` into an annotation token. V1 requires imports for short-name use.

## Name Resolution

Custom annotation declarations are ordinary namespaced symbols for lookup purposes, but
they live in an annotation-symbol space separate from classes, traits, variables, and
constants.

The canonical annotation identity is:

```text
<namespace>.<annotation_name>
```

or just `<annotation_name>` for the global namespace. Duplicate canonical annotation
identities are hard errors. A custom annotation declaration also cannot reuse a built-in
annotation name such as `export`, `rpc`, or `abstract`; built-in annotation names remain
reserved in the annotation-symbol space.

For a usage such as `@timeout`, the analyzer resolves the short name in this order:

1. Custom annotation declarations in the current file's namespace.
2. Custom annotation declarations in explicitly imported namespaces.
3. Built-in annotations from the existing engine registry, for existing built-in behavior.

If multiple imported namespaces provide the same short annotation name, the usage is an
ambiguity error. Users resolve the ambiguity by removing an import or waiting for the
future fully qualified usage feature.

Built-in annotations continue to use their current static registry for parser/analyzer
behavior. They are not recorded as passive metadata and are not returned by custom
annotation reflection.

Namespace import validation must consider namespaces that contain annotation declarations,
not only namespaces that contain `class_name` or `trait_name` scripts. This lets pure
annotation libraries expose declarations without inventing empty marker classes.

## Validation

Validation belongs in the analyzer, not in tokenization. The parser records unresolved
custom annotation usage nodes instead of rejecting every unknown non-built-in annotation
immediately.

The analyzer reports hard errors for:

- Unknown custom annotation name.
- Ambiguous imported annotation name.
- Annotation used on an unsupported target.
- Unknown target name in a declaration.
- Duplicate target name in a declaration.
- Duplicate canonical annotation declaration.
- Custom declaration that shadows a built-in annotation name.
- Annotation declaration outside the root script body.
- Missing parameter type.
- Variadic parameter not in final position.
- Default value that is not constant.
- Too few or too many arguments at a usage site.
- Unknown named argument.
- Duplicate named argument.
- Positional argument after a named argument.
- Providing a parameter both positionally and by name.
- Non-constant annotation argument.
- Argument value that cannot be strictly converted to the declared parameter type.

Type conversion should reuse the existing strict conversion behavior used by built-in
annotation argument validation. For example, numeric conversions that already warn or
error for built-ins should behave the same for custom annotations.

## Runtime Metadata Shape

Only passive custom annotation usages are persisted. Built-in annotations are excluded.

Compiled `GDScript` stores compact metadata tables:

```cpp
Vector<GDScriptAnnotationUsage> class_annotations;
HashMap<StringName, Vector<GDScriptAnnotationUsage>> method_annotations;
HashMap<StringName, Vector<GDScriptAnnotationUsage>> variable_annotations;
```

`GDScriptAnnotationUsage` contains:

```cpp
StringName name;          // Short name, without "@": "timeout".
StringName qualified_name; // Canonical declaration identity: "cafecito.test.timeout".
Array args;               // Positional arguments as resolved Variants.
Dictionary kwargs;        // StringName -> Variant for named arguments.
```

Reflection preserves caller intent:

- Values supplied positionally are returned in `args`.
- Values supplied by name are returned in `kwargs`.
- Defaults are validated but not injected into either collection.
- Repeated annotation uses produce multiple usage objects in source order.

The metadata is static. Reading it is a reflection operation. Method calls, property
access, instance construction, and ordinary script execution do not inspect annotation
metadata.

## Effective Member Views

Class annotations are direct-only and are not inherited.

Method annotation reflection follows the effective method view already exposed by
`godot.reflection.get_methods()`:

- Methods declared on a base script carry their annotations when reflected from a derived
  script.
- Concrete trait methods flattened into an implementer carry their declaration
  annotations in the implementer's effective method metadata.
- If a concrete class overrides a base or trait method, the overriding method's
  annotations are the effective annotations for that name.

Variable annotation reflection follows the effective property/member view:

- Member variables declared on a base script carry their annotations when reflected from a
  derived script.
- Trait-flattened member variables carry their declaration annotations in the implementer.
- If a concrete class shadows a base or trait variable, the concrete variable's
  annotations are effective for that name.

This matches the test-discovery use case: reflection over a concrete script sees the
declarations that are actually available through that script.

## Structured Reflection Type

Add a read-only descriptor class:

```gdscript
class GDScriptAnnotation extends RefCounted:
	var name: StringName
	var qualified_name: StringName
	var args: Array
	var kwargs: Dictionary
```

The actual implementation should bind getters rather than writable script-visible
properties:

```cpp
StringName get_annotation_name() const;
StringName get_qualified_name() const;
Array get_arguments() const;
Dictionary get_named_arguments() const;
```

The script-facing property names can be `name`, `qualified_name`, `args`, and `kwargs`,
or the methods can use explicit names if that better matches Godot documentation
conventions. Returned arrays and dictionaries should be snapshots. They do not mutate the
compiled script metadata.

## Reflection API

Add canonical APIs to `GDScriptReflection`:

```gdscript
TypedArray<GDScriptAnnotation> get_class_annotations(target)
TypedArray<GDScriptAnnotation> get_method_annotations(target, method: StringName)
TypedArray<GDScriptAnnotation> get_variable_annotations(target, variable: StringName)

bool has_annotation(
	target,
	member: StringName,
	annotation: StringName,
	kind: StringName = "method",
)

GDScriptAnnotation get_annotation(
	target,
	member: StringName,
	annotation: StringName,
	kind: StringName = "method",
)

TypedArray<GDScriptAnnotation> get_annotations(
	target,
	member: StringName = &"",
	kind: StringName = "class",
)
```

`kind` accepts `"class"`, `"method"`, and `"variable"`. The kind-specific methods are the
preferred public API; the generic method is a convenience for frameworks that need to
parameterize the query.

Matching by `annotation` should accept either short name (`"timeout"`) or qualified name
(`"cafecito.test.timeout"`). `get_annotation()` returns the first matching annotation in
the reflected order. Callers that care about repeats use the array-returning APIs.

Invalid, freed, or non-script targets keep existing reflection behavior: return empty
arrays, a null `GDScriptAnnotation` reference, or `false`, without crashes.

### Descriptor Embedding

`get_methods()` and `get_method_info()` continue returning method descriptor
dictionaries for compatibility, but each GDScript method descriptor gains:

```gdscript
"annotations": Array[GDScriptAnnotation]
```

`get_properties()` gains the same key for declared script variables:

```gdscript
"annotations": Array[GDScriptAnnotation]
```

Native methods/properties and unsupported targets omit the key or use an empty typed
array. The implementation should prefer consistency within `godot.reflection`: every
GDScript descriptor returned there can include an `annotations` key even when it is empty.

## Parser Touchpoints

Current relevant facts:

- `AnnotationNode` already exists and stores `name`, raw argument expression nodes, and
  resolved positional arguments.
- Existing annotation parsing uses the static `GDScriptParser::valid_annotations`
  registry and rejects unknown names early.
- Annotation tokens currently encode only `@identifier`.

Required parser changes:

1. Add contextual root-level recognition for `annotation`.
2. Add `AnnotationDeclarationNode`.
3. Add a root class member kind for annotation declarations, but reject them in inner
   class/trait bodies.
4. Parse target lists after `targets`.
5. Parse typed declaration parameters, defaults, and final variadic parameters.
6. Extend annotation usage argument parsing to accept `identifier = expression` named
   arguments only inside annotation calls.
7. Store usage argument entries as positional or named while preserving source order for
   diagnostics.
8. Allow unknown non-built-in annotation names through parsing so the analyzer can perform
   import-aware resolution. Keep special diagnostics for known documentation-comment
   mistakes such as `@deprecated`, `@experimental`, and `@tutorial` if those remain useful.

The tokenizer does not need to support dotted annotation names in v1.

## Analyzer Touchpoints

Required analyzer changes:

1. Resolve annotation declarations before validating annotation usages in a script body.
2. Build or query an annotation-symbol index across the current namespace and imported
   namespaces.
3. Make annotation-only namespaces visible to import validation.
4. Validate declaration signatures and constant defaults.
5. Resolve each custom annotation usage to a declaration.
6. Validate target, arity, positional/named binding, defaults, variadic arguments,
   constant-ness, and types.
7. Preserve resolved metadata on the AST node for the compiler.
8. Continue applying built-in annotations through existing `AnnotationAction` handlers.
9. Ensure built-ins are not recorded as passive custom metadata.

The existing constant-expression reduction and strict type conversion paths should be
reused rather than reimplemented.

## Compiler And Runtime Touchpoints

Required compiler/runtime changes:

1. Add `GDScriptAnnotationUsage` storage to `GDScript`.
2. Add helper conversion from resolved AST annotation usage to runtime metadata.
3. Populate class metadata from class annotation nodes.
4. Populate method metadata while compiling method functions.
5. Populate variable metadata while preparing member indices/properties.
6. Include trait-flattened method and variable annotation metadata through the existing
   `_collect_flattened_trait_members()` collection path.
7. Preserve inherited annotations by reading base-script metadata in reflection, matching
   current base-chain method/property enumeration.
8. Add or extend a namespace annotation index so imports and editor tooling can discover
   annotation declarations from annotation-only scripts.
9. Clear annotation metadata during script reload/clear paths with other compiled script
   state.
10. Add `GDScriptAnnotation` as a bound `RefCounted` descriptor class.
11. Extend `GDScriptReflection` methods and descriptor embedding.

The metadata tables should not store AST pointers. All reflected values must be copied or
reference-counted runtime-safe data.

## Editor And LSP

Editor/LSP work should follow the existing annotation completion and namespace/import
completion patterns:

- Annotation completion includes visible custom annotations from the same namespace and
  explicit imports.
- Completion inserts `(` when the declaration has parameters.
- Annotation argument hints show parameter names, types, defaults, and variadic status.
- Lookup/go-to-definition on `@test` resolves to `annotation test ...`.
- Diagnostics for unknown, ambiguous, wrong-target, and wrong-argument annotations flow
  through normal parser/analyzer diagnostics.

If the existing LSP test harness cannot cover go-to-definition for annotation
declarations yet, that can be an implementation subtask rather than a blocker for the
core runtime feature.

## Documentation

Add or update:

- `GDScriptAnnotation` doc class.
- `GDScriptReflection` doc methods.
- GDScript syntax/reference docs for custom annotation declarations.
- Examples for class, method, and member-variable annotations.
- Explanation that custom annotations are passive static metadata.
- Explanation that annotation arguments must be constant expressions.
- Explanation of imports and lack of fully qualified usage in v1.

## Testing

Parser/analyzer fixtures:

- Root-level annotation declarations.
- Rejection of inner/local annotation declarations.
- Same-namespace annotation resolution.
- Imported annotation resolution.
- Missing import / unknown annotation diagnostics.
- Ambiguous imported annotation diagnostics.
- Target validation for `CLASS`, `METHOD`, and `VARIABLE`.
- Marker, positional, named, default, variadic, stacked, and repeated annotations.
- Duplicate named args, unknown named args, positional-after-named, and parameter supplied
  twice.
- Non-constant argument and non-constant default diagnostics.
- Strict argument type conversion diagnostics.
- Built-in annotations still parse and behave as before.

Runtime fixtures:

- `get_class_annotations()` returns structured descriptors.
- `get_method_annotations()` returns method metadata.
- `get_variable_annotations()` returns member-variable metadata.
- `has_annotation()` and `get_annotation()` match short and qualified names.
- Repeated annotations are preserved in order.
- Method descriptors from `get_methods()` and `get_method_info()` embed annotation objects.
- Property descriptors from `get_properties()` embed annotation objects.
- Base method and variable annotations are visible through derived scripts.
- Trait-flattened method and variable annotations are visible through implementers.
- Class annotations are direct-only and not inherited.
- Invalid, freed, and non-script targets return empty results without crashes.

C++ tests:

- Extend the existing `GDScriptReflection` test block or add a neighboring block for
  annotation descriptors and invalid target behavior.

Completion/LSP tests:

- Visible custom annotation completion.
- Imported annotation completion.
- Ambiguous annotation diagnostics if completion infrastructure exposes it.
- Annotation argument hints.
- Go-to-definition from annotation usage to declaration where practical.

## Follow-Up Issues

These should be filed under the implementation epic but kept out of the v1 acceptance
criteria unless explicitly reprioritized:

- Support fully qualified annotation usage such as `@cafecito.test.timeout(...)`.
- Add targets for signals, constants, parameters, local variables, statements, and
  expression positions.
- Reflect built-in annotations as metadata.
- Add direct-only/effective-view switches to reflection APIs.
- Add structured method and property descriptor types to replace the remaining reflection
  dictionaries.
- Integrate annotation declarations into generated class/reference docs beyond syntax
  documentation.

## Acceptance Checklist

- `annotation` declarations parse at the root of a namespaced or global script.
- Custom annotations must be in same-namespace/import scope.
- Unknown and ambiguous annotation usages are analyzer errors.
- `@test`, `@timeout(10.0)`, `@skip("reason")`, `@tags("a", "b")`, and
  `@cases(provider = "crit_rows")` parse and validate when declared.
- Custom annotations can target classes, methods, and member variables.
- Wrong target, wrong arity, wrong argument type, and non-constant argument uses are
  analyzer errors.
- Repeated same-name annotations are legal and reflected in source order.
- Reflection returns `GDScriptAnnotation` objects for classes, methods, and variables.
- `get_methods()`, `get_method_info()`, and `get_properties()` include annotation objects
  for GDScript declarations.
- Effective method/variable annotation reflection includes base and trait-flattened
  members; class annotations remain direct-only.
- Built-in annotations keep existing behavior and are not reflected as passive custom
  metadata.
- Reading annotations is static metadata access and adds no per-call runtime cost.
