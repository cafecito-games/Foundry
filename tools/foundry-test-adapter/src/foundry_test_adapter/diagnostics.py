"""Stable diagnostic vocabulary and result model for the v1 conformance validator.

The diagnostic codes are a closed registry: messages may improve over time, but a
code never changes meaning. Consumers match on codes, never on message text.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Optional, Sequence

from . import __version__

PROTOCOL_NAME = "foundry-test-adapter"
PROTOCOL_VERSION = 1

VALIDATOR_NAME = "foundry-test-adapter"

ARTIFACT_MISSING = "artifact.missing"
ARTIFACT_ENCODING = "artifact.encoding"
ARTIFACT_READ = "artifact.read"

CAPABILITIES_LINE_ENDING = "capabilities.line_ending"
CAPABILITIES_JSON = "capabilities.json"
CAPABILITIES_SCHEMA = "capabilities.schema"
CAPABILITIES_VERSION_ORDER = "capabilities.version_order"
CAPABILITIES_EXIT = "capabilities.exit"

DISCOVERY_LINE_ENDING = "discovery.line_ending"
DISCOVERY_JSON = "discovery.json"
DISCOVERY_SCHEMA = "discovery.schema"
DISCOVERY_START = "discovery.start"
DISCOVERY_END = "discovery.end"
DISCOVERY_ORDERING = "discovery.ordering"
DISCOVERY_DUPLICATE_ID = "discovery.duplicate_id"
DISCOVERY_PARENT = "discovery.parent"
DISCOVERY_PATH = "discovery.path"
DISCOVERY_RANGE = "discovery.range"
DISCOVERY_SKIP = "discovery.skip"
DISCOVERY_COUNTS = "discovery.counts"
DISCOVERY_INCOMPLETE = "discovery.incomplete"
DISCOVERY_EXIT = "discovery.exit"

REPORT_LINE_ENDING = "report.line_ending"
REPORT_HEADER = "report.header"
REPORT_ADAPTER_VERSION = "report.adapter_version"
REPORT_PLAN = "report.plan"
REPORT_POINT = "report.point"
REPORT_DIRECTIVE = "report.directive"
REPORT_YAML = "report.yaml"
REPORT_METADATA = "report.metadata"
REPORT_DUPLICATE_ID = "report.duplicate_id"
REPORT_SELECTION = "report.selection"
REPORT_ORDER = "report.order"
REPORT_SKIP = "report.skip"
REPORT_STATUS = "report.status"
REPORT_LOCATION = "report.location"
REPORT_BAILOUT = "report.bailout"
REPORT_INCOMPLETE = "report.incomplete"
REPORT_CANCELLATION = "report.cancellation"
REPORT_EXIT = "report.exit"

CODE_REGISTRY = (
    ARTIFACT_MISSING,
    ARTIFACT_ENCODING,
    ARTIFACT_READ,
    CAPABILITIES_LINE_ENDING,
    CAPABILITIES_JSON,
    CAPABILITIES_SCHEMA,
    CAPABILITIES_VERSION_ORDER,
    CAPABILITIES_EXIT,
    DISCOVERY_LINE_ENDING,
    DISCOVERY_JSON,
    DISCOVERY_SCHEMA,
    DISCOVERY_START,
    DISCOVERY_END,
    DISCOVERY_ORDERING,
    DISCOVERY_DUPLICATE_ID,
    DISCOVERY_PARENT,
    DISCOVERY_PATH,
    DISCOVERY_RANGE,
    DISCOVERY_SKIP,
    DISCOVERY_COUNTS,
    DISCOVERY_INCOMPLETE,
    DISCOVERY_EXIT,
    REPORT_LINE_ENDING,
    REPORT_HEADER,
    REPORT_ADAPTER_VERSION,
    REPORT_PLAN,
    REPORT_POINT,
    REPORT_DIRECTIVE,
    REPORT_YAML,
    REPORT_METADATA,
    REPORT_DUPLICATE_ID,
    REPORT_SELECTION,
    REPORT_ORDER,
    REPORT_SKIP,
    REPORT_STATUS,
    REPORT_LOCATION,
    REPORT_BAILOUT,
    REPORT_INCOMPLETE,
    REPORT_CANCELLATION,
    REPORT_EXIT,
)

CONFORMING = "conforming"
DISCOVERY_FAILURES = "discovery_failures"
TEST_FAILURES = "test_failures"
INFRASTRUCTURE_FAILURE = "infrastructure_failure"
CANCELLED = "cancelled"
UNSUPPORTED = "unsupported"
INVALID = "invalid"


@dataclass(frozen=True)
class Violation:
    code: str
    message: str
    file: str
    line: Optional[int] = None
    column: Optional[int] = None
    path: Optional[str] = None

    def to_json(self) -> dict[str, Any]:
        return {
            "code": self.code,
            "message": self.message,
            "file": self.file,
            "line": self.line,
            "column": self.column,
            "path": self.path,
        }


@dataclass(frozen=True)
class ValidationResult:
    artifact: str
    valid: bool
    complete: bool
    classification: str
    violations: tuple[Violation, ...] = field(default=())

    @property
    def codes(self) -> tuple[str, ...]:
        return tuple(violation.code for violation in self.violations)

    def to_json(self) -> dict[str, Any]:
        return {
            "validator": VALIDATOR_NAME,
            "validator_version": __version__,
            "protocol_version": PROTOCOL_VERSION,
            "artifact": self.artifact,
            "valid": self.valid,
            "complete": self.complete,
            "classification": self.classification,
            "violations": [violation.to_json() for violation in self.violations],
        }


def sort_violations(violations: Sequence[Violation]) -> tuple[Violation, ...]:
    """Orders diagnostics by artifact location and then by code.

    The ordering is total so that fixture manifests and golden comparisons stay
    stable across Python versions and dictionary iteration orders.
    """

    def key(violation: Violation) -> tuple[str, int, int, str, str]:
        return (
            violation.file,
            violation.line if violation.line is not None else 0,
            violation.column if violation.column is not None else 0,
            violation.path if violation.path is not None else "",
            violation.code,
        )

    return tuple(sorted(violations, key=key))


def make_result(
    artifact: str,
    valid: bool,
    complete: bool,
    classification: str,
    violations: Sequence[Violation] = (),
) -> ValidationResult:
    return ValidationResult(artifact, valid, complete, classification, sort_violations(violations))


def render_json(result: ValidationResult) -> str:
    return json.dumps(result.to_json(), indent=2, sort_keys=False)


def render_text(result: ValidationResult) -> str:
    lines: list[str] = []
    for violation in result.violations:
        location = violation.file
        if violation.line is not None:
            location += ":{}".format(violation.line)
            if violation.column is not None:
                location += ":{}".format(violation.column)
        suffix = " ({})".format(violation.path) if violation.path else ""
        lines.append("{}: {}: {}{}".format(location, violation.code, violation.message, suffix))
    lines.append(
        "{}: valid={} complete={} classification={}".format(
            result.artifact,
            "true" if result.valid else "false",
            "true" if result.complete else "false",
            result.classification,
        )
    )
    return "\n".join(lines)
