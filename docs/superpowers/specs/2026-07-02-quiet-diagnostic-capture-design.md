# Quiet Diagnostic Capture for `ScriptDiagnosticCapture`

## Summary

Add an opt-in quiet mode to `ScriptDiagnosticCapture` so that FoundryScript test frameworks can capture and assert `push_error`/`push_warning`/`push_fatal` diagnostics without those diagnostics also being printed to stderr on a successful run. Diagnostics remain fully recorded as structured events for assertions and reporting; only the immediate stderr print is suppressed.

Source request: `docs/foundry-test-runner-request.md` in FoundryLib.

## Scope

In scope: the low-level `ScriptDiagnosticCapture` quiet-capture API and its interaction with existing capture/fatal-termination behavior.

Out of scope: the optional higher-level `--test-script` headless CLI mode described in the request doc. That can be designed separately once quiet capture exists to build on.

## Background

`ScriptDiagnosticCapture` (`core/object/script_diagnostic_capture.h`/`.cpp`) captures diagnostics via the engine's `ErrorHandlerList` mechanism (`core/error/error_macros.h:70-78`): `start()` registers an error handler callback that records each diagnostic as an `Event`; `stop()` removes it. This callback runs *after* the diagnostic has already been printed — `_err_print_error()` (`core/error/error_macros.cpp:98-142`) calls `OS::print_error(...)` (which is gated by `OS::_stderr_enabled`, `core/os/os.cpp:98-100`) before it invokes the registered error handlers (lines 133-139). So capturing a diagnostic today does not, and structurally cannot, prevent it from also reaching stderr — the two are separate steps.

The mechanism that actually suppresses ERROR/WARNING/FATAL stderr output is `CoreGlobals::print_error_enabled` (`core/core_globals.h:40`), a plain static bool. `Logger::should_log(bool p_err)` (`core/io/logger.cpp:50-52`) gates every error-type log call on it (`OS::print_error` → `_logger->log_error` → `should_log(true)`), and `OS::print_error` itself is also gated by `OS::_stderr_enabled` (`core/os/os.cpp:97-100`) — but `print_error_enabled` is the narrower, purpose-built flag: it only affects ERROR-type output, not general `print()`/`printerr()` traffic. It's already the established idiom in this codebase for exactly this purpose: `tests/test_macros.h:58-63` defines `ERR_PRINT_OFF`/`ERR_PRINT_ON` as direct sets of this flag, used throughout the test suite to silence expected-error output; `main.cpp:2463` and `modules/foundry_script/fs_format.cpp:2582-2630` use it the same way.

`ScriptDiagnosticCapture` already supports overlapping/nested captures today via a static `active_capture_count` (`script_diagnostic_capture.cpp:37`), checked by `has_active_capture()`. That counter is separately consulted by `VariantUtilityFunctions::push_fatal`'s `request_fatal_termination()` (`core/variant/variant_utility.cpp:1053-1069`) to avoid actually terminating the process while any capture — quiet or not — is active. That guard is pre-existing and unaffected by this change.

## Design

### Ref-counted global suppression

Add a second static counter, `quiet_capture_count`, alongside the existing `active_capture_count` in `script_diagnostic_capture.cpp`. Each `ScriptDiagnosticCapture` instance tracks whether *it* was started in quiet mode (`bool quiet`).

- On `start(true)`: set `quiet = true`, increment `quiet_capture_count`. On the 0→1 transition, snapshot `CoreGlobals::print_error_enabled` into a static `bool saved_print_error_enabled`, then set `CoreGlobals::print_error_enabled = false`.
- On `stop()` for an instance where `quiet == true`: decrement `quiet_capture_count`. On the 1→0 transition, restore `CoreGlobals::print_error_enabled = saved_print_error_enabled` (not unconditionally `true`), so quiet mode composes correctly with any other code that had already disabled error printing before the quiet capture started (e.g. an enclosing `ERR_PRINT_OFF`).
- `start(false)` / `start()` (default): behaves exactly as today — no interaction with `quiet_capture_count` at all. This preserves current behavior for all existing callers, including `FSTest`.

This makes quiet suppression engine-wide (global `CoreGlobals::print_error_enabled`) for as long as at least one quiet capture is active, ref-counted so nested or sibling captures compose: error printing only comes back on once the last quiet capture stops. This is a deliberate, accepted simplification — a diagnostic emitted by unrelated code (or by a different, non-quiet capture) while a quiet capture happens to be active elsewhere on the stack will also be suppressed. This matches the target use case (a single-threaded headless test runner) and mirrors the granularity of the existing `ERR_PRINT_OFF`/`ERR_PRINT_ON` test macros — but unlike those macros, it doesn't touch `print_line_enabled` or the `OS::_stdout_enabled`/`_stderr_enabled` flags used by `FSTest::disable_stdout()`/`enable_stdout()` (`modules/foundry_script/tests/fs_test_runner.cpp:519-529`), so plain `print()`/`printerr()` output stays unaffected and the two mechanisms don't interact.

