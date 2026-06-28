# GDScript Namespaces Design

## Overview

GDScript should support Java/C#-style namespace declarations for globally named script classes while keeping the first version intentionally restricted. A script without a namespace remains in the global namespace and behaves like current GDScript. A script with a namespace uses the namespace plus its `class_name` as its canonical script class identity.

Example:

```gdscript
namespace characters

@abstract
class_name BaseCharacter
extends Node
```

The canonical script class name is `characters.BaseCharacter`.

Nested namespace example:

```gdscript
namespace characters.controllers

class_name MyCharacterController
extends Node
```

The canonical script class name is `characters.controllers.MyCharacterController`.

## Goals

- Provide real namespacing for `class_name` script classes.
- Keep existing global namespace behavior unchanged for scripts without a namespace.
- Make imports file-local and explicit.
- Preserve the current flat `ScriptServer` global class model by using fully qualified names as keys.
- Keep v1 small enough to implement across parser, analyzer, editor, LSP, and tests without broad UI redesign.

## Non-Goals

- Namespace blocks are not supported.
- Import aliases are not supported in v1.
- Wildcard imports are not supported.
- Relative imports are not supported.
- Inner classes are not independently registered into namespaces.
- Editor UI grouping by namespace is not required in v1.

## Language Semantics

A GDScript file may declare one optional top-level namespace:

```gdscript
namespace characters.controllers
```

The namespace applies to the whole file. It is an identifier chain separated by periods. An empty namespace is not valid syntax; omitting `namespace` means the file is in the global namespace.

The root script class keeps its local `class_name`:

```gdscript
class_name MyCharacterController
```

When a namespace is present, the canonical global script class name is:

```text
<namespace>.<class_name>
```

When no namespace is present, the canonical global script class name is the existing `class_name`.

Imports are file-local:

```gdscript
import characters

func do_things() -> void:
	var base: BaseCharacter
	var controller := controllers.MyCharacterController.new()
```

`import characters` makes direct classes in `characters` visible by short name and direct child namespaces visible by child name. It does not recursively import all descendant classes. In the example above, `BaseCharacter` is a direct class in `characters`, while `controllers` is a direct child namespace.

Ambiguous short names from multiple imports are analyzer errors. Users must disambiguate with a fully qualified name or remove one import.

## Top-Level Ordering

The v1 ordering is intentionally strict:

1. Optional script annotations such as `@tool`.
2. Optional `namespace`.
3. Zero or more `import` declarations.
4. Existing `@abstract`, `class_name`, `extends`, and body declarations.

`namespace` and `import` are top-level only. They are not allowed inside functions, inner classes, or after normal declarations have begun.

## Parser Model

The tokenizer already reserves `namespace`. Add `import` as a declaration keyword and parse both declarations as period-separated identifier chains.

Extend root `ClassNode` metadata with namespace and import data:

```cpp
String namespace_name;          // Empty means global namespace.
String qualified_global_name;   // Root class only: namespace + "." + class_name.
Vector<String> imports;
```

The existing `identifier` remains the local class name. For `namespace characters` plus `class_name BaseCharacter`, `identifier->name` remains `BaseCharacter`, while `qualified_global_name` is `characters.BaseCharacter`.

This keeps local diagnostics, class docs, and inner-class behavior close to current code while letting global registration and external lookup use the qualified name.

## Registration And Cache

`GDScriptLanguage::get_global_class_name()` should return the canonical qualified name when a namespace is present. Internal editor filesystem metadata should also retain the local class name and namespace so diagnostics and future UI can distinguish display name from canonical identity.

`ScriptServer` can remain a flat map. The map key is the canonical script class name:

```text
characters.BaseCharacter -> { language, path, base, is_abstract, is_tool }
```

Existing APIs such as `is_global_class()`, `get_global_class_path()`, `get_global_class_native_base()`, and inheriters cache continue to work with fully qualified names.

The saved global script class cache stores the qualified name in the existing `"class"` field. It may add optional `"namespace"` and `"class_name"` fields for diagnostics and UI. Cache loading must continue to accept old entries that only have `"class"`.

## Analyzer Lookup

Fully qualified names resolve directly:

```gdscript
var base: characters.BaseCharacter
```

Unqualified type and class-name lookup should use this order:

1. Existing local scope and member rules.
2. Same namespace direct classes.
3. Direct classes from imported namespaces.
4. Existing global namespace classes, native classes, built-in types, autoloads, and constants.

Native and built-in names keep precedence over namespace imports. Importing a namespace cannot shadow `Node`, `Vector2`, or other native/built-in names.

Child namespace lookup is import-aware. If the current file imports `characters`, then `controllers.MyCharacterController` resolves to `characters.controllers.MyCharacterController` when `controllers` is a direct child namespace of `characters`.

Missing imports are analyzer errors. A namespace exists if at least one known script class is registered under that namespace or one of its descendants.

## Diagnostics And Restrictions

Parser errors:

- Duplicate `namespace` declarations.
- `namespace` after imports or normal declarations.
- `import` before `namespace` when a namespace later appears.
- `import` after normal declarations.
- Malformed namespace or import identifier chains.

Analyzer errors:

- Import of a missing namespace.
- Ambiguous unqualified class from imports.
- Unknown class after considering namespace/import context.
- Fully qualified class name collision, with diagnostics showing both script paths.

Warning:

- Mixed namespaces in one folder.

A directory is expected to contain either all global-namespace script classes or all script classes in the same namespace. Mixing `characters`, `characters.controllers`, and global namespace script classes in one folder triggers the warning. The warning should be added to the normal GDScript warning system so projects can promote it to an error.

## Editor And LSP

V1 editor and LSP support should cover the common workflows:

- Completion after `import ` suggests known namespaces.
- Completion in type positions suggests same-namespace classes, imported direct class names, and imported child namespaces.
- Completion after an imported child namespace, such as `controllers.`, suggests direct classes in that namespace.
- Go to definition works for imported short names and fully qualified names.
- Rename of a namespaced class updates references that resolve to that class, including imported short-name references.
- The create dialog and class lists may display qualified names in v1.

Because the canonical identity remains a string key in `ScriptServer`, export, debugger, resource loader/saver, and existing editor class list paths should continue to work after they use qualified names.

## Testing

Parser tests:

- `namespace characters`.
- `namespace characters.controllers`.
- duplicate namespace declarations.
- imports before and after namespace.
- imports after class declarations.
- malformed namespace/import chains.

Analyzer tests:

- same-namespace unqualified resolution.
- explicit fully qualified resolution.
- direct import resolution.
- imported child namespace resolution.
- missing namespace import.
- ambiguous import.
- native/builtin precedence over imported classes.
- class collision on fully qualified name.

Editor/LSP/completion tests:

- namespace import completion.
- imported short-name completion.
- child namespace completion after `controllers.`.
- go to definition for imported short names and qualified names.
- rename of a namespaced class updates all statically resolved references.

Filesystem/editor tests:

- global script class cache stores qualified names.
- old cache entries without namespace metadata still load.
- mixed folder namespaces emit the configured warning when a practical harness is available.

## Open Implementation Notes

The implementation should prefer a small set of namespace helper functions over scattering string concatenation:

- build a qualified class name from namespace and local class name.
- split a qualified class name into namespace and local class name.
- test whether a qualified class is a direct member of a namespace.
- test whether one namespace is a direct child of another namespace.

These helpers should be shared by analyzer, completion, and diagnostics where practical.
