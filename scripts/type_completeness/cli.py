"""Command-line entry point: ``python3 -m scripts.type_completeness <command>``."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

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


def _propose(arguments: argparse.Namespace) -> int:
    artifacts = comparator.deserialize_many(Path(arguments.comparison).read_text(encoding="utf-8"))
    matching = [artifact for artifact in artifacts if artifact.case_id == arguments.case_id]
    if not matching:
        print(f"comparison has no artifact for case {arguments.case_id!r}", file=sys.stderr)
        return 2
    artifact = matching[0]
    findings = [
        finding for finding in artifact.branch.get("findings") or [] if finding.get("dimension") == arguments.dimension
    ]
    if len(findings) != 1 or not findings[0].get("finding_id"):
        print(
            f"comparison artifact for case {arguments.case_id!r} has no finding for dimension {arguments.dimension!r}",
            file=sys.stderr,
        )
        return 2
    if artifact.capability_slice is None:
        if not arguments.capability_path:
            print(
                "comparison artifact carries no capability slice; pass --capability-path explicitly",
                file=sys.stderr,
            )
            return 2
        capability_paths = list(arguments.capability_path)
    else:
        producing = artifact.capability_slice.paths
        capability_paths = list(arguments.capability_path or producing)
        outside = sorted(set(capability_paths) - set(producing))
        if outside:
            print(
                f"--capability-path {outside} lies outside the producing capability slice {list(producing)}",
                file=sys.stderr,
            )
            return 2
    # The runner echoes the ledger fields of a finding it already knows; a command-line value wins, an echoed
    # value is kept, and only a genuinely new finding needs every field supplied.
    echoed = findings[0]
    issue_url = arguments.issue_url or str(echoed.get("issue_url") or "")
    closure_packet_url = arguments.closure_packet_url or str(echoed.get("closure_packet_url") or "")
    permanent_test_paths = list(
        arguments.permanent_test_path or [str(path) for path in echoed.get("permanent_test_paths") or []]
    )
    classification = str(echoed.get("classification") or "unclassified")
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
        print(f"finding is not in the ledger yet; {', '.join(missing)} must be supplied", file=sys.stderr)
        return 2
    payload = ledger.proposed_record(
        finding_id=str(echoed["finding_id"]),
        family=artifact.family,
        case_id=artifact.case_id,
        dimension=arguments.dimension,
        issue_url=issue_url,
        closure_packet_url=closure_packet_url,
        permanent_test_paths=permanent_test_paths,
        classification=classification,
    )
    detected_at = (
        deadline.parse_timestamp(arguments.detected_at) if arguments.detected_at else datetime.now(timezone.utc)
    )
    record = provisional.ProvisionalRecord.create(
        finding_id=payload["finding_id"],
        payload=payload,
        capability_slice=capability_paths,
        workstream_owner=arguments.workstream_owner,
        detection_artifact=arguments.detection_artifact,
        develop_comparison=artifact.to_dict(),
        detected_at=detected_at,
        bot_pr_url=arguments.bot_pr_url,
        origin=arguments.origin,
    )
    output_dir = Path(arguments.output_dir)
    ledger.write_record(output_dir, payload)
    _write_json(output_dir / "provisional.json", json.dumps(record.to_dict(), sort_keys=True, indent=2) + "\n")
    _write_json(output_dir / "tracking_issue_body.md", provisional.render_issue_body(record))
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
    propose.add_argument("--case-id", required=True)
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
