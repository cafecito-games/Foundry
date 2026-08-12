# EditorNode Modularization Epic Delivery Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create and verify one Foundry GitHub epic with ten ordered native sub-issues that fully specify the approved `EditorNode` modularization work.

**Architecture:** The approved design spec is the source of truth for boundaries and acceptance criteria. The parent issue records the target architecture and aggregate definition of done; each child owns one independently mergeable extraction and is attached through GitHub's native `subIssues` relationship. Code implementation is intentionally planned just in time per child after its dependencies land, because all ten issues move state from the same header and implementation and precomputed line references would immediately become stale.

**Tech Stack:** GitHub Issues, GitHub GraphQL `addSubIssue`, GitHub CLI 2.97+, Markdown, Foundry repository conventions.

---

## Source documents and fixed decisions

- Design: `docs/superpowers/specs/2026-08-11-editor-node-modularization-design.md`
- Repository: `cafecito-games/Foundry`
- Parent labels: `epic`, `area:editor`
- Child labels: `area:editor`
- Milestone, assignee, and priority: unset unless the user separately requests them
- Landing order: serial, matching child numbers 1 through 10
- External dependency: child 1 depends on #2108
- Relationship: GitHub-native sub-issues, not checklist links alone
- Test policy: behavioral tests only; no tests that assert on source or documentation text

Each child body must contain these sections:

1. Summary
2. Problem
3. Required design
4. Dependencies
5. Observable invariants
6. Non-goals
7. Acceptance criteria
8. Required tests
9. Expected code areas
10. Verification

## Issue manifest

| Order | Exact title | Dependency |
| ---: | --- | --- |
| 1 | Extract the editor shell view from EditorNode | #2108 |
| 2 | Extract workspace focus and scene-context coordination | Child 1 |
| 3 | Move board lifecycle and workspace persistence into the workspace controller | Child 2 |
| 4 | Extract scene and resource persistence mechanics | Child 3, landing order |
| 5 | Extract scene lifecycle workflows | Child 4 |
| 6 | Extract plugin lifecycle and edit routing | Child 5, landing order |
| 7 | Extract project and filesystem change coordination | Child 6, landing order; child 4 functionally |
| 8 | Separate command policy from menus and dialogs | Children 1-7 |
| 9 | Move mutable editor-wide state into EditorSession | Child 8 |
| 10 | Contract the EditorNode facade and migrate internal consumers | Child 9 |

### Task 1: Verify repository metadata and the approved source

**Files:**
- Read: `docs/superpowers/specs/2026-08-11-editor-node-modularization-design.md`

- [ ] **Step 1: Confirm GitHub authentication and repository identity**

Run:

```sh
gh auth status
gh repo view cafecito-games/Foundry --json nameWithOwner,defaultBranchRef
```

Expected: authenticated as an account with `repo` scope; repository is `cafecito-games/Foundry`; default branch is `develop`.

- [ ] **Step 2: Confirm required labels exist**

Run:

```sh
gh label list --repo cafecito-games/Foundry --limit 200 --json name \
  --jq 'map(.name) | map(select(. == "epic" or . == "area:editor")) | sort | .[]'
```

Expected:

```text
area:editor
epic
```

- [ ] **Step 3: Confirm #2108 remains the startup dependency**

Run:

```sh
gh issue view 2108 --repo cafecito-games/Foundry --json number,title,state,url
```

Expected: issue #2108 is returned with title `macOS startup: make Main::start() and EditorNode construction resumable`. Its open/closed state is recorded in child 1 without changing the dependency.

- [ ] **Step 4: Scan the approved spec for incomplete issue content**

Run:

```sh
rg -n 'T[B]D|T[O]DO|F[I]XME|implement la[t]er|fill i[n]' \
  docs/superpowers/specs/2026-08-11-editor-node-modularization-design.md
```

Expected: no output and exit status 1 from `rg` because there are no placeholders.

### Task 2: Create the parent epic

**GitHub target:**
- Create issue in: `cafecito-games/Foundry`
- Labels: `epic`, `area:editor`

- [ ] **Step 1: Assemble the parent body from the approved spec**

