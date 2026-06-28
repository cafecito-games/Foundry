# Foundry Script Style Order Refactor Design

## Goal

Add a Foundry Script refactoring option that sorts script members according to the official Foundry Script style guide while preserving the original relative order inside each style category.

## Context

The existing refactor system lives in `modules/foundry_script/editor/gdscript_refactoring.*` and already serves both the script editor refactor submenu and Foundry Script LSP code actions. It returns `RefactorResult` objects with text edits that are applied through the existing script editor and LSP paths.

The official style guide recommends this member order:

1. script annotations such as `@tool`, `@icon`, and `@static_unload`
2. `class_name`
3. `extends`
4. script documentation comment
5. signals
6. enums
7. constants
8. static variables
9. exported variables
10. regular variables
11. `@onready` variables
12. `_static_init()`
13. remaining static methods
14. overridden built-in virtual methods
15. overridden custom methods
16. remaining methods
17. inner classes

The refactor will implement the member ordering portion. It will not alphabetize within a bucket because declaration order can affect variable initialization and readability.

This CafecitoGames fork supports Foundry Script declarations that the upstream style guide does not cover yet: `namespace`, `import`, `trait_name`, inline `trait` declarations, and `uses`. The refactor must treat those as fork-specific syntax with an explicit local policy instead of forcing them into upstream-only categories.

## User Experience

The script editor will expose a new Foundry Script-only refactor menu item named `Sort Members by Style Guide`.

The action will be available for a Foundry Script file when the script parses successfully and at least one class in the file has reorderable members. It does not depend on the caret being on a specific symbol. If the current file is already in style-guide order, the action should be disabled with a clear reason such as `Members are already sorted by the Foundry Script style guide.`

The language server will expose the same operation as a rewrite code action so external LSP clients can request it. The action should resolve lazily like the existing extract, add type annotation, and inline refactors.

## Scope

The first implementation will sort class member blocks in the active script. It will process the root class and nested classes that have reorderable member blocks. It will not sort local variables inside functions.

The refactor will preserve relative order within each style bucket. For example, if three exported variables appear in a script, they remain in their original exported-variable order after the refactor.

The refactor will move complete declaration blocks, not just declaration lines. A declaration block includes directly attached annotations, documentation comments, and ordinary contiguous comments immediately above the declaration when those comments are separated from the previous member by a blank line or the declaration starts the class body.

The refactor will preserve comments and blank lines inside a moved block. It may normalize the separator between moved blocks to the existing block text that already belongs to those blocks, but it should avoid broad formatting changes outside the reordered span.

Top-level header declarations stay outside the member-sort span. For root scripts, the fork-local header order is:

1. script annotations such as `@tool`, `@icon`, and `@static_unload`
2. optional `namespace`
3. zero or more `import` declarations, preserving their existing relative order
4. optional `@abstract` plus either `class_name` or `trait_name`
5. optional `extends`
6. optional `uses`
7. script documentation comment

The refactor should not alphabetize or otherwise reorder imports. If header declarations are already accepted by the parser but not in this local order, this refactor should leave them untouched in the first implementation; the behavior is member sorting, not header normalization.

## Classification

Each parser member receives a sortable bucket:

- signals
- enums, including named enums and unnamed enum values where the parser exposes them as class members
- constants
- static variables
- exported variables
- regular public variables
- regular private variables
- `@onready` public variables
- `@onready` private variables
- `_static_init()`
- other static methods
- built-in virtual callbacks
- custom override methods when the analyzer resolves the overridden base member; unresolved methods stay in the remaining-methods buckets
- remaining public methods
- remaining private methods
- inner classes and inline traits

Public members sort before private members inside variable and method groupings. A member is private when its name begins with `_`, except built-in virtual callbacks keep their callback bucket.

Export grouping annotations such as `@export_category`, `@export_group`, and `@export_subgroup` are not independent style buckets. They should move with the contiguous exported-variable region they describe. If a grouping annotation cannot be attached unambiguously to an exported-variable block, the refactor should leave that local region unchanged rather than risk separating group metadata from variables.

Inline `trait` declarations sort in the same final inner-type bucket as inner classes, preserving their relative order with neighboring inner classes and traits. Global `trait_name` files should sort the trait body with the same member buckets as class bodies. The `uses` clause is a header declaration and must not be moved as a class member.

## Rewrite Strategy

The refactor will parse and analyze the current source through the existing `GDScriptRefactoring` parse-provider path. It will derive text ranges from parser node line and column information, using source lines to expand each member range upward to include attached comments and annotations.

For each class body, it will build a list of reorderable blocks from the parser member list. It will compute the stable style order by sorting blocks by bucket and original index. If the sorted block list matches the original block list, no edit is needed for that class.

When a class or trait body needs sorting, the refactor will replace the contiguous member span for that class or trait with the reordered block text. It should not rewrite the file header, `namespace`, `import`, `class_name`, `trait_name`, `extends`, `uses`, or script-level annotations.

Nested classes must be handled from the deepest class outward so text ranges do not invalidate parent ranges. If parent and child edits would overlap, the implementation should use the parent edit and avoid emitting the child edit separately.

The operation should fail safely with a user-facing error when the script cannot be parsed or analyzed, or when source ranges cannot be mapped back to the current text.

## Integration

Add a `RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE` entry after the existing refactor kinds. Update the script editor enum mapping and static assertions accordingly.

Add the availability entry to `GDScriptRefactoring::get_available_refactors()`. It should be disabled for invalid scripts, non-Foundry Script scripts, or already sorted scripts.

Add preparation logic to `GDScriptRefactoring::prepare()` that returns the reorder edits.

Update `modules/foundry_script/language_server/gdscript_text_document.cpp` so the new refactor maps to `refactor.rewrite`. No new source-action kind is required for the first implementation.

## Testing

Add focused C++ tests in `modules/foundry_script/tests/test_refactor.h` for:

- availability reports the new refactor and disables it when the file is already sorted
- variables, constants, signals, functions, static members, exported members, and onready members reorder into the style-guide bucket order
- relative order inside a bucket is preserved
- private variables and methods remain after public members in the same broad group
- built-in callbacks sort before remaining methods, with `_static_init()` and static methods in their dedicated positions
- nested class members are sorted without corrupting parent class text
- attached doc comments and annotations move with their declarations
- export group annotations stay with the exported members they describe or make the refactor decline the ambiguous region
- namespace, import, `trait_name`, and `uses` header declarations remain in place while body members sort around them
- inline traits sort in the inner-type bucket without changing their internal member order unless the trait body itself needs sorting
- invalid scripts return a disabled availability or error result without edits

Run targeted tests through the Godot test binary after building:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --force-colors --test-case='*[Modules][Foundry Script][Refactor]*'
```

If the local binary is not built yet, build it with:

```bash
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)
```

## Non-Goals

This feature will not implement a full Foundry Script formatter. It will not rename members to match naming conventions, change whitespace broadly, alphabetize declarations, sort local variables, or reorder statements inside function bodies.
