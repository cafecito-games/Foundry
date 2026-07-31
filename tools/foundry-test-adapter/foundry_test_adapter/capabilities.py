"""Validator for the `adapter capabilities` document."""

from __future__ import annotations

from typing import Any, Optional

from . import strict_json
from .fields import is_integer, require_present, require_string
from .paths import PROTOCOL_NAME
from .violations import Violation, ViolationCode


class CapabilitiesResult:
    """Parsed capabilities plus every violation found in the document."""

    __slots__ = (
        "violations",
        "supported_versions",
        "framework_id",
        "framework_name",
        "framework_version",
        "extensions",
    )

    def __init__(
        self,
        violations: list[Violation],
        supported_versions: Optional[tuple[int, ...]] = None,
        framework_id: Optional[str] = None,
        framework_name: Optional[str] = None,
        framework_version: Optional[str] = None,
        extensions: Optional[tuple[str, ...]] = None,
    ) -> None:
        self.violations = violations
        self.supported_versions = supported_versions
        self.framework_id = framework_id
        self.framework_name = framework_name
        self.framework_version = framework_version
        self.extensions = extensions

    @property
    def conforms(self) -> bool:
        return not self.violations

    def negotiate(self, client_versions: tuple[int, ...]) -> Optional[int]:
        """Highest mutually supported protocol version, or None when there is none.

        A non-conforming document never negotiates: a client must treat a
        missing or invalid capabilities artifact as an unsupported runner.
        """

        if not self.conforms or not self.supported_versions:
            return None
        shared = set(self.supported_versions) & set(client_versions)
        return max(shared) if shared else None


def validate_capabilities_document(text: str) -> CapabilitiesResult:
    """Validates the complete JSON document written by `adapter capabilities`."""

    violations: list[Violation] = []
    try:
        document: Any = strict_json.loads(text)
    except ValueError as error:
        violations.append(Violation(ViolationCode.INVALID_JSON, str(error)))
        return CapabilitiesResult(violations)

    if not isinstance(document, dict):
        violations.append(Violation(ViolationCode.DOCUMENT_NOT_OBJECT, "Capabilities must be a JSON object"))
        return CapabilitiesResult(violations)

    protocol = require_string(document, "protocol", violations)
    if protocol is not None and protocol != PROTOCOL_NAME:
        violations.append(
            Violation(
                ViolationCode.WRONG_PROTOCOL,
                "Expected protocol '{}', found '{}'".format(PROTOCOL_NAME, protocol),
                "/protocol",
            )
        )

    supported_versions = _validate_supported_versions(document, violations)
    framework = _validate_framework(document, violations)
    extensions = _validate_extensions(document, violations)

    return CapabilitiesResult(
        violations,
        supported_versions,
        framework[0],
        framework[1],
        framework[2],
        extensions,
    )


def _validate_supported_versions(document: dict[str, Any], violations: list[Violation]) -> Optional[tuple[int, ...]]:
    if not require_present(document, "supported_versions", violations):
        return None
    value = document["supported_versions"]
    if not isinstance(value, list):
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_TYPE,
                "Field 'supported_versions' must be an array",
                "/supported_versions",
            )
        )
        return None
    if not value:
        violations.append(
            Violation(
                ViolationCode.INVALID_FIELD_VALUE,
                "Field 'supported_versions' must not be empty",
                "/supported_versions",
            )
        )
        return None

    entries: list[int] = []
    well_formed = True
    for index, entry in enumerate(value):
        pointer = "/supported_versions/{}".format(index)
        if not is_integer(entry):
            violations.append(
                Violation(ViolationCode.INVALID_FIELD_TYPE, "Supported version must be an integer", pointer)
            )
            well_formed = False
            continue
        if entry <= 0:
            violations.append(
                Violation(ViolationCode.INVALID_FIELD_VALUE, "Supported version must be positive", pointer)
            )
            well_formed = False
            continue
        entries.append(entry)

    if not well_formed:
        return None
    if len(set(entries)) != len(entries):
        violations.append(
            Violation(
                ViolationCode.DUPLICATE_VALUE,
                "Field 'supported_versions' must not repeat a version",
                "/supported_versions",
            )
        )
        return None
    if entries != sorted(entries):
        violations.append(
            Violation(
                ViolationCode.VERSIONS_NOT_ASCENDING,
                "Field 'supported_versions' must be in ascending order",
                "/supported_versions",
            )
        )
        return None
    return tuple(entries)


def _validate_framework(
    document: dict[str, Any], violations: list[Violation]
) -> tuple[Optional[str], Optional[str], Optional[str]]:
    if not require_present(document, "framework", violations):
        return (None, None, None)
    framework = document["framework"]
    if not isinstance(framework, dict):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field 'framework' must be an object", "/framework")
        )
        return (None, None, None)
    identifier = require_string(framework, "id", violations, pointer_base="/framework")
    name = require_string(framework, "name", violations, pointer_base="/framework")
    version = require_string(framework, "version", violations, pointer_base="/framework")
    return (identifier, name, version)


def _validate_extensions(document: dict[str, Any], violations: list[Violation]) -> Optional[tuple[str, ...]]:
    if not require_present(document, "extensions", violations):
        return None
    value = document["extensions"]
    if not isinstance(value, list):
        violations.append(
            Violation(ViolationCode.INVALID_FIELD_TYPE, "Field 'extensions' must be an array", "/extensions")
        )
        return None

    names: list[str] = []
    well_formed = True
    for index, entry in enumerate(value):
        pointer = "/extensions/{}".format(index)
        if not isinstance(entry, str):
            violations.append(Violation(ViolationCode.INVALID_FIELD_TYPE, "Extension name must be a string", pointer))
            well_formed = False
            continue
        if not entry:
            violations.append(Violation(ViolationCode.INVALID_FIELD_VALUE, "Extension name must not be empty", pointer))
            well_formed = False
            continue
        names.append(entry)

    if not well_formed:
        return None
    if len(set(names)) != len(names):
        violations.append(
            Violation(ViolationCode.DUPLICATE_VALUE, "Field 'extensions' must not repeat a name", "/extensions")
        )
        return None
    return tuple(names)
