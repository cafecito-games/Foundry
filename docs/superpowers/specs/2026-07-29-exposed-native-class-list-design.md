# Exposed Native Class List Design

## Goal

Add a dedicated, script-accessible `ClassDB.get_exposed_class_list()` method that returns exactly
the native class names Foundry Script treats as visible and reserved.

## API Contract

`ClassDB.get_exposed_class_list()` returns a `PackedStringArray` containing every registered class
for which native `ClassDB::is_class_exposed()` is true. The result is sorted lexically so callers
receive deterministic output across invocations.

The new method does not change `ClassDB.get_class_list()`, which continues to return all registered
classes. Normal exposed classes such as `Node` appear in both lists, while internal registrations
such as `FSNativeClass` and `ThemeContext` appear only in the unfiltered list.

## Architecture

Add `ClassDB::get_exposed_class_list()` to the native ClassDB implementation in
`core/object/class_db.{h,cpp}`. It will collect exposed registrations while holding one ClassDB read
lock and sort the collected names with the same lexical comparator used by `get_class_list()`.

Expose the native helper through `Special::ClassDB` in `core/core_bind.{h,cpp}` as a no-argument
method returning `PackedStringArray`. Update the extension API dump to consume the same helper so
the script-visible list and the emitted `extension_api.json` class-name set share one authoritative
filter.

Document the new method in `doc/classes/ClassDB.xml`, including its relationship to Foundry Script
native class visibility and the unchanged behavior of `get_class_list()`.

## Data Flow and Error Handling

The script call enters the `Special::ClassDB` wrapper, which requests the already filtered and
sorted native list, converts each `StringName` to a `PackedStringArray` entry, and returns it.
Registered classes are stable for the duration of the ClassDB read lock. The method takes no user
input and introduces no new recoverable error path.

## Testing

Extend the ClassDB unit tests to prove the exposed list:

- equals `get_class_list()` filtered by `ClassDB::is_class_exposed()`;
- is in deterministic lexical order;
- contains `Node`; and
- omits `FSNativeClass` and `ThemeContext`.

Add a Foundry CLI regression test that invokes `ClassDB.get_exposed_class_list()` through
`foundry --headless script eval`, proving the method is bound and usable by projectless generators
and linters. Run the relevant focused ClassDB and `FoundryCLI` doctest filters, then complete the
repository's CI-style macOS build and full headless test suite.

## Scope

This change does not expose `is_class_exposed()` as a per-class scripting API, alter class
registration, modify Foundry Script grammar, or change analyzer behavior.
