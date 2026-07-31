"""Validator for the streaming TAP13 report written by `adapter run`."""

from __future__ import annotations

import json
import re
from typing import Any, Optional

from .paths import PROTOCOL_NAME, PROTOCOL_VERSION
from .violations import Violation, ViolationCode

TAP_VERSION_LINE = "TAP version 13"
PROTOCOL_COMMENT_LINE = "# {}: {}".format(PROTOCOL_NAME, PROTOCOL_VERSION)

STATUS_DETAILS = ("", "discovery_error", "runtime_error", "timed_out", "aborted", "setup_error")

_PLAN_PATTERN = re.compile(r"^1\.\.(\d+)$")
_POINT_PATTERN = re.compile(
    r"^(?P<status>not ok|ok) (?P<number>\d+) - (?P<description>[^#]*?)(?: # (?P<directive>.*))?$"
)
_DIRECTIVE_PATTERN = re.compile(r"^(?P<name>SKIP|TODO)(?: (?P<reason>.*))?$")
_INTEGER_PATTERN = re.compile(r"^-?\d+$")

_BLOCK_INDENT = "  "
_CONTENT_INDENT = 2


class SourceLocation:
    __slots__ = ("file_name", "line_number", "column_number")

    def __init__(self, file_name: str, line_number: int, column_number: int) -> None:
        self.file_name = file_name
        self.line_number = line_number
        self.column_number = column_number


class TapPoint:
    """One TAP test point with its decoded `_foundry` diagnostic metadata."""

    __slots__ = (
        "number",
        "ok",
        "description",
        "skipped",
        "skip_reason",
        "id",
        "duration_ms",
        "status_detail",
        "message",
        "at",
    )

    def __init__(
        self,
        number: int,
        ok: bool,
        description: str,
        skipped: bool,
        skip_reason: Optional[str],
        identifier: Optional[str],
        duration_ms: Optional[int],
        status_detail: Optional[str],
        message: Optional[str],
        at: Optional[SourceLocation],
    ) -> None:
        self.number = number
        self.ok = ok
        self.description = description
        self.skipped = skipped
        self.skip_reason = skip_reason
        self.id = identifier
        self.duration_ms = duration_ms
        self.status_detail = status_detail
        self.message = message
        self.at = at


class TapReport:
    """Parsed report plus every violation found in it.

    `complete` is true only when a plan was declared, every planned point was
    emitted, and the adapter did not bail out. A cancelled or crashed run
    produces a report whose flushed points still parse while `complete` is
    false.
    """

    __slots__ = ("violations", "plan", "points", "bailed_out", "bail_message")

    def __init__(
        self,
        violations: list[Violation],
        plan: Optional[int],
        points: list[TapPoint],
        bailed_out: bool,
        bail_message: Optional[str],
    ) -> None:
        self.violations = violations
        self.plan = plan
        self.points = points
        self.bailed_out = bailed_out
        self.bail_message = bail_message

    @property
    def conforms(self) -> bool:
        return not self.violations

    @property
    def complete(self) -> bool:
        return self.plan is not None and not self.bailed_out and len(self.points) == self.plan

    @property
    def passed(self) -> bool:
        """True when the report is complete and every point is ok or skipped."""

        return self.complete and all(point.ok for point in self.points)

    def point_ids(self) -> list[str]:
        return [point.id for point in self.points if point.id is not None]


