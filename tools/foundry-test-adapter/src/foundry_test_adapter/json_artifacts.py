"""Validation of the JSON capabilities document and the JSONL discovery stream."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence

from jsonschema import Draft202012Validator

from .diagnostics import (
    ARTIFACT_ENCODING,
    ARTIFACT_MISSING,
    ARTIFACT_READ,
    CAPABILITIES_EXIT,
    CAPABILITIES_JSON,
    CAPABILITIES_LINE_ENDING,
    CAPABILITIES_SCHEMA,
    CAPABILITIES_VERSION_ORDER,
    CONFORMING,
    DISCOVERY_COUNTS,
    DISCOVERY_DUPLICATE_ID,
    DISCOVERY_END,
    DISCOVERY_EXIT,
    DISCOVERY_FAILURES,
    DISCOVERY_INCOMPLETE,
    DISCOVERY_JSON,
    DISCOVERY_LINE_ENDING,
    DISCOVERY_ORDERING,
    DISCOVERY_PARENT,
    DISCOVERY_PATH,
    DISCOVERY_RANGE,
    DISCOVERY_SCHEMA,
    DISCOVERY_SKIP,
    DISCOVERY_START,
    INFRASTRUCTURE_FAILURE,
    INVALID,
    PROTOCOL_VERSION,
    UNSUPPORTED,
    ValidationResult,
    Violation,
    make_result,
)

PROTOCOL_DIRECTORY = Path(__file__).resolve().parents[2] / "protocol" / "v1"

_BYTE_ORDER_MARK = b"\xef\xbb\xbf"


def _load_schema(name: str) -> Draft202012Validator:
    document = json.loads((PROTOCOL_DIRECTORY / name).read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(document)
    return Draft202012Validator(document)


_capabilities_validator: Optional[Draft202012Validator] = None
_discovery_validator: Optional[Draft202012Validator] = None


def capabilities_schema() -> Draft202012Validator:
    global _capabilities_validator
    if _capabilities_validator is None:
        _capabilities_validator = _load_schema("capabilities.schema.json")
    return _capabilities_validator


def discovery_record_schema() -> Draft202012Validator:
    global _discovery_validator
    if _discovery_validator is None:
        _discovery_validator = _load_schema("discovery-record.schema.json")
    return _discovery_validator


def read_artifact(path: str) -> tuple[str, Optional[str]]:
    """Reads one artifact as UTF-8 text.

    Returns the decoded text plus `None`, or empty text plus one of `missing`,
    `encoding`, or `read` describing why the artifact could not be examined.
    """

    try:
        data = Path(path).read_bytes()
    except FileNotFoundError:
        return ("", "missing")
    except OSError:
        return ("", "read")
    if data.startswith(_BYTE_ORDER_MARK):
        return ("", "encoding")
    try:
        return (data.decode("utf-8"), None)
    except UnicodeDecodeError:
        return ("", "encoding")


def _reject_constant(value: str) -> Any:
    raise ValueError("Non-standard JSON constant '{}'".format(value))


def parse_strict_json(text: str) -> Any:
    """Parses JSON without the non-standard `NaN`/`Infinity` extensions."""

    return json.loads(text, parse_constant=_reject_constant)


def json_pointer(parts: Sequence[Any]) -> str:
    pointer = "$"
    for part in parts:
        if isinstance(part, int):
            pointer += "[{}]".format(part)
        else:
            pointer += ".{}".format(part)
    return pointer


def is_canonical_resource_path(value: str) -> bool:
    if not value.startswith("res://"):
        return False
    remainder = value[len("res://") :]
    if not remainder or "\\" in remainder:
        return False
    for segment in remainder.split("/"):
        if segment in ("", ".", ".."):
            return False
    return True


def _line_ending_failure(text: str) -> Optional[str]:
    if not text:
        return None
    if "\r\n" in text:
        return "The artifact uses CRLF line endings; v1 requires LF"
    if not text.endswith("\n"):
        return "The artifact does not end with a terminal LF"
    return None


def validate_capabilities(path: str, process_exit: Optional[int] = None) -> ValidationResult:
    text, failure = read_artifact(path)
    if failure is not None:
        return _artifact_failure(path, "capabilities", failure, UNSUPPORTED, UNSUPPORTED)

    violations: list[Violation] = []
    complete = True

    line_ending = _line_ending_failure(text)
    if line_ending is not None:
        complete = False
        violations.append(Violation(CAPABILITIES_LINE_ENDING, line_ending, path))

    document: Any = None
    parsed = False
    if not text.strip():
        complete = False
        violations.append(Violation(CAPABILITIES_JSON, "The artifact contains no JSON document", path))
    else:
        try:
            document = parse_strict_json(text)
        except ValueError as parse_error:
            complete = False
            violations.append(Violation(CAPABILITIES_JSON, "Malformed JSON document: {}".format(parse_error), path))
        else:
            if not isinstance(document, dict):
                violations.append(
                    Violation(CAPABILITIES_JSON, "The capabilities artifact must contain one JSON object", path)
                )
            else:
                parsed = True

    supports_v1 = False
    if parsed:
        schema_errors = sorted(capabilities_schema().iter_errors(document), key=lambda error: list(error.absolute_path))
        for error in schema_errors:
            violations.append(
                Violation(CAPABILITIES_SCHEMA, error.message, path, path=json_pointer(error.absolute_path))
            )
        if not schema_errors:
            versions = document["supported_versions"]
            if list(versions) != sorted(versions):
                violations.append(
                    Violation(
                        CAPABILITIES_VERSION_ORDER,
                        "'supported_versions' must be ascending",
                        path,
                        path="$.supported_versions",
                    )
                )
            elif PROTOCOL_VERSION not in versions:
                violations.append(
                    Violation(
                        CAPABILITIES_VERSION_ORDER,
                        "'supported_versions' does not include protocol version {}".format(PROTOCOL_VERSION),
                        path,
                        path="$.supported_versions",
                    )
                )
            else:
                supports_v1 = True

    if supports_v1 and process_exit is not None and process_exit != 0:
        violations.append(
            Violation(
                CAPABILITIES_EXIT,
                "A conforming capabilities operation exits 0, not {}".format(process_exit),
                path,
            )
        )
        return make_result("capabilities", False, complete, INVALID, violations)

    if violations:
        return make_result("capabilities", False, complete, UNSUPPORTED, violations)
    return make_result("capabilities", True, complete, CONFORMING, violations)


def _artifact_failure(
    path: str,
    artifact: str,
    failure: str,
    missing_classification: str,
    encoding_classification: str,
) -> ValidationResult:
    if failure == "missing":
        return make_result(
            artifact,
            False,
            False,
            missing_classification,
            [Violation(ARTIFACT_MISSING, "The artifact does not exist", path)],
        )
    if failure == "encoding":
        return make_result(
            artifact,
            False,
            False,
            encoding_classification,
            [Violation(ARTIFACT_ENCODING, "The artifact is not BOM-free UTF-8", path)],
        )
    return make_result(
        artifact,
        False,
        False,
        INVALID,
        [Violation(ARTIFACT_READ, "The artifact could not be read", path)],
    )


@dataclass(frozen=True)
class DiscoveryItem:
    kind: str
    id: str
    label: str
    parent_id: Optional[str]
    path: Optional[str]
    runnable: bool
    skipped: bool
    skip_reason: Optional[str]
    line: int


@dataclass(frozen=True)
class DiscoveryModel:
    root: Optional[str]
    items: tuple[DiscoveryItem, ...]
    complete: bool
    conforming: bool

    def suites(self) -> tuple[DiscoveryItem, ...]:
        return tuple(item for item in self.items if item.kind == "suite")

    def tests(self) -> tuple[DiscoveryItem, ...]:
        return tuple(item for item in self.items if item.kind == "test")

    def by_id(self) -> dict[str, DiscoveryItem]:
        return {item.id: item for item in self.items}


def validate_discovery(
    path: str, process_exit: Optional[int] = None
) -> tuple[ValidationResult, Optional[DiscoveryModel]]:
    text, failure = read_artifact(path)
    if failure is not None:
        return (
            _artifact_failure(path, "discovery", failure, INFRASTRUCTURE_FAILURE, INFRASTRUCTURE_FAILURE),
            None,
        )

    violations: list[Violation] = []
    truncated = False

    line_ending = _line_ending_failure(text)
    if line_ending is not None:
        truncated = True
        violations.append(Violation(DISCOVERY_LINE_ENDING, line_ending, path))

    records: list[tuple[int, dict[str, Any]]] = []
    rejected_ids: set[str] = set()
    rejected = False
    if text:
        segments = text.split("\n")
        if text.endswith("\n"):
            segments.pop()
        for index, segment in enumerate(segments):
            line_number = index + 1
            if not segment.strip():
                rejected = True
                violations.append(Violation(DISCOVERY_JSON, "Blank lines are not valid records", path, line_number))
                continue
            try:
                value = parse_strict_json(segment)
            except ValueError as parse_error:
                rejected = True
                violations.append(
                    Violation(DISCOVERY_JSON, "Malformed JSON record: {}".format(parse_error), path, line_number)
                )
                continue
            if not isinstance(value, dict):
                rejected = True
                violations.append(Violation(DISCOVERY_JSON, "Each record must be a JSON object", path, line_number))
                continue
            schema_errors = sorted(
                discovery_record_schema().iter_errors(value), key=lambda error: list(error.absolute_path)
            )
            if schema_errors:
                rejected = True
                identifier = value.get("id")
                if isinstance(identifier, str) and identifier:
                    rejected_ids.add(identifier)
                for error in schema_errors:
                    violations.append(
                        Violation(
                            DISCOVERY_SCHEMA,
                            error.message,
                            path,
                            line_number,
                            path=json_pointer(error.absolute_path),
                        )
                    )
                continue
            records.append((line_number, value))

    model, complete = _validate_discovery_records(path, records, rejected, rejected_ids, violations)
    if truncated:
        complete = False
        violations.append(Violation(DISCOVERY_INCOMPLETE, "The discovery stream is truncated", path))

    error_count = sum(1 for item in model.items if item.kind == "error")
    conforming = not violations
    if conforming and process_exit is not None:
        expected = 1 if error_count else 0
        if process_exit != expected:
            violations.append(
                Violation(
                    DISCOVERY_EXIT,
                    "A discovery run in this state exits {}, not {}".format(expected, process_exit),
                    path,
                )
            )
            return (
                make_result("discovery", False, complete, INVALID, violations),
                _finalize(model, complete, False),
            )

    if not violations:
        classification = DISCOVERY_FAILURES if error_count else CONFORMING
        return (
            make_result("discovery", True, complete, classification, violations),
            _finalize(model, complete, True),
        )

    classification = INVALID if complete else INFRASTRUCTURE_FAILURE
    return (
        make_result("discovery", False, complete, classification, violations),
        _finalize(model, complete, False),
    )


def _finalize(model: DiscoveryModel, complete: bool, conforming: bool) -> DiscoveryModel:
    return DiscoveryModel(model.root, model.items, complete, conforming)


def _validate_discovery_records(
    path: str,
    records: Sequence[tuple[int, dict[str, Any]]],
    rejected: bool,
    rejected_ids: set[str],
    violations: list[Violation],
) -> tuple[DiscoveryModel, bool]:
    root: Optional[str] = None
    items: list[DiscoveryItem] = []
    suite_ids: dict[str, int] = {}
    seen_ids: dict[str, int] = {}
    start_lines: list[int] = []
    end_lines: list[int] = []
    end_record: Optional[dict[str, Any]] = None

    for line_number, record in records:
        event = record["event"]
        if event == "discovery_start":
            start_lines.append(line_number)
            if root is None:
                root = record["root"]
                if not is_canonical_resource_path(root):
                    violations.append(
                        Violation(
                            DISCOVERY_PATH,
                            "'root' is not a canonical res:// path",
                            path,
                            line_number,
                            path="$.root",
                        )
                    )
            continue
        if event == "discovery_end":
            end_lines.append(line_number)
            if end_record is None:
                end_record = record
            continue

        identifier = record["id"]
        if identifier in seen_ids:
            violations.append(
                Violation(
                    DISCOVERY_DUPLICATE_ID,
                    "ID '{}' was already emitted on line {}".format(identifier, seen_ids[identifier]),
                    path,
                    line_number,
                    path="$.id",
                )
            )
        else:
            seen_ids[identifier] = line_number

        parent_id = record["parent_id"]
        # A parent check that depends on a record the validator already rejected would
        # cascade a second diagnostic out of one defect, so it is suppressed.
        if parent_id is not None and parent_id not in suite_ids and parent_id not in rejected_ids:
            violations.append(
                Violation(
                    DISCOVERY_PARENT,
                    "'parent_id' must identify a previously emitted suite",
                    path,
                    line_number,
                    path="$.parent_id",
                )
            )

        item_path = record["path"]
        if item_path is not None and not is_canonical_resource_path(item_path):
            violations.append(
                Violation(
                    DISCOVERY_PATH,
                    "'path' is not a canonical res:// path",
                    path,
                    line_number,
                    path="$.path",
                )
            )

        _check_range(path, line_number, record, violations)

        if event == "discovery_error":
            items.append(
                DiscoveryItem(
                    "error", identifier, record["label"], parent_id, item_path, False, False, None, line_number
                )
            )
            continue

        runnable = record["runnable"]
        skipped = record["skipped"]
        skip_reason = record["skip_reason"]
        if skipped and skip_reason is None:
            violations.append(
                Violation(
                    DISCOVERY_SKIP,
                    "A skipped item requires a non-empty 'skip_reason'",
                    path,
                    line_number,
                    path="$.skip_reason",
                )
            )
        if not skipped and skip_reason is not None:
            violations.append(
                Violation(
                    DISCOVERY_SKIP,
                    "'skip_reason' must be null when the item is not skipped",
                    path,
                    line_number,
                    path="$.skip_reason",
                )
            )
        if skipped and not runnable and event == "test":
            violations.append(
                Violation(
                    DISCOVERY_SKIP,
                    "A non-runnable test cannot be marked skipped",
                    path,
                    line_number,
                    path="$.skipped",
                )
            )

        if event == "suite":
            suite_ids[identifier] = line_number
        items.append(
            DiscoveryItem(
                event, identifier, record["label"], parent_id, item_path, runnable, skipped, skip_reason, line_number
            )
        )

    _check_skipped_suites(path, items, violations)

    complete = True
    if not start_lines:
        violations.append(Violation(DISCOVERY_START, "The stream has no 'discovery_start' record", path))
    else:
        for extra in start_lines[1:]:
            violations.append(
                Violation(DISCOVERY_START, "The stream has more than one 'discovery_start' record", path, extra)
            )
        if records and records[0][0] != start_lines[0]:
            violations.append(
                Violation(DISCOVERY_ORDERING, "'discovery_start' must be the first record", path, start_lines[0])
            )

    if not end_lines:
        complete = False
        violations.append(Violation(DISCOVERY_END, "The stream has no 'discovery_end' record", path))
        violations.append(Violation(DISCOVERY_INCOMPLETE, "The discovery stream is truncated", path))
    else:
        for extra in end_lines[1:]:
            violations.append(
                Violation(DISCOVERY_END, "The stream has more than one 'discovery_end' record", path, extra)
            )
        if records and records[-1][0] != end_lines[-1]:
            violations.append(
                Violation(DISCOVERY_ORDERING, "No record may follow 'discovery_end'", path, records[-1][0])
            )

    if end_record is not None and not rejected:
        expected = {
            "suite_count": sum(1 for item in items if item.kind == "suite"),
            "test_count": sum(1 for item in items if item.kind == "test"),
            "error_count": sum(1 for item in items if item.kind == "error"),
        }
        for key, value in expected.items():
            if end_record[key] != value:
                violations.append(
                    Violation(
                        DISCOVERY_COUNTS,
                        "'{}' is {} but the stream emitted {}".format(key, end_record[key], value),
                        path,
                        end_lines[0],
                        path="$." + key,
                    )
                )

    return (DiscoveryModel(root, tuple(items), complete, False), complete)


def _check_range(path: str, line_number: int, record: dict[str, Any], violations: list[Violation]) -> None:
    item_range = record["range"]
    if item_range is None:
        return
    if record["path"] is None:
        violations.append(
            Violation(DISCOVERY_RANGE, "'range' must be null when 'path' is null", path, line_number, path="$.range")
        )
        return
    start = item_range["start"]
    end = item_range["end"]
    if (start["line"], start["character"]) > (end["line"], end["character"]):
        violations.append(
            Violation(DISCOVERY_RANGE, "'range.start' must not follow 'range.end'", path, line_number, path="$.range")
        )


def _check_skipped_suites(path: str, items: Sequence[DiscoveryItem], violations: list[Violation]) -> None:
    """A skipped suite must mark each runnable descendant test skipped explicitly."""

    skipped_suites = {item.id for item in items if item.kind == "suite" and item.skipped}
    if not skipped_suites:
        return
    by_id = {item.id: item for item in items}
    for item in items:
        if item.kind != "test" or not item.runnable or item.skipped:
            continue
        ancestor = item.parent_id
        while ancestor is not None and ancestor in by_id:
            if ancestor in skipped_suites:
                violations.append(
                    Violation(
                        DISCOVERY_SKIP,
                        "A runnable test beneath a skipped suite must be marked skipped explicitly",
                        path,
                        item.line,
                        path="$.skipped",
                    )
                )
                break
            ancestor = by_id[ancestor].parent_id
