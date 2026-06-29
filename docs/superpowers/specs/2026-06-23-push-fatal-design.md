# Design: `push_fatal()` — terminate execution on unrecoverable errors

**Date:** 2026-06-23
**Status:** Approved (pending implementation)
**Branch:** `feature/push-fatal` (worktree off `origin/develop`)

## Summary

Add a global `push_fatal(...)` utility function as a sibling to the existing
`push_error` / `push_warning`. It logs an error like `push_error` does, then
gracefully requests the program to quit with a non-zero exit code. It is meant
for unrecoverable conditions the program should never silently continue past
(e.g. a required asset failed to load, an invariant was violated in a way that
makes continuing meaningless).

It differs from `assert()` — which is compiled out entirely in release builds
and only breaks to the debugger in debug builds — by firing in **all** builds
by default and by actually ending the process cleanly.

## Goals

- A single call (`push_fatal("message")`) that logs and then terminates the
  running project gracefully.
- Available everywhere `push_error` is (Foundry Script, C#, engine C++), since it is
  a `core` Variant utility function.
- Safe by construction: it must not be able to kill the editor when invoked
  from a `@tool` script during editing.
- An escape hatch for production: a project setting that downgrades it to plain
  `push_error` (log-only) behavior.

## Non-goals

- No hard crash / `abort()` / crash-handler backtrace. Termination is a
  **graceful quit** (loop unwinds at a frame boundary, `finalize()` runs).
- No Foundry Script-specific keyword or opcode; this is a plain utility function.
- No conditional form (`push_fatal(cond, msg)`); the call site decides when to
  invoke it. `assert()` already covers the conditional case.

## Requirements (decisions captured during brainstorming)

1. **Termination semantics:** graceful quit — log the error, then request the
   main loop to exit with a non-zero exit code. Cleanup/finalizers run.
2. **Build configurations:** fires in debug **and** release/exported builds by
   default. A project setting can downgrade it to `push_error` behavior.
3. **API surface:** a global vararg `Variant` utility function, sibling to
   `push_error` / `push_warning`. Available in every scripting language.
4. **Editor behavior:** inside the editor process (`@tool` scripts, etc.) it
   logs only and does **not** quit, so it cannot kill the editor. Termination
   applies to the running game / project process.

## Design

### API

```gdscript
push_fatal("Failed to load required asset: ", path)
```

- Vararg; all arguments are joined into a single string using the same
  `join_string` helper `push_error` uses.
- Requires at least one argument; with zero arguments it reports
  `CALL_ERROR_TOO_FEW_ARGUMENTS` (matching `push_error`).
- Registered in `core/variant/variant_utility.cpp` via
  `FUNCBINDVARARGV(push_fatal, sarray(), Variant::UTILITY_FUNC_TYPE_GENERAL);`.
- Fixed exit code `EXIT_FAILURE`. (Considered making the exit code an optional
  trailing argument; rejected for now to keep the signature identical in shape
  to `push_error`. Can be added later without breaking callers.)

### Function behavior

`VariantUtilityFunctions::push_fatal`:

1. Validate argument count (≥1), build the joined message string.
2. **Always** emit the message as an error via `ERR_PRINT` (so it appears in the
   log with a script backtrace, exactly like `push_error`).
3. Decide whether to terminate. The call is **log-only** (no quit) when *either*:
   - `Engine::get_singleton()->is_editor_hint()` is `true` (editor process /
     `@tool` script), **or**
   - the project setting `application/run/push_fatal_terminates` is `false`.
4. Otherwise call `OS::get_singleton()->request_exit(EXIT_FAILURE)`.

> Note: when a game is launched from the editor it runs as a separate process
> where `is_editor_hint()` is `false`, so `push_fatal` terminates that game
> process normally — it only logs-only when running *inside* the editor itself.

### Termination mechanism (Approach A — OS exit-request flag)

`core/` utility functions cannot depend on `scene/SceneTree`, so termination is
routed through `OS` (which already owns the exit code) and picked up by the main
iteration loop.

- **`core/os/os.h` / `core/os/os.cpp`:**
  - Add a `SafeFlag _exit_requested` member (atomic — safe if `request_exit` is
    called from a non-main thread).
  - `void request_exit(int p_exit_code = EXIT_FAILURE);` — stores the exit code
    in the existing `_exit_code` field and sets `_exit_requested`.
  - `bool is_exit_requested() const;` — returns the flag.
  - Relax / update the existing "`set_exit_code` should only be used from
    `SceneTree`" comment to acknowledge the new `request_exit` path.

- **`main/main.cpp` → `Main::iteration()`:**
  - OR `OS::get_singleton()->is_exit_requested()` into the `exit` boolean that
    `iteration()` already computes and returns. The loop unwinds at the next
    frame boundary, `MainLoop::finalize()` runs, and the process exits with the
    stored exit code. This makes the quit graceful rather than mid-frame.

This works headless, with any `MainLoop` (not just `SceneTree`), and reuses the
existing exit-code plumbing.

### Project setting

- Key: `application/run/push_fatal_terminates`
- Type: `bool`, default `true`.
- Defined once via `GLOBAL_DEF` and read through a cached `GLOBAL_GET` so the
  hot path does not re-parse settings on every call.
- When `false`, `push_fatal` degrades to exactly `push_error` behavior
  (log, do not quit).

### Documentation

- Add the `push_fatal` method entry to `doc/classes/@GlobalScope.xml`,
  documenting:
  - the terminate-by-default behavior and non-zero exit code,
  - the editor exemption (log-only inside the editor),
  - the `application/run/push_fatal_terminates` project setting.

### C# parity (`GD.PushFatal`)

`GD.PushError` / `GD.PushWarning` are hand-written C# wrappers that log directly;
they are not auto-generated from the Variant utility functions. To give C# the
same behavior, the terminate-decision tail of `push_fatal` is extracted into
`VariantUtilityFunctions::request_fatal_termination()` (editor exemption +
project-setting check + `OS::request_exit`). The C# `GD.PushFatal` overloads log
via the existing `ErrPrintError` path (preserving C# caller context) and then
call a new no-argument native interop function
`godotsharp_request_fatal_termination()`, which simply invokes
`request_fatal_termination()`. This shares the termination policy between the
Foundry Script and C# entry points so they cannot diverge.

The interop function is appended as the **last** entry of both the
`unmanaged_callbacks[]` table in `modules/mono/glue/runtime_interop.cpp` and the
declaration list in `NativeFuncs.cs` (the two are index-matched and must stay in
the same order).

## Edge cases

- **Zero arguments:** `CALL_ERROR_TOO_FEW_ARGUMENTS`, like `push_error`.
- **Called before a main loop exists:** `request_exit` just sets the flag; it is
  read once the loop starts. Harmless.
- **Multiple calls:** idempotent in effect; the last exit code wins and the flag
  stays set.
- **Off-main-thread call:** `SafeFlag` makes setting the flag safe; the main
  loop observes it on its next iteration.
- **Headless / tool runs where `is_editor_hint()` is true:** log-only, which is
  the intended conservative behavior.

## Testing

Actually quitting would terminate the test runner, so tests assert the
*decision* (the flag and exit code), not a real shutdown.

- C++ doctest for `OS::request_exit()` / `is_exit_requested()`: flag transitions
  and exit-code storage.
- C++ test for `push_fatal`:
  - with the project setting `false` (or `is_editor_hint()` true): the message is
    logged and `is_exit_requested()` remains `false`;
  - with terminate conditions met: `is_exit_requested()` becomes `true` and the
    exit code is `EXIT_FAILURE`, **without** running the main loop.
- No Foundry Script `.fs` fixture that calls `push_fatal` in a terminating
  configuration (it would quit the runner). A fixture exercising the log-only
  path (setting off) may be added if convenient.

## Files touched

- `core/variant/variant_utility.cpp` — implement + register `push_fatal`.
- `core/variant/variant_utility.h` — declaration alongside `push_error` (line 140).
- `core/os/os.h` / `core/os/os.cpp` — `request_exit` / `is_exit_requested`.
- `main/main.cpp` — honor `is_exit_requested()` in `Main::iteration()`.
- Project setting registration (`GLOBAL_DEF` for
  `application/run/push_fatal_terminates`).
- `doc/classes/@GlobalScope.xml` — documentation.
- `doc/classes/ProjectSettings.xml` — document the project setting.
- `tests/` — C++ tests for the OS flag and `push_fatal` decision logic.

### C# parity files

- `core/variant/variant_utility.{h,cpp}` — extract `request_fatal_termination()`.
- `modules/mono/glue/runtime_interop.cpp` — `godotsharp_request_fatal_termination`
  function + table entry.
- `modules/mono/glue/GodotSharp/GodotSharp/Core/NativeInterop/NativeFuncs.cs` —
  matching interop declaration.
- `modules/mono/glue/GodotSharp/GodotSharp/Core/GD.cs` — `GD.PushFatal` overloads.

> **Verification note:** the mono module is not enabled in the standard dev build
> and no dotnet SDK is present in the authoring environment, so the managed
> (`GodotSharp`) assembly cannot be compiled or run here. The C++ glue compiles
> under `module_mono_enabled=yes`; the C# changes follow the existing
> `PushError`/`PushWarning` patterns and must be validated by a mono + dotnet
> build before merge.
