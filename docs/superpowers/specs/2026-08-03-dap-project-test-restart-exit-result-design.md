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

Make debugger shutdown notifications launch-specific.

The editor will pass its monotonic launch ID to each owned child as an internal
runtime option. The child will return that ID with the debugger's initial PID
handshake, and `ScriptEditorDebugger` will include it in its internal `stopped`
signal. This makes the identity intrinsic to the connecting child instead of
inferring it from whichever launch is current when a queued socket is accepted.
`EditorDebuggerNode` will aggregate active debugger sessions by launch and forward
the launch ID through `EditorNode` to
`EditorRunBar::debug_sessions_exited()`. The run bar will act on the notification
only when it identifies the current launch. A delayed notification from the
replaced launch will therefore be ignored instead of starting forced-cleanup
handling for the replacement.

A newly accepted socket is explicitly marked unidentified until its initial
handshake is parsed. If another session stops during that short window, the node
defers the launch-stop decision. It re-evaluates as soon as the pending socket is
identified or closes, preventing both premature cleanup of a surviving sibling and
indefinite suppression by an unrelated connection.

The same launch identity remains authoritative for accepting process-completion
results in `DebugSessionResultCoordinator` and `DebugAdapterProtocol`. Using it for
debugger-socket lifecycle events also covers multi-instance runs and connections
whose PID handshake was already queued before disconnect; an unidentified socket
remains unowned rather than borrowing the current launch. It does not turn unknown
results into successful ones.

## Lifecycle Behavior

- A current editor-owned child whose debugger disconnects naturally retains the
  existing bounded grace period for its OS exit result.
- A delayed debugger-stop notification whose launch ID differs from the current
  run is ignored.
- Every debugger belonging to a multi-instance launch shares the launch ID, so the
  last session to close can still start the current run's bounded cleanup.
- An active connection awaiting its initial handshake defers cleanup until its
  launch is known; matching siblings cancel the deferred stop, while unrelated
  sessions release it.
- A child sends its launch ID in the first debugger handshake alongside its PID, so
  a queued connection from a replaced launch remains attributable to that launch.
- A connection that never supplies the initial handshake remains unowned and cannot
  start cleanup for an unrelated current launch.
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
