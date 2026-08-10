# FileSystem TextFile Creation Design

## Problem

Selecting **Create New → TextFile...** in the FileSystem dock silently does nothing when the workspace has no active script editor view. `FileSystemDock` delegates to `ScriptEditorController::open_text_file_create_dialog()`, but that method currently asks an active `ScriptEditorView` to configure and open the shared file dialog. With no registered view, it only updates the hidden dialog's path and returns.

Submitting the dialog also depends on a script view: `ScriptEditorController::_on_file_dialog_selected()` routes the selected path to an active view, where the new-text-file operation is implemented. This means both opening and completing the global creation flow incorrectly depend on per-workspace script UI state.

## Desired Behavior

- Selecting **TextFile...** always opens the New Text File save dialog.
- The dialog starts in the folder selected in the FileSystem dock.
- The behavior works when no script editor view exists.
- Opening the dialog does not create or reveal a script workspace.
- Confirming a filename creates the empty file, refreshes the FileSystem entry when applicable, and opens the new document through the editor's normal resource-loading path.
- Existing New Text File actions inside a script editor continue to use the same behavior.

## Design

Text-file creation will become a global `ScriptEditorController` operation because the controller already owns the shared `EditorFileDialog` and its global lifecycle.

`ScriptEditorController::open_text_file_create_dialog()` will configure the shared dialog for saving, populate the configured text-file extension filters, set the title and destination, record the dialog operation, and show the dialog directly. It will not consult or create a `ScriptEditorView`.

When the shared dialog emits `file_selected`, the controller will handle the new-text-file operation before any view routing. It will:

1. Reset the pending dialog operation.
2. Create or truncate the selected file through `FileAccess`.
3. Show the existing editor warning if writing fails.
4. Notify `EditorFileSystem` when the selected extension is configured as a text-file extension.
5. Route the new path through `EditorNode::load_resource()` so scripts and ordinary text documents reach their canonical editor surfaces.

Other dialog operations that genuinely belong to a particular script view will retain the existing focused-view routing.

`ScriptEditorView::_menu_option(FILE_MENU_NEW_TEXTFILE)` will delegate to the controller-owned operation. The FileSystem dock already calls the controller API, so it requires no separate dialog or file-creation implementation.

## Error Handling

If the selected file cannot be opened for writing, the controller will show the existing “Error writing TextFile” warning and will not attempt to refresh or open the path. Optional editor singletons remain guarded so the controller-level behavior is testable in the editor test harness.

## Testing

A C++ regression test will construct `ScriptEditorController` global services without registering any `ScriptEditorView`. It will:

1. Request a new text file in a scratch-space directory.
2. Verify the shared dialog becomes visible and uses the requested directory and base name.
3. Emit the dialog's selected-file signal with a scratch-space path.
4. Verify that the file exists on disk.

The focused editor test suite will be run after the red/green cycle. The final built editor will also be exercised through the FileSystem dock's context menu with no script view open, verifying that the save dialog appears without changing the workspace into a script surface.

## Scope

This change does not redesign the save dialog, alter supported text-file extensions, or change how completed files are opened. It only removes the invalid dependency between a global creation command and the presence of a per-workspace script editor view.
