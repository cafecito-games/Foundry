# Debugger Payload Cleanup Order Design

## Context

Issue #1634 was reopened after the structured selected-test DAP conformance still
lost its natural result after breakpoint, restart, breakpoint, and continue. The
previous fix made debugger stop notifications launch-specific, but its regression
project configured a main scene while the external selected-test project does not.

Stress-running the exact external no-main-scene fixture against the merged
`2b399079d` binary produced both a pass and a native child crash. The failing run
accepted `continue`, then emitted `exited(6)` and `terminated`. Its backtrace ran
from `EngineDebugger::deinitialize()` through destruction of the remote peer's
queued `Array` values into `FSInstance::~FSInstance()`.

The debugger's stack-variable wire path retains live values until its background
peer serializes them. `RemoteDebugger::_send_stack_vars()` builds a
`ScriptStackVariable`; `ScriptStackVariable::serialize()` keeps a valid object in
the message array; and `RemoteDebuggerPeerTCP::put_message()` holds that array in
`out_queue`. Normal engine cleanup currently finishes script languages and
uninitializes server modules, deleting `FSLanguage`, before it deinitializes the
engine debugger. A queued object released at that point destroys an `FSInstance`
whose destructor needs the already-deleted language mutex.

The configured main scene is therefore a timing influence, not a run target or
root cause. A structured `project_test` launches its runner through
`test_runner_path` and deliberately bypasses main-scene resolution.

## Approaches Considered

### 1. Deinitialize the debugger before script languages

Stop the debugger transport, join its peer thread, and release its inbound and
outbound payloads before `ScriptServer::finish_languages()` and server-module
uninitialization.

This restores the lifetime invariant directly: debugger values cannot outlive the
script runtime that owns them. It also covers all debugger payload types and all
script languages without changing the wire format. This is the recommended
approach.

### 2. Convert every queued object to an object ID before enqueueing

Recursively replace live objects in debugger payloads with transport-only values.
This narrows the lifetime of object references but requires a new recursive
conversion contract for arrays, dictionaries, resources, and future Variant
types. It duplicates encoding behavior and risks changing debugger semantics.

### 3. Extend the run-bar process-result grace period

A longer timeout can make some abnormal exits easier to observe, but it leaves the
unsafe destruction order intact. It cannot turn a crashing child into the required
natural `exited(0)` result and is rejected.

## Design

`Main::cleanup()` will deinitialize `EngineDebugger` immediately after the main
loop is deleted and language worker threads have stopped, but before
`ScriptServer::finish_languages()`. The existing later deinitialization call will
be removed so the debugger still tears down exactly once.

At that point no game frame can enqueue new debugger work, the remote peer can
safely join its transport thread, and every queued Variant still has access to its
language runtime during destruction. Renderer, scene, and server teardown remain
after language finish as before. Error-path cleanup and the test-only cleanup path
already deinitialize the debugger before module teardown and remain unchanged
unless verification identifies an equivalent ordering gap.

No DAP messages, result-coordinator rules, launch identities, or timeout values
change. A natural owned child still reports its OS exit code. Explicit termination
still ends the session without fabricating `exited`.

## Regression Coverage

The checked-in DAP exit-status fixture will gain a no-main-scene staging mode (or a
dedicated no-main-scene project file) so the regression matches the external
project shape. The tooling-host regression will:

1. start a structured `project_test` in that project;
2. stop at a breakpoint and request stack locals/evaluation, exercising an
   object-bearing debugger payload;
3. restart and stop the replacement at the same breakpoint;
4. continue the replacement and wait for its natural result;
5. assert exactly one `exited(0)` followed by `terminated`;
6. repeat enough times, or use a deliberately backlogged payload, to make the old
   cleanup order fail during the RED step.

The implementation must record a real RED run against the old cleanup order. If
the initial test does not fail, it must be strengthened before production code is
changed. Existing structured-result, ordinary-scene, attach/disconnect, restart,
and forced-termination tests remain part of focused validation. The exact external
Foundry-Scripting conformance test is the final behavior gate.

## Scope

This change owns debugger-versus-language shutdown ordering and the regression
needed to prove it. It does not redesign debugger serialization, change the DAP
wire contract, alter selected-test reports, or treat a configured main scene as a
requirement for structured test debugging.