def validate_tap_report(text: str) -> TapReport:
    """Validates a complete, truncated, or bailed-out TAP13 adapter report."""

    violations: list[Violation] = []
    points: list[TapPoint] = []
    plan: Optional[int] = None
    plan_line: Optional[int] = None
    bailed_out = False
    bail_message: Optional[str] = None
    reported_content_after_bail_out = False

    lines = text.splitlines()
    numbered = [(index + 1, line) for index, line in enumerate(lines)]
    significant = [(number, line) for number, line in numbered if line.strip()]

    if not significant or significant[0][1] != TAP_VERSION_LINE:
        violations.append(
            Violation(ViolationCode.MISSING_TAP_VERSION, "Report must start with '{}'".format(TAP_VERSION_LINE))
        )
    if len(significant) < 2 or significant[1][1] != PROTOCOL_COMMENT_LINE:
        violations.append(
            Violation(
                ViolationCode.MISSING_PROTOCOL_COMMENT,
                "The second line must be '{}'".format(PROTOCOL_COMMENT_LINE),
            )
        )

    index = 0
    while index < len(numbered):
        line_number, line = numbered[index]
        index += 1
        if not line.strip():
            continue
        if line == TAP_VERSION_LINE and line_number == significant[0][0]:
            continue
        if line == PROTOCOL_COMMENT_LINE and len(significant) > 1 and line_number == significant[1][0]:
            continue

        # Checked before the bail-out line itself is handled, so a second
        # `Bail out!` is reported as forbidden trailing output instead of
        # silently replacing the first failure message.
        if bailed_out and not reported_content_after_bail_out:
            violations.append(
                Violation(
                    ViolationCode.CONTENT_AFTER_BAIL_OUT,
                    "No further TAP output is allowed after 'Bail out!'",
                    _at(line_number),
                )
            )
            reported_content_after_bail_out = True

        if line.startswith("Bail out!"):
            if not bailed_out:
                bailed_out = True
                bail_message = line[len("Bail out!") :].strip()
            continue

        plan_match = _PLAN_PATTERN.match(line)
        if plan_match is not None:
            if plan is not None:
                violations.append(
                    Violation(ViolationCode.MISPLACED_PLAN, "The plan must appear once", _at(line_number))
                )
            elif points:
                violations.append(
                    Violation(
                        ViolationCode.MISPLACED_PLAN,
                        "The plan must precede every test point",
                        _at(line_number),
                    )
                )
            plan = int(plan_match.group(1))
            plan_line = line_number
            continue

        if line.startswith("1..") or line.startswith("0.."):
            violations.append(Violation(ViolationCode.INVALID_PLAN, "Malformed TAP plan", _at(line_number)))
            continue

        if line.startswith("#"):
            # Free-form TAP comments carry no protocol meaning.
            continue

        point_match = _POINT_PATTERN.match(line)
        if point_match is None:
            violations.append(
                Violation(ViolationCode.UNRECOGNIZED_LINE, "Line is not valid TAP13 output", _at(line_number))
            )
            continue

        block, index, terminated = _collect_diagnostic_block(numbered, index)
        if block is not None and not terminated:
            # An unterminated block means the report stopped mid-point. The point was
            # never completely flushed, so it is not counted: the plan then reports the
            # report as unsatisfied rather than letting a partial result look complete.
            violations.append(
                Violation(
                    ViolationCode.INVALID_DIAGNOSTIC_BLOCK,
                    "Diagnostic block is not terminated by '{}...'".format(_BLOCK_INDENT),
                    _at(line_number),
                )
            )
            continue
        point = _build_point(point_match, block, line_number, violations)
        if point is not None:
            expected_number = len(points) + 1
            if point.number != expected_number:
                violations.append(
                    Violation(
                        ViolationCode.POINT_OUT_OF_ORDER,
                        "Expected test point {}, found {}".format(expected_number, point.number),
                        _at(line_number),
                    )
                )
            points.append(point)

    if plan is None:
        violations.append(Violation(ViolationCode.MISSING_PLAN, "Report declares no TAP plan"))
    elif not bailed_out:
        if len(points) > plan:
            violations.append(
                Violation(
                    ViolationCode.PLAN_EXCEEDED,
                    "Plan declares {} test points but the report emitted {}".format(plan, len(points)),
                    _at(plan_line) if plan_line is not None else "",
                )
            )
        elif len(points) < plan:
            violations.append(
                Violation(
                    ViolationCode.PLAN_UNSATISFIED,
                    "Plan declares {} test points but the report emitted only {}".format(plan, len(points)),
                    _at(plan_line) if plan_line is not None else "",
                )
            )

    _check_duplicate_ids(points, violations)
    return TapReport(violations, plan, points, bailed_out, bail_message)


def _at(line_number: int) -> str:
    return "line {}".format(line_number)


def _check_duplicate_ids(points: list[TapPoint], violations: list[Violation]) -> None:
    seen: dict[str, int] = {}
    for point in points:
        if point.id is None:
            continue
        if point.id in seen:
            violations.append(
                Violation(
                    ViolationCode.DUPLICATE_ID,
                    "Test point {} repeats identifier '{}' from point {}".format(
                        point.number, point.id, seen[point.id]
                    ),
                    "",
                )
            )
            continue
        seen[point.id] = point.number


def _collect_diagnostic_block(
    numbered: list[tuple[int, str]], index: int
) -> tuple[Optional[list[tuple[int, str]]], int, bool]:
    """Consumes an indented YAML block following a test point, if present.

    Returns the block lines, the index of the first line after it, and whether
    the block was terminated. A report truncated part-way through a diagnostic
    block leaves an unterminated block, which is an incomplete point rather than
    a usable result.
    """

    if index >= len(numbered) or numbered[index][1] != _BLOCK_INDENT + "---":
        return (None, index, True)
    block: list[tuple[int, str]] = []
    index += 1
    while index < len(numbered):
        line_number, line = numbered[index]
        index += 1
        if line == _BLOCK_INDENT + "...":
            return (block, index, True)
        block.append((line_number, line))
    # An unterminated block is still returned so its contents can be reported.
    return (block, index, False)


