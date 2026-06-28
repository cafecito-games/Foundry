# Inline Multi-Caret Rename Design

## Goal

Replace the modal rename prompt with an inline multi-caret rename experience when a Foundry Script rename can be handled entirely in the current file. Preserve the existing modal and diff-preview paths for renames that need extra user input, validation, or cross-file review.

## User Experience

Pressing F2 on a renameable Foundry Script symbol starts inline rename if the resolved rename only affects the active file and has no unresolved references. The editor selects every resolved in-file occurrence with multiple carets, preselects the current symbol text, and lets the user type the replacement once. Typing updates every selected occurrence live through the existing `TextEdit` multi-caret editing behavior.

Enter commits the rename. Escape cancels it and restores the original buffer text and caret state. Losing focus does not implicitly commit; the mode remains explicit so users do not accidentally apply a partial rename.

If the refactor engine reports cross-file edits, unresolved references, or advisory warnings that need review, F2 continues to use the existing modal/diff-preview flow. The modal remains as the fallback rather than being removed.

## Architecture

`GDScriptRefactoring::prepare()` remains the source of truth for symbol resolution. The inline UI must not scan text for matching words; it should derive occurrence ranges from the rename result's resolved edit ranges. To support pre-name inline setup, the refactor layer will expose rename occurrence ranges for the current symbol without requiring a new name.

`ScriptTextEditor` owns the editor interaction state. It decides whether a rename result is inline-safe, configures the active `CodeEdit` selections, tracks the original source and caret state for cancellation, and validates the committed name through the existing rename prepare path before finalizing.

The existing file-edit apply path stays responsible for cross-file and diff-preview renames.

## Components

- `modules/foundry_script/editor/gdscript_refactoring.h`: extend `RefactorResult` with current-file rename occurrence ranges, or add an equivalent inline-rename preparation result that reuses `RefactorTextEdit` ranges.
- `modules/foundry_script/editor/gdscript_refactoring.cpp`: populate occurrence ranges from the same resolved references used by rename edits. The ranges must cover declarations and references only, excluding comments, strings, dynamic references, and unrelated same-name symbols.
- `editor/script/script_text_editor.h/.cpp`: add inline rename state and handlers for start, commit, cancel, and text-editor input interception.
- `modules/foundry_script/tests/test_refactor.h`: cover occurrence range extraction and inline-safe classification at the refactor layer.

## Data Flow

1. F2 calls `ScriptTextEditor::_run_refactor(RefactorKind::RENAME)`.
2. The editor checks rename availability as it does today.
3. The editor asks the refactor engine for occurrences using the current context and caret location.
4. If the result is single-file and warning-free, the editor enters inline mode by selecting each occurrence range in the active `CodeEdit`.
5. The user edits the selections using existing multi-caret typing.
6. On Enter, the editor reads the replacement from the primary occurrence, validates it as a Foundry Script identifier, then runs normal rename preparation with that final name.
7. If validation succeeds and the prepared result still applies only to the active file, the editor leaves the already-edited text in place and ends the inline rename as one undoable action.
8. On Escape, the editor restores the original source and exits inline mode.

## Error Handling

If occurrence preparation fails, the editor shows the existing refactor error toast and does not enter inline mode. If the final name is invalid, the editor keeps inline mode active and shows the validation error using the existing toaster/error-message style rather than applying or canceling. If final rename preparation discovers cross-file edits or unresolved references, the editor cancels inline mode back to the original source and opens the existing modal/diff-preview path for the same target.

If multi-caret setup cannot select every occurrence, the editor restores any partially changed caret state and falls back to the modal path.

## Undo And Cancellation

Inline rename should commit as one text undo step. The implementation should open a `TextEdit` complex operation when entering inline rename and close it only when committing or canceling. Cancel restores the original source inside that grouped operation so a canceled inline rename leaves no lasting text change.

The original caret and selection state should be restored on cancel. On commit, the caret should land on the first renamed occurrence, matching the current rename anchor behavior as closely as the multi-caret model allows.

## Testing

Refactor unit tests should prove that occurrence ranges are produced for local variables, members, and functions, and that strings/comments/dynamic references are not included. Tests should also prove that cross-file rename results are not inline-safe.

Editor-level behavior is harder to cover without a full UI harness, so the implementation should keep most decision logic in small helpers that can be tested through focused C++ unit tests where practical. Manual verification should include F2 inline rename commit, invalid-name rejection, Escape cancellation, and cross-file fallback.
