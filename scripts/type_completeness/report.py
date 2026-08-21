"""Loading of the type-completeness runner's JSON report.

The runner currently emits per-case ``passed``/``status`` plus evidence fields. Until the runner emits a
structured per-case ``category`` (product finding, structural failure, failed witness, stale or resolved
ledger entry), every failed case is treated as a product finding; a ``category`` member is consumed as soon
as the runner writes one.
"""

from __future__ import annotations

import enum
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Optional

SUPPORTED_SCHEMA_VERSION = 1

OBSERVATION_FIELDS = (
    "expected",
    "actual",
    "status",
    "passed",
    "runtime_passed",
    "runtime_status",
    "diagnostics",
    "produced_output",
    "expected_output",
)


class ReportError(ValueError):
    """Raised when a runner report cannot be interpreted."""


class Category(str, enum.Enum):
    PRODUCT_FINDING = "product_finding"
    STRUCTURAL_FAILURE = "structural_failure"
    FAILED_WITNESS = "failed_witness"
    STALE_LEDGER_ENTRY = "stale_ledger_entry"
    RESOLVED_LEDGER_ENTRY = "resolved_ledger_entry"


def _category_for(case: Mapping[str, Any]) -> Category:
    raw = case.get("category")
    if raw is None:
        return Category.PRODUCT_FINDING
    try:
        return Category(str(raw))
    except ValueError as error:
        raise ReportError(f"unknown case category {raw!r} for case {case.get('case_id')!r}") from error


@dataclass(frozen=True)
class CaseResult:
    case_id: str
    passed: bool
    coordinates: dict[str, Any]
    observation: dict[str, Any]
    artifact_path: str
    category: Category
    # The runner's top-level findings for this case, one per independently scoped dimension, sorted by dimension.
    findings: tuple[dict[str, Any], ...] = ()

    @property
    def failed(self) -> bool:
        return not self.passed


@dataclass(frozen=True)
class Report:
    family: str
    success: bool
    cases: tuple[CaseResult, ...]
    raw: dict[str, Any]

    def case(self, case_id: str) -> CaseResult:
        for case in self.cases:
            if case.case_id == case_id:
                return case
        raise KeyError(case_id)

    def find_case(self, case_id: str) -> Optional[CaseResult]:
        try:
            return self.case(case_id)
        except KeyError:
            return None

    def failed_cases(self) -> list[CaseResult]:
        return [case for case in self.cases if case.failed]


def _require(mapping: Mapping[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise ReportError(f"{context} is missing required member {key!r}")
    return mapping[key]


def load_report(data: Mapping[str, Any]) -> Report:
    version = _require(data, "schema_version", "report")
    if isinstance(version, bool) or not isinstance(version, int) or version != SUPPORTED_SCHEMA_VERSION:
        raise ReportError(f"unsupported report schema_version {version!r}; expected {SUPPORTED_SCHEMA_VERSION}")
    family = str(_require(data, "family", "report"))
    raw_cases = _require(data, "cases", "report")
    if not isinstance(raw_cases, list):
        raise ReportError("report member 'cases' must be an array")
    raw_findings = data.get("findings", [])
    if not isinstance(raw_findings, list):
        raise ReportError("report member 'findings' must be an array")
    findings_by_case: dict[str, list[dict[str, Any]]] = {}
    for raw_finding in raw_findings:
        if not isinstance(raw_finding, Mapping):
            raise ReportError("every report finding must be an object")
        finding = dict(raw_finding)
        finding_case_id = str(_require(finding, "case_id", "finding"))
        _require(finding, "dimension", f"finding for case {finding_case_id!r}")
        findings_by_case.setdefault(finding_case_id, []).append(finding)
    cases: list[CaseResult] = []
    seen = set()
    for raw_case in raw_cases:
        if not isinstance(raw_case, Mapping):
            raise ReportError("every report case must be an object")
        case_id = str(_require(raw_case, "case_id", "case"))
        if case_id in seen:
            raise ReportError(f"duplicate case_id {case_id!r} in report")
        seen.add(case_id)
        passed = _require(raw_case, "passed", f"case {case_id!r}")
        if not isinstance(passed, bool):
            raise ReportError(f"case {case_id!r} member 'passed' must be a JSON boolean; got {passed!r}")
        observation = {field: raw_case[field] for field in OBSERVATION_FIELDS if field in raw_case}
        cases.append(
            CaseResult(
                case_id=case_id,
                passed=passed,
                coordinates=dict(raw_case.get("coordinates", {})),
                observation=observation,
                artifact_path=str(raw_case.get("artifact_path", "")),
                category=_category_for(raw_case),
                findings=tuple(
                    sorted(findings_by_case.get(case_id, []), key=lambda finding: str(finding["dimension"]))
                ),
            )
        )
    cases.sort(key=lambda case: case.case_id)
    return Report(family=family, success=bool(data.get("success", False)), cases=tuple(cases), raw=dict(data))


def load_report_file(path: Path) -> Report:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReportError(f"cannot read report {path}: {error}") from error
    if not isinstance(data, dict):
        raise ReportError(f"report {path} must contain a JSON object")
    return load_report(data)


@dataclass(frozen=True)
class CapabilitySlice:
    family: str
    paths: tuple[str, ...]
    broad_core: bool

    def to_dict(self) -> dict[str, Any]:
        return {"family": self.family, "paths": list(self.paths), "broad_core": self.broad_core}


def capability_slice_for_family(manifest: Mapping[str, Any], family: str) -> CapabilitySlice:
    paths: list[str] = []
    for entry in manifest.get("production", []):
        if family in entry.get("families", []):
            for path in entry.get("paths", []):
                if path not in paths:
                    paths.append(path)
    if not paths:
        raise ReportError(f"capabilities manifest does not map family {family!r} to any production path")
    broad_core = family in manifest.get("broad_core_families", [])
    return CapabilitySlice(family=family, paths=tuple(sorted(paths)), broad_core=broad_core)