def _build_point(
    match: re.Match[str],
    block: Optional[list[tuple[int, str]]],
    line_number: int,
    violations: list[Violation],
) -> Optional[TapPoint]:
    number = int(match.group("number"))
    ok = match.group("status") == "ok"
    description = (match.group("description") or "").strip()
    directive = match.group("directive")
    skipped = False
    skip_reason: Optional[str] = None
    if directive is not None:
        directive_match = _DIRECTIVE_PATTERN.match(directive.strip())
        if directive_match is None:
            violations.append(
                Violation(
                    ViolationCode.UNRECOGNIZED_LINE,
                    "Unknown TAP directive '{}'".format(directive.strip()),
                    _at(line_number),
                )
            )
        elif directive_match.group("name") == "SKIP":
            skipped = True
            skip_reason = (directive_match.group("reason") or "").strip() or None

    if block is None:
        violations.append(
            Violation(
                ViolationCode.MISSING_DIAGNOSTIC_BLOCK,
                "Test point {} has no '_foundry' diagnostic block".format(number),
                _at(line_number),
            )
        )
        return TapPoint(number, ok, description, skipped, skip_reason, None, None, None, None, None)

    document = _parse_restricted_yaml(block, violations)
    if document is None:
        return TapPoint(number, ok, description, skipped, skip_reason, None, None, None, None, None)

    metadata = document.get("_foundry")
    identifier: Optional[str] = None
    duration_ms: Optional[int] = None
    status_detail: Optional[str] = None
    if not isinstance(metadata, dict):
        violations.append(
            Violation(
                ViolationCode.MISSING_FIELD,
                "Diagnostic block requires a '_foundry' mapping",
                _at(line_number),
            )
        )
    else:
        identifier = _read_required_id(metadata, line_number, violations)
        duration_ms = _read_duration(metadata, line_number, violations)
        status_detail = _read_status_detail(metadata, line_number, violations)

    message = _read_message(document, ok, skipped, line_number, violations)
    at = _read_location(document, line_number, violations)
    return TapPoint(number, ok, description, skipped, skip_reason, identifier, duration_ms, status_detail, message, at)


def _read_required_id(metadata: dict[str, Any], line_number: int, violations: list[Violation]) -> Optional[str]:
    if "id" not in metadata:
        violations.append(Violation(ViolationCode.MISSING_FIELD, "'_foundry.id' is required", _at(line_number)))
        return None
    value = metadata["id"]
    if not isinstance(value, str):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "'_foundry.id' must be a string", _at(line_number))
        )
        return None
    if not value:
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_VALUE, "'_foundry.id' must not be empty", _at(line_number))
        )
        return None
    return value


def _read_duration(metadata: dict[str, Any], line_number: int, violations: list[Violation]) -> Optional[int]:
    if "duration_ms" not in metadata:
        violations.append(
            Violation(ViolationCode.MISSING_FIELD, "'_foundry.duration_ms' is required", _at(line_number))
        )
        return None
    value = metadata["duration_ms"]
    if not isinstance(value, int) or isinstance(value, bool):
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_TYPE,
                "'_foundry.duration_ms' must be an integer",
                _at(line_number),
            )
        )
        return None
    if value < 0:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "'_foundry.duration_ms' must not be negative",
                _at(line_number),
            )
        )
        return None
    return value


def _read_status_detail(metadata: dict[str, Any], line_number: int, violations: list[Violation]) -> Optional[str]:
    if "status_detail" not in metadata:
        violations.append(
            Violation(ViolationCode.MISSING_FIELD, "'_foundry.status_detail' is required", _at(line_number))
        )
        return None
    value = metadata["status_detail"]
    if not isinstance(value, str):
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_TYPE,
                "'_foundry.status_detail' must be a string",
                _at(line_number),
            )
        )
        return None
    if value not in STATUS_DETAILS:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "Unknown '_foundry.status_detail' value '{}'".format(value),
                _at(line_number),
            )
        )
        return None
    return value


