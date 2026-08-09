# Pre-Commit Cleanliness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `pre-commit run --all-files` leave the repository and byte-exact protocol fixtures unchanged.

**Architecture:** Exclude the protocol's entire byte-exact fixture tree from the mutating file-format hook, correct real
en-US spelling debt, and whitelist only the valid analyzer term `statics`. Existing behavioral tests remain the proof;
no source/YAML text assertion is added.

**Tech Stack:** pre-commit YAML, codespell/TOML, Foundry Script fixtures, Python unittest.

---

### Task 1: Protect every byte-exact adapter fixture

**Files:**
- Modify: `.pre-commit-config.yaml`

- [ ] **Step 1: Extend the existing `file-format` exclusion**

Add this path to the current verbose exclusion block:

```yaml
tools/foundry-test-adapter/protocol/v1/fixtures/.*|
```

Keep `types_or: [text]` and every existing exception unchanged. Exclude the full tree because one currently affected
fixture is under `valid/report/`, not `invalid/`.

- [ ] **Step 2: Run adapter behavioral tests**

```sh
uv run --frozen --project tools/foundry-test-adapter \
  python -m unittest discover -s tools/foundry-test-adapter/tests
```

Expected: PASS; manifest tests continue validating BOM, CRLF, truncation, missing LF, and valid partial reports.

### Task 2: Resolve current codespell debt narrowly

**Files:**
- Modify: `pyproject.toml`
- Modify: `docs/superpowers/plans/2026-07-31-custom-json-marshalling.md`
- Modify: `docs/superpowers/plans/2026-07-31-custom-json-marshalling.md.tasks.json`
- Modify: `docs/superpowers/specs/2026-07-31-custom-json-marshalling-design.md`
- Modify: `modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_concrete.fs`

- [ ] **Step 1: Correct en-US spellings**

Replace the reported British spellings in prose and the fixture's private helper identifier with their en-US forms.
Update every call site of the renamed helper. Do not regenerate `.out` unless observable output actually changes.

- [ ] **Step 2: Ignore the valid analyzer term**

Add `statics` to the existing codespell ignore word list in `pyproject.toml`. Do not exclude the numeric-union design or
the whole docs tree.

- [ ] **Step 3: Run the affected analyzer fixture**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Script compilation and runtime*"
```

Expected: `typed_rest_parameter_concrete.fs` passes without fixture-output changes.

### Task 3: Commit the scoped fix

- [ ] **Step 1: Root review and commit**

```sh
git add .pre-commit-config.yaml pyproject.toml \
  docs/superpowers/plans/2026-07-31-custom-json-marshalling.md \
  docs/superpowers/plans/2026-07-31-custom-json-marshalling.md.tasks.json \
  docs/superpowers/specs/2026-07-31-custom-json-marshalling-design.md \
  modules/foundry_script/tests/scripts/analyzer/features/typed_rest_parameter_concrete.fs
git commit -m "Keep all-files pre-commit checks clean"
```

### Task 4: Prove all-files cleanliness after other workers stop editing

- [ ] **Step 1: Capture the integrated tree state**

```sh
git status --short
```

Expected: only intentional tracked branch changes are present; no generated scratch files are tracked.

- [ ] **Step 2: Run the repository-wide hook once, serialized**

```sh
pre-commit run --all-files
```

Expected: every hook exits zero.

- [ ] **Step 3: Prove the hook rewrote nothing**

```sh
git diff --exit-code
```

Expected: exit zero relative to the pre-hook working tree snapshot. If a hook formats intentional branch changes, review
and commit those changes, rerun the hook, and require the second run to produce no diff.
