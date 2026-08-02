# Local macOS Editor Install Task

## Goal

Provide one Taskfile command that builds the editor from the currently checked-out commit and installs the resulting
macOS application locally. The command does not fetch, pull, switch branches, or otherwise modify Git state.

## Interface

`task install` is macOS-only. It installs `Foundry.app` into `/Applications` by default. Developers may override the
destination directory with `INSTALL_DIR` and the signing identity with `SIGN_IDENTITY`; the default identity is `-`
for an ad-hoc local signature.

## Build flow

The task invokes `python3 scripts/agent_build.py --backend ninja`. This keeps compilation on the repository's shared
Ninja and ccache path. The task uses the host CPU count unless `JOBS` is supplied.

The wrapper remains responsible for prerequisite diagnostics, per-worktree Ninja state, ccache configuration, build
logs, progress events, and the compiled editor result. The install script then reproduces the existing macOS bundle
builder's packaging steps directly: copy the standard app template, render its version values, install the editor
binary, and codesign it with the debug entitlements. This avoids SCons' delegated bundle action retraversing the
entire engine graph from inside Ninja.

## Installation flow

After a successful build, the task assembles and signs `Foundry.app` in a temporary directory under `bin/`. It copies
the completed bundle to a temporary sibling inside the destination directory before replacing any installed
`Foundry.app`, preventing a failed copy from damaging the current installation. Replacement removes stale files from
older bundles. The task removes a quarantine attribute from the locally built copy when present and prints the final
installed path.

## Failure handling

The task rejects non-macOS hosts before building. Missing SCons, Ninja, ccache, a failed build or signature, a missing
bundle, and insufficient destination permissions all stop the task with a nonzero exit. Installation cleanup is
limited to the temporary path and the exact `Foundry.app` destination.

## Verification

Taskfile listing and dry-run commands verify discovery and command expansion. A behavioral smoke test installs into a
temporary directory, verifies the installed bundle and its code signature, and runs the installed executable's help
command. This configuration-only change does not add a permanent test that asserts on Taskfile source text.
