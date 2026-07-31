"""Stable command-line interface of the v1 conformance validator.

Only this command surface and the `--format json` result document are public
interfaces; the Python modules behind them are not.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional, Sequence

from .diagnostics import (
    ARTIFACT_ENCODING,
    ARTIFACT_MISSING,
    ARTIFACT_READ,
    CONFORMING,
    ValidationResult,
    Violation,
    render_json,
    render_text,
)
from .json_artifacts import DiscoveryModel, read_artifact, validate_capabilities, validate_discovery
from .tap13 import validate_report

EXIT_CONFORMING = 0
EXIT_VIOLATIONS = 1
EXIT_UNUSABLE = 2

_CONFORMING_CLASSIFICATIONS = frozenset({CONFORMING, "discovery_failures", "test_failures", "infrastructure_failure", "cancelled"})


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="foundry-test-adapter",
        description="Validate Foundry Test Adapter Protocol v1 artifacts.",
    )
    subparsers = parser.add_subparsers(dest="operation", required=True)

    def add_common(target: argparse.ArgumentParser) -> None:
        target.add_argument("artifact")
        target.add_argument("--exit-code", type=int, default=None)
        target.add_argument("--format", choices=("text", "json"), default="text")

    add_common(subparsers.add_parser("capabilities"))
    add_common(subparsers.add_parser("discovery"))

    report = subparsers.add_parser("report")
    report.add_argument("artifact")
    report.add_argument("--discovery", default=None)
    report.add_argument("--select", action="append", default=[], dest="select")
    report.add_argument("--exit-code", type=int, default=None)
    report.add_argument("--cancelled", action="store_true")
    report.add_argument("--format", choices=("text", "json"), default="text")

    fixtures = subparsers.add_parser("fixtures")
    fixtures.add_argument("manifest")
    return parser


def _extract_opaque_selections(argv: Sequence[str]) -> tuple[list[str], list[str]]:
    """Removes `--select <value>` pairs before argparse can reinterpret them.

    A selection ID is opaque: it may be spelled `--`, `-x`, or anything else that
    an option parser would otherwise treat as syntax.
    """

    remaining: list[str] = []
    selections: list[str] = []
    index = 0
    while index < len(argv):
        argument = argv[index]
        if argument == "--select":
            if index + 1 >= len(argv):
                remaining.append(argument)
                index += 1
                continue
            selections.append(argv[index + 1])
            index += 2
            continue
        if argument.startswith("--select="):
            selections.append(argument[len("--select=") :])
            index += 1
            continue
        remaining.append(argument)
        index += 1
    return (remaining, selections)


def _load_discovery_context(
    path: str,
) -> tuple[Optional[DiscoveryModel], list[Violation], bool, bool, int]:
    """Loads the optional discovery context for a report validation.

    Returns the model when usable, context violations to merge into the report
    result, whether selection checks must be suppressed, whether the context was
    readable but nonconforming, and the minimum validator exit it forces.
    """

    text, failure = read_artifact(path)
    if failure == "missing":
        return (None, [Violation(ARTIFACT_MISSING, "The discovery context does not exist", path)], True, False, EXIT_VIOLATIONS)
    if failure == "encoding":
        return (
            None,
            [Violation(ARTIFACT_ENCODING, "The discovery context is not BOM-free UTF-8", path)],
            True,
            False,
            EXIT_VIOLATIONS,
        )
    if failure == "read":
        return (None, [Violation(ARTIFACT_READ, "The discovery context could not be read", path)], True, False, EXIT_UNUSABLE)
    assert text is not None
    result, model = validate_discovery(path)
    if model is None or not result.valid or not result.complete:
        return (None, [], True, True, EXIT_VIOLATIONS)
    return (model, [], False, False, EXIT_CONFORMING)


def _emit(result: ValidationResult, output_format: str, stdout: Any, stderr: Any) -> None:
    if output_format == "json":
        stdout.write(render_json(result) + "\n")
        return
    stderr.write(render_text(result) + "\n")


def _result_exit(result: ValidationResult, floor: int = EXIT_CONFORMING) -> int:
    if result.valid and result.classification in _CONFORMING_CLASSIFICATIONS:
        return max(EXIT_CONFORMING, floor)
    if any(violation.code == ARTIFACT_READ for violation in result.violations):
        return EXIT_UNUSABLE
    return max(EXIT_VIOLATIONS, floor)


def _run_fixtures(manifest_path: str, stdout: Any, stderr: Any) -> int:
    try:
        manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    except OSError as error:
        stderr.write("Cannot read the fixture manifest: {}\n".format(error))
        return EXIT_UNUSABLE
    except ValueError as error:
        stderr.write("Malformed fixture manifest: {}\n".format(error))
        return EXIT_UNUSABLE

    base = Path(manifest_path).resolve().parent
    failures = 0
    entries = manifest.get("fixtures", [])
    for entry in entries:
        expected = entry["expected"]
        artifact = str(base / entry["artifact"])
        discovery_path = entry.get("discovery")
        selections = entry.get("selections", [])
        process_exit = entry.get("exit_code")
        cancelled = bool(entry.get("cancelled", False))
        operation = entry["operation"]

        if operation == "capabilities":
            result = validate_capabilities(artifact, process_exit)
            exit_code = _result_exit(result)
        elif operation == "discovery":
            result, _ = validate_discovery(artifact, process_exit)
            exit_code = _result_exit(result)
        else:
            model = None
            context: Sequence[Violation] = ()
            suppress = False
            context_invalid = False
            floor = EXIT_CONFORMING
            if discovery_path is not None:
                model, context, suppress, context_invalid, floor = _load_discovery_context(str(base / discovery_path))
            result = validate_report(
                artifact,
                discovery=model,
                selections=selections,
                process_exit=process_exit,
                cancelled=cancelled,
                suppress_selection=suppress,
                context_invalid=context_invalid,
                extra_violations=context,
            )
            exit_code = _result_exit(result, floor)

        mismatches = []
        if exit_code != expected["validator_exit"]:
            mismatches.append("validator_exit {} != {}".format(exit_code, expected["validator_exit"]))
        if result.valid != expected["valid"]:
            mismatches.append("valid {} != {}".format(result.valid, expected["valid"]))
        if result.complete != expected["complete"]:
            mismatches.append("complete {} != {}".format(result.complete, expected["complete"]))
        if result.classification != expected["classification"]:
            mismatches.append("classification {} != {}".format(result.classification, expected["classification"]))
        actual_codes = sorted(set(result.codes))
        if actual_codes != sorted(set(expected["codes"])):
            mismatches.append("codes {} != {}".format(actual_codes, sorted(set(expected["codes"]))))
        if mismatches:
            failures += 1
            stderr.write("{}: {}\n".format(entry["id"], "; ".join(mismatches)))

    stdout.write("{} fixture(s) checked, {} mismatch(es)\n".format(len(entries), failures))
    return EXIT_CONFORMING if failures == 0 else EXIT_VIOLATIONS


def main(argv: Optional[Sequence[str]] = None, stdout: Any = None, stderr: Any = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    stdout = sys.stdout if stdout is None else stdout
    stderr = sys.stderr if stderr is None else stderr

    argv, selections = _extract_opaque_selections(argv)
    parser = _build_parser()
    try:
        arguments = parser.parse_args(argv)
    except SystemExit:
        # Invalid syntax never establishes an artifact operation, so no result
        # document is produced even when `--format json` was requested.
        return EXIT_UNUSABLE

    if arguments.operation == "fixtures":
        return _run_fixtures(arguments.manifest, stdout, stderr)

    if selections and (arguments.operation != "report" or arguments.discovery is None):
        parser.print_usage(stderr)
        stderr.write("foundry-test-adapter: --select requires the report operation with --discovery\n")
        return EXIT_UNUSABLE

    if arguments.operation == "capabilities":
        result = validate_capabilities(arguments.artifact, arguments.exit_code)
        _emit(result, arguments.format, stdout, stderr)
        return _result_exit(result)

    if arguments.operation == "discovery":
        result, _ = validate_discovery(arguments.artifact, arguments.exit_code)
        _emit(result, arguments.format, stdout, stderr)
        return _result_exit(result)

    if arguments.cancelled and arguments.exit_code is not None:
        parser.print_usage(stderr)
        stderr.write("foundry-test-adapter: --cancelled and --exit-code are mutually exclusive\n")
        return EXIT_UNUSABLE

    model = None
    context: Sequence[Violation] = ()
    suppress = False
    context_invalid = False
    floor = EXIT_CONFORMING
    if arguments.discovery is not None:
        model, context, suppress, context_invalid, floor = _load_discovery_context(arguments.discovery)
    result = validate_report(
        arguments.artifact,
        discovery=model,
        selections=selections,
        process_exit=arguments.exit_code,
        cancelled=arguments.cancelled,
        suppress_selection=suppress,
        context_invalid=context_invalid,
        extra_violations=context,
    )
    _emit(result, arguments.format, stdout, stderr)
    return _result_exit(result, floor)


if __name__ == "__main__":
    raise SystemExit(main())
