"""Violation records shared by every Foundry Test Adapter Protocol validator."""

from __future__ import annotations


class ViolationCode:
    """Stable violation identifiers reported by the conformance validator.

    Downstream consumers may branch on these strings, so they are part of the
    validator's public interface and change only with a protocol version bump.
    """

    # Shared document/record problems.
    INVALID_JSON = "invalid_json"
    DOCUMENT_NOT_OBJECT = "document_not_object"
    MISSING_FIELD = "missing_field"
    INVALID_FIELD_TYPE = "invalid_field_type"
    INVALID_FIELD_VALUE = "invalid_field_value"
    WRONG_PROTOCOL = "wrong_protocol"
    UNSUPPORTED_VERSION = "unsupported_version"
    DUPLICATE_VALUE = "duplicate_value"

    # Capabilities.
    VERSIONS_NOT_ASCENDING = "versions_not_ascending"

    # Discovery.
    EMPTY_STREAM = "empty_stream"
    UNKNOWN_EVENT = "unknown_event"
    MISSING_DISCOVERY_START = "missing_discovery_start"
    MISPLACED_DISCOVERY_START = "misplaced_discovery_start"
    MISSING_DISCOVERY_END = "missing_discovery_end"
    MISPLACED_DISCOVERY_END = "misplaced_discovery_end"
    DUPLICATE_ID = "duplicate_id"
    UNKNOWN_PARENT = "unknown_parent"
    INVALID_RANGE = "invalid_range"
    MISSING_SKIP_REASON = "missing_skip_reason"
    COUNT_MISMATCH = "count_mismatch"

    # TAP report.
    MISSING_TAP_VERSION = "missing_tap_version"
    MISSING_PROTOCOL_COMMENT = "missing_protocol_comment"
    MISSING_PLAN = "missing_plan"
    MISPLACED_PLAN = "misplaced_plan"
    INVALID_PLAN = "invalid_plan"
    PLAN_UNSATISFIED = "plan_unsatisfied"
    PLAN_EXCEEDED = "plan_exceeded"
    POINT_OUT_OF_ORDER = "point_out_of_order"
    UNRECOGNIZED_LINE = "unrecognized_line"
    INVALID_DIRECTIVE = "invalid_directive"
    INVALID_DIRECTIVE = "invalid_directive"
    MISSING_DIAGNOSTIC_BLOCK = "missing_diagnostic_block"
    INVALID_DIAGNOSTIC_BLOCK = "invalid_diagnostic_block"
    CONTENT_AFTER_BAIL_OUT = "content_after_bail_out"

    # Cross-artifact selection and result correlation.
    UNKNOWN_SELECTION_ID = "unknown_selection_id"
    SELECTION_NOT_RUNNABLE = "selection_not_runnable"
    UNEXPECTED_RESULT_ID = "unexpected_result_id"
    MISSING_RESULT_ID = "missing_result_id"
    RESULT_ORDER_MISMATCH = "result_order_mismatch"
    SKIP_STATE_MISMATCH = "skip_state_mismatch"
    SKIP_STATE_MISMATCH = "skip_state_mismatch"


ALL_VIOLATION_CODES: frozenset[str] = frozenset(
    value for name, value in vars(ViolationCode).items() if not name.startswith("_") and isinstance(value, str)
)


class Violation:
    """A single conformance failure, addressed by a stable location string.

    `location` is a JSON pointer for JSON documents (`/framework/id`), a
    `line <n>` reference for line-oriented artifacts, or a combination of both
    for JSONL records (`line 3 /range/end/line`). It is empty when the failure
    applies to the artifact as a whole.
    """

    __slots__ = ("code", "message", "location")

    def __init__(self, code: str, message: str, location: str = "") -> None:
        if code not in ALL_VIOLATION_CODES:
            raise ValueError("Unregistered violation code: {}".format(code))
        self.code = code
        self.message = message
        self.location = location

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Violation):
            return NotImplemented
        return self.code == other.code and self.message == other.message and self.location == other.location

    def __hash__(self) -> int:
        return hash((self.code, self.message, self.location))

    def __repr__(self) -> str:
        return "Violation(code={!r}, location={!r}, message={!r})".format(self.code, self.location, self.message)

    def __str__(self) -> str:
        if self.location:
            return "{} [{}]: {}".format(self.code, self.location, self.message)
        return "{}: {}".format(self.code, self.message)

    def as_dict(self) -> dict[str, str]:
        return {"code": self.code, "location": self.location, "message": self.message}


def summarize(violations: list[Violation]) -> str:
    """Renders violations one per line, in the order they were reported."""

    return "\n".join(str(violation) for violation in violations)
