"""Validator for the JSONL stream written by `adapter discover`."""

from __future__ import annotations

import json
from typing import Any, Optional

from .fields import (
    is_integer,
    require_bool,
    require_non_negative_integer,
    require_nullable_string,
    require_present,
    require_string,
)
from .paths import PROTOCOL_NAME, PROTOCOL_VERSION
from .violations import Violation, ViolationCode

DISCOVERY_START = "discovery_start"
DISCOVERY_END = "discovery_end"
SUITE = "suite"
TEST = "test"
DISCOVERY_ERROR = "discovery_error"

KNOWN_EVENTS = (DISCOVERY_START, SUITE, TEST, DISCOVERY_ERROR, DISCOVERY_END)


class Position:
    __slots__ = ("line", "character")

    def __init__(self, line: int, character: int) -> None:
        self.line = line
        self.character = character

    def as_tuple(self) -> tuple[int, int]:
        return (self.line, self.character)


class Range:
    __slots__ = ("start", "end")

    def __init__(self, start: Position, end: Position) -> None:
        self.start = start
        self.end = end


class DiscoveryItem:
    """A `suite` or `test` record."""

    __slots__ = (
        "kind",
        "id",
        "label",
        "parent_id",
        "path",
        "range",
        "runnable",
        "skipped",
        "skip_reason",
        "case_key",
    )

    def __init__(
        self,
        kind: str,
        identifier: str,
        label: str,
        parent_id: Optional[str],
        path: Optional[str],
        item_range: Optional[Range],
        runnable: bool,
        skipped: bool,
        skip_reason: Optional[str],
        case_key: Optional[str],
    ) -> None:
        self.kind = kind
        self.id = identifier
        self.label = label
        self.parent_id = parent_id
        self.path = path
        self.range = item_range
        self.runnable = runnable
        self.skipped = skipped
        self.skip_reason = skip_reason
        self.case_key = case_key


class DiscoveryError:
    """A recoverable `discovery_error` record."""

    __slots__ = ("id", "label", "parent_id", "message", "path", "range")

    def __init__(
        self,
        identifier: str,
        label: str,
        parent_id: Optional[str],
        message: str,
        path: Optional[str],
        error_range: Optional[Range],
    ) -> None:
        self.id = identifier
        self.label = label
        self.parent_id = parent_id
        self.message = message
        self.path = path
        self.range = error_range


class DiscoveryResult:
    """Parsed discovery stream plus every violation found in it."""

    __slots__ = ("violations", "root", "items", "errors", "complete")

    def __init__(
        self,
        violations: list[Violation],
        root: Optional[str],
        items: list[DiscoveryItem],
        errors: list[DiscoveryError],
        complete: bool,
    ) -> None:
        self.violations = violations
        self.root = root
        self.items = items
        self.errors = errors
        self.complete = complete

    @property
    def conforms(self) -> bool:
        return not self.violations

    @property
    def suites(self) -> list[DiscoveryItem]:
        return [item for item in self.items if item.kind == SUITE]

    @property
    def tests(self) -> list[DiscoveryItem]:
        return [item for item in self.items if item.kind == TEST]

    def item_by_id(self, identifier: str) -> Optional[DiscoveryItem]:
        for item in self.items:
            if item.id == identifier:
                return item
        return None

    def children_of(self, identifier: str) -> list[DiscoveryItem]:
        return [item for item in self.items if item.parent_id == identifier]


