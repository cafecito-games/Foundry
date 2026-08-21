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
  (`missing`). Each artifact carries the case ID, family, configuration, coordinates, both
  observations with their canonical SHA-256 digests, and a stable `comparison_id`. `--fail-on-regression`
  exits non-zero when any artifact is `new`, `worsened`, or `missing`. The same inputs always yield byte-identical output.
  Each artifact also carries the producing capability slice, resolved from the report's family through
  `modules/foundry_script/tests/type_completeness/capabilities.json` (`--capabilities` overrides the manifest).
- `propose --comparison comparison.json --case-id ID ...` writes the proposed per-finding ledger file
  (`<finding_id>.json`, exactly the fields the runner validates; the finding ID is the one the runner emitted
  for that case and dimension, never recomputed), the machine-readable provisional record
  (`provisional.json`), and the tracking-issue body that embeds it. The deadline is 17:00 America/New_York on
  the second weekday after detection, skipping weekends; `--detected-at` makes it reproducible.
- `reconcile --provisional provisional.json --ledger-dir <findings dir> --pull-request-state open|merged|closed`
  reports `pending_merge`, `merged`, `resolved`, `conflicting`, or `overdue` and exits non-zero when the state
  blocks the producing capability slice (`overdue`, `conflicting`). The output also carries `blocks_release`,
  which is true for every finding that is still unclassified, including `pending_merge` ones. A finding with neither a provisional record
  nor a merged ledger entry, any disagreement between the two other than classifying an `unclassified` proposal, or a merged pull request with no ledger
  file is `conflicting`; nothing disappears silently. Without `--pull-request-state` a ledger file is not trusted:
  the finding stays `pending_merge` (or `overdue`) and keeps blocking release until the merge is confirmed.

The `github` module wraps the `gh` CLI for the bot path: stable `bot/type-completeness/<finding_id>` branches,
one tracking issue per finding (label `type-completeness-finding`; a closed one is reopened and updated, never
duplicated), and one ledger pull request that is updated rather than duplicated. It refuses protected branches and never passes `--auto` or merges.

## Obtaining the `develop` report

The runner is currently driven from the doctest pilot (`--suite "*TypeCompleteness*"`) and writes its report
to the `report_path` in its options. Produce the branch and `develop` reports from binaries built at each
revision with the same configuration, then feed both files to `compare`.

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

`propose` takes the capability slice from the artifact; `--capability-path` may only narrow it and any path
outside the producing slice is rejected, so a provisional record cannot block a capability that did not produce
the finding.

## Deferred to the #2477 reconciliation

The runner on `develop` emits no per-case `outcome`, `structural_failures`, `diagnostic_records`, or
`diagnostic_severity`; those belong to the gate-safe evidence contract #2477 is introducing. This package
deliberately does not read or invent them. Once #2477 lands, the comparator's category handling (`report.Category`)
and the digest projection are the two places to extend, and the reconciliation relay tracks that work.

## Runner numbers

The engine JSON writer renders every Variant number as a float (`"schema_version": 1.0`, `"cell_count": 2.0`), so
loaders accept any integral JSON number for versions and counts and reject strings, booleans, and fractional
values. The fixture `RUNNER_REPORT_TEXT` in `scripts/tests/test_type_completeness_comparator.py` is the
byte-faithful reference for the runner's output.

## Runner categories

Until the runner emits a per-case `category`, every failed case is treated as `product_finding`. When a
`category` member is present (`product_finding`, `structural_failure`, `failed_witness`,
`stale_ledger_entry`, `resolved_ledger_entry`) it is consumed as-is and recorded on each comparison artifact.
