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


# Every member the runner writes per case (case_report block) and per finding (finding_report); a report that
# lacks one is malformed rather than a report with defaults.
CASE_REQUIRED_FIELDS = OBSERVATION_FIELDS + ("coordinates", "artifact_path")
FINDING_REQUIRED_FIELDS = ("finding_id", "case_id", "family", "dimension", "expected", "actual", "classification")
# Members the runner writes to describe the run or the build rather than the product it observed. They vary
# between two runs that saw exactly the same thing - wall-clock timings, and the surfaces the binary was built
# with - so they are dropped at load and can never reach a digest, a comparison, or a fixture. The runner owns
# the same list in FSCompletenessRunner::non_evidence_report_members.
NON_EVIDENCE_MEMBERS = ("timings_ms", "configuration")

# The verdicts the runner writes at the top level of a report. A run that is not "passed" published
# evidence that no comparison may be drawn from: the run either found a product mismatch or broke
# before it could finish, and in both cases a clean comparison verdict would be a lie.
KNOWN_OUTCOMES = ("passed", "product_mismatch", "structural_failure")

KNOWN_CLASSIFICATIONS = (
    "unclassified",
    "product_defect",
    "specification_defect",
    "harness_defect",
    "intentional_unsupported",
    "duplicate",
)


@dataclass(frozen=True)
class Configuration:
    """What the binary that produced a report could have observed at all.

    A surface that is not compiled into the build is a property of the build, not of the product, so a
    consumer reads it from here rather than inferring it from cases the report does not carry.
    """

    tools_enabled: bool
    adapters: tuple[str, ...]


class ReportError(ValueError):
    """Raised when a runner report cannot be interpreted."""


def is_integral_number(value: Any) -> bool:
    """True for a JSON number with no fractional part.

    The runner stores every count and version as a Variant FLOAT and the engine JSON writer renders it as
    ``1.0``, so an integral float is the normal on-disk form; booleans and strings are never accepted.
    """
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return True
    return isinstance(value, float) and value.is_integer()


def schema_version_matches(value: Any, expected: int) -> bool:
    return is_integral_number(value) and int(value) == expected


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
    outcome: str
    cases: tuple[CaseResult, ...]
    configuration: Configuration
    raw: dict[str, Any]

    @property
    def is_clean(self) -> bool:
        """True for a run that finished with nothing to report."""
        return self.outcome == "passed"

    @property
    def is_structural_failure(self) -> bool:
        """True when the run itself failed rather than the product it measured.

        Nothing may be concluded from such a report. Its per-case verdicts can all read "passed"
        while the run as a whole did not - a run that crossed its budget while publishing is
        republished exactly that way - so a consumer that looks only at the cases would draw a clean
        verdict from a run that never finished. A product mismatch is not in this category:
        classifying those is what a comparison is for.
        """
        return self.outcome == "structural_failure"

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


def _load_configuration(data: Mapping[str, Any]) -> Configuration:
    raw = _require(data, "configuration", "report")
    if not isinstance(raw, Mapping):
        raise ReportError(f"report member 'configuration' must be an object; got {raw!r}")
    tools_enabled = _require(raw, "tools_enabled", "report configuration")
    if not isinstance(tools_enabled, bool):
        raise ReportError(f"report configuration member 'tools_enabled' must be a JSON boolean; got {tools_enabled!r}")
    adapters = _require(raw, "adapters", "report configuration")
    if not isinstance(adapters, list) or any(not isinstance(adapter, str) or not adapter for adapter in adapters):
        raise ReportError("report configuration member 'adapters' must be an array of adapter id strings")
    if list(adapters) != sorted(adapters) or len(set(adapters)) != len(adapters):
        raise ReportError(f"report configuration member 'adapters' must be sorted and unique; got {adapters!r}")
    return Configuration(tools_enabled=tools_enabled, adapters=tuple(adapters))