def validate_discovery_stream(text: str) -> DiscoveryResult:
    """Validates a complete or truncated discovery JSONL stream.

    A truncated stream is reported as `missing_discovery_end` with
    `complete == False`; already-emitted records are still parsed so a caller
    can tell a crashed adapter apart from a malformed one.
    """

    violations: list[Violation] = []
    items: list[DiscoveryItem] = []
    errors: list[DiscoveryError] = []
    root: Optional[str] = None
    known_ids: dict[str, int] = {}

    records: list[tuple[int, Any]] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        if not raw_line.strip():
            continue
        try:
            records.append((line_number, json.loads(raw_line)))
        except ValueError as error:
            violations.append(Violation(ViolationCode.INVALID_JSON, str(error), _at(line_number)))

    if not records:
        violations.append(Violation(ViolationCode.EMPTY_STREAM, "Discovery stream contains no records"))
        return DiscoveryResult(violations, None, items, errors, False)

    seen_start = False
    end_index: Optional[int] = None
    reported_content_after_end = False

    for record_index, (line_number, record) in enumerate(records):
        location = _at(line_number)
        if not isinstance(record, dict):
            violations.append(
                Violation(ViolationCode.DOCUMENT_NOT_OBJECT, "Discovery record must be a JSON object", location)
            )
            continue

        if end_index is not None and not reported_content_after_end:
            violations.append(
                Violation(
                    ViolationCode.MISPLACED_DISCOVERY_END,
                    "'discovery_end' must be the final record",
                    location,
                )
            )
            reported_content_after_end = True

        _validate_envelope(record, violations, location)

        event = require_string(record, "event", violations, prefix=location)
        if event is None:
            continue
        if event not in KNOWN_EVENTS:
            violations.append(Violation(ViolationCode.UNKNOWN_EVENT, "Unknown event '{}'".format(event), location))
            continue

        if event == DISCOVERY_START:
            if seen_start or record_index != 0:
                violations.append(
                    Violation(
                        ViolationCode.MISPLACED_DISCOVERY_START,
                        "'discovery_start' must be the first record and appear once",
                        location,
                    )
                )
            seen_start = True
            root = require_string(record, "root", violations, prefix=location)
            continue

        if event == DISCOVERY_END:
            if end_index is not None:
                violations.append(
                    Violation(
                        ViolationCode.MISPLACED_DISCOVERY_END,
                        "'discovery_end' must appear once",
                        location,
                    )
                )
            end_index = record_index
            _validate_counts(record, violations, location, items, errors)
            continue

        if event == DISCOVERY_ERROR:
            discovery_error = _validate_discovery_error(record, violations, line_number, known_ids)
            if discovery_error is not None:
                errors.append(discovery_error)
            continue

        item = _validate_item(event, record, violations, line_number, known_ids)
        if item is not None:
            items.append(item)

    if not seen_start:
        violations.append(Violation(ViolationCode.MISSING_DISCOVERY_START, "No 'discovery_start' record was emitted"))

    complete = end_index is not None
    if not complete:
        violations.append(
            Violation(
                ViolationCode.MISSING_DISCOVERY_END,
                "Stream ended without a 'discovery_end' record",
            )
        )

    return DiscoveryResult(violations, root, items, errors, complete)


def _at(line_number: int) -> str:
    return "line {}".format(line_number)


def _validate_envelope(record: dict[str, Any], violations: list[Violation], location: str) -> None:
    protocol = require_string(record, "protocol", violations, prefix=location)
    if protocol is not None and protocol != PROTOCOL_NAME:
        violations.append(
            Violation(
                ViolationCode.WRONG_PROTOCOL,
                "Expected protocol '{}', found '{}'".format(PROTOCOL_NAME, protocol),
                "{} /protocol".format(location),
            )
        )
    if require_present(record, "version", violations, prefix=location):
        version = record["version"]
        if not is_integer(version):
            violations.append(
                Violation(
                    ViolationCode.INVALID_FIELD_TYPE,
                    "Field 'version' must be an integer",
                    "{} /version".format(location),
                )
            )
        elif version != PROTOCOL_VERSION:
            violations.append(
                Violation(
                    ViolationCode.UNSUPPORTED_VERSION,
                    "Expected protocol version {}, found {}".format(PROTOCOL_VERSION, version),
                    "{} /version".format(location),
                )
            )


def _register_id(
    identifier: Optional[str],
    known_ids: dict[str, int],
    violations: list[Violation],
    line_number: int,
) -> None:
    if identifier is None:
        return
    if identifier in known_ids:
        violations.append(
            Violation(
                ViolationCode.DUPLICATE_ID,
                "Identifier '{}' was already emitted on line {}".format(identifier, known_ids[identifier]),
                "{} /id".format(_at(line_number)),
            )
        )
        return
    known_ids[identifier] = line_number


def _validate_parent(
    record: dict[str, Any],
    known_ids: dict[str, int],
    violations: list[Violation],
    location: str,
) -> Optional[str]:
    parent_id = require_nullable_string(record, "parent_id", violations, prefix=location)
    if parent_id is not None and parent_id not in known_ids:
        violations.append(
            Violation(
                ViolationCode.UNKNOWN_PARENT,
                "Parent '{}' has not been emitted yet".format(parent_id),
                "{} /parent_id".format(location),
            )
        )
    return parent_id


