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
    Category,
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
    VANISHED = "vanished"
    PASSING = "passing"
    # One of the two builds made no judgment about the case at all, so there is nothing to compare. It is
    # neither a regression nor progress: reading a build's own limits as a verdict about the product is
    # exactly what the not-covered status exists to prevent.
    NOT_COVERED = "not_covered"


# A develop case that vanishes from the branch report is a regression whether it failed (missing) or passed
# (vanished: lost coverage) on develop; neither counts as progress.
REGRESSION_STATUSES = (Status.NEW, Status.WORSENED, Status.MISSING, Status.VANISHED)
# Statuses meaning the case no longer fails on the branch; reconciliation uses this set so it cannot drift.
NO_LONGER_FAILING_STATUSES = (Status.RESOLVED, Status.PASSING)
# Statuses with a branch failure to file a ledger proposal for; every other status has nothing to propose.
PROPOSABLE_STATUSES = (Status.NEW, Status.WORSENED, Status.UNCHANGED)
# A known develop mismatch reproduced unchanged: not a regression, not progress, but proposable.
KNOWN_BASELINE_STATUSES = (Status.UNCHANGED,)
# Statuses that report the absence of a comparison rather than its outcome. Nothing may be concluded about
# the product from one, so they belong to no other partition.
NOT_COMPARABLE_STATUSES = (Status.NOT_COVERED,)


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
    # dimension/expected/actual are required by the report loader; parity_evidence is only written for parity
    # findings, so its absence is itself part of the finding's semantic identity and digests as None.
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
        """Integrity digest binding every consumed identity field to the evidence digests."""
        return digest_of(
            {
                "case_id": self.case_id,
                "family": self.family,
                "configuration": self.configuration,
                "category": self.category,
                "capability_slice": None if self.capability_slice is None else self.capability_slice.to_dict(),
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

    def validate(self) -> None:
        """A deserialized artifact must carry digests that match its evidence and a status the classifier
        re-derives from its two sides; a relabelled or tampered artifact is rejected."""
        _check_side(self.branch, "branch", self.case_id)
        _check_side(self.develop, "develop", self.case_id)
        for name, side in (("branch", self.branch), ("develop", self.develop)):
            for finding in side.get("findings") or []:
                _check_finding_identity(finding, self.case_id, self.family, name)
        if self.capability_slice is not None and self.capability_slice.family != self.family:
            raise ReportError(f"artifact for case {self.case_id!r} carries a capability slice for another family")
        derived = classify(self.branch, self.develop)
        if derived is not self.status:
            raise ReportError(
                f"artifact for case {self.case_id!r} is labelled {self.status.value!r} but its sides classify as "
                f"{derived.value!r}"
            )

    @classmethod
    def from_dict(cls, data: Mapping[str, Any]) -> ComparisonArtifact:
        try:
            status = Status(str(data["status"]))
        except ValueError as error:
            raise ReportError(f"unknown comparison status {data.get('status')!r}") from error
        artifact = cls(
            case_id=str(data["case_id"]),
            family=str(data["family"]),
            configuration=str(data["configuration"]),
            status=status,
            category=str(data["category"]),
            coordinates=dict(data["coordinates"]),
            branch=dict(data["branch"]),
            develop=dict(data["develop"]),
            capability_slice=CapabilitySlice.from_dict(data["capability_slice"])
            if data.get("capability_slice") is not None
            else None,
        )
        artifact.validate()
        if str(data.get("comparison_id")) != artifact.comparison_id:
            raise ReportError(
                f"artifact for case {artifact.case_id!r} has a comparison_id that does not match its fields"
            )
        return artifact


def _made_no_judgment(side: Mapping[str, Any]) -> bool:
    return bool(side.get("present")) and side.get("category") == Category.NOT_COVERED.value


def classify(branch: Mapping[str, Any], develop: Mapping[str, Any]) -> Status:
    """The one status classifier, defined over the serialized sides so a deserialized artifact re-derives its
    status from exactly the data it carries."""
    if not branch.get("present") and not develop.get("present"):
        raise ReportError("a comparison artifact needs at least one side")
    # A side that made no judgment carries `passed: false` because it did not pass, not because it failed.
    # Reading that as a failure would turn one build's missing surface into a regression of the product.
    if _made_no_judgment(branch) or _made_no_judgment(develop):
        return Status.NOT_COVERED
    if not branch.get("present"):
        return Status.VANISHED if develop.get("passed") else Status.MISSING
    if branch.get("passed"):
        return Status.RESOLVED if develop.get("present") and not develop.get("passed") else Status.PASSING
    if not develop.get("present") or develop.get("passed"):
        return Status.NEW
    if branch.get("digest") == develop.get("digest"):
        return Status.UNCHANGED
    return Status.WORSENED


def _check_finding_identity(finding: Mapping[str, Any], case_id: str, family: str, side: str) -> None:
    """A finding's ID is a pure function of the case it was filed under and its dimension (make_finding_id in
    the runner); a migrated finding echoes the historical entry, so it is filed under migrated_from."""
    from .ledger import runner_finding_id

    if finding.get("case_id") != case_id or finding.get("family") != family:
        raise ReportError(f"{side} finding on artifact for case {case_id!r} names a different case or family")
    dimension = finding.get("dimension")
    if not isinstance(dimension, str) or not dimension:
        raise ReportError(f"{side} finding on artifact for case {case_id!r} has no dimension")
    filed_under = str(finding.get("migrated_from") or case_id)
    expected_id = runner_finding_id(filed_under, dimension)
    if finding.get("finding_id") != expected_id:
        raise ReportError(
            f"{side} finding on artifact for case {case_id!r} has finding_id {finding.get('finding_id')!r}; "
            f"the runner formula gives {expected_id!r} for {filed_under!r}/{dimension!r}"
        )


def _check_side(side: Mapping[str, Any], name: str, case_id: str) -> None:
    if not side.get("present"):
        return
    if not isinstance(side.get("passed"), bool) or not isinstance(side.get("findings"), list):
        raise ReportError(f"artifact for case {case_id!r} has a malformed {name} side")
    expected = digest_of(
        {"observation": side.get("observation"), "findings": [_finding_digest_view(f) for f in side["findings"]]}
    )
    if side.get("digest") != expected:
        raise ReportError(f"artifact for case {case_id!r} has a {name} digest that does not match its evidence")


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
    if branch_case is None and develop_case is None:
        raise KeyError(case_id)
    anchor = branch_case if branch_case is not None else develop_case
    assert anchor is not None
    branch_side, develop_side = _side(branch_case), _side(develop_case)
    return ComparisonArtifact(
        case_id=case_id,
        family=branch.family,
        configuration=configuration,
        status=classify(branch_side, develop_side),
        category=anchor.category.value,
        coordinates=anchor.coordinates,
        branch=branch_side,
        develop=develop_side,
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


def serialize_structural_failure(reasons: list[str]) -> str:
    """A comparison that refuses to draw a verdict, in the same envelope a real comparison uses.

    ``artifacts`` is empty on purpose: there is no case-level conclusion to publish, and a consumer
    that reads only the artifact list therefore sees nothing it could mistake for "unchanged".
    """
    payload = {
        "schema_version": COMPARISON_SCHEMA_VERSION,
        "artifacts": [],
        "structural_failure": {"reasons": list(reasons)},
    }
    return json.dumps(payload, sort_keys=True, indent=2) + "\n"


def deserialize_many(text: str) -> list[ComparisonArtifact]:
    data = json.loads(text)
    if not schema_version_matches(data.get("schema_version"), COMPARISON_SCHEMA_VERSION):
        raise ReportError("unsupported comparison schema_version")
    if "structural_failure" in data:
        # A refusal carries no conclusions; reading it as an empty, clean comparison is exactly the
        # mistake the refusal exists to prevent.
        raise ReportError(f"comparison refused to draw a verdict: {data['structural_failure']}")
    try:
        return [ComparisonArtifact.from_dict(entry) for entry in data["artifacts"]]
    except (KeyError, TypeError) as error:
        raise ReportError(f"malformed comparison artifact: {error!r}") from error
