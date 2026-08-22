"""Per-finding ledger records in the shape the runner validates."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable

from .report import require_object, schema_version_matches

LEDGER_SCHEMA_VERSION = 1
LEDGER_FIELDS = (
    "schema_version",
    "finding_id",
    "case_id",
    "family",
    "dimension",
    "classification",
    "issue_url",
    "closure_packet_url",
    "permanent_test_paths",
)
KNOWN_CLASSIFICATIONS = (
    "unclassified",
    "product_defect",
    "specification_defect",
    "harness_defect",
    "intentional_unsupported",
    "duplicate",
)


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def record_digest(record: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(record).encode("utf-8")).hexdigest()


IDENTITY_FIELDS = ("finding_id", "case_id", "family", "dimension")
# make_finding_id in fs_type_completeness_runner.cpp: "fstcf-v1-" + sha256(case_id + "|" + dimension)[:20].
FINDING_ID_PATTERN = re.compile(r"^fstcf-v1-[0-9a-f]{20}$")


def runner_finding_id(case_id: str, dimension: str) -> str:
    """Mirror of make_finding_id in fs_type_completeness_runner.cpp, used only for the children of a migration
    split, which have no runner-emitted ID of their own until their ledger entries exist. A test pins this
    against an ID the runner actually emitted."""
    digest = hashlib.sha256(f"{case_id}|{dimension}".encode("utf-8")).hexdigest()[:20]
    return f"fstcf-v1-{digest}"


def require_runner_finding_id(finding_id: Any) -> str:
    """The finding ID names a file, so it is validated against the runner's exact format before any path use."""
    if not isinstance(finding_id, str) or not FINDING_ID_PATTERN.match(finding_id):
        raise ValueError(f"finding_id must match the runner format fstcf-v1-<20 hex>; got {finding_id!r}")
    return finding_id


def identity_digest(record: dict[str, Any]) -> str:
    return record_digest({field: record[field] for field in IDENTITY_FIELDS})


def payload_digest_without_classification(record: dict[str, Any]) -> str:
    """Digest of everything a reviewer must keep verbatim; only the classification may change after proposal."""
    return record_digest({field: value for field, value in record.items() if field != "classification"})


def proposed_record(
    finding_id: str,
    family: str,
    case_id: str,
    dimension: str,
    issue_url: str,
    closure_packet_url: str,
    permanent_test_paths: Iterable[str],
    classification: str = "unclassified",
) -> dict[str, Any]:
    if classification not in KNOWN_CLASSIFICATIONS:
        raise ValueError(f"unknown classification {classification!r}")
    require_runner_finding_id(finding_id)
    paths: list[str] = []
    for path in permanent_test_paths:
        if path not in paths:
            paths.append(path)
    if not paths:
        raise ValueError("at least one permanent test path is required")
    return {
        "schema_version": LEDGER_SCHEMA_VERSION,
        "finding_id": finding_id,
        "case_id": case_id,
        "family": family,
        "dimension": dimension,
        "classification": classification,
        "issue_url": issue_url,
        "closure_packet_url": closure_packet_url,
        "permanent_test_paths": paths,
    }


def validate_record(record: dict[str, Any]) -> None:
    """Mirror the runner's structural checks so a generated file is never rejected by the harness.

    The runner additionally verifies that each permanent test path is a tracked file and that the case ID is
    live or migrated; those checks need a repository checkout and stay with the runner.
    """
    record = require_object(record, "ledger record", ValueError)
    if set(record) != set(LEDGER_FIELDS):
        raise ValueError(f"ledger record must have exactly {sorted(LEDGER_FIELDS)}; got {sorted(record)}")
    version = record["schema_version"]
    if not schema_version_matches(version, LEDGER_SCHEMA_VERSION):
        raise ValueError(f"schema_version must be the number {LEDGER_SCHEMA_VERSION}; got {version!r}")
    for field in ("finding_id", "case_id", "family", "dimension", "classification", "issue_url", "closure_packet_url"):
        if not isinstance(record[field], str) or not record[field]:
            raise ValueError(f"{field} must be a non-empty string")
    if record["classification"] not in KNOWN_CLASSIFICATIONS:
        raise ValueError(f"unknown classification {record['classification']!r}")
    require_runner_finding_id(record["finding_id"])
    for field in ("issue_url", "closure_packet_url"):
        if not record[field].startswith(("https://", "http://")):
            raise ValueError(f"{field} must be an http(s) URL")
    paths = record["permanent_test_paths"]
    if not isinstance(paths, list) or not paths:
        raise ValueError("permanent_test_paths must be a non-empty array")
    if any(not isinstance(path, str) or not path for path in paths) or len(set(paths)) != len(paths):
        raise ValueError("permanent_test_paths must be unique non-empty strings")


def write_record(directory: Path, record: dict[str, Any]) -> Path:
    validate_record(record)
    directory.mkdir(parents=True, exist_ok=True)
    resolved_directory = directory.resolve()
    path = (resolved_directory / f"{record['finding_id']}.json").resolve()
    if path.parent != resolved_directory:
        raise ValueError(f"refusing to write ledger record outside {resolved_directory}: {path}")
    path.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return path


def read_record(path: Path) -> dict[str, Any]:
    record = require_object(json.loads(path.read_text(encoding="utf-8")), f"ledger record {path}", ValueError)
    validate_record(record)
    if path.stem != record["finding_id"]:
        raise ValueError(f"{path} basename does not match finding_id {record['finding_id']!r}")
    return record


def load_ledger(directory: Path) -> dict[str, dict[str, Any]]:
    ledger: dict[str, dict[str, Any]] = {}
    if not directory.is_dir():
        return ledger
    for path in sorted(directory.glob("*.json")):
        record = read_record(path)
        ledger[record["finding_id"]] = record
    return ledger