def _read_message(
    document: dict[str, Any],
    ok: bool,
    skipped: bool,
    line_number: int,
    violations: list[Violation],
) -> Optional[str]:
    if "message" not in document:
        if not ok and not skipped:
            violations.append(
                Violation(
                    ViolationCode.MISSING_FIELD,
                    "A failing test point requires a 'message' diagnostic",
                    _at(line_number),
                )
            )
        return None
    value = document["message"]
    if not isinstance(value, str):
        violations.append(Violation(ViolationCode.INVALID_FIELD_TYPE, "'message' must be a string", _at(line_number)))
        return None
    return value


def _read_location(document: dict[str, Any], line_number: int, violations: list[Violation]) -> Optional[SourceLocation]:
    if "at" not in document:
        return None
    value = document["at"]
    if not isinstance(value, dict):
        violations.append(Violation(ViolationCode.INVALID_FIELD_TYPE, "'at' must be a mapping", _at(line_number)))
        return None

    file_name = value.get("fileName")
    if not isinstance(file_name, str) or not file_name:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "'at.fileName' must be a non-empty string",
                _at(line_number),
            )
        )
        return None

    positions: list[int] = []
    for field in ("lineNumber", "columnNumber"):
        entry = value.get(field)
        if not isinstance(entry, int) or isinstance(entry, bool):
            violations.append(
                Violation(
                    ViolationCode.INVALID_FIELD_TYPE,
                    "'at.{}' must be an integer".format(field),
                    _at(line_number),
                )
            )
            return None
        if entry < 1:
            violations.append(
                Violation(
                    ViolationCode.INVALID_FIELD_VALUE,
                    "'at.{}' is one-based and must be at least 1".format(field),
                    _at(line_number),
                )
            )
            return None
        positions.append(entry)
    return SourceLocation(file_name, positions[0], positions[1])


def _parse_restricted_yaml(block: list[tuple[int, str]], violations: list[Violation]) -> Optional[dict[str, Any]]:
    """Parses the restricted YAML subset allowed in v1 diagnostic blocks.

    The subset is nested block mappings with two-space indentation steps and
    scalar values that are double-quoted strings, integers, booleans, or null.
    Anything else is a conformance failure rather than a parser extension
    point, so adapters cannot rely on a full YAML implementation downstream.
    """

    root: dict[str, Any] = {}
    stack: list[tuple[int, dict[str, Any]]] = [(_CONTENT_INDENT, root)]
    for line_number, line in block:
        stripped = line.strip()
        if not stripped:
            continue
        indent = len(line) - len(line.lstrip(" "))
        if indent < _CONTENT_INDENT or (indent - _CONTENT_INDENT) % 2 != 0 or line[:indent].strip(" "):
            violations.append(
                Violation(
                    ViolationCode.INVALID_DIAGNOSTIC_BLOCK,
                    "Diagnostic lines use two-space indentation steps starting at column {}".format(
                        _CONTENT_INDENT + 1
                    ),
                    _at(line_number),
                )
            )
            return None
        while len(stack) > 1 and indent < stack[-1][0]:
            stack.pop()
        if indent > stack[-1][0]:
            violations.append(
                Violation(
                    ViolationCode.INVALID_DIAGNOSTIC_BLOCK,
                    "Unexpected indentation increase",
                    _at(line_number),
                )
            )
            return None

        if ":" not in stripped:
            violations.append(
                Violation(
                    ViolationCode.INVALID_DIAGNOSTIC_BLOCK,
                    "Diagnostic lines must be 'key: value' mappings",
                    _at(line_number),
                )
            )
            return None
        key, _, raw_value = stripped.partition(":")
        key = key.strip()
        raw_value = raw_value.strip()
        if not key:
            violations.append(Violation(ViolationCode.INVALID_DIAGNOSTIC_BLOCK, "Empty mapping key", _at(line_number)))
            return None

        container = stack[-1][1]
        if not raw_value:
            child: dict[str, Any] = {}
            container[key] = child
            stack.append((indent + 2, child))
            continue

        value, parsed = _parse_scalar(raw_value)
        if not parsed:
            violations.append(
                Violation(
                    ViolationCode.INVALID_DIAGNOSTIC_BLOCK,
                    "Value for '{}' is not a supported scalar".format(key),
                    _at(line_number),
                )
            )
            return None
        container[key] = value
    return root


def _parse_scalar(raw_value: str) -> tuple[Any, bool]:
    if raw_value.startswith('"'):
        try:
            return (json.loads(raw_value), True)
        except ValueError:
            return (None, False)
    if _INTEGER_PATTERN.match(raw_value):
        return (int(raw_value), True)
    if raw_value == "true":
        return (True, True)
    if raw_value == "false":
        return (False, True)
    if raw_value == "null":
        return (None, True)
    return (None, False)
