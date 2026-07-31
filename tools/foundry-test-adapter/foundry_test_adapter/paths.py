"""Locations of the normative protocol assets shipped with the validator."""

from __future__ import annotations

from pathlib import Path

PROTOCOL_NAME = "foundry-test-adapter"
PROTOCOL_VERSION = 1

_PACKAGE_ROOT = Path(__file__).resolve().parent

CAPABILITIES_SCHEMA = "capabilities.v1.schema.json"
DISCOVERY_RECORD_SCHEMA = "discovery-record.v1.schema.json"


def schemas_root() -> Path:
    """Directory holding the normative JSON Schema documents."""

    return _PACKAGE_ROOT / "schemas"


def schema_path(name: str) -> Path:
    """Absolute path of a normative schema, by file name."""

    path = schemas_root() / name
    if not path.is_file():
        raise FileNotFoundError("No such protocol schema: {}".format(name))
    return path


def fixtures_root() -> Path:
    """Directory holding the normative conformance fixtures for v1."""

    return _PACKAGE_ROOT / "fixtures" / "v{}".format(PROTOCOL_VERSION)


def expectations_path() -> Path:
    """Manifest pairing every checked-in fixture with its expected validator output."""

    return fixtures_root() / "expectations.json"
