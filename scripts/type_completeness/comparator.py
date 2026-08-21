"""Deterministic comparison of a branch report against the matching ``develop`` report."""

from __future__ import annotations

import enum
import hashlib
import json
from dataclasses import dataclass
from typing import Any, Mapping, Optional

from .report import (
    OBSERVATION_FIELDS,
    CapabilitySlice,
    CaseResult,
    Report,
    ReportError,
    capability_slice_for_family,
    schema_version_matches,
)

COMPARISON_SCHEMA_VERSION = 1


class Status(str, enum.Enum):
    NEW = "new"
    WORSENED = "worsened"
    UNCHANGED = "unchanged"
    RESOLVED = "resolved"
    MISSING = "missing"
    PASSING = "passing"


# A known develop failure that vanishes without passing is treated as a regression, not as progress.
REGRESSION_STATUSES = (Status.NEW, Status.WORSENED, Status.MISSING)
# Statuses meaning the case no longer fails on the branch; reconciliation uses this set so it cannot drift.
NO_LONGER_FAILING_STATUSES = (Status.RESOLVED, Status.PASSING)


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def digest_of(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def observation_digest(case: Mapping[str, Any]) -> str:
    return digest_of({field: case[field] for field in OBSERVATION_FIELDS if field in case})


# Only the semantic content of a finding feeds the digest. Artifact paths differ between worktrees and ledger
# metadata (classification, issue and closure-packet URLs, permanent tests) changes without any product change.
# parity_evidence carries the text/bytecode disagreement itself and is digested minus any path or timestamp key.
FINDING_DIGEST_FIELDS = ("dimension", "expected", "actual", "parity_evidence")
NON_SEMANTIC_KEY_MARKERS = ("path", "timestamp", "_at")


def _is_non_semantic_key(key: str) -> bool:
    lowered = key.lower()
    return any(marker in lowered if marker != "_at" else lowered.endswith("_at") for marker in NON_SEMANTIC_KEY_MARKERS)


def _semantic_view(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {key: _semantic_view(item) for key, item in value.items() if not _is_non_semantic_key(str(key))}
    if isinstance(value, list):
        return [_semantic_view(item) for item in value]
    return value


def _finding_digest_view(finding: Mapping[str, Any]) -> dict[str, Any]:
    return {field: _semantic_view(finding.get(field)) for field in FINDING_DIGEST_FIELDS}


def _case_digest(case: CaseResult) -> str:
    return digest_of(
        {"observation": case.observation, "findings": [_finding_digest_view(finding) for finding in case.findings]}
    )


def _side(case: Optional[CaseResult]) -> dict[str, Any]:
    if case is None:
        return {"present": False, "passed": None, "digest": None, "observation": None, "artifact_path": None}
    return {
        "present": True,
        "passed": case.passed,
        "digest": _case_digest(case),
        "observation": case.observation,
        "artifact_path": case.artifact_path,
        "category": case.category.value,
        "findings": list(case.findings),
    }


@dataclass(frozen=True)
class ComparisonArtifact:
    case_id: str
    family: str
    configuration: str
    status: Status
    category: str
    coordinates: dict[str, Any]
    branch: dict[str, Any]
    develop: dict[str, Any]
    capability_slice: Optional[CapabilitySlice] = None

    @property
    def branch_digest(self) -> Optional[str]:
        digest = self.branch["digest"]
        return None if digest is None else str(digest)

    @property
    def develop_digest(self) -> Optional[str]:
        digest = self.develop["digest"]
        return None if digest is None else str(digest)

    @property
    def comparison_id(self) -> str:
        return digest_of(
            {
                "case_id": self.case_id,
                "family": self.family,
                "configuration": self.configuration,
                "branch_digest": self.branch_digest,
                "develop_digest": self.develop_digest,
            }
        )[:16]

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": COMPARISON_SCHEMA_VERSION,
            "comparison_id": self.comparison_id,
            "case_id": self.case_id,
            "family": self.family,
            "configuration": self.configuration,
            "category": self.category,
            "status": self.status.value,
            "coordinates": self.coordinates,
            "branch": self.branch,
            "develop": self.develop,
            "capability_slice": None if self.capability_slice is None else self.capability_slice.to_dict(),
        }

    @classmethod
    def from_dict(cls, data: Mapping[str, Any]) -> ComparisonArtifact:
        return cls(
            case_id=str(data["case_id"]),
            family=str(data["family"]),
            configuration=str(data["configuration"]),
            status=Status(str(data["status"])),
            category=str(data["category"]),
            coordinates=dict(data.get("coordinates", {})),
            branch=dict(data["branch"]),
            develop=dict(data["develop"]),
            capability_slice=CapabilitySlice.from_dict(data["capability_slice"])
            if data.get("capability_slice") is not None
            else None,
        )


def _status(branch_case: CaseResult, develop_case: Optional[CaseResult]) -> Status:
    if branch_case.passed:
        return Status.RESOLVED if develop_case is not None and develop_case.failed else Status.PASSING
    if develop_case is None or develop_case.passed:
        return Status.NEW
    if _case_digest(branch_case) == _case_digest(develop_case):
        return Status.UNCHANGED
    return Status.WORSENED


def compare_case(
    branch: Report,
    develop: Report,
    case_id: str,
    configuration: str,
    capabilities: Optional[Mapping[str, Any]] = None,
) -> ComparisonArtifact:
    if branch.family != develop.family:
        raise ReportError(f"branch family {branch.family!r} does not match develop family {develop.family!r}")
    capability_slice = None if capabilities is None else capability_slice_for_family(capabilities, branch.family)
    branch_case = branch.find_case(case_id)
    develop_case = develop.find_case(case_id)
    if branch_case is None:
        if develop_case is None:
            raise KeyError(case_id)
        status = Status.MISSING if develop_case.failed else Status.PASSING
    else:
        status = _status(branch_case, develop_case)
    anchor = branch_case if branch_case is not None else develop_case
    assert anchor is not None
    return ComparisonArtifact(
        case_id=case_id,
        family=branch.family,
        configuration=configuration,
        status=status,
        category=anchor.category.value,
        coordinates=anchor.coordinates,
        branch=_side(branch_case),
        develop=_side(develop_case),
        capability_slice=capability_slice,
    )


def compare_reports(
    branch: Report,
    develop: Report,
    configuration: str,
    capabilities: Optional[Mapping[str, Any]] = None,
) -> list[ComparisonArtifact]:
    """One artifact per branch failure, per develop failure the branch resolved, and per develop failure
    whose case is absent from the branch report (a known mismatch must never vanish without passing)."""
    artifacts = []
    case_ids = sorted({case.case_id for case in branch.cases} | {case.case_id for case in develop.cases})
    for case_id in case_ids:
        artifact = compare_case(branch, develop, case_id, configuration, capabilities)
        if artifact.status is not Status.PASSING:
            artifacts.append(artifact)
    return artifacts


def serialize(artifact: ComparisonArtifact) -> str:
    return json.dumps(artifact.to_dict(), sort_keys=True, indent=2) + "\n"


def serialize_many(artifacts: list[ComparisonArtifact]) -> str:
    payload = {
        "schema_version": COMPARISON_SCHEMA_VERSION,
        "artifacts": [artifact.to_dict() for artifact in artifacts],
    }
    return json.dumps(payload, sort_keys=True, indent=2) + "\n"


def deserialize_many(text: str) -> list[ComparisonArtifact]:
    data = json.loads(text)
    if not schema_version_matches(data.get("schema_version"), COMPARISON_SCHEMA_VERSION):
        raise ReportError("unsupported comparison schema_version")
    return [ComparisonArtifact.from_dict(entry) for entry in data["artifacts"]]
