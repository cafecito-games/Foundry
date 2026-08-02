# Ninja Source Inventory Invalidation Design

## Problem

`scripts/agent_build.py` reuses a generated Ninja graph while the build configuration and the contents of
`SConstruct`, `SCsub`, and repository Python files remain unchanged. SCons commonly discovers translation units and
other generated-input sets through `Glob()`. Adding, removing, or renaming a file matched by one of those globs does
not modify the build descriptions, so the wrapper can reuse a graph that does not represent the checkout.

Commit `54fb870525` exposed this failure mode by adding `core/object/class_handle.cpp` without changing
`core/object/SCsub`, whose `*.cpp` glob should have discovered it. The stale graph omitted the object and the final
link failed with unresolved `ClassHandle` symbols.

## Design

Extend the existing build-description fingerprint with an inventory of source-controlled repository file paths. In a
Git checkout, combine all tracked paths with non-ignored untracked paths whose names or suffixes can shape the build
graph. Filter both sets through the wrapper's existing excluded-directory policy, sort them deterministically, and
hash each repository-relative path. For source archives without Git metadata, use the existing filesystem walk while
excluding known generated-output names. Continue hashing the contents of build descriptions and selected external
profiles as today.

The inventory deliberately hashes paths but not ordinary source contents:

- Adding, removing, or renaming any repository input selects a new Ninja state directory and regenerates the graph.
- Editing an existing C++, shader, XML, font, or other input keeps the same graph; Ninja's dependency tracking handles
  the rebuild.
- Git-ignored SCons outputs such as `*.gen.*`, `.scons_env.json`, and generated `.uid` files do not participate, so a
  successful build or test run does not immediately invalidate its own graph.
- Files under `.git`, `.ninja`, `.foundry`, `.test_scratch`, `.worktrees`, `bin`, `build`, `out`, and `__pycache__`
  remain excluded, so generated state does not churn the graph key.

Hashing every tracked path avoids maintaining an incomplete extension allowlist for committed inputs used by the
engine's many `Glob()` patterns. The untracked allowlist preserves normal local development for new source files; a
new glob pattern necessarily changes a content-hashed build description before later untracked files can rely on it.
The trade-off is an extra graph generation after an unrelated tracked file is added or removed, but graph generation
is short and file inventory changes are much rarer than content edits.

## Testing

Add focused Python unit tests proving that:

1. Adding a `.cpp` file changes `resolve_ninja_state()` and removing it restores the original state.
2. Editing the contents of an existing `.cpp` file leaves the Ninja state unchanged.
3. Adding a Git-ignored generated file leaves the Ninja state unchanged.
4. The existing excluded-directory tests continue to pass.

Run the complete `scripts.tests.test_agent_build` suite after the change. Finally, generate or inspect a Ninja state
from the affected checkout and confirm `core/object/class_handle.cpp` appears in its graph.
