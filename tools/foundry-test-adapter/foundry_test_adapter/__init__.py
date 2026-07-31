"""Reusable conformance validator for Foundry Test Adapter Protocol v1.

The package is framework-neutral: it accepts artifacts produced by any test
runner that implements the protocol over the `project test --runner ... -- ...`
boundary, and reports every violation it finds rather than stopping at the
first one.
"""

from __future__ import annotations

from .capabilities import CapabilitiesResult, validate_capabilities_document
from .discovery import (
    DiscoveryError,
    DiscoveryItem,
    DiscoveryResult,
    Position,
    Range,
    validate_discovery_stream,
)
from .paths import (
    CAPABILITIES_SCHEMA,
    DISCOVERY_RECORD_SCHEMA,
    PROTOCOL_NAME,
    PROTOCOL_VERSION,
    expectations_path,
    fixtures_root,
    schema_path,
    schemas_root,
)
from .run import expected_leaf_ids, is_leaf, validate_run
from .tap import STATUS_DETAILS, SourceLocation, TapPoint, TapReport, validate_tap_report
from .text import utf16_length, utf16_offset_to_index
from .violations import ALL_VIOLATION_CODES, Violation, ViolationCode, summarize

__all__ = [
    "ALL_VIOLATION_CODES",
    "CAPABILITIES_SCHEMA",
    "CapabilitiesResult",
    "DISCOVERY_RECORD_SCHEMA",
    "DiscoveryError",
    "DiscoveryItem",
    "DiscoveryResult",
    "PROTOCOL_NAME",
    "PROTOCOL_VERSION",
    "Position",
    "Range",
    "STATUS_DETAILS",
    "SourceLocation",
    "TapPoint",
    "TapReport",
    "Violation",
    "ViolationCode",
    "expectations_path",
    "expected_leaf_ids",
    "fixtures_root",
    "is_leaf",
    "schema_path",
    "schemas_root",
    "summarize",
    "validate_capabilities_document",
    "validate_discovery_stream",
    "validate_run",
    "validate_tap_report",
    "utf16_length",
    "utf16_offset_to_index",
]