Use these exact spec sections, preserving their substance:

- `Summary`
- `Context`
- `Goals`
- `Non-goals`
- `Target architecture`
- `Dependency and data-flow rules`
- `Migration strategy`
- `Parent epic acceptance criteria`
- `Epic sequencing`
- `Definition of done`

The first version uses the issue manifest titles without guessed issue numbers. Do not invent milestones, priorities, dates, or ownership.

- [ ] **Step 2: Create the parent through the GitHub connector**

Create an issue with:

```text
Title: Modularize EditorNode into an editor shell and application controllers
Labels: epic, area:editor
```

Expected: one open issue is returned with its issue number, URL, and node ID discoverable through GraphQL.

- [ ] **Step 3: Record and verify the parent node ID**

Run, replacing `<EPIC_NUMBER>` with the created number:

```sh
gh api graphql \
  -F owner=cafecito-games \
  -F repo=Foundry \
  -F number=<EPIC_NUMBER> \
  -f query='query($owner:String!,$repo:String!,$number:Int!){repository(owner:$owner,name:$repo){issue(number:$number){id number title labels(first:20){nodes{name}}}}}'
```

Expected: the title matches exactly and labels contain `epic` and `area:editor`.

### Task 3: Create the ten child issues in dependency order

**GitHub target:**
- Create issues in: `cafecito-games/Foundry`
- Labels: `area:editor`

- [ ] **Step 1: Create child 1 from spec section 1**

Use the exact title in the manifest. Expand the corresponding spec section into the required ten-section body. State `Depends on #2108`; include shell/startup invariants, all approved acceptance criteria, the issue-1 expected code areas, and its focused verification row.

Expected: one open issue labeled `area:editor`; record number and node ID as `CHILD_1`.

- [ ] **Step 2: Create child 2 from spec section 2**

Use the exact title in the manifest. State `Depends on #<CHILD_1_NUMBER>` and include the approved workspace focus/context contract, invariants, tests, code areas, and verification.

Expected: one open issue labeled `area:editor`; record number and node ID as `CHILD_2`.

- [ ] **Step 3: Create child 3 from spec section 3**

State `Depends on #<CHILD_2_NUMBER>`. Include board close atomicity, stable identity, dormant-board focus, tab semantics, persistence compatibility, and all approved tests.

Expected: record number and node ID as `CHILD_3`.

- [ ] **Step 4: Create child 4 from spec section 4**

State `Depends on #<CHILD_3_NUMBER> for landing order`. Include structured persistence outcomes, UI independence, scratch-only files, failure atomicity, round-trip coverage, and approved code areas.

Expected: record number and node ID as `CHILD_4`.

- [ ] **Step 5: Create child 5 from spec section 5**

State `Depends on #<CHILD_4_NUMBER>`. Include scene workflow ownership, explicit pending decisions, the complete save/close decision matrix, signal ordering, and required real-editor workflows.

Expected: record number and node ID as `CHILD_5`.

- [ ] **Step 6: Create child 6 from spec section 6**

State `Depends on #<CHILD_5_NUMBER> for landing order`. Include plugin registration order, duplicate prevention, activation rollback, routing, extension reload, teardown, and plugin API non-goals.

Expected: record number and node ID as `CHILD_6`.

- [ ] **Step 7: Create child 7 from spec section 7**

State `Depends on #<CHILD_6_NUMBER> for landing order; uses the persistence contract from #<CHILD_4_NUMBER>`. Include subscription lifetime, affected-resource behavior, external decisions, translation coalescing, and queued-work teardown.

Expected: record number and node ID as `CHILD_7`.

- [ ] **Step 8: Create child 8 from spec section 8**

State `Depends on #<CHILD_1_NUMBER> through #<CHILD_7_NUMBER>`. Include compatibility command IDs, authoritative dispatch, projectless restrictions, structured outcomes, and duplicate-signal prevention.

Expected: record number and node ID as `CHILD_8`.

- [ ] **Step 9: Create child 9 from spec section 9**

State `Depends on #<CHILD_8_NUMBER>`. Include one-owner state, compatibility forwarding, atomic context observation, no-scene validity, project transition isolation, and session construction without `EditorNode`.

