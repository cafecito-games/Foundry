# Override Method Refactor Design

## Goal

Add a Foundry Script refactoring that lets a user choose one valid base-class method to override, then inserts a matching method stub in the current class. The experience should match professional IDEs: invoke the refactor from inside a class, pick an overridable method from a list, and get a compilable stub with a sensible default body.

## User Flow

The user places the caret inside a Foundry Script class body and opens `Refactor > Override Method...`.

If the current class has missing override candidates, the editor shows a picker list. Each row shows a readable signature and origin, for example `_process(delta: float) -> void - Node` or `configure(speed: float) -> int - res://base.fs`. Selecting a row inserts exactly one method stub into the class and moves the caret into the inserted method body.

Methods already declared directly in the target class are not listed. If no valid candidates exist, the menu entry is disabled with a clear reason.

## Architecture

Add a new `RefactorKind::OVERRIDE_METHOD` in `modules/foundry_script/editor/fs_refactoring.h`. The core refactoring layer owns candidate discovery, method identity, and stub rendering. The script editor owns only the picker UI and selected-candidate handoff.

`FSRefactoring` will expose override candidates through value data attached to a result or through a small dedicated query result. Each candidate carries:

- A stable selected identity used by `RefactorParams`.
- The method name.
- A display signature.
- The origin label and origin kind, such as native class, script path, or trait/abstract requirement.
- Enough rendered information to reconstruct the stub after the user selects it, without caching live parser nodes.

The editor adds an `Override Method...` refactor menu item and a dialog/model similar in scope to the existing Extract Method naming flow. The dialog requests candidates, displays them in a searchable/selectable list, and calls `FSRefactoring::prepare(ctx, loc, RefactorKind::OVERRIDE_METHOD, params)` with the selected identity.

`prepare()` re-runs candidate discovery and matches the selected identity before producing edits. This prevents stale dialog data from editing the file if the buffer or base class changed while the picker was open.

## Candidate Discovery

Candidate discovery starts from the class enclosing the caret. It is available for classes, including abstract classes, but not for traits. It follows the same resolved parse/analyze path used by existing refactors, especially `IMPLEMENT_ABSTRACT_METHODS`, so script inheritance, cross-file bases, native bases, trait requirements, and abstract methods use the analyzer's understanding of the program.

The target class contributes a set of already-declared method names. Any base candidate with a name already declared directly in the target class is skipped.

Candidates include every method that would be valid to override:

- Native virtual methods from the native base chain, identified through `ClassDB` virtual method metadata.
- Concrete methods from Foundry Script base classes, including cross-file bases when resolvable.
- Abstract or required methods from script bases or traits.

Candidates exclude:

- Final methods.
- Static/instance mismatches the generated method cannot satisfy.
- Methods whose signature cannot be rendered into valid Foundry Script.
- Methods shadowed by a more-derived declaration in the base chain.
- Methods already declared in the target class.

When analysis or cross-file parsing is incomplete, discovery should degrade conservatively. It may show candidates from resolvable bases, but it must not invent speculative methods. If no reliable candidate remains, the refactor is disabled with a clear reason.

## Stub Rendering

The generated signature preserves the base contract:

- `static` when required.
- `async` when required.
- Method name.
- Generic parameters for script-base functions when available.
- Parameter names and types.
- Default arguments when recoverable from source or metadata.
- Rest parameters when representable.
- Return type, including explicit `void`.

Concrete base methods call the base implementation by default:

- Void concrete override: `super.method(args)`.
- Non-void concrete override: `return super.method(args)`.

The call uses declared parameter names. If a rest parameter can be forwarded with valid Foundry Script syntax, the renderer forwards it. If safe forwarding is not available, the renderer emits a compiling stub and warns rather than emitting invalid code.

Abstract or required methods have no base implementation to call. They reuse the existing implement-abstract body style:

- `push_error("Not implemented: method")`.
- A default literal return for simple built-in return types.
- `pass` where no clean default literal exists.

The insertion point reuses the same class insertion logic as `IMPLEMENT_ABSTRACT_METHODS`: append after the last class member or trait-use line, respecting root-class header-only files and class indentation. The result sets `rename_anchor_line` and `rename_anchor_column` to place the caret inside the inserted body.

## Editor Integration

`ScriptTextEditor` keeps the existing enum-to-`RefactorKind` mapping by inserting the new enum value and corresponding menu command in the same order. The refactor submenu remains populated from `FSRefactoring::get_available_refactors()`.

When the user selects `Override Method...`, the editor:

1. Builds the current `RefactorContext` and `RefactorLocation`.
2. Requests candidates.
3. Shows the picker if candidates are available.
4. On confirmation, passes the selected override identity through `RefactorParams`.
5. Applies the returned edits with the existing refactor apply path.

The picker should avoid broad UI behavior. It only needs method selection, filtering, confirmation, cancellation, and stale-selection error handling.

## LSP Integration

The first implementation focuses on the Godot editor picker. The core candidate API should still be reusable by the language server later.

If LSP support is added in the same change or a follow-up, the natural shape is one lazily resolved `refactor.rewrite` code action per candidate, titled `Override <signature>`. That should reuse the same selected identity and `prepare()` path as the editor picker.

## Error Handling

Disabled reasons should be specific:

- No class under caret: `Place the caret inside a class.`
- Trait target: `Traits cannot override methods.`
- Abstract class target may still list override candidates. Required bulk implementation remains covered by `Implement Abstract Methods`.
- No candidates: `No overridable methods found.`
- Parse/analyze failure with no reliable candidates: `Cannot analyze this script.`
- Stale selection: `Selected override method is no longer available.`

The refactor must fail without editing if the selected identity cannot be resolved at confirmation time.

## Tests

Add pure `FSRefactoring` tests first:

- The refactor is listed and disabled outside a class.
- A script-base concrete method appears as a candidate.
- Preparing a script-base concrete override emits a matching signature and `super` call.
- A native virtual method appears as a candidate.
- Preparing a native virtual override emits a matching signature and `super` call.
- Abstract or required methods appear as candidates and reuse the implement-abstract stub body.
- Already-declared methods are not listed.
- Final methods are not listed.
- Cross-file script bases are discovered when the parse result provider can resolve them.
- A stale selected identity fails without edits.

Add editor/model tests only if the picker logic is separated into a standalone model. Avoid brittle UI tests that depend on popup rendering.

## Non-Goals

This design does not change Foundry Script grammar.

This design does not add multi-select override insertion. The picker inserts one selected method per invocation.

This design does not replace `Implement Abstract Methods`; that action remains useful for bulk implementation of required abstract contracts.
