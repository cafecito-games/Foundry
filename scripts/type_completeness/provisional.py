"""Machine-readable provisional record carried by the tracking issue until the ledger PR merges."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Iterable, Mapping, Optional

from . import deadline, ledger
from .comparator import ComparisonArtifact
from .report import (
    DEFAULT_CAPABILITIES_PATH,
    ReportError,
    capability_slice_for_family,
    load_capabilities_file,
    schema_version_matches,
)

# A provisional record embeds the develop comparison it stands on, so it inherits that document's digest
# contract: a record written under an older comparison formula cannot be validated by this tooling. Version 2
# is the first that embeds a version-2 comparison. Bump it whenever the embedded contract changes.
PROVISIONAL_SCHEMA_VERSION = 2
ORIGINS = ("automation", "manual")
BLOCK_START = "<!-- type-completeness-provisional-record -->"
BLOCK_END = "<!-- /type-completeness-provisional-record -->"
_BLOCK = re.compile(re.escape(BLOCK_START) + r"\s*```json\s*(.*?)\s*```\s*" + re.escape(BLOCK_END), re.DOTALL)


class ProvisionalError(ValueError):
    """Raised when a provisional record is missing or malformed."""


def _validated_comparison(comparison: Mapping[str, Any], capabilities: Mapping[str, Any]) -> dict[str, Any]:
    """The embedded develop comparison must be a complete artifact that passes the comparator's own
    validation (identity, digests, status) and must record exactly the capability slice the manifest derives for
    its family, so neither list inside a tracking issue can be edited to redirect blocking."""
    try:
        artifact = ComparisonArtifact.from_dict(comparison)
    except (ReportError, KeyError, TypeError, ValueError) as error:
        raise ProvisionalError(f"develop_comparison is not a valid comparison artifact: {error}") from error
    expected_slice = capability_slice_for_family(capabilities, artifact.family)
    if artifact.capability_slice is None or artifact.capability_slice != expected_slice:
        raise ProvisionalError(
            f"develop_comparison records capability slice {artifact.capability_slice} but the manifest derives "
            f"{expected_slice} for family {artifact.family!r}"
        )
    return artifact.to_dict()


def _producing_slice(comparison: Mapping[str, Any]) -> frozenset[str]:
    recorded = comparison["capability_slice"]
    return frozenset(str(path) for path in recorded["paths"])


def _path_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        raise ProvisionalError("capability_slice must be a JSON array of paths")
    return [path if isinstance(path, str) else "" for path in value]


@dataclass(frozen=True)
class ProvisionalRecord:
    finding_id: str
    payload: dict[str, Any]
    payload_digest: str
    capability_slice: tuple[str, ...]
    workstream_owner: str
    detection_artifact: str
    develop_comparison: dict[str, Any]
    detected_at: datetime
    due_at: datetime
    bot_pr_url: Optional[str]
    origin: str
    state: str
    # Migration bookkeeping: the historical case ID the ledger entry was filed under, every case ID it resolves
    # to today, and the subset this proposal covers. Empty for a finding that was never migrated.
    migrated_from: Optional[str] = None
    resolved_case_ids: tuple[str, ...] = ()
    proposed_case_ids: tuple[str, ...] = ()

    @classmethod
    def create(
        cls,
        finding_id: str,
        payload: Mapping[str, Any],
        capability_slice: Iterable[str],
        workstream_owner: str,
        detection_artifact: str,
        develop_comparison: Mapping[str, Any],
        detected_at: datetime,
        bot_pr_url: Optional[str],
        origin: str,
        state: str = "pending_merge",
        migrated_from: Optional[str] = None,
        resolved_case_ids: Iterable[str] = (),
        proposed_case_ids: Iterable[str] = (),
        capabilities: Optional[Mapping[str, Any]] = None,
    ) -> ProvisionalRecord:
        if origin not in ORIGINS:
            raise ProvisionalError(f"origin must be one of {ORIGINS}; got {origin!r}")
        record = dict(payload)
        ledger.validate_record(record)
        paths = tuple(capability_slice)
        if not paths or any(not isinstance(path, str) or not path for path in paths):
            raise ProvisionalError("capability_slice must be a non-empty list of non-empty path strings")
        manifest = load_capabilities_file(DEFAULT_CAPABILITIES_PATH) if capabilities is None else capabilities
        validated_comparison = _validated_comparison(develop_comparison, manifest)
        producing = _producing_slice(validated_comparison)
        outside = sorted(set(paths) - producing)
        if outside:
            raise ProvisionalError(
                f"capability_slice {outside} lies outside the producing slice {sorted(producing)} the manifest "
                "derives for the finding's family"
            )
        if record["finding_id"] != finding_id:
            raise ProvisionalError("finding_id does not match the proposed ledger payload")
        return cls(
            finding_id=finding_id,
            payload=record,
            payload_digest=ledger.record_digest(record),
            capability_slice=paths,
            workstream_owner=workstream_owner,
            detection_artifact=detection_artifact,
            develop_comparison=validated_comparison,
            detected_at=detected_at.astimezone(deadline.NEW_YORK),
            due_at=deadline.classification_deadline(detected_at),
            bot_pr_url=bot_pr_url,
            origin=origin,
            state=state,
            migrated_from=migrated_from or None,
            resolved_case_ids=tuple(resolved_case_ids),
            proposed_case_ids=tuple(proposed_case_ids),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": PROVISIONAL_SCHEMA_VERSION,
            "finding_id": self.finding_id,
            "payload": self.payload,
            "payload_digest": self.payload_digest,
            "capability_slice": list(self.capability_slice),
            "workstream_owner": self.workstream_owner,
            "detection_artifact": self.detection_artifact,
            "develop_comparison": self.develop_comparison,
            "detected_at": deadline.format_timestamp(self.detected_at),
            "due_at": deadline.format_timestamp(self.due_at),
            "bot_pr_url": self.bot_pr_url,
            "origin": self.origin,
            "state": self.state,
            "migrated_from": self.migrated_from,
            "resolved_case_ids": list(self.resolved_case_ids),
            "proposed_case_ids": list(self.proposed_case_ids),
        }

    @classmethod
    def from_dict(cls, data: Mapping[str, Any], capabilities: Optional[Mapping[str, Any]] = None) -> ProvisionalRecord:
        # Checked before anything is read out of the record, and outside the malformed-record wrapper below,
        # so a record of another version is refused by name rather than by the digest mismatch its embedded
        # comparison would raise: a stale record is stale, not tampered with.
        if "schema_version" not in data:
            raise ProvisionalError("malformed provisional record: 'schema_version'")
        if not schema_version_matches(data["schema_version"], PROVISIONAL_SCHEMA_VERSION):
            raise ProvisionalError(
                f"provisional schema_version {data['schema_version']!r} is not supported; expected "
                f"{PROVISIONAL_SCHEMA_VERSION}. Regenerate the record with this version of the tooling."
            )
        try:
            record = cls.create(
                finding_id=str(data["finding_id"]),
                payload=data["payload"],
                capability_slice=_path_list(data["capability_slice"]),
                workstream_owner=str(data["workstream_owner"]),
                detection_artifact=str(data["detection_artifact"]),
                develop_comparison=data["develop_comparison"],
                detected_at=deadline.parse_timestamp(str(data["detected_at"])),
                bot_pr_url=None if data.get("bot_pr_url") is None else str(data["bot_pr_url"]),
                origin=str(data["origin"]),
                state=str(data["state"]),
                migrated_from=None if data["migrated_from"] is None else str(data["migrated_from"]),
                resolved_case_ids=[str(case_id) for case_id in data["resolved_case_ids"]],
                proposed_case_ids=[str(case_id) for case_id in data["proposed_case_ids"]],
                capabilities=capabilities,
            )
        except (KeyError, TypeError, ValueError) as error:
            raise ProvisionalError(f"malformed provisional record: {error}") from error
        if record.payload_digest != data.get("payload_digest"):
            raise ProvisionalError("payload_digest does not match the proposed ledger payload")
        if deadline.format_timestamp(record.due_at) != data.get("due_at"):
            raise ProvisionalError("due_at does not match the detection time")
        return record


def render_issue_body(record: ProvisionalRecord, title_note: str = "") -> str:
    lines = []
    if title_note:
        lines.append(title_note)
        lines.append("")
    lines.append(
        "This tracking issue is the provisional source of truth for the finding until the linked ledger pull "
        "request merges. Do not edit the machine-readable block by hand; regenerate it with "
        "`scripts/type_completeness`."
    )
    lines.append("")
    lines.append(BLOCK_START)
    lines.append("```json")
    lines.append(json.dumps(record.to_dict(), sort_keys=True, indent=2))
    lines.append("```")
    lines.append(BLOCK_END)
    return "\n".join(lines) + "\n"


def parse_issue_body(body: str, capabilities: Optional[Mapping[str, Any]] = None) -> ProvisionalRecord:
    match = _BLOCK.search(body)
    if match is None:
        raise ProvisionalError("issue body has no provisional record block")
    try:
        data = json.loads(match.group(1))
    except ValueError as error:
        raise ProvisionalError(f"provisional record block is not valid JSON: {error}") from error
    return ProvisionalRecord.from_dict(data, capabilities=capabilities)
