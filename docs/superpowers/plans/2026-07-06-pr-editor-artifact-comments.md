# PR Editor Artifact Comments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a permission-gated PR comment workflow that builds downloadable Linux and macOS editor artifacts on demand.

**Architecture:** Keep the workflow readable and put request parsing, permission decisions, deterministic artifact names,
and artifact search helpers in a small Python script with unit tests. The GitHub Actions YAML handles event wiring,
runner setup, platform-specific compilation, artifact upload, concurrency, and comments.

**Tech Stack:** GitHub Actions, existing Foundry composite build/cache/artifact actions, Python `unittest`, GitHub REST
API through `GITHUB_TOKEN`.

---

### Task 1: Request helper tests

**Files:**
- Create: `.github/scripts/test_pr_editor_artifact_request.py`
- Create: `.github/scripts/pr_editor_artifact_request.py`

- [ ] **Step 1: Write failing tests for comment parsing, permission checks, artifact names, matrix generation, and
artifact lookup.**

Run: `python3 .github/scripts/test_pr_editor_artifact_request.py`

Expected: FAIL because `.github/scripts/pr_editor_artifact_request.py` does not exist yet.

- [ ] **Step 2: Implement the helper script.**

The script exposes pure functions for the workflow and a CLI with `plan` and `find-artifact` subcommands. `plan` emits
GitHub step outputs for requested platforms, authorization, PR metadata, and matrix JSON. `find-artifact` searches
paginated artifact JSON and emits whether a non-expired artifact with the deterministic name already exists.

- [ ] **Step 3: Verify the helper tests pass.**

Run: `python3 .github/scripts/test_pr_editor_artifact_request.py`

Expected: PASS.

### Task 2: Artifact upload outputs

**Files:**
- Modify: `.github/actions/upload-artifact/action.yml`

- [ ] **Step 1: Add wrapper outputs for `artifact-id`, `artifact-url`, and `artifact-digest`.**

The wrapper keeps the current default name, path, and retention behavior, but gives new workflows direct access to the
download URL produced by `actions/upload-artifact`.

- [ ] **Step 2: Validate YAML syntax.**

Run: `python3 -c "import pathlib, yaml; [yaml.safe_load(path.read_text()) for path in pathlib.Path('.github').rglob('*.yml')]"`

Expected: exit code 0.

### Task 3: Comment-triggered workflow

**Files:**
- Create: `.github/workflows/pr_editor_artifacts.yml`

- [ ] **Step 1: Add the `issue_comment` workflow.**

The preflight job checks PR context, collaborator permission, parses requested platforms, fetches PR head metadata, and
comments for unauthorized requests or accepted requests.

- [ ] **Step 2: Add the matrix build job.**

Each requested OS checks for an existing artifact, skips duplicates with a comment, otherwise checks out the PR head,
builds/packages the editor, uploads the deterministic artifact, and comments success or failure. The job uses concurrency
keyed by PR head SHA and OS with `cancel-in-progress: false`.

- [ ] **Step 3: Validate workflow references and YAML syntax.**

Run:
`python3 .github/scripts/test_pr_editor_artifact_request.py && python3 -c "import pathlib, yaml; [yaml.safe_load(path.read_text()) for path in pathlib.Path('.github').rglob('*.yml')]"`

Expected: PASS and exit code 0.
