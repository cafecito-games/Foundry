"""Shared field-level checks used by the capabilities and discovery validators."""

from __future__ import annotations

from typing import Any, Optional

from .violations import Violation, ViolationCode


def is_integer(value: Any) -> bool:
    """True for JSON integers. JSON booleans are Python ints and are rejected."""

    return isinstance(value, int) and not isinstance(value, bool)


def join_location(prefix: str, pointer: str) -> str:
    if not prefix:
        return pointer
    if not pointer:
        return prefix
    return "{} {}".format(prefix, pointer)


def require_present(
    document: dict[str, Any],
    field: str,
    violations: list[Violation],
    prefix: str = "",
    pointer_base: str = "",
) -> bool:
    if field in document:
        return True
    violations.append(
        Violation(
            ViolationCode.MISSING_FIELD,
            "Required field '{}' is missing".format(field),
            join_location(prefix, "{}/{}".format(pointer_base, field)),
        )
    )
    return False


def require_string(
    document: dict[str, Any],
    field: str,
    violations: list[Violation],
    prefix: str = "",
    pointer_base: str = "",
    allow_empty: bool = False,
) -> Optional[str]:
    if not require_present(document, field, violations, prefix, pointer_base):
        return None
    pointer = join_location(prefix, "{}/{}".format(pointer_base, field))
    value = document[field]
    if not isinstance(value, str):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field '{}' must be a string".format(field), pointer)
        )
        return None
    if not allow_empty and not value:
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_VALUE, "Field '{}' must not be empty".format(field), pointer)
        )
        return None
    return value


def require_bool(
    document: dict[str, Any],
    field: str,
    violations: list[Violation],
    prefix: str = "",
    pointer_base: str = "",
) -> Optional[bool]:
    if not require_present(document, field, violations, prefix, pointer_base):
        return None
    pointer = join_location(prefix, "{}/{}".format(pointer_base, field))
    value = document[field]
    if not isinstance(value, bool):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field '{}' must be a boolean".format(field), pointer)
        )
        return None
    return value


def require_nullable_string(
    document: dict[str, Any],
    field: str,
    violations: list[Violation],
    prefix: str = "",
    pointer_base: str = "",
) -> Optional[str]:
    """Requires a present field that is either a non-empty string or JSON null."""

    if not require_present(document, field, violations, prefix, pointer_base):
        return None
    pointer = join_location(prefix, "{}/{}".format(pointer_base, field))
    value = document[field]
    if value is None:
        return None
    if not isinstance(value, str):
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_TYPE,
                "Field '{}' must be a string or null".format(field),
                pointer,
            )
        )
        return None
    if not value:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "Field '{}' must be null rather than an empty string".format(field),
                pointer,
            )
        )
        return None
    return value


def require_non_negative_integer(
    document: dict[str, Any],
    field: str,
    violations: list[Violation],
    prefix: str = "",
    pointer_base: str = "",
) -> Optional[int]:
    if not require_present(document, field, violations, prefix, pointer_base):
        return None
    pointer = join_location(prefix, "{}/{}".format(pointer_base, field))
    value = document[field]
    if not is_integer(value):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field '{}' must be an integer".format(field), pointer)
        )
        return None
    if value < 0:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "Field '{}' must not be negative".format(field),
                pointer,
            )
        )
        return None
    return int(value)
