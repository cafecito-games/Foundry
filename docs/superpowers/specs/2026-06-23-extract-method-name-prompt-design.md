# Extract Method Name Prompt Design

## Context

Issue #47 adds an editor prompt for naming the function created by the GDScript Extract Method refactor. The current Extract Method implementation already computes a safe generated name, stores it in `RefactorResult::suggested_name`, and immediately applies edits through `ScriptTextEditor::_run_refactor()`.

The new flow should affect only the script editor interaction. Headless callers, tests, and LSP code-action resolution must still work when no name is supplied.

## Goals

- Prompt for a method name before applying Extract Method from the script editor.
- Prefill the prompt with the generated non-colliding name.
- Validate names with `GDScriptRefactorNames::validate_identifier()`.
- Reject names that collide with existing members in the target class.
- Apply Extract Method with the chosen name after confirmation.
- Leave the source unchanged when the prompt is canceled.
- Preserve generated-name behavior for callers that do not provide a name.

## Non-Goals

- No LSP client prompt support in this change. LSP code actions keep resolving with the generated name.
- No changes to Extract Method selection analysis, parameter analysis, output handling, or insertion placement.
- No changes to Rename Symbol UI beyond reusing the same validation conventions.

## Core Refactor API

`RefactorParams::new_name` will be accepted for `RefactorKind::EXTRACT_METHOD`.

When `new_name` is empty, `prepare_extract_method()` keeps using the generated name from `make_unique_method_name()`. This preserves the existing headless and LSP behavior.

When `new_name` is non-empty, `prepare_extract_method()` validates it with `GDScriptRefactorNames::validate_identifier()`. It then rejects the name if the target class already has a member with that name. On success, the Extract Method edits are built with the custom name and `RefactorResult::suggested_name` is set to that name.

Collision errors should be clear and specific, for example: `A member named 'foo' already exists in this class.`

## Editor Flow

`ScriptTextEditor::_run_refactor()` will special-case `RefactorKind::EXTRACT_METHOD`.

First it calls `GDScriptRefactoring::prepare()` with empty params to validate availability and compute the generated default name. If preparation fails, the existing error toast behavior remains.

If preparation succeeds, the editor stores the current `RefactorContext` and `RefactorLocation` as pending Extract Method state, opens a dedicated confirmation dialog, and fills the line edit with `result.suggested_name`.

As the user edits the name, the dialog validates the text with `GDScriptRefactorNames::validate_identifier()`. If the identifier is valid, it also prepares Extract Method with that name to catch member collisions and any stale context failures before OK is enabled. The error label shows the validation or collision message, and OK is disabled until the name is acceptable.

On confirm, the editor prepares Extract Method again with the chosen name and applies the returned result through the existing `_apply_refactor_result()` path.

On cancel, pending Extract Method state is cleared and no source edits are applied.

## UI Components

Add a dedicated Extract Method name dialog beside the existing rename dialog state:

- `ConfirmationDialog *extract_method_dialog`
- `LineEdit *extract_method_line_edit`
- `Label *extract_method_error_label`
- pending `RefactorContext`
- pending `RefactorLocation`

The dialog title should be `Extract Method`. The single text field should be registered for Enter confirmation. The implementation can mirror the existing rename dialog structure to stay consistent with the script editor UI.

## Tests

Core headless tests in `modules/gdscript/tests/test_refactor.h` will cover:

- Empty `RefactorParams::new_name` still uses the generated non-colliding name.
- A custom valid method name is used in both the replacement call and generated function declaration.
- An invalid custom name is rejected with the identifier validation message.
- A custom name colliding with an existing member is rejected with a clear collision message.

Editor dialog behavior should be covered if an existing lightweight editor test harness can exercise `ScriptTextEditor` dialogs without a full editor session. If not, the behavior is verified by focused core tests plus manual/editor-level inspection after build.

## Risks

The main risk is stale pending editor context: the buffer may change between prompt open and confirm. Re-preparing on every valid text change and again on confirm keeps the apply path using fresh validation before edits are applied.

Another risk is diverging validation between the dialog and core API. Keeping the final validation in `prepare_extract_method()` ensures non-UI callers and the editor agree on valid names.
