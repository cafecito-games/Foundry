"""Command-line entry point for the Foundry Test Adapter Protocol validator."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional, Sequence

from .capabilities import validate_capabilities_document
from .discovery import DiscoveryResult, validate_discovery_stream
from .paths import PROTOCOL_VERSION
from .run import validate_run
from .tap import TapReport, validate_tap_report
from .violations import Violation

EXIT_CONFORMS = 0
EXIT_VIOLATIONS = 1
EXIT_USAGE = 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="foundry-test-adapter-validate",
        description=(
            "Validates Foundry Test Adapter Protocol v{} artifacts produced by any test runner. "
            "Reports every violation found rather than stopping at the first one.".format(PROTOCOL_VERSION)
        ),
    )
    parser.add_argument("--capabilities", metavar="FILE", help="capabilities JSON document to validate")
    parser.add_argument("--discovery", metavar="FILE", help="discovery JSONL stream to validate")
    parser.add_argument("--report", metavar="FILE", help="TAP13 execution report to validate")
    parser.add_argument(
        "--select",
        metavar="ID",
        action="append",
        default=[],
        help="stable identifier passed to the runner; repeatable, and correlated against the report",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable results on stdout")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    if not (arguments.capabilities or arguments.discovery or arguments.report):
        parser.error("at least one of --capabilities, --discovery, or --report is required")

    results: dict[str, Any] = {"protocol_version": PROTOCOL_VERSION, "artifacts": {}}
    violations: list[Violation] = []
    discovery_result: Optional[DiscoveryResult] = None
    report_result: Optional[TapReport] = None

    if arguments.capabilities:
        text = _read(arguments.capabilities)
        if text is None:
            return EXIT_USAGE
        capabilities = validate_capabilities_document(text)
        violations.extend(capabilities.violations)
        results["artifacts"]["capabilities"] = {
            "path": arguments.capabilities,
            "supported_versions": list(capabilities.supported_versions or ()),
            "framework_id": capabilities.framework_id,
            "violations": [violation.as_dict() for violation in capabilities.violations],
        }

    if arguments.discovery:
        text = _read(arguments.discovery)
        if text is None:
            return EXIT_USAGE
        discovery_result = validate_discovery_stream(text)
        violations.extend(discovery_result.violations)
        results["artifacts"]["discovery"] = {
            "path": arguments.discovery,
            "complete": discovery_result.complete,
            "suite_count": len(discovery_result.suites),
            "test_count": len(discovery_result.tests),
            "error_count": len(discovery_result.errors),
            "violations": [violation.as_dict() for violation in discovery_result.violations],
        }

    if arguments.report:
        text = _read(arguments.report)
        if text is None:
            return EXIT_USAGE
        report_result = validate_tap_report(text)
        violations.extend(report_result.violations)
        results["artifacts"]["report"] = {
            "path": arguments.report,
            "plan": report_result.plan,
            "point_count": len(report_result.points),
            "complete": report_result.complete,
            "bailed_out": report_result.bailed_out,
            "violations": [violation.as_dict() for violation in report_result.violations],
        }

    if discovery_result is not None and report_result is not None:
        correlation = validate_run(discovery_result, report_result, arguments.select)
        violations.extend(correlation)
        results["artifacts"]["run"] = {
            "selection": list(arguments.select),
            "violations": [violation.as_dict() for violation in correlation],
        }

    results["violation_count"] = len(violations)
    results["conforms"] = not violations

    if arguments.json:
        json.dump(results, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        for artifact, payload in results["artifacts"].items():
            for violation in payload["violations"]:
                location = " [{}]".format(violation["location"]) if violation["location"] else ""
                sys.stdout.write("{}: {}{}: {}\n".format(artifact, violation["code"], location, violation["message"]))
        if violations:
            sys.stdout.write("{} violation(s)\n".format(len(violations)))
        else:
            sys.stdout.write("conforms to Foundry Test Adapter Protocol v{}\n".format(PROTOCOL_VERSION))

    return EXIT_VIOLATIONS if violations else EXIT_CONFORMS


def _read(path: str) -> Optional[str]:
    try:
        return Path(path).read_text(encoding="utf-8")
    except OSError as error:
        sys.stderr.write("Cannot read '{}': {}\n".format(path, error))
        return None
    except UnicodeDecodeError as error:
        sys.stderr.write("'{}' is not valid UTF-8: {}\n".format(path, error))
        return None
