# PR Every Platform Checks Design

## Goal

Allow maintainers to run the full platform build matrix for a pull request on demand by commenting
`check every platform`. The feature is for CI confidence, not for producing downloadable editor artifacts.

## Trigger and authorization

Add a PR comment workflow triggered by `issue_comment` creation events. It handles comments on pull requests only and
matches `check every platform` case-insensitively. `build every platform` is also accepted as a natural alias because it
matches the way maintainers describe the action.

The request is accepted only when the commenter has `write`, `maintain`, or `admin` permission on the repository.
Unauthorized requests receive a short PR comment explaining that every-platform checks can only be requested by users
with write access. Non-request comments and matching comments on issues are ignored.

## Workflow architecture

Create a separate workflow, `.github/workflows/pr_platform_checks.yml`, instead of extending the PR editor artifact
workflow. The artifact workflow remains focused on downloadable Linux/macOS editor binaries, while the new workflow is
focused on whether all supported platform builds pass for the PR head.

The new workflow resolves the PR head repository and SHA, posts an accepted comment with the workflow run URL, and then
fans out to the existing reusable platform workflows:

- `.github/workflows/linux_builds.yml`
- `.github/workflows/macos_builds.yml`
- `.github/workflows/windows_builds.yml`
- `.github/workflows/android_builds.yml`
- `.github/workflows/ios_builds.yml`
- `.github/workflows/web_builds.yml`

Static checks are not included in the initial every-platform command because the request is specifically about platform
build coverage. The existing PR workflow can continue to cover normal Linux build/test behavior on every PR.

## Pull request checkout

Each reusable platform workflow gets optional `workflow_call` inputs for the checkout repository and checkout ref. When
the inputs are omitted, the workflows keep their current behavior for manual dispatch and existing callers. When the PR
check workflow calls them, it passes the PR head repository and SHA so every platform builds the requested PR commit.

All `actions/checkout` steps in those reusable workflows use the new inputs. This includes platform workflows with
multiple jobs, such as Linux, so every job in the requested platform run tests the same PR head.

## Reporting and concurrency

The accepted-request comment includes the PR number, short head SHA, and workflow run URL. The workflow result itself is
the source of truth for success or failure; maintainers can inspect the individual platform jobs in GitHub Actions.

The workflow posts a completion comment after the fan-out jobs finish. The comment reports success when every called
platform workflow succeeds, and failure when one or more platform workflows fail or are cancelled. The comment links to
the workflow run rather than duplicating job logs in the PR thread.

Use a concurrency group keyed by PR head SHA for the every-platform workflow, with `cancel-in-progress: false`. Repeated
requests for the same commit should queue instead of cancelling an already-running expensive matrix.

## Security notes

The workflow uses `GITHUB_TOKEN` with the minimum permissions needed to read PR metadata and comment on the PR. It
checks out and builds PR code only after the permission gate. Because the build executes PR code, the comment trigger
remains limited to repository users with write-level access or stronger.

## Tests

Add Python tests for the comment parser and planning helper, covering accepted phrases, unrelated comments, issue
comments, unauthorized users, and emitted PR metadata. Add a lightweight workflow-shape test that asserts the new
workflow checks authorization before calling reusable platform workflows, passes the PR head repository/ref into each
platform workflow, and includes a completion comment.

No engine build is required to validate this change locally; the relevant behavior is GitHub workflow planning and YAML
structure.

## Non-goals

This design does not add downloadable artifacts beyond what the platform workflows already upload. It also does not
replace normal PR CI, add per-platform phrase selection, or add a release-quality packaging workflow.