def _validate_range(
    record: dict[str, Any],
    violations: list[Violation],
    location: str,
) -> Optional[Range]:
    if not require_present(record, "range", violations, prefix=location):
        return None
    value = record["range"]
    if value is None:
        return None
    pointer = "{} /range".format(location)
    if not isinstance(value, dict):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field 'range' must be an object or null", pointer)
        )
        return None

    start = _validate_position(value, "start", violations, location)
    end = _validate_position(value, "end", violations, location)
    if start is None or end is None:
        return None
    if end.as_tuple() < start.as_tuple():
        violations.append(Violation(ViolationCode.INVALID_RANGE, "Range end precedes range start", pointer))
        return None
    return Range(start, end)


def _validate_position(
    range_value: dict[str, Any],
    field: str,
    violations: list[Violation],
    location: str,
) -> Optional[Position]:
    pointer = "{} /range/{}".format(location, field)
    if field not in range_value:
        violations.append(
            Violation(ViolationCode.MISSING_FIELD, "Required field '{}' is missing".format(field), pointer)
        )
        return None
    value = range_value[field]
    if not isinstance(value, dict):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field '{}' must be an object".format(field), pointer)
        )
        return None
    line = require_non_negative_integer(value, "line", violations, prefix=location, pointer_base="/range/" + field)
    character = require_non_negative_integer(
        value, "character", violations, prefix=location, pointer_base="/range/" + field
    )
    if line is None or character is None:
        return None
    return Position(line, character)


def _validate_item(
    event: str,
    record: dict[str, Any],
    violations: list[Violation],
    line_number: int,
    known_ids: dict[str, int],
) -> Optional[DiscoveryItem]:
    location = _at(line_number)
    identifier = require_string(record, "id", violations, prefix=location)
    _register_id(identifier, known_ids, violations, line_number)
    label = require_string(record, "label", violations, prefix=location)
    parent_id = _validate_parent(record, known_ids, violations, location)
    path = require_nullable_string(record, "path", violations, prefix=location)
    item_range = _validate_range(record, violations, location)
    runnable = require_bool(record, "runnable", violations, prefix=location)
    skipped = require_bool(record, "skipped", violations, prefix=location)
    skip_reason = require_nullable_string(record, "skip_reason", violations, prefix=location)
    if skipped and skip_reason is None:
        violations.append(
            Violation(
                ViolationCode.MISSING_SKIP_REASON,
                "A skipped item requires a non-empty 'skip_reason'",
                "{} /skip_reason".format(location),
            )
        )

    case_key: Optional[str] = None
    if event == TEST:
        case_key = require_nullable_string(record, "case_key", violations, prefix=location)

    if identifier is None or label is None or runnable is None or skipped is None:
        return None
    return DiscoveryItem(
        event, identifier, label, parent_id, path, item_range, runnable, skipped, skip_reason, case_key
    )


def _validate_discovery_error(
    record: dict[str, Any],
    violations: list[Violation],
    line_number: int,
    known_ids: dict[str, int],
) -> Optional[DiscoveryError]:
    location = _at(line_number)
    identifier = require_string(record, "id", violations, prefix=location)
    _register_id(identifier, known_ids, violations, line_number)
    label = require_string(record, "label", violations, prefix=location)
    parent_id = _validate_parent(record, known_ids, violations, location)
    message = require_string(record, "message", violations, prefix=location)
    path = require_nullable_string(record, "path", violations, prefix=location)
    error_range = _validate_range(record, violations, location)
    if identifier is None or label is None or message is None:
        return None
    return DiscoveryError(identifier, label, parent_id, message, path, error_range)


def _validate_counts(
    record: dict[str, Any],
    violations: list[Violation],
    location: str,
    items: list[DiscoveryItem],
    errors: list[DiscoveryError],
) -> None:
    observed = {
        "suite_count": len([item for item in items if item.kind == SUITE]),
        "test_count": len([item for item in items if item.kind == TEST]),
        "error_count": len(errors),
    }
    for field, expected in observed.items():
        declared = require_non_negative_integer(record, field, violations, prefix=location)
        if declared is None:
            continue
        if declared != expected:
            violations.append(
                Violation(
                    ViolationCode.COUNT_MISMATCH,
                    "Declared {} {} does not match {} emitted records".format(field, declared, expected),
                    "{} /{}".format(location, field),
                )
            )