Expected: record number and node ID as `CHILD_9`.

- [ ] **Step 10: Create child 10 from spec section 10**

State `Depends on #<CHILD_9_NUMBER>`. Include the final responsibility audit, remaining include audit, compatibility-only forwarding, shutdown order, strict/full verification, and end-to-end acceptance workflow.

Expected: record number and node ID as `CHILD_10`.

### Task 4: Attach children as native sub-issues

**GitHub target:**
- Parent node ID: recorded in Task 2
- Child node IDs: recorded in Task 3

- [ ] **Step 1: Attach each child in manifest order**

For each child node ID, run this mutation with the recorded values:

```sh
gh api graphql \
  -F parent='<EPIC_NODE_ID>' \
  -F child='<CHILD_NODE_ID>' \
  -f query='mutation($parent:ID!,$child:ID!){addSubIssue(input:{issueId:$parent,subIssueId:$child}){issue{id number} subIssue{id number}}}'
```

Expected for every call: the returned parent number is the epic number and the returned sub-issue number is the intended child. Stop on the first mismatch instead of attaching later children out of order.

- [ ] **Step 2: Verify native order and parent links**

Run:

```sh
gh api graphql \
  -F owner=cafecito-games \
  -F repo=Foundry \
  -F number=<EPIC_NUMBER> \
  -f query='query($owner:String!,$repo:String!,$number:Int!){repository(owner:$owner,name:$repo){issue(number:$number){subIssues(first:20){nodes{number title state parent{number}}}}}}'
```

Expected: exactly ten nodes, in manifest order; every `parent.number` equals the epic number.

### Task 5: Add numbered links to the parent and verify every issue

- [ ] **Step 1: Update the parent body with the actual child numbers**

Replace the provisional manifest with a Markdown checklist in manifest order:

```markdown
- [ ] #<CHILD_1_NUMBER> — Extract the editor shell view from EditorNode
- [ ] #<CHILD_2_NUMBER> — Extract workspace focus and scene-context coordination
- [ ] #<CHILD_3_NUMBER> — Move board lifecycle and workspace persistence into the workspace controller
- [ ] #<CHILD_4_NUMBER> — Extract scene and resource persistence mechanics
- [ ] #<CHILD_5_NUMBER> — Extract scene lifecycle workflows
- [ ] #<CHILD_6_NUMBER> — Extract plugin lifecycle and edit routing
- [ ] #<CHILD_7_NUMBER> — Extract project and filesystem change coordination
- [ ] #<CHILD_8_NUMBER> — Separate command policy from menus and dialogs
- [ ] #<CHILD_9_NUMBER> — Move mutable editor-wide state into EditorSession
- [ ] #<CHILD_10_NUMBER> — Contract the EditorNode facade and migrate internal consumers
```

Keep the native relationship; the checklist is a readable index, not a substitute for it.

- [ ] **Step 2: Verify issue labels, bodies, and relationships**

Fetch the parent and all ten children. Confirm:

- parent has `epic` and `area:editor`;
- every child has `area:editor`;
- every child has all ten required body sections;
- every child has its approved acceptance criteria and no placeholder text;
- dependencies reference actual issue numbers;
- parent has exactly ten native sub-issues in the approved order;
- no duplicate issue was created.

- [ ] **Step 3: Report the created epic**

Return the parent URL, the ordered child URLs, the native relationship verification result, and any deviation from the approved spec. Do not begin code implementation as part of issue creation.

## Per-child implementation planning rule

Before implementation of each child begins:

1. update local `develop` after its dependencies merge;
2. re-open the child and approved design spec;
3. inspect the current ownership and line locations in the modified `EditorNode`;
4. create a dedicated `docs/superpowers/plans/YYYY-MM-DD-<child-topic>.md` with TDD steps, exact current paths,
   signatures, commands, expected failures, expected passes, and commit boundaries;
5. execute only that child and its stated prerequisites;
6. attach the plan or its commit to the child PR.

This rule prevents later children from implementing against stale pre-epic line numbers or transitional APIs that
earlier children have already removed.
