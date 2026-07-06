# PR Editor Artifact Comment Requests Design

## Goal

Allow maintainers to request downloadable editor builds from a pull request comment, using phrases such as
`Build me a macOS editor` or `Build me a linux editor`.

## Trigger and authorization

The workflow runs from `issue_comment` creation events and only handles comments made on pull requests. The comment body is
matched case-insensitively for Linux and macOS editor requests. A request is accepted only when the commenter has
`write`, `maintain`, or `admin` permission on the repository.

Unauthorized requests receive a short PR comment explaining that editor artifacts can only be requested by users with
write access. Non-request comments and matching comments on issues are ignored.

## Build behavior

Accepted requests build and package the requested editor artifact only; they do not run the unit test suite. A single
comment can request either platform or both platforms.

Linux builds use the existing Linux editor build flags and upload `bin/foundry.linuxbsd.editor.dev.x86_64`. macOS
builds reuse the existing macOS editor setup, compile `x86_64` and `arm64`, combine them with `lipo`, and upload
`bin/foundry.macos.editor.universal`.

## Dedupe and concurrency

Each artifact name is deterministic: `pr-<number>-<short-head-sha>-<os>-editor`. Before building, the workflow searches
the repository's non-expired workflow artifacts for the exact name. If one exists, the job skips compilation and comments
with the existing artifact link.

Each platform job also uses a concurrency group keyed by pull request head SHA and OS with `cancel-in-progress: false`.
Two near-simultaneous requests for the same PR SHA and OS cannot build at the same time; the queued job re-checks for an
existing artifact before doing work.

## User feedback

The workflow comments when a request is accepted, when a platform build starts, when an artifact already exists, when a
build succeeds, and when a build fails. Build-start comments include the requested platform and workflow run link. Success
comments include the artifact download link and workflow run link. Failure comments include the requested OS, PR SHA, and
workflow run link.

## Security notes

The workflow uses only `GITHUB_TOKEN`, with `contents: read`, `pull-requests: read`, `issues: write`, and
`actions: read`. It checks out the PR head only after the permission gate. The build still executes PR code, so the
comment trigger is intentionally limited to repository users with write-level access or stronger.