### API surface

```cpp
// script_diagnostic_capture.h
void start(bool p_quiet = false);
bool is_quiet() const { return quiet; }
```

- GDScript: `capture.start()` — unchanged. `capture.start(true)` — new quiet mode.
- `is_active()` is unchanged. `is_quiet()` is new, so a test runner can tell whether the currently active capture is quiet (e.g. to decide whether to also dump raw diagnostic text itself on a failing run).
- `stop()`'s signature is unchanged; it already has access to the instance's `quiet` flag to know whether to decrement `quiet_capture_count`.
- Bind `start`'s new parameter with `DEFVAL(false)` in `_bind_methods()` so existing GDScript call sites (`capture.start()`) keep working unmodified.

### Fatal diagnostics

No change to `request_fatal_termination()`'s existing behavior: it already checks `ScriptDiagnosticCapture::has_active_capture()` (not quiet-specific) and does not terminate the process while any capture is active. Quiet mode only adds suppression of the "FATAL ERROR: ..." stderr text itself, via the same global `CoreGlobals::print_error_enabled` toggle described above — the event is still recorded and assertable via `has_fatal()`.

This guard is already sufficient to make a captured fatal diagnostic purely representational — an `Event`, not a process-level fatal — with no gaps to close: `OS::request_exit(EXIT_FAILURE)` (`variant_utility.cpp:1069`) is the only process-level effect `push_fatal` triggers, and it's the one thing `has_active_capture()` guards. `ERR_HANDLER_FATAL` itself carries no other special-cased behavior in the engine — `_err_print_error()` (`core/error/error_macros.cpp:107-141`) treats it purely as a `Logger::ErrorType` label for the printed string, and every registered `ErrorHandlerList` consumer (remote debugger forwarding in `core/debugger/remote_debugger.cpp:110-125`, Windows `OutputDebugStringW` in `platform/windows/os_windows.cpp:283-291`, editor log/toaster UI) treats `ERR_HANDLER_FATAL` the same as any other severity — none of them abort, break, or dump on it. So there is no crash-handler, debugger-break, or abort() path reachable from `push_fatal` that bypasses `has_active_capture()`. This is a documentation-only addition (plus a regression test below) — it confirms behavior the codebase already has, rather than changing anything.

## Testing

New/extended C++ test coverage (doctest, under `tests/`, e.g. `tests/core/object/test_script_diagnostic_capture.h` — create via `tests/create_test.py` if no existing test file covers this class) for:

- `start(true)` suppresses stderr output for `push_error`, `push_warning`, and `push_fatal`, while the corresponding events are still recorded and assertable via `has_error`/`has_warning`/`has_fatal`.
- `start()` / `start(false)` continues to print to stderr exactly as today (regression guard for "existing scripts using `ScriptDiagnosticCapture` without quiet mode keep current behavior").
- Two overlapping quiet captures: stderr stays suppressed for the full outer duration and is only restored after the last one stops (ref-count composition).
- A quiet capture nested inside a non-quiet capture, and a non-quiet capture nested inside a quiet capture, to pin down and document the accepted "engine-wide while any quiet capture is active" behavior.
- A diagnostic emitted with no capture active at all still prints normally, both before and after a prior quiet capture has started and stopped (confirms `saved_print_error_enabled` restoration is correct, not just hardcoded to `true`).
- A quiet capture started while `CoreGlobals::print_error_enabled` was already `false` (e.g. nested inside an enclosing `ERR_PRINT_OFF`) leaves it `false`, not `true`, after the quiet capture stops.
- `push_fatal` inside a non-quiet capture does not terminate the process (existing `has_active_capture()` behavior — regression guard) and does not trigger any other process-level effect (no exit code change beyond what the test itself sets, no abort); the fatal is only observable as a recorded `Event`.

## Acceptance criteria (from the request doc, scoped to this design)

- A FoundryScript test can call `push_error`, `push_warning`, and `push_fatal` inside a quiet `ScriptDiagnosticCapture` block.
- The test framework can assert those diagnostics through the capture object exactly as before.
- Diagnostics captured under quiet mode are not printed to stderr.
- Diagnostics not covered by a quiet capture continue to print normally.
- Existing scripts using `ScriptDiagnosticCapture` without quiet mode keep current behavior.
- Fatal diagnostics inside any active capture remain representable purely as `Event`s, with no process-level fatal side effect (termination, abort, or otherwise) — confirmed as already-correct existing behavior, pinned down with an explicit regression test and documentation.
