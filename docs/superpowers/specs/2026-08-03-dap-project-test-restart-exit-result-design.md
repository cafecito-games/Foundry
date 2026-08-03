# DAP Project-Test Restart Exit Result Design

## Context

Foundry's debug adapter reports a natural editor-owned process lifecycle as
`process`, `exited(exitCode)`, then `terminated`. A structured `project_test`
session can lose `exited` after this sequence:

1. The selected-test runner stops at a breakpoint.
2. The DAP client requests `restart`.
3. The replacement runner stops at the same breakpoint.
4. The client continues the replacement runner.

On Linux CI, the replacement sometimes emits only `terminated`. The process-result
coordinator can still recover a natural result in either process-first or
debugger-first order, but the run bar is notified that "all debug sessions exited"
without knowing which process ended. A delayed stop notification from the replaced
debugger can therefore be applied to the replacement process.

## Design

Make debugger shutdown notifications process-specific.

`ScriptEditorDebugger` will capture its `remote_pid` before clearing session state
and include that PID in its internal `stopped` signal. `EditorDebuggerNode` will
forward the PID through `EditorNode` to `EditorRunBar::debug_sessions_exited()`.
The run bar will act on the notification only when it identifies the process the
current launch represents. A delayed notification from the replaced process will
therefore be ignored instead of starting forced-cleanup handling for the replacement.

The existing launch identity remains authoritative for accepting process-completion
results in `DebugSessionResultCoordinator` and `DebugAdapterProtocol`. PID matching
only prevents stale debugger-socket lifecycle events from being applied to the
wrong current launch; it does not replace launch IDs or turn unknown results into
successful ones.

## Lifecycle Behavior

- A current editor-owned child whose debugger disconnects naturally retains the
  existing bounded grace period for its OS exit result.
- A delayed debugger-stop notification whose PID differs from the represented
  child is ignored.
- A replacement `project_test` child that exits naturally emits exactly one
  `exited` event with its real exit code, followed by exactly one `terminated`.
- Explicit termination still kills the current child and emits `terminated`
  without fabricating `exited`.
- Unowned/native sessions retain their existing result-less termination behavior.

## Regression Coverage

Extend the native tooling-host tests in
`tests/editor/test_editor_tooling_host.h` with a structured `project_test` session
using the checked-in exit-status runner. The test will:

1. Register a breakpoint in the runner.
2. Launch a passing selected test and observe the first breakpoint stop.
3. Restart with the same structured launch arguments.
4. Observe the replacement `process` and breakpoint stop.
5. Continue the replacement.
6. Assert the replacement lifecycle is exactly `process`, `exited`, `terminated`,
   with exit code `0`.

Existing structured-result, ordinary-scene restart, disconnect, and forced-
termination tests will remain unchanged and must stay green. Final validation will
use the repository's strict native agent build and focused tooling-host tests.

## Scope

The change is limited to debugger-stop identity propagation, run-bar stale-event
filtering, and the structured restart regression. It does not change the DAP wire
format, selected-test protocol, report format, timeout duration, or ordinary test
execution semantics.
