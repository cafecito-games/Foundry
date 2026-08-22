# Type-completeness baseline comparator and finding automation

Stdlib-only tooling behind the provisional-finding policy in
`docs/superpowers/specs/2026-08-20-foundry-script-type-system-completeness-design.md`. It consumes the JSON
report the type-completeness runner writes (`schema_version` 1, `cases[]` with `passed`, `expected`, `actual`,
`runtime_status`, `diagnostics`, `produced_output`, `expected_output`), compares a branch report against the
matching `develop` report, and produces the artifacts the policy requires.

Run it from the repository root as `python3 -m scripts.type_completeness <command>`.

## Commands

- `compare --branch-report B --develop-report D --configuration text --output comparison.json`
  emits one deterministic artifact per branch failure (`new`, `worsened`, `unchanged`), per develop failure
  the branch fixed (`resolved`), and per develop failure whose case the branch report no longer contains
  (`missing`) or that the branch report no longer contains although it passed on develop (`vanished`, lost
  coverage). Each artifact carries the case ID, family, configuration, coordinates, both
  observations with their canonical SHA-256 digests, and a stable `comparison_id`. `--fail-on-regression`
  exits non-zero when any artifact is `new`, `worsened`, `missing`, or `vanished`. The same inputs always yield byte-identical output.
  Each artifact also carries the producing capability slice, resolved from the report's family through
  `modules/foundry_script/tests/type_completeness/capabilities.json` (`--capabilities` overrides the manifest).
- `propose --comparison comparison.json --case-id ID ...` keeps the classification, issue URL, closure-packet
  URL, and permanent test paths the runner echoes for a finding already in the ledger (command-line values
  override them); only a genuinely new finding defaults to `unclassified` and must supply those fields. It writes the proposed per-finding ledger file
  (`<finding_id>.json`, exactly the fields the runner validates; the finding ID is the one the runner emitted
  for that case and dimension, never recomputed), the machine-readable provisional record
  (`provisional.json`), and the tracking-issue body that embeds it. The deadline is 17:00 America/New_York on
  the second weekday after detection, skipping weekends; `--detected-at` makes it reproducible. Timestamps on
  the command line are ISO-8601 with an explicit offset (`Z`, `+HH:MM`, or `+HHMM`); naive values are rejected.
- `reconcile --provisional provisional.json --ledger-dir <findings dir> --pull-request-state open|merged|closed`
  reports `pending_merge`, `merged`, `resolved`, `conflicting`, or `overdue` and exits non-zero when the state
  blocks the producing capability slice (`overdue`, `conflicting`). The output also carries `blocks_release`,
  which is true for every finding that is still unclassified, including `pending_merge` ones. A finding with neither a provisional record
  nor a merged ledger entry, any disagreement between the two other than classifying an `unclassified` proposal, or a merged pull request with no ledger
  file is `conflicting`; nothing disappears silently. Without `--pull-request-state` a ledger file is not trusted:
  the finding stays `pending_merge` (or `overdue`) and keeps blocking release until the merge is confirmed.

The `github` module wraps the `gh` CLI for the bot path: stable `bot/type-completeness/<finding_id>` branches,
one tracking issue per finding (label `type-completeness-finding`; a closed one is reopened and updated, never
duplicated), and one ledger pull request per bot branch looked up in any state. The finding ID in the issue
title and the bot branch name are the idempotency keys; GitHub cannot enforce them atomically, so after every
create the client re-lists and converges concurrent duplicates onto the lowest-numbered open one (closing the
others with a comment naming the survivor). Duplicates are converged, not prevented. Pull requests are matched on head branch and base branch: one from the bot branch to another base is never
silently reused; the only open one is retargeted with `gh pr edit --base`, several open ones are an error.
For pull requests to the requested base: an open one is updated, otherwise a merged one is
authoritative and left alone, and only when neither exists is a closed unmerged one reopened and updated. It refuses protected branches and never passes `--auto` or merges. Pushes to a bot branch carry an explicit
lease: the automation records the branch's remote SHA with `observe_remote_branch` before it starts (absent
for a new branch) and pushes with `--force-with-lease=<ref>:<that SHA>`, so a stale run can never replace a
newer reviewed commit; a rejected lease is an error naming both SHAs and is not retried.

## Obtaining the `develop` report

`foundry --headless test completeness run --family <f> --catalog <dir> --scratch <dir> --report <path>
--tier presubmit` publishes one report per family. CI runs that command for every family on each push to
`develop` and uploads the reports as `type-completeness-baseline-<sha>`; the presubmit gate downloads the
artifact for `git merge-base origin/develop HEAD`, so the two sides of a comparison always come from the same
configuration. Locally, produce the branch and `develop` reports from binaries built at each revision with
the same configuration and feed both files to `compare`.

## Manual fallback

While the bot path is unavailable, a human follows the same contract:

1. Run `compare` with the branch and `develop` reports and attach `comparison.json` to the pull request.
2. Run `propose` with `--origin manual`, the tracking issue URL, the linked ledger pull request URL, the
   capability paths from `modules/foundry_script/tests/type_completeness/capabilities.json`, and the owner.
3. Open the tracking issue with the generated `tracking_issue_body.md`; the embedded block is the provisional
   source of truth until the ledger pull request merges.
4. Commit `<finding_id>.json` under `modules/foundry_script/tests/type_completeness/findings/` on a normal
   reviewed pull request; never auto-merge it.
5. Run `reconcile` until it reports `merged`. Manual records carry `"origin": "manual"` and otherwise have the
   same fields, deadline, reconciliation, and blocking semantics as automated ones.

