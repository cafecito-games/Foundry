"""Command-line entry point: ``python3 -m scripts.type_completeness <command>``."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from . import comparator, deadline, ledger, provisional, reconcile, report

DEFAULT_CAPABILITIES_PATH = (
    Path(__file__).resolve().parents[2]
    / "modules"
    / "foundry_script"
    / "tests"
    / "type_completeness"
    / "capabilities.json"
)


def _write_json(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _compare(arguments: argparse.Namespace) -> int:
    branch = report.load_report_file(Path(arguments.branch_report))
    develop = report.load_report_file(Path(arguments.develop_report))
    capabilities = report.load_capabilities_file(Path(arguments.capabilities))
    artifacts = comparator.compare_reports(branch, develop, arguments.configuration, capabilities)
    _write_json(Path(arguments.output), comparator.serialize_many(artifacts))
    blocking = [artifact for artifact in artifacts if artifact.status in comparator.REGRESSION_STATUSES]
    for artifact in artifacts:
        print(f"{artifact.status.value:10} {artifact.case_id}")
    return 1 if blocking and arguments.fail_on_regression else 0


def _finding_for(artifact: comparator.ComparisonArtifact, dimension: str) -> Optional[dict[str, Any]]:
    findings = [finding for finding in artifact.branch["findings"] or [] if finding["dimension"] == dimension]
    return findings[0] if len(findings) == 1 else None


def _capability_paths(
    artifact: comparator.ComparisonArtifact, requested: Optional[list[str]]
) -> tuple[Optional[list[str]], Optional[str]]:
    if artifact.capability_slice is None:
        if not requested:
            return None, "comparison artifact carries no capability slice; pass --capability-path explicitly"
        return list(requested), None
    producing = artifact.capability_slice.paths
    paths = list(requested or producing)
    outside = sorted(set(paths) - set(producing))
    if outside:
        return None, f"--capability-path {outside} lies outside the producing capability slice {list(producing)}"
    return paths, None


def _ledger_fields(
    echoed: dict[str, Any], arguments: argparse.Namespace
) -> tuple[Optional[dict[str, Any]], Optional[str]]:
    """Ledger fields for a proposal: a command-line value wins, an echoed ledger value is kept, and only a
    genuinely new finding must supply every field."""
    issue_url = arguments.issue_url or str(echoed.get("issue_url") or "")
    closure_packet_url = arguments.closure_packet_url or str(echoed.get("closure_packet_url") or "")
    permanent_test_paths = list(
        arguments.permanent_test_path or [str(path) for path in echoed.get("permanent_test_paths") or []]
    )
    missing = [
        name
        for name, value in (
            ("--issue-url", issue_url),
            ("--closure-packet-url", closure_packet_url),
            ("--permanent-test-path", permanent_test_paths),
        )
        if not value
    ]
    if missing:
        return None, f"finding is not in the ledger yet; {', '.join(missing)} must be supplied"
    return {
        "issue_url": issue_url,
        "closure_packet_url": closure_packet_url,
        "permanent_test_paths": permanent_test_paths,
        "classification": str(echoed["classification"]),
    }, None


def _propose(arguments: argparse.Namespace) -> int:
    artifacts = comparator.deserialize_many(Path(arguments.comparison).read_text(encoding="utf-8"))
    by_case = {artifact.case_id: artifact for artifact in artifacts}
    requested = list(dict.fromkeys(arguments.case_id))
    selected: list[tuple[comparator.ComparisonArtifact, dict[str, Any]]] = []
    for case_id in requested:
        artifact = by_case.get(case_id)
        if artifact is None:
            print(f"comparison has no artifact for case {case_id!r}", file=sys.stderr)
            return 2
        finding = _finding_for(artifact, arguments.dimension)
        if finding is None:
            print(
                f"comparison artifact for case {case_id!r} has no finding for dimension {arguments.dimension!r}",
                file=sys.stderr,
            )
            return 2
        selected.append((artifact, finding))

    detected_at = (
        deadline.parse_timestamp(arguments.detected_at) if arguments.detected_at else datetime.now(timezone.utc)
    )
    proposals: list[tuple[dict[str, Any], provisional.ProvisionalRecord]] = []
    handled_finding_ids: set[str] = set()
    for artifact, echoed in selected:
        finding_id = str(echoed["finding_id"])
        if finding_id in handled_finding_ids:
            continue
        capability_paths, error = _capability_paths(artifact, arguments.capability_path)
        if error or capability_paths is None:
            print(error, file=sys.stderr)
            return 2
        fields, error = _ledger_fields(echoed, arguments)
        if error or fields is None:
            print(error, file=sys.stderr)
            return 2
        migrated_from = str(echoed.get("migrated_from") or "")
        resolved_case_ids = [str(case_id) for case_id in echoed.get("resolved_case_ids") or []]
        group = [case_id for case_id in requested if case_id in (resolved_case_ids or [artifact.case_id])]
        if migrated_from and not set(resolved_case_ids) <= set(group):
            # A partial proposal for a migrated entry must not replace the historical record, or the siblings
            # that still resolve through it would lose their classification: re-propose it verbatim.
            targets = [(migrated_from, finding_id, artifact)]
        elif migrated_from:
            targets = [
                (child, ledger.runner_finding_id(child, arguments.dimension), by_case[child])
                for child in resolved_case_ids
            ]
        else:
            targets = [(artifact.case_id, finding_id, artifact)]
        for case_id, target_finding_id, target_artifact in targets:
            payload = ledger.proposed_record(
                finding_id=target_finding_id,
                family=target_artifact.family,
                case_id=case_id,
                dimension=arguments.dimension,
                **fields,
            )
            record = provisional.ProvisionalRecord.create(
                finding_id=payload["finding_id"],
                payload=payload,
                capability_slice=capability_paths,
                workstream_owner=arguments.workstream_owner,
                detection_artifact=arguments.detection_artifact,
                develop_comparison=target_artifact.to_dict(),
                detected_at=detected_at,
                bot_pr_url=arguments.bot_pr_url,
                origin=arguments.origin,
                migrated_from=migrated_from or None,
                resolved_case_ids=resolved_case_ids,
                proposed_case_ids=group if migrated_from else (),
            )
            proposals.append((payload, record))
        handled_finding_ids.add(finding_id)

    output_dir = Path(arguments.output_dir)
    for payload, record in proposals:
        ledger.write_record(output_dir, payload)
        suffix = "" if len(proposals) == 1 else f"_{record.finding_id}"
        _write_json(
            output_dir / f"provisional{suffix}.json", json.dumps(record.to_dict(), sort_keys=True, indent=2) + "\n"
        )
        _write_json(output_dir / f"tracking_issue_body{suffix}.md", provisional.render_issue_body(record))
        print(f"{record.finding_id} due {deadline.format_timestamp(record.due_at)}")
    return 0


def _reconcile(arguments: argparse.Namespace) -> int:
    record = provisional.ProvisionalRecord.from_dict(
        json.loads(Path(arguments.provisional).read_text(encoding="utf-8"))
    )
    merged = ledger.load_ledger(Path(arguments.ledger_dir)).get(record.finding_id)
    pull_request = None
    if arguments.pull_request_state:
        pull_request = reconcile.PullRequest(url=record.bot_pr_url or "", state=arguments.pull_request_state)
    now = deadline.parse_timestamp(arguments.now) if arguments.now else datetime.now(timezone.utc)
    result = reconcile.reconcile_finding(
        finding_id=record.finding_id,
        provisional_record=record,
        merged_record=merged,
        pull_request=pull_request,
        now=now,
        comparison_status=arguments.comparison_status,
    )
    text = json.dumps(result.to_dict(), sort_keys=True, indent=2) + "\n"
    if arguments.output:
        _write_json(Path(arguments.output), text)
    print(text, end="")
    return 1 if result.blocks_slice else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="type_completeness")
    commands = parser.add_subparsers(dest="command", required=True)

    compare = commands.add_parser("compare", help="compare a branch report against the matching develop report")
    compare.add_argument("--branch-report", required=True)
    compare.add_argument("--develop-report", required=True)
    compare.add_argument("--configuration", required=True, help="capability configuration label, e.g. text")
    compare.add_argument(
        "--capabilities",
        default=str(DEFAULT_CAPABILITIES_PATH),
        help="capabilities manifest mapping families to production paths (default: the repository manifest)",
    )
    compare.add_argument("--output", required=True)
    compare.add_argument("--fail-on-regression", action="store_true")
    compare.set_defaults(handler=_compare)

    propose = commands.add_parser("propose", help="emit a proposed ledger entry and provisional record")
    propose.add_argument("--comparison", required=True)
    propose.add_argument(
        "--case-id",
        action="append",
        required=True,
        help="repeatable; every resolved child of a migrated entry must be listed to split it",
    )
    propose.add_argument("--dimension", required=True)
    propose.add_argument(
        "--issue-url", help="required for a new finding; defaults to the ledger value the runner echoes"
    )
    propose.add_argument(
        "--closure-packet-url", help="required for a new finding; defaults to the ledger value the runner echoes"
    )
    propose.add_argument(
        "--permanent-test-path",
        action="append",
        help="required for a new finding; defaults to the ledger paths the runner echoes",
    )
    propose.add_argument(
        "--capability-path",
        action="append",
        default=None,
        help="narrow the producing capability slice; defaults to the slice on the comparison artifact",
    )
    propose.add_argument("--workstream-owner", required=True)
    propose.add_argument("--detection-artifact", required=True)
    propose.add_argument("--detected-at", help="ISO-8601 timestamp with offset; defaults to now")
    propose.add_argument("--bot-pr-url")
    propose.add_argument("--origin", choices=provisional.ORIGINS, default="automation")
    propose.add_argument("--output-dir", required=True)
    propose.set_defaults(handler=_propose)

    reconcile_parser = commands.add_parser("reconcile", help="reconcile a provisional record with the ledger")
    reconcile_parser.add_argument("--provisional", required=True)
    reconcile_parser.add_argument("--ledger-dir", required=True)
    reconcile_parser.add_argument("--pull-request-state", choices=("open", "merged", "closed"))
    reconcile_parser.add_argument("--comparison-status", choices=[status.value for status in comparator.Status])
    reconcile_parser.add_argument("--now", help="ISO-8601 timestamp with offset; defaults to now")
    reconcile_parser.add_argument("--output")
    reconcile_parser.set_defaults(handler=_reconcile)
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    handler = arguments.handler
    exit_code: int = handler(arguments)
    return exit_code
