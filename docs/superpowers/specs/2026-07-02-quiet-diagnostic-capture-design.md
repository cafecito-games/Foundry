# Quiet Diagnostic Capture for `ScriptDiagnosticCapture`

## Summary

Add an opt-in quiet mode to `ScriptDiagnosticCapture` so that FoundryScript test frameworks can capture and assert `push_error`/`push_warning`/`push_fatal` diagnostics without those diagnostics also being printed to stderr on a successful run. Diagnostics remain fully recorded as structured events for assertions and reporting; only the immediate stderr print is suppressed.

Source request: `docs/foundry-test-runner-request.md` in FoundryLib.

## Scope

In scope: the low-level `ScriptDiagnosticCapture` quiet-capture API and its interaction with existing capture/fatal-termination behavior.

Out of scope: the optional higher-level `--test-script` headless CLI mode described in the request doc. That can be designed separately once quiet capture exists to build on.

## Background

`ScriptDiagnosticCapture` (`core/object/script_diagnostic_capture.h`/`.cpp`) captures diagnostics via the engine's `ErrorHandlerList` mechanism (`core/error/error_macros.h:70-78`): `start()` registers an error handler callback that records each diagnostic as an `Event`; `stop()` removes it. This callback runs *after* the diagnostic has already been printed — `_err_print_error()` (`core/error/error_macros.cpp:98-142`) calls `OS::print_error(...)` (which is gated by `OS::_stderr_enabled`, `core/os/os.cpp:98-100`) before it invokes the registered error handlers (lines 133-139). So capturing a diagnostic today does not, and structurally cannot, prevent it from also reaching stderr — the two are separate steps.

The only existing mechanism that actually suppresses stderr output is `OS::set_stderr_enabled(bool)` (`core/os/os.h:270-273`, impl `core/os/os.cpp:196-209`), a single global flag. The fork's own C++ test harness already uses this exact pattern: `FSTest::disable_stdout()`/`enable_stdout()` (`modules/foundry_script/tests/fs_test_runner.cpp:519-529`) toggle both `_stdout_enabled` and `_stderr_enabled` around test execution.

`ScriptDiagnosticCapture` already supports overlapping/nested captures today via a static `active_capture_count` (`script_diagnostic_capture.cpp:37`), checked by `has_active_capture()`. That counter is separately consulted by `VariantUtilityFunctions::push_fatal`'s `request_fatal_termination()` (`core/variant/variant_utility.cpp:1053-1069`) to avoid actually terminating the process while any capture — quiet or not — is active. That guard is pre-existing and unaffected by this change.

## Design

### Ref-counted global suppression

Add a second static counter, `quiet_capture_count`, alongside the existing `active_capture_count` in `script_diagnostic_capture.cpp`. Each `ScriptDiagnosticCapture` instance tracks whether *it* was started in quiet mode (`bool quiet`).

- On `start(true)`: set `quiet = true`, increment `quiet_capture_count`. On the 0→1 transition, snapshot `OS::get_singleton()->is_stderr_enabled()` into a static `bool saved_stderr_enabled`, then call `OS::get_singleton()->set_stderr_enabled(false)`.
- On `stop()` for an instance where `quiet == true`: decrement `quiet_capture_count`. On the 1→0 transition, restore `OS::set_stderr_enabled(saved_stderr_enabled)` (not unconditionally `true`), so quiet mode composes correctly with any other code that had already disabled stderr before the quiet capture started.
- `start(false)` / `start()` (default): behaves exactly as today — no interaction with `quiet_capture_count` at all. This preserves current behavior for all existing callers, including `FSTest`.

This makes quiet suppression engine-wide (global `OS::_stderr_enabled`) for as long as at least one quiet capture is active, ref-counted so nested or sibling captures compose: stderr only comes back on once the last quiet capture stops. This is a deliberate, accepted simplification — a diagnostic emitted by unrelated code (or by a different, non-quiet capture) while a quiet capture happens to be active elsewhere on the stack will also be suppressed. This matches the target use case (a single-threaded headless test runner) and mirrors the granularity of the existing `FSTest::disable_stdout` mechanism.

`FSTest::disable_stdout`/`enable_stdout` are unaffected: they remain their own direct, unconditional toggle used only by the internal C++ doctest suite, not `ScriptDiagnosticCapture`. The two mechanisms don't compose today (nothing currently nests them), and this change doesn't introduce that interaction — it only ref-counts *within* `ScriptDiagnosticCapture` itself.

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

No change to `request_fatal_termination()`'s existing behavior: it already checks `ScriptDiagnosticCapture::has_active_capture()` (not quiet-specific) and does not terminate the process while any capture is active. Quiet mode only adds suppression of the "FATAL ERROR: ..." stderr text itself, via the same global `OS::_stderr_enabled` toggle described above — the event is still recorded and assertable via `has_fatal()`.

## Testing

New/extended C++ test coverage (doctest, under `tests/`, e.g. `tests/core/object/test_script_diagnostic_capture.h` — create via `tests/create_test.py` if no existing test file covers this class) for:

- `start(true)` suppresses stderr output for `push_error`, `push_warning`, and `push_fatal`, while the corresponding events are still recorded and assertable via `has_error`/`has_warning`/`has_fatal`.
- `start()` / `start(false)` continues to print to stderr exactly as today (regression guard for "existing scripts using `ScriptDiagnosticCapture` without quiet mode keep current behavior").
- Two overlapping quiet captures: stderr stays suppressed for the full outer duration and is only restored after the last one stops (ref-count composition).
- A quiet capture nested inside a non-quiet capture, and a non-quiet capture nested inside a quiet capture, to pin down and document the accepted "engine-wide while any quiet capture is active" behavior.
- A diagnostic emitted with no capture active at all still prints normally, both before and after a prior quiet capture has started and stopped (confirms `saved_stderr_enabled` restoration is correct, not just hardcoded to `true`).

## Acceptance criteria (from the request doc, scoped to this design)

- A FoundryScript test can call `push_error`, `push_warning`, and `push_fatal` inside a quiet `ScriptDiagnosticCapture` block.
- The test framework can assert those diagnostics through the capture object exactly as before.
- Diagnostics captured under quiet mode are not printed to stderr.
- Diagnostics not covered by a quiet capture continue to print normally.
- Existing scripts using `ScriptDiagnosticCapture` without quiet mode keep current behavior.
