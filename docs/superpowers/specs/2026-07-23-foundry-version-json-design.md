# `foundry --version --json` Design

**Date:** 2026-07-23

**Status:** Approved design

## Goal

Add a machine-readable version query to the command-first Foundry CLI. The
command `foundry --version --json` (and the equivalent option order
`foundry --json --version`) prints one JSON object containing the release,
source, target, build, and extension compatibility metadata embedded in the
binary at build time.

The human-readable `foundry --version` output remains unchanged.

## Output contract

The JSON object has this stable shape:

```json
{
  "product": "Foundry",
  "version": "0.1.0",
  "release_tag": "v0.1.0-alpha.7",
  "channel": "alpha",
  "git_commit": "full-sha",
  "git_dirty": false,
  "build_id": "gh",
  "target": "macos-arm64",
  "extension_api": {
    "interface_format": 1,
    "abi_revision": 7
  }
}
```

`version` always includes major, minor, and patch components, including a
zero patch. `git_dirty` is a JSON boolean. The two extension API values are
JSON integers. The serializer owns the schema and tests parse the result, so
consumers do not depend on object member ordering.

## Build-time metadata

The existing generated `core/version_generated.gen.h` remains the single
compiled source of version metadata. SCons extends its input dictionary and
the version builder emits escaped string macros plus integer and boolean
macros for the new fields.

The build resolver uses the following precedence:

| Field | Explicit override | Local fallback |
| --- | --- | --- |
| `release_tag` | `FOUNDRY_RELEASE_TAG` | Exact Git tag at `HEAD`, otherwise `v<major>.<minor>.<patch>-dev` |
| `channel` | `FOUNDRY_CHANNEL` | Channel parsed from the release tag, otherwise `dev` |
| `git_commit` | `FOUNDRY_GIT_COMMIT` | Existing generated full Git hash, otherwise `unknown` |
| `git_dirty` | `FOUNDRY_GIT_DIRTY` | Git working-tree status, otherwise `false` when Git is unavailable |
| `build_id` | `FOUNDRY_BUILD_ID` | Existing `BUILD_NAME`, otherwise `local` |
| `target` | `FOUNDRY_TARGET` | `<scons platform>-<resolved architecture>` |
| `extension_api.interface_format` | `FOUNDRY_EXTENSION_INTERFACE_FORMAT` | `1` |
| `extension_api.abi_revision` | `FOUNDRY_EXTENSION_ABI_REVISION` | `7` |

The existing `FOUNDRY_VERSION_STATUS` override continues to control the
engine's existing version configuration string. It is not reused as the
channel because prerelease statuses include a sequence number, such as
`alpha7`, while the JSON channel is only `alpha`, `beta`, `rc`, `stable`, or
`dev`.

The target fallback uses the actual SCons platform and architecture for each
compiled binary. Thus an arm64 macOS build reports `macos-arm64`; a universal
artifact is assembled from per-architecture binaries and retains the metadata
of the final binary selected by the packaging step unless the pipeline passes
an explicit universal target.

## Runtime architecture

Add a small `FoundryVersionInfo` helper under `main/` with one responsibility:
construct the version dictionary and serialize it with the engine JSON class.
It reads only generated compile-time macros and does not inspect Git,
environment variables, projects, or the filesystem at runtime. This keeps an
installed binary self-describing and makes the result reproducible.

Extend `FoundryCLIParser::ParseResult` with a top-level version-query flag.
Before normal command routing, the parser recognizes `--version` and allows
the machine-readable `--json` global flag in either order. A version query is
standalone: command names, arbitrary options, and other positional arguments
are rejected with the regular CLI error path. Arguments after `--` remain
user arguments for command invocations and are never interpreted as a version
query.

`Main::setup()` handles a successful version query immediately after CLI
parsing and before project loading, editor startup, header printing, or
command execution. JSON mode prints exactly the serialized object followed by
one newline and exits through the existing successful help/early-exit path.
Plain mode continues to print `get_full_version_string()` exactly as it does
today. The old raw `--version` handling is removed or made unreachable so it
cannot produce duplicate output.

## Release pipeline integration

`.github/scripts/resolve_release.py` will expose a `channel` output in
addition to the existing `status` output. Release-producing jobs in
`.github/workflows/release.yml` will pass these explicit values into every
SCons build:

```yaml
FOUNDRY_RELEASE_TAG: ${{ needs.resolve.outputs.tag }}
FOUNDRY_CHANNEL: ${{ needs.resolve.outputs.channel }}
FOUNDRY_BUILD_ID: gh
FOUNDRY_GIT_COMMIT: ${{ github.sha }}
FOUNDRY_GIT_DIRTY: false
```

The existing build action continues to set `BUILD_NAME` for non-release CI;
the generated metadata resolver uses it as the fallback build ID. Platform
and architecture are already passed to the build action as SCons inputs, so
the target fallback is populated for all release matrices without duplicating
target strings in workflow YAML. Extension API values use their defaults in
the release pipeline unless a future compatibility revision explicitly
overrides them.

The release packaging job will run `--version --json` against the packaged
Linux and macOS desktop binaries and validate the resolved release tag,
channel, build ID, commit, and clean-tree flag. This makes missing workflow
environment wiring fail the release before publication.

## Testing and documentation

Add C++ doctests for:

- top-level `--version` parsing in both option orders;
- rejection of extra version-query arguments;
- preservation of `--version` text after the user-argument separator;
- JSON validity, required field names, value types, generated values, and
  nested extension API values.

Extend the release resolver tests to assert the channel for prerelease and
stable releases and to assert that the workflow exposes the new metadata
contract. Add a built-binary smoke check to the release workflow as described
above.

Update `doc/tools/foundry_cli.md` with the new command, schema, override
variables, and the distinction between compile-time release metadata and
runtime command behavior.

## Files and responsibilities

- `methods.py`, `core/core_builders.py`, `core/SCsub`: resolve and emit
  compile-time metadata.
- `main/version_info.h`, `main/version_info.cpp`: serialize the version JSON.
- `main/cli_parser.h`, `main/cli_parser.cpp`: parse the standalone version
  query.
- `main/main.cpp`: dispatch plain and JSON version output before startup.
- `tests/core/os/test_foundry_cli_parser.h`: parser regression coverage.
- `tests/core/os/test_foundry_version_info.h`, `tests/test_main.cpp`: JSON
  contract coverage and test registration.
- `.github/scripts/resolve_release.py`,
  `misc/scripts/test_release_resolver.py`: expose and verify release channel
  metadata.
- `.github/workflows/release.yml`: provide release metadata to builds and
  validate packaged binaries.
- `doc/tools/foundry_cli.md`: document the public CLI contract.

No generated header or generated hash source is edited by hand.
