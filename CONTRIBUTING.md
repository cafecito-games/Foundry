# Contributing to Foundry

Foundry is an experimental project focused on stricter scripting, editor
tooling, and language-server/refactoring surfaces. It is not a general-purpose
support channel, and support, stability, timelines, and backwards compatibility
are not guaranteed.

Contributions are welcome when they fit the direction of the project, are
focused enough to review, and include enough context to understand the problem
being solved.

## Table of contents

- [Reporting bugs](#reporting-bugs)
- [Requesting features](#requesting-features)
- [Contributing pull requests](#contributing-pull-requests)
- [Commit style](#commit-style)
- [Documentation](#documentation)
- [Tests](#tests)
- [Communication](#communication)

## Reporting bugs

Open an issue in this repository when you find a reproducible bug. Please
include:

- A clear description of the problem.
- The Foundry commit, branch, or build you tested.
- Your operating system and relevant build options.
- Steps to reproduce the issue from a clean project or minimal script.
- The expected behavior and the actual behavior.
- Logs, assertions, crashes, screenshots, or sample files when they help explain
  the issue.

Small, focused reproductions are much easier to diagnose than full projects. If
the issue affects Foundry Script, include the smallest `.fs` snippet or fixture
that shows the behavior.

If the bug is a regression, mention the last known commit or build where it
worked and the first one where it failed, if you know them.

## Requesting features

Feature requests can be opened as issues in this repository. This project is
experimental and opinionated, so an open feature request is not a commitment
that the work will be accepted, prioritized, or implemented.

Before opening a request, describe the real workflow or problem behind it. The
most useful requests explain:

- What you are trying to build or maintain.
- Why the current behavior is insufficient.
- What a good outcome would look like.
- Whether you are willing to help design, implement, or test the change.

Large language, editor, or workflow changes may need discussion before a pull
request is practical.

## Contributing pull requests

Keep pull requests focused on one behavior change or cleanup. Small PRs are
easier to review and easier to merge safely.

Before opening a PR:

- Make sure the change fits Foundry's current direction.
- Check whether there is an existing issue or discussion that gives useful
  context.
- Include tests for behavior changes whenever practical.
- Update documentation or class reference files when public behavior changes.
- Describe the user-visible behavior, risk, and validation you performed.

For larger changes, especially in Foundry Script, editor tooling, refactoring,
or the language server, it is usually best to open an issue first. That helps
avoid spending time on an implementation that does not fit the project.

Avoid bundling unrelated formatting, refactors, generated files, or cleanup with
behavior changes unless they are required for the PR.

## Commit style

Use readable, focused commits. Prefer concise imperative subjects, usually under
72 characters, such as:

- Fix strict nullability diagnostic for typed arrays
- Add Foundry Script rename coverage for traits
- docs: Clarify experimental support policy

Each commit should move the tree from one working state to another. If a later
commit only fixes a typo, build breakage, or test failure introduced by an
earlier commit in the same PR, squash or amend before review when practical.

When updating a branch with upstream changes, prefer rebasing over merge commits
unless there is a specific reason to preserve a merge.

## Documentation

Document changes that affect contributors, users, scripting APIs, editor
behavior, or command-line workflows.

If a PR adds or changes exposed methods, properties, signals, annotations, or
language behavior, update the relevant class reference, module documentation,
or Foundry Script fixtures alongside the code.

Comments in code should explain non-obvious decisions, invariants, or edge
cases. Avoid comments that only restate what the code already says.

## Tests

Add or update tests for bug fixes and behavior changes. Foundry Script parser,
analyzer, completion, LSP, formatter, and refactor behavior should usually have
fixture coverage under `modules/foundry_script/tests/scripts/` or focused C++
tests under `modules/foundry_script/tests/`.

Typical local validation commands for Linux:

```sh
python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)
./bin/<editor-binary> --headless --test --force-colors
```

Use the editor binary produced by your build. Use the equivalent platform and
binary names for your local machine.

Regenerate Foundry Script expected-output fixtures only when the behavior change
is intentional, and include the regenerated files in the same pull request.

## Communication

Use issues and pull requests in this repository for project discussion. Keep
reports and review comments concrete, technical, and tied to reproducible
behavior where possible.

Thanks for your interest in Foundry.
