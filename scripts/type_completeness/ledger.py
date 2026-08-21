"""Per-finding ledger records in the shape the runner validates."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable

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
_SLUG = re.compile(r"[^a-z0-9_]+")


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def record_digest(record: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(record).encode("utf-8")).hexdigest()


IDENTITY_FIELDS = ("finding_id", "case_id", "family", "dimension")


def finding_id(family: str, case_id: str, dimension: str) -> str:
    """Stable ID for one finding: a case may fail along several dimensions, each its own finding."""
    slug = _SLUG.sub("_", family.lower()).strip("_") or "finding"
    digest = hashlib.sha256(canonical_json([family, case_id, dimension]).encode("utf-8")).hexdigest()[:16]
    return f"{slug}-{digest}"


def identity_digest(record: dict[str, Any]) -> str:
    return record_digest({field: record[field] for field in IDENTITY_FIELDS})


def payload_digest_without_classification(record: dict[str, Any]) -> str:
    """Digest of everything a reviewer must keep verbatim; only the classification may change after proposal."""
    return record_digest({field: value for field, value in record.items() if field != "classification"})


def proposed_record(
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
    paths: list[str] = []
    for path in permanent_test_paths:
        if path not in paths:
            paths.append(path)
    if not paths:
        raise ValueError("at least one permanent test path is required")
    return {
        "schema_version": LEDGER_SCHEMA_VERSION,
        "finding_id": finding_id(family, case_id, dimension),
        "case_id": case_id,
        "family": family,
        "dimension": dimension,
        "classification": classification,
        "issue_url": issue_url,
        "closure_packet_url": closure_packet_url,
        "permanent_test_paths": paths,
    }


def validate_record(record: dict[str, Any]) -> None:
    if set(record) != set(LEDGER_FIELDS):
        raise ValueError(f"ledger record must have exactly {sorted(LEDGER_FIELDS)}; got {sorted(record)}")
    if record["classification"] not in KNOWN_CLASSIFICATIONS:
        raise ValueError(f"unknown classification {record['classification']!r}")


def write_record(directory: Path, record: dict[str, Any]) -> Path:
    validate_record(record)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{record['finding_id']}.json"
    path.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return path


def read_record(path: Path) -> dict[str, Any]:
    record = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(record, dict):
        raise ValueError(f"{path} must contain a JSON object")
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