def _require(mapping: Mapping[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise ReportError(f"{context} is missing required member {key!r}")
    return mapping[key]


def load_report(data: Mapping[str, Any]) -> Report:
    version = _require(data, "schema_version", "report")
    if not schema_version_matches(version, SUPPORTED_SCHEMA_VERSION):
        raise ReportError(f"unsupported report schema_version {version!r}; expected {SUPPORTED_SCHEMA_VERSION}")
    family = str(_require(data, "family", "report"))
    configuration = _load_configuration(data)
    raw_cases = _require(data, "cases", "report")
    if not isinstance(raw_cases, list):
        raise ReportError("report member 'cases' must be an array")
    success = _require(data, "success", "report")
    if not isinstance(success, bool):
        raise ReportError(f"report member 'success' must be a JSON boolean; got {success!r}")
    outcome = _require(data, "outcome", "report")
    if not isinstance(outcome, str) or outcome not in KNOWN_OUTCOMES:
        raise ReportError(f"report member 'outcome' must be one of {KNOWN_OUTCOMES}; got {outcome!r}")
    # The runner derives one from the other, so a document where they disagree was not written by a
    # run: taking either at face value would mean trusting a report nobody produced.
    if success != (outcome == "passed"):
        raise ReportError(f"report members disagree: success={success!r} with outcome {outcome!r}")
    raw_findings = _require(data, "findings", "report")
    if not isinstance(raw_findings, list):
        raise ReportError("report member 'findings' must be an array")
    findings_by_case: dict[str, list[dict[str, Any]]] = {}
    for raw_finding in raw_findings:
        if not isinstance(raw_finding, Mapping):
            raise ReportError("every report finding must be an object")
        finding = dict(raw_finding)
        finding_case_id = str(_require(finding, "case_id", "finding"))
        context = f"finding for case {finding_case_id!r}"
        for field in FINDING_REQUIRED_FIELDS:
            _require(finding, field, context)
        if finding["classification"] not in KNOWN_CLASSIFICATIONS:
            raise ReportError(f"{context} has unknown classification {finding['classification']!r}")
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
        context = f"case {case_id!r}"
        for field in CASE_REQUIRED_FIELDS:
            _require(raw_case, field, context)
        observation = {field: raw_case[field] for field in OBSERVATION_FIELDS}
        cases.append(
            CaseResult(
                case_id=case_id,
                passed=passed,
                coordinates=dict(raw_case["coordinates"]),
                observation=observation,
                artifact_path=str(raw_case["artifact_path"]),
                category=_category_for(raw_case),
                findings=tuple(
                    sorted(findings_by_case.get(case_id, []), key=lambda finding: str(finding["dimension"]))
                ),
            )
        )
    cases.sort(key=lambda case: case.case_id)
    orphans = sorted(set(findings_by_case) - seen)
    if orphans:
        raise ReportError(f"report findings target cases it does not report: {orphans}")
    # The runner marks a case failed whenever a finding targets it, so a finding on a passed case is contradictory.
    contradictory = [case.case_id for case in cases if case.passed and case.findings]
    if contradictory:
        raise ReportError(f"report marks cases passed although findings target them: {contradictory}")
    evidence = {member: value for member, value in data.items() if member not in NON_EVIDENCE_MEMBERS}
    return Report(
        family=family,
        success=success,
        outcome=outcome,
        cases=tuple(cases),
        configuration=configuration,
        raw=evidence,
    )


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

    @classmethod
    def from_dict(cls, data: Mapping[str, Any]) -> CapabilitySlice:
        paths = data.get("paths")
        if not isinstance(paths, list) or not paths or any(not isinstance(path, str) or not path for path in paths):
            raise ReportError("capability_slice.paths must be a non-empty array of path strings")
        broad_core = data.get("broad_core")
        if "family" not in data or not isinstance(broad_core, bool):
            raise ReportError("capability_slice must carry 'family' and a boolean 'broad_core'")
        return cls(family=str(data["family"]), paths=tuple(paths), broad_core=broad_core)


DEFAULT_CAPABILITIES_PATH = (
    Path(__file__).resolve().parents[2]
    / "modules"
    / "foundry_script"
    / "tests"
    / "type_completeness"
    / "capabilities.json"
)


def load_capabilities_file(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ReportError(f"cannot read capabilities manifest {path}: {error}") from error
    if not isinstance(data, dict):
        raise ReportError(f"capabilities manifest {path} must contain a JSON object")
    return data


def capability_slice_for_family(manifest: Mapping[str, Any], family: str) -> CapabilitySlice:
    production = _require(manifest, "production", "capabilities manifest")
    broad_core_families = _require(manifest, "broad_core_families", "capabilities manifest")
    if not isinstance(production, list) or not isinstance(broad_core_families, list):
        raise ReportError("capabilities manifest 'production' and 'broad_core_families' must be arrays")
    paths: list[str] = []
    for entry in production:
        if not isinstance(entry, Mapping):
            raise ReportError("every capabilities manifest production entry must be an object")
        entry_paths = _require(entry, "paths", "capabilities manifest production entry")
        entry_families = _require(entry, "families", "capabilities manifest production entry")
        if not isinstance(entry_paths, list) or not isinstance(entry_families, list):
            raise ReportError("capabilities manifest production entry 'paths' and 'families' must be arrays")
        if family in entry_families:
            for path in entry_paths:
                if not isinstance(path, str) or not path:
                    raise ReportError("capabilities manifest paths must be non-empty strings")
                if path not in paths:
                    paths.append(path)
    if not paths:
        raise ReportError(f"capabilities manifest does not map family {family!r} to any production path")
    broad_core = family in broad_core_families
    return CapabilitySlice(family=family, paths=tuple(sorted(paths)), broad_core=broad_core)
