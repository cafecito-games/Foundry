"""Parser and validator for the strict streaming TAP13 profile used by `adapter run`.

The profile is deliberately narrow: an exact three-line preamble, contiguous point
numbering, and one explicit YAML diagnostic block per point. A general TAP parser
would accept output this protocol forbids, so the structure is parsed directly here
and only the diagnostic block body is handed to `yaml.safe_load`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any, Optional, Sequence

import yaml

from .diagnostics import (
    ARTIFACT_ENCODING,
    ARTIFACT_MISSING,
    ARTIFACT_READ,
    CANCELLED,
    CONFORMING,
    INFRASTRUCTURE_FAILURE,
    INVALID,
    PROTOCOL_NAME,
    PROTOCOL_VERSION,
    REPORT_ADAPTER_VERSION,
    REPORT_BAILOUT,
    REPORT_CANCELLATION,
    REPORT_DIRECTIVE,
    REPORT_DUPLICATE_ID,
    REPORT_EXIT,
    REPORT_HEADER,
    REPORT_INCOMPLETE,
    REPORT_LINE_ENDING,
    REPORT_LOCATION,
    REPORT_METADATA,
    REPORT_ORDER,
    REPORT_PLAN,
    REPORT_POINT,
    REPORT_SELECTION,
    REPORT_SKIP,
    REPORT_STATUS,
    REPORT_YAML,
    TEST_FAILURES,
    ValidationResult,
    Violation,
    make_result,
)
from .json_artifacts import DiscoveryModel, is_canonical_resource_path, read_artifact
from .selection import SelectionError, build_leaf_plan

TAP_VERSION_LINE = "TAP version 13"
ADAPTER_COMMENT_LINE = "# {}: {}".format(PROTOCOL_NAME, PROTOCOL_VERSION)
BLOCK_OPEN = "  ---"
BLOCK_CLOSE = "  ..."
BAIL_OUT_PREFIX = "Bail out!"

STATUS_DETAILS = ("", "discovery_error", "runtime_error", "timed_out", "aborted", "setup_error")

# Plan and point counts are bounded so a pathological digit run is a structural
# violation rather than an `int()` conversion failure: CPython refuses to convert
# strings longer than its integer-string limit.
_PLAN_PATTERN = re.compile(r"^1\.\.(\d{1,9})$")
_POINT_PATTERN = re.compile(r"^(?P<status>ok|not ok) (?P<number>\d{1,9}) - (?P<rest>.*)$")
_SKIP_PATTERN = re.compile(r"^SKIP (?P<reason>.+)$")
_CONTROL_PATTERN = re.compile("[\\x00-\\x1f\\x7f]")

_INCOMPLETENESS_CODES = frozenset({REPORT_INCOMPLETE, REPORT_LINE_ENDING})


@dataclass(frozen=True)
class SourceLocation:
    file_name: str
    line_number: int
    column_number: int


@dataclass(frozen=True)
class TapPoint:
    number: int
    ok: bool
    label: str
    skip_reason: Optional[str]
    test_id: Optional[str]
    duration_ms: Optional[int]
    status_detail: Optional[str]
    message: Optional[str]
    location: Optional[SourceLocation]
    line: int

    @property
    def skipped(self) -> bool:
        return self.skip_reason is not None


def _has_control_characters(value: str) -> bool:
    return _CONTROL_PATTERN.search(value) is not None


def _split_lines(text: str) -> list[str]:
    if not text:
        return []
    lines = text.split("\n")
    if text.endswith("\n"):
        lines.pop()
    return lines


def _retain_cancellation_prefix(text: str) -> str:
    """Keeps only the longest completed protocol prefix of a cancelled report.

    Bytes after the final LF were never guaranteed flushed, and a trailing point
    whose diagnostic block has no closing marker was not a completed point.
    """

    last_newline = text.rfind("\n")
    retained = "" if last_newline < 0 else text[: last_newline + 1]
    lines = _split_lines(retained)
    index = 0
    # Preamble lines are complete units on their own.
    while index < len(lines) and index < 3:
        index += 1
    keep = index
    while index < len(lines):
        if _POINT_PATTERN.match(lines[index]) is None:
            # Completed non-point content is retained and validated normally.
            keep = len(lines)
            break
        cursor = index + 1
        closed = -1
        while cursor < len(lines):
            if lines[cursor] == BLOCK_CLOSE:
                closed = cursor
                break
            cursor += 1
        if closed < 0:
            break
        index = closed + 1
        keep = index
    if keep >= len(lines):
        return retained
    return "".join(line + "\n" for line in lines[:keep])


def validate_report(
    path: str,
    discovery: Optional[DiscoveryModel] = None,
    selections: Sequence[str] = (),
    process_exit: Optional[int] = None,
    cancelled: bool = False,
    suppress_selection: bool = False,
    context_invalid: bool = False,
    extra_violations: Sequence[Violation] = (),
) -> ValidationResult:
    """Validates one execution report against the v1 strict TAP13 profile."""

    text, failure = read_artifact(path)
    context: list[Violation] = list(extra_violations)

    if failure is not None:
        if failure == "missing" and cancelled:
            return make_result("report", not context, False, _context_classification(context, CANCELLED), context)
        if failure == "missing":
            context.append(Violation(ARTIFACT_MISSING, "The artifact does not exist", path))
            return make_result("report", False, False, INFRASTRUCTURE_FAILURE, context)
        if failure == "encoding":
            context.append(Violation(ARTIFACT_ENCODING, "The artifact is not BOM-free UTF-8", path))
            return make_result("report", False, False, INFRASTRUCTURE_FAILURE, context)
        context.append(Violation(ARTIFACT_READ, "The artifact could not be read", path))
        return make_result("report", False, False, INVALID, context)

    if cancelled:
        return _validate_cancelled(path, text, process_exit, context)

    violations: list[Violation] = []
    parsed = _parse_report(path, text, violations)
    _check_lifecycle(path, parsed, violations)
    if discovery is not None and not suppress_selection:
        # Cross-artifact checks need a successfully parsed plan and point set; running
        # them over a structurally broken report would cascade a second diagnostic out
        # of one defect.
        if not violations:
            _check_selection(path, parsed, discovery, selections, violations)
    elif context_invalid:
        violations.append(
            Violation(
                REPORT_SELECTION, "Selection could not be checked against a nonconforming discovery context", path
            )
        )

    if not violations and process_exit is not None:
        expected = _expected_exit(parsed)
        if expected is not None and process_exit != expected:
            violations.append(
                Violation(
                    REPORT_EXIT,
                    "A report in this state exits {}, not {}".format(expected, process_exit),
                    path,
                )
            )

    complete = parsed.complete
    all_violations = violations + context
    if not all_violations:
        if parsed.bailed_out:
            return make_result("report", True, complete, INFRASTRUCTURE_FAILURE, all_violations)
        classification = TEST_FAILURES if any(not point.ok for point in parsed.points) else CONFORMING
        return make_result("report", True, complete, classification, all_violations)

    codes = {violation.code for violation in all_violations}
    if codes <= _INCOMPLETENESS_CODES and not complete:
        classification = INFRASTRUCTURE_FAILURE
    else:
        classification = INVALID
    return make_result("report", False, complete, classification, all_violations)


def _context_classification(context: Sequence[Violation], default: str) -> str:
    return INVALID if context else default


def _expected_exit(parsed: ParsedReport) -> Optional[int]:
    if parsed.bailed_out:
        return 2
    if not parsed.complete:
        return None
    return 0 if all(point.ok for point in parsed.points) else 1


@dataclass
class ParsedReport:
    plan: Optional[int]
    points: list[TapPoint]
    bailed_out: bool
    line_ending_ok: bool
    stopped_early: bool

    @property
    def complete(self) -> bool:
        """True when a valid leading plan is satisfied by complete point blocks.

        Completeness is independent of validity: a satisfied plan followed by
        forbidden trailing content is still a complete report.
        """

        return (
            self.plan is not None and self.line_ending_ok and not self.stopped_early and len(self.points) >= self.plan
        )


def _parse_report(path: str, text: str, violations: list[Violation]) -> ParsedReport:
    line_ending_ok = True
    if text:
        if "\r\n" in text:
            line_ending_ok = False
            violations.append(Violation(REPORT_LINE_ENDING, "The report uses CRLF line endings; v1 requires LF", path))
        elif not text.endswith("\n"):
            line_ending_ok = False
            violations.append(Violation(REPORT_LINE_ENDING, "The report does not end with a terminal LF", path))
    if not line_ending_ok:
        violations.append(Violation(REPORT_INCOMPLETE, "The report is not a completely flushed artifact", path))

    lines = [line[:-1] if line.endswith("\r") else line for line in _split_lines(text)]

    if not lines or lines[0] != TAP_VERSION_LINE:
        violations.append(Violation(REPORT_HEADER, "Line 1 must be '{}'".format(TAP_VERSION_LINE), path, 1))
        return ParsedReport(None, [], False, line_ending_ok, True)
    if len(lines) < 2 or lines[1] != ADAPTER_COMMENT_LINE:
        violations.append(
            Violation(REPORT_ADAPTER_VERSION, "Line 2 must be '{}'".format(ADAPTER_COMMENT_LINE), path, 2)
        )
        return ParsedReport(None, [], False, line_ending_ok, True)

    if len(lines) < 3:
        violations.append(Violation(REPORT_PLAN, "The report declares no TAP plan", path, 3))
        violations.append(Violation(REPORT_INCOMPLETE, "The report stops before its plan", path))
        return ParsedReport(None, [], False, line_ending_ok, True)

    if lines[2].startswith(BAIL_OUT_PREFIX):
        report = ParsedReport(None, [], True, line_ending_ok, False)
        _check_bail_out(path, lines, 2, report, violations)
        return report

    plan_match = _PLAN_PATTERN.match(lines[2])
    if plan_match is None:
        violations.append(Violation(REPORT_PLAN, "Line 3 must be a leading '1..N' plan", path, 3))
        violations.append(Violation(REPORT_INCOMPLETE, "The report declares no usable plan", path))
        return ParsedReport(None, [], False, line_ending_ok, True)

    report = ParsedReport(int(plan_match.group(1)), [], False, line_ending_ok, False)
    index = 3
    while index < len(lines):
        line = lines[index]
        if line.startswith(BAIL_OUT_PREFIX):
            report.bailed_out = True
            _check_bail_out(path, lines, index, report, violations)
            return report
        point_match = _POINT_PATTERN.match(line)
        if point_match is None:
            violations.append(Violation(REPORT_POINT, "Line is not a conforming TAP test point", path, index + 1))
            index += 1
            continue
        index = _read_point(path, lines, index, point_match, report, violations)
    return report


def _check_bail_out(
    path: str, lines: Sequence[str], index: int, report: ParsedReport, violations: list[Violation]
) -> None:
    message = lines[index][len(BAIL_OUT_PREFIX) :].strip()
    if not message:
        violations.append(Violation(REPORT_BAILOUT, "'Bail out!' requires a message", path, index + 1))
    if report.plan is not None and len(report.points) >= report.plan:
        violations.append(
            Violation(REPORT_BAILOUT, "A bailout after the plan is satisfied is trailing content", path, index + 1)
        )
    if index + 1 < len(lines):
        violations.append(Violation(REPORT_BAILOUT, "No output is allowed after 'Bail out!'", path, index + 2))


def _read_point(
    path: str,
    lines: Sequence[str],
    index: int,
    match: re.Match[str],
    report: ParsedReport,
    violations: list[Violation],
) -> int:
    line_number = index + 1
    ok = match.group("status") == "ok"
    number = int(match.group("number"))
    rest = match.group("rest")

    label = rest
    skip_reason: Optional[str] = None
    if " # " in rest:
        label, _, directive = rest.partition(" # ")
        skip_match = _SKIP_PATTERN.match(directive)
        if skip_match is None:
            violations.append(
                Violation(REPORT_DIRECTIVE, "v1 allows only a '# SKIP <reason>' directive", path, line_number)
            )
        else:
            skip_reason = skip_match.group("reason")
            if not ok:
                violations.append(Violation(REPORT_STATUS, "A skipped point must use 'ok'", path, line_number))
    if not label or label != label.strip() or "#" in label or _has_control_characters(label):
        violations.append(
            Violation(REPORT_POINT, "A point label must be non-empty single-line text without '#'", path, line_number)
        )

    expected_number = len(report.points) + 1
    if number != expected_number:
        violations.append(
            Violation(
                REPORT_POINT,
                "Expected test point {}, found {}".format(expected_number, number),
                path,
                line_number,
            )
        )
    if report.plan is not None and expected_number > report.plan:
        violations.append(
            Violation(
                REPORT_POINT,
                "The plan declares {} points but the report emitted more".format(report.plan),
                path,
                line_number,
            )
        )

    block, next_index, terminated = _collect_block(lines, index + 1)
    if block is None:
        violations.append(
            Violation(REPORT_YAML, "Every point requires an immediately following YAML block", path, line_number)
        )
        return next_index
    if not terminated:
        violations.append(
            Violation(REPORT_YAML, "The YAML block is not terminated by '{}'".format(BLOCK_CLOSE), path, line_number)
        )
        return next_index

    document = _load_block(path, block, line_number, violations)
    point = _build_point(path, number, ok, label, skip_reason, document, line_number, violations)
    report.points.append(point)
    return next_index


def _collect_block(lines: Sequence[str], index: int) -> tuple[Optional[list[str]], int, bool]:
    if index >= len(lines) or lines[index] != BLOCK_OPEN:
        return (None, index, True)
    body: list[str] = []
    cursor = index + 1
    while cursor < len(lines):
        if lines[cursor] == BLOCK_CLOSE:
            return (body, cursor + 1, True)
        body.append(lines[cursor])
        cursor += 1
    return (body, cursor, False)


def _load_block(
    path: str, block: Sequence[str], line_number: int, violations: list[Violation]
) -> Optional[dict[str, Any]]:
    unindented: list[str] = []
    for line in block:
        if not line.strip():
            unindented.append("")
            continue
        if not line.startswith("  "):
            violations.append(
                Violation(REPORT_YAML, "Diagnostic block lines use two-space TAP indentation", path, line_number)
            )
            return None
        unindented.append(line[2:])
    try:
        document = yaml.safe_load("\n".join(unindented))
    except Exception as error:
        # Any loader failure is a conformance violation. The catch is deliberately broad
        # because PyYAML's scalar constructors raise plain ValueError and AttributeError
        # for several standard tags, and an adversarial artifact must never crash the
        # validator instead of producing a diagnostic.
        violations.append(
            Violation(REPORT_YAML, "Malformed YAML diagnostic block: {}".format(error), path, line_number)
        )
        return None
    if not isinstance(document, dict):
        violations.append(Violation(REPORT_YAML, "A diagnostic block must be a YAML mapping", path, line_number))
        return None
    return document


def _build_point(
    path: str,
    number: int,
    ok: bool,
    label: str,
    skip_reason: Optional[str],
    document: Optional[dict[str, Any]],
    line_number: int,
    violations: list[Violation],
) -> TapPoint:
    if document is None:
        return TapPoint(number, ok, label, skip_reason, None, None, None, None, None, line_number)

    metadata = document.get("_foundry")
    test_id: Optional[str] = None
    duration_ms: Optional[int] = None
    status_detail: Optional[str] = None
    if not isinstance(metadata, dict):
        violations.append(
            Violation(REPORT_METADATA, "A diagnostic block requires a '_foundry' mapping", path, line_number)
        )
    else:
        value = metadata.get("id")
        if not isinstance(value, str) or not value or _has_control_characters(value):
            violations.append(
                Violation(
                    REPORT_METADATA,
                    "'_foundry.id' must be a non-empty string without control characters",
                    path,
                    line_number,
                )
            )
        else:
            test_id = value

        duration = metadata.get("duration_ms")
        if not isinstance(duration, int) or isinstance(duration, bool) or duration < 0:
            violations.append(
                Violation(REPORT_METADATA, "'_foundry.duration_ms' must be a non-negative integer", path, line_number)
            )
        else:
            duration_ms = duration

        detail = metadata.get("status_detail")
        if not isinstance(detail, str) or detail not in STATUS_DETAILS:
            violations.append(
                Violation(REPORT_METADATA, "'_foundry.status_detail' is outside the v1 closed set", path, line_number)
            )
        else:
            status_detail = detail

    message = document.get("message")
    if not ok:
        if not isinstance(message, str) or not message:
            violations.append(
                Violation(REPORT_METADATA, "A failing point requires a non-empty 'message'", path, line_number)
            )
            message = None
    elif message is not None and not isinstance(message, str):
        violations.append(Violation(REPORT_METADATA, "'message' must be a string", path, line_number))
        message = None

    if status_detail is not None and status_detail and ok:
        violations.append(
            Violation(REPORT_STATUS, "A non-empty 'status_detail' requires a 'not ok' point", path, line_number)
        )

    location = _read_location(path, document, line_number, violations)
    return TapPoint(
        number,
        ok,
        label,
        skip_reason,
        test_id,
        duration_ms,
        status_detail,
        message if isinstance(message, str) else None,
        location,
        line_number,
    )


def _read_location(
    path: str, document: dict[str, Any], line_number: int, violations: list[Violation]
) -> Optional[SourceLocation]:
    if "at" not in document:
        return None
    value = document["at"]
    if not isinstance(value, dict):
        violations.append(Violation(REPORT_LOCATION, "'at' must be a mapping", path, line_number))
        return None
    file_name = value.get("fileName")
    if not isinstance(file_name, str) or not is_canonical_resource_path(file_name):
        violations.append(
            Violation(REPORT_LOCATION, "'at.fileName' must be a canonical res:// path", path, line_number)
        )
        return None
    positions: list[int] = []
    for field_name in ("lineNumber", "columnNumber"):
        entry = value.get(field_name)
        if not isinstance(entry, int) or isinstance(entry, bool) or entry < 1:
            violations.append(
                Violation(
                    REPORT_LOCATION,
                    "'at.{}' is one-based and must be a positive integer".format(field_name),
                    path,
                    line_number,
                )
            )
            return None
        positions.append(entry)
    return SourceLocation(file_name, positions[0], positions[1])


def _check_lifecycle(path: str, parsed: ParsedReport, violations: list[Violation]) -> None:
    seen: dict[str, int] = {}
    for point in parsed.points:
        if point.test_id is None:
            continue
        if point.test_id in seen:
            violations.append(
                Violation(
                    REPORT_DUPLICATE_ID,
                    "'_foundry.id' '{}' was already used by point {}".format(point.test_id, seen[point.test_id]),
                    path,
                    point.line,
                )
            )
        else:
            seen[point.test_id] = point.number

    if parsed.plan is None or parsed.bailed_out:
        return
    if len(parsed.points) < parsed.plan:
        violations.append(
            Violation(
                REPORT_INCOMPLETE,
                "The plan declares {} points but the report emitted {}".format(parsed.plan, len(parsed.points)),
                path,
            )
        )


def _check_selection(
    path: str,
    parsed: ParsedReport,
    discovery: DiscoveryModel,
    selections: Sequence[str],
    violations: list[Violation],
) -> None:
    if not discovery.conforming or not discovery.complete:
        violations.append(
            Violation(
                REPORT_SELECTION, "Selection could not be checked against a nonconforming discovery context", path
            )
        )
        return
    try:
        plan = build_leaf_plan(discovery, selections)
    except SelectionError as error:
        violations.append(Violation(REPORT_SELECTION, str(error), path))
        return

    expected_ids = [leaf.id for leaf in plan]
    if parsed.plan is not None and parsed.plan != len(expected_ids):
        violations.append(
            Violation(
                REPORT_SELECTION,
                "The selection plans {} runnable leaves but the report declares {}".format(
                    len(expected_ids), parsed.plan
                ),
                path,
                3,
            )
        )
        return

    reported_ids = [point.test_id for point in parsed.points]
    if None in reported_ids:
        return
    if not parsed.complete:
        # An interrupted or bailed-out report can only be required to be a prefix
        # of the plan; the missing points were never emitted.
        if reported_ids != expected_ids[: len(reported_ids)]:
            violations.append(
                Violation(REPORT_SELECTION, "The emitted points are not a prefix of the planned leaves", path)
            )
        return
    if sorted(str(value) for value in reported_ids) != sorted(expected_ids):
        violations.append(
            Violation(REPORT_SELECTION, "The report points do not match the selected runnable leaves", path)
        )
        return
    if reported_ids != expected_ids:
        violations.append(Violation(REPORT_ORDER, "Report points must follow planned discovery order", path))
        return

    by_id = discovery.by_id()
    for point in parsed.points:
        leaf = by_id[str(point.test_id)]
        if leaf.skipped and not point.skipped:
            violations.append(
                Violation(
                    REPORT_SKIP, "Discovered skipped leaf '{}' requires a SKIP point".format(leaf.id), path, point.line
                )
            )
        elif point.skipped and not leaf.skipped:
            violations.append(
                Violation(REPORT_SKIP, "Leaf '{}' was not discovered as skipped".format(leaf.id), path, point.line)
            )
        elif point.skipped and point.skip_reason != leaf.skip_reason:
            violations.append(
                Violation(REPORT_SKIP, "The SKIP reason must equal the discovered 'skip_reason'", path, point.line)
            )


def _validate_cancelled(
    path: str, text: str, process_exit: Optional[int], context: list[Violation]
) -> ValidationResult:
    violations: list[Violation] = []
    if process_exit is not None:
        violations.append(Violation(REPORT_CANCELLATION, "A cancelled run has no portable child exit code", path))

    retained = _retain_cancellation_prefix(text)
    lines = _split_lines(retained)
    if not lines:
        return _cancellation_result(violations, context)
    if lines == [TAP_VERSION_LINE] or lines == [TAP_VERSION_LINE, ADAPTER_COMMENT_LINE]:
        return _cancellation_result(violations, context)

    structural: list[Violation] = []
    parsed = _parse_report(path, retained, structural)
    cancellation_failure = bool(structural)
    if parsed.bailed_out:
        violations.append(Violation(REPORT_CANCELLATION, "A bailout is not a cancelled run", path))
    elif parsed.plan is None:
        cancellation_failure = True
    elif len(parsed.points) >= parsed.plan:
        violations.append(Violation(REPORT_CANCELLATION, "A satisfied plan is not a cancelled run", path))
    if cancellation_failure:
        violations.append(
            Violation(REPORT_CANCELLATION, "The retained prefix contains a completed malformed unit", path)
        )
    violations.extend(structural)
    return _cancellation_result(violations, context)


def _cancellation_result(violations: Sequence[Violation], context: Sequence[Violation]) -> ValidationResult:
    combined = list(violations) + list(context)
    if not combined:
        return make_result("report", True, False, CANCELLED, combined)
    return make_result("report", False, False, INVALID, combined)
