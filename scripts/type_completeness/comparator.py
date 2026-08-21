"""Deterministic comparison of a branch report against the matching ``develop`` report."""

from __future__ import annotations

import enum
import hashlib
import json
from dataclasses import dataclass
from typing import Any, Mapping, Optional

from .report import OBSERVATION_FIELDS, CaseResult, Report, ReportError

COMPARISON_SCHEMA_VERSION = 1


class Status(str, enum.Enum):
    NEW = "new"
    WORSENED = "worsened"
    UNCHANGED = "unchanged"
    RESOLVED = "resolved"
    MISSING = "missing"
    PASSING = "passing"


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def digest_of(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def observation_digest(case: Mapping[str, Any]) -> str:
    return digest_of({field: case[field] for field in OBSERVATION_FIELDS if field in case})


def _case_digest(case: CaseResult) -> str:
    return digest_of(case.observation)


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
        )


def _status(branch_case: CaseResult, develop_case: Optional[CaseResult]) -> Status:
    if branch_case.passed:
        return Status.RESOLVED if develop_case is not None and develop_case.failed else Status.PASSING
    if develop_case is None or develop_case.passed:
        return Status.NEW
    if _case_digest(branch_case) == _case_digest(develop_case):
        return Status.UNCHANGED
    return Status.WORSENED


def compare_case(branch: Report, develop: Report, case_id: str, configuration: str) -> ComparisonArtifact:
    if branch.family != develop.family:
        raise ReportError(f"branch family {branch.family!r} does not match develop family {develop.family!r}")
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
    )


def compare_reports(branch: Report, develop: Report, configuration: str) -> list[ComparisonArtifact]:
    """One artifact per branch failure, per develop failure the branch resolved, and per develop failure
    whose case is absent from the branch report (a known mismatch must never vanish without passing)."""
    artifacts = []
    case_ids = sorted({case.case_id for case in branch.cases} | {case.case_id for case in develop.cases})
    for case_id in case_ids:
        artifact = compare_case(branch, develop, case_id, configuration)
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
    if int(data.get("schema_version", 0)) != COMPARISON_SCHEMA_VERSION:
        raise ReportError("unsupported comparison schema_version")
    return [ComparisonArtifact.from_dict(entry) for entry in data["artifacts"]]