`propose` only files proposals for `new`, `worsened`, and `unchanged` artifacts; every other status has no
branch failure to file and exits 2 with a message. Deserializing a comparison re-derives each artifact's status from its two sides with the same classifier
`compare` used and checks every digest against its evidence, so a relabelled or edited artifact is rejected. `propose` accepts `--case-id` more than once. For a finding the ledger filed under a historical case ID that
migrations now resolve to several cases, the historical entry is re-proposed verbatim unless every resolved
child is listed in the same run, in which case one record per child is written (named by the runner's ID
formula) and the provisional records carry `migrated_from`, `resolved_case_ids`, and `proposed_case_ids`. The
historical entry is never replaced implicitly.

A provisional record embeds its develop comparison as a full artifact that is re-validated on every parse
(identity, digests, status) and whose recorded capability slice must equal the slice the capabilities
manifest derives for its family; the record's own slice must lie within it. `propose` and `reconcile` take
`--capabilities` for that manifest. `propose` takes the capability slice from the artifact; `--capability-path` may only narrow it and any path
outside the producing slice is rejected, so a provisional record cannot block a capability that did not produce
the finding.

## Gate-safe evidence

The runner publishes a top-level `outcome` (`passed`, `product_mismatch`, `structural_failure`) alongside
`success`, and a `structural_failures` array carrying the evidence behind a broken run. `report.load_report`
requires `outcome`, refuses a document where it disagrees with `success`, and exposes it as
`Report.is_structural_failure`; `compare` refuses to draw any case-level verdict from such a report and writes
a refusal envelope instead. Per-case `diagnostics` and `diagnostic_records` are ordinary evidence: they feed
observation digests and parity findings, and nothing recomputes them.

## Runner numbers

The engine JSON writer renders every Variant number in a *runner report* as a float
(`"schema_version": 1.0`, `"cell_count": 2.0`), so loaders accept any integral JSON number for versions and
counts and reject strings, booleans, and fractional values. The fixture `RUNNER_REPORT_TEXT` in
`scripts/tests/test_type_completeness_comparator.py` is the byte-faithful reference for the runner's output.

The *selection* document is different and deliberately so. `foundry test completeness select --json` builds
its document from a Dictionary holding `int64_t` members, so `JSON::stringify` renders `"schema_version": 1`
as a JSON integer. Neither producer is normalized to match the other: both spellings are accepted wherever a
version or a count is read, and `scripts/tests/fixtures/type_completeness/` holds captured samples of each.

## Runner categories

Every failed case with no `category` member is treated as `product_finding`. When a `category` member is
present (`product_finding`, `structural_failure`, `failed_witness`, `stale_ledger_entry`,
`resolved_ledger_entry`) it is consumed as-is and recorded on each comparison artifact.

## Presubmit gate

`scripts/type_completeness/presubmit.py` is the pull-request gate. It orchestrates and decides nothing the
steps already decide:

```sh
FOUNDRY_TEST_SCRATCH=$PWD/.test_scratch python3 scripts/type_completeness/presubmit.py \
  --binary bin/foundry.linuxbsd.editor.dev.x86_64 \
  --output-dir type-completeness \
  --baseline-dir baseline
```

The runner refuses a report path outside the test scratch space, so `--scratch` is required unless
`FOUNDRY_TEST_SCRATCH` names that root; the wrapper refuses the invocation rather than inventing a path
every family run would then fail on.

It writes `selection.json`, one `report-<family>.json` per family it ran, `comparison.json`, and
`verdict.json` into `--output-dir`, and the same documents come out of a CI run and a local run on the same
inputs, `timings` aside. The runner itself writes below `--scratch`, which defaults to
`$FOUNDRY_TEST_SCRATCH/type-completeness-presubmit`, because the runner refuses a report path outside the
test scratch space; each published report is copied out so the artifact carries the evidence behind the
verdict. `verdict.json` carries a digest over the selection, the families run, the
blocking and known-mismatch identities, the baseline state, and the state itself - never over timings, paths,
run ids, or timestamps.

Selection happens only in C++. The wrapper shells out to `foundry test completeness select`, parses its JSON,
and refuses to gate on a selection whose `validation_errors` is non-empty; it never re-derives a family from a
path. Comparison is the `compare` command above, statuses come from `comparator.Status`, and an unchanged
in-slice failure is reconciled through `reconcile.reconcile_finding`.

Every state maps to exactly one exit code:

| State | Exit | Meaning |
| --- | --- | --- |
| `passed` | 0 | Nothing in the selected slice regressed. |
| `nothing_selected` | 0 | No changed path maps to a family; no slice ran. |
| `blocked` | 1 | An in-slice regression, or an unchanged in-slice failure with no ledger authority. |
| `baseline_missing` | 1 | A slice ran with no `develop` side, so no absence of regression can be shown. |
| `structural_failure` | 2 | A run broke, published a structural failure, or exited outside the runner's vocabulary. |
| `baseline_malformed` | 2 | A baseline artifact exists but cannot be loaded. This is not "missing". |
| `malformed_input` | 2 | The selection or comparison document cannot be interpreted. |
| `timeout` | 3 | The runner crossed its budget, or the wrapper's deadline fired. |
| `selector_validation_failed` | 4 | The selector reported a validation error; the slice it reached still ran and published. |

A baseline artifact that does not exist is not an error and is never downgraded: every in-slice failure is
then compared against an absent `develop` side, which `classify` reads as `new`, so the gate blocks. A run
that found no failure at all still does not pass in that situation. Comparison can only see cases that exist
on one of its two sides, so a case that existed on `develop` and vanished from the branch produces no
artifact when the `develop` side is absent - exactly the coverage regression `vanished` exists to catch.
Rather than report a clean run it cannot substantiate, the gate reports `baseline_missing`.
