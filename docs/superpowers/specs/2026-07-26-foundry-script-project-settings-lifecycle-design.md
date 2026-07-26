# Foundry Script ProjectSettings Lifecycle Restoration Design

## Problem

Foundry Script tests can leave `ProjectSettings::resource_path` pointing at
`modules/foundry_script/tests/scripts`. `ScriptRunnerProjectFixture` currently switches to that
project before `init_language()` records the state it must later restore. Direct `TEST_CASE`
declarations also have an empty doctest suite name, and `FSLanguageSuiteFixture` currently uses an
empty `owner_suite` both for a valid empty-suite owner and for "no owner." A later named suite can
therefore inherit the language and eventually restore the already-switched scripts root.

The lifecycle must preserve the exact `ProjectSettings` values that existed before Foundry Script
test setup:

- resource path;
- project-loaded state; and
- `application/config/name`.

Language shutdown must continue clearing Foundry Script caches, `ResourceCache` entries, global
classes, and reflection singletons.

## Approaches Considered

### Delegate ScriptRunner project setup to `init_language()` and track ownership explicitly

This is the selected approach. `ScriptRunnerProjectFixture` will construct its existing
`TestProjectSettingsRestoreScope` and then call `init_language()` directly. `init_language()` will
therefore snapshot the true pre-case settings before activating the scripts project. The listener
will gain a boolean that distinguishes "an owner has been assigned" from the owner's possibly empty
suite name.

This changes both proven lifecycle seams without changing production engine behavior or the
once-per-suite language hoist.

### Tear the language down in every ScriptRunner fixture destructor

This would hide the leak but discard the suite-hoisting optimization and leave the listener's
empty-suite ownership model broken for other direct cases. It is rejected as a symptom-level fix.

### Pass an explicit ProjectSettings snapshot into `init_language()`

This could compensate for callers that switch projects first, but it expands a test-only API and
duplicates state ownership between the fixture and language lifecycle. It is rejected in favor of
making the existing setup order correct.

## Lifecycle Design

### ScriptRunner fixture

`ScriptRunnerProjectFixture` retains `TestProjectSettingsRestoreScope` as its first member. Its
constructor calls `init_language(test_runner_scripts_root)` without first calling
`ProjectSettings::setup()`.

On the first direct case, `init_language()` records the pre-case path, loaded flag, and app name,
then activates the scripts project. On later direct cases, the idempotent path re-applies the
language's active project when each per-case restore scope has restored the outer settings.

Destroying the per-case scope restores the outer state immediately. When the listener later calls
`finish_language()`, the language restores the same outer state rather than the scripts project.

### Suite listener

`FSLanguageSuiteFixture` adds an explicit ownership flag. The suite name remains a string and may
legitimately be empty.

- When a case ends with language state live and there is no owner, the listener assigns the current
  suite name and sets the ownership flag.
- A repeated direct/empty-suite case matches the empty owner and keeps the language hoisted.
- A transition from an empty-suite owner to a named suite is a real owner change, so the listener
  finishes the old language before the new case starts.
- Named-to-named transitions keep the existing behavior.
- Run-end teardown clears both the owner name and ownership flag.

The warning-reset behavior and per-case `reset_language_state()` behavior remain unchanged.

## Regression Coverage

Three regressions will exercise the real lifecycle surfaces.

1. A direct `ScriptRunnerProjectFixture` case records sentinel path, loaded, and app-name values,
   destroys the per-case fixture, calls `finish_language()`, and checks all three values exactly.
   Before the fix, `finish_language()` overwrites them with the scripts-project snapshot.
2. A listener transition test invokes actual `test_case_start()` and `test_case_end()` callbacks for
   two repeated empty-suite cases followed by a named-suite case. It proves the language remains
   hoisted across the repeated empty owner, tears down at the empty-to-named boundary, restores all
   three ProjectSettings fields, clears a loaded script from `FSCache` and `ResourceCache`, and
   invalidates the reflection, project-scripts, and namespace singletons.
3. The existing direct-init teardown test is strengthened to activate the language from sentinel
   ProjectSettings state and verify exact restoration of all three fields at a named hoisted-suite
   boundary, in addition to verifying that the language is inactive.

Tests will protect the surrounding process with `TestProjectSettingsRestoreScope` and explicit
language teardown so filtered and full-suite runs remain order-independent. Regression assets are
existing checked-in scripts; no files are written outside test scratch space.

## Verification

Verification will include:

- the new lifecycle cases in RED before the implementation;
- focused ScriptRunner and SuiteLanguageHoist cases after the fix;
- broader Foundry Script lifecycle/cache cases;
- a strict optimized test build with warnings as errors; and
- the exact full test suite with a structured JSONL progress file and `DISPLAY=:1`.

Cursor Agent will review the committed branch against a freshly fetched `origin/develop` until it
returns `RESULT: clean`.

## Scope

This change is confined to the Foundry Script test harness and its tests. It does not add a guard to
export-manifest tests, change production ProjectSettings behavior, alter language syntax, or change
cache semantics.
