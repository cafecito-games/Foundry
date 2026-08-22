"""Mutation campaigns over the type-completeness matrix.

A mutation recipe is a unified diff against production code plus the matrix cells that must fail once
the diff is applied. ``run`` applies one recipe in a disposable worktree, builds it once, executes the
family through ``foundry test completeness run`` and classifies the published report; ``validate``
checks a catalog; ``summarize`` folds result files into one document. Every terminal outcome is a
member of :class:`Outcome`, and every outcome owns a distinct exit code.

Stdlib only, Python 3.8.
"""

from __future__ import annotations

import argparse
import enum
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Optional, Sequence

from .report import (
    Report,
    ReportError,
    capability_slice_for_family,
    load_capabilities_file,
    load_report_file,
)

RECIPE_SCHEMA_VERSION = 1
SHARDS_SCHEMA_VERSION = 1
RESULT_SCHEMA_VERSION = 1
RESULT_FILE_NAME = "mutation_result.json"

DEFAULT_BUDGET_SECONDS = 1800
HARD_CAP_BUDGET_SECONDS = 2400

# Dimensions the runner judges for every case in addition to the family's own dimension ids.
RUNNER_BUILTIN_DIMENSIONS = ("output", "diagnostics", "runtime_status", "text_bytecode_parity", "diagnostic_severity")

# Exit code of an invocation that could not start: a malformed recipe, catalog, shard list, or
# argument. It is deliberately not an Outcome code so a gate never reads a misinvocation as a verdict.
EXIT_INVALID_INPUT = 2

# ^[a-z0-9_]{1,64}$; an id is never joined into a path before it matches.
ID_PATTERN = re.compile(r"^[a-z0-9_]{1,64}$")
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")


class MutationError(ValueError):
    """Raised for any input the runner refuses."""


class BuildFailed(RuntimeError):
    """Raised by a toolchain when the build of the mutated worktree fails."""


class Outcome(str, enum.Enum):
    DETECTED = "detected"
    MISSED = "missed"
    RECIPE_STALE = "recipe_stale"
    BUILD_FAILED = "build_failed"
    STRUCTURAL_FAILURE = "structural_failure"
    TIMEOUT = "timeout"
    # The unpatched baseline already fails a detector's case, so a failure after the patch proves
    # nothing about the mutation.
    BASELINE_FAILED = "baseline_failed"
    # A summary over zero result files: every recipe invocation died before writing a result. Only a
    # summary may carry it; a recipe result claiming it is rejected.
    NO_RESULTS = "no_results"


_EXIT_CODES = {
    Outcome.DETECTED: 0,
    Outcome.MISSED: 1,
    Outcome.TIMEOUT: 3,
    Outcome.RECIPE_STALE: 4,
    Outcome.BUILD_FAILED: 5,
    Outcome.STRUCTURAL_FAILURE: 6,
    Outcome.BASELINE_FAILED: 7,
    Outcome.NO_RESULTS: 8,
}

# Outcomes a recipe result file may carry; the rest belong to the summary alone.
RECIPE_OUTCOMES = tuple(outcome for outcome in Outcome if outcome is not Outcome.NO_RESULTS)


def exit_code_for(outcome: Outcome) -> int:
    return _EXIT_CODES[outcome]


def parse_outcome(value: Any, context: str) -> Outcome:
    try:
        return Outcome(str(value))
    except ValueError as error:
        raise MutationError(
            f"{context}: unknown outcome {value!r}; expected one of {[o.value for o in Outcome]}"
        ) from error


class DetectorState(str, enum.Enum):
    FAILED_ON_DIMENSION = "failed_on_dimension"
    PASSED = "passed"
    ABSENT = "absent"
    FAILED_ON_OTHER_DIMENSION = "failed_on_other_dimension"
    # The baseline run did not pass this case, so the mutated observation cannot be attributed.
    BASELINE_FAILED = "baseline_failed"


def state_detects(state: DetectorState) -> bool:
    return state is DetectorState.FAILED_ON_DIMENSION


def require_id(value: Any, context: str) -> str:
    if not isinstance(value, str) or ID_PATTERN.match(value) is None:
        raise MutationError(f"{context} must match {ID_PATTERN.pattern}; got {value!r}")
    return value


def _require(mapping: Mapping[str, Any], key: str, context: str) -> Any:
    if not isinstance(mapping, Mapping) or key not in mapping:
        raise MutationError(f"{context} is missing required member {key!r}")
    return mapping[key]


def _require_json_integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise MutationError(f"{context} must be a JSON integer; got {value!r}")
    return int(value)


def _require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise MutationError(f"{context} must be a non-empty string; got {value!r}")
    return value


def _read_json_object(path: Path, context: str) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise MutationError(f"cannot read {context} {path}: {error}") from error
    if not isinstance(data, dict):
        raise MutationError(f"{context} {path} must contain a JSON object")
    return data


@dataclass(frozen=True)
class ExpectedDetector:
    family: str
    case_coordinates: dict[str, Any]
    dimension: str


@dataclass(frozen=True)
class Recipe:
    recipe_id: str
    description: str
    escape_class: str
    expected_detectors: tuple[ExpectedDetector, ...]

    @property
    def families(self) -> tuple[str, ...]:
        seen: list[str] = []
        for detector in self.expected_detectors:
            if detector.family not in seen:
                seen.append(detector.family)
        return tuple(seen)


@dataclass(frozen=True)
class Shard:
    shard_id: str
    recipes: tuple[str, ...]
    budget_seconds: int


def load_recipe(data: Mapping[str, Any], expected_id: str) -> Recipe:
    version = _require(data, "schema_version", "recipe")
    if _require_json_integer(version, "recipe schema_version") != RECIPE_SCHEMA_VERSION:
        raise MutationError(f"unsupported recipe schema_version {version!r}; expected {RECIPE_SCHEMA_VERSION}")
    recipe_id = require_id(_require(data, "recipe_id", "recipe"), "recipe_id")
    if recipe_id != expected_id:
        raise MutationError(f"recipe_id {recipe_id!r} does not match its file name {expected_id!r}")
    description = _require_string(_require(data, "description", "recipe"), "recipe description")
    escape_class = require_id(_require(data, "escape_class", "recipe"), "recipe escape_class")
    raw_detectors = _require(data, "expected_detectors", "recipe")
    if not isinstance(raw_detectors, list) or not raw_detectors:
        raise MutationError("recipe expected_detectors must be a non-empty array")
    detectors: list[ExpectedDetector] = []
    for index, raw in enumerate(raw_detectors):
        context = f"expected_detectors[{index}]"
        if not isinstance(raw, Mapping):
            raise MutationError(f"{context} must be an object")
        unknown = sorted(set(raw) - {"family", "case_coordinates", "dimension"})
        if unknown:
            raise MutationError(f"{context} has unknown members {unknown}")
        family = require_id(_require(raw, "family", context), f"{context}.family")
        coordinates = _require(raw, "case_coordinates", context)
        if not isinstance(coordinates, Mapping) or not coordinates:
            raise MutationError(f"{context}.case_coordinates must be a non-empty object")
        for axis, leaf in coordinates.items():
            _require_string(leaf, f"{context}.case_coordinates.{axis}")
        dimension = require_id(_require(raw, "dimension", context), f"{context}.dimension")
        detectors.append(ExpectedDetector(family, dict(coordinates), dimension))
    return Recipe(recipe_id, description, escape_class, tuple(detectors))


def load_recipe_file(path: Path) -> Recipe:
    if path.suffix != ".json":
        raise MutationError(f"recipe {path} must be a .json file")
    expected_id = require_id(path.stem, f"recipe file name {path.name}")
    return load_recipe(_read_json_object(path, "recipe"), expected_id)


def patch_path_for(catalog: Path, recipe_id: str) -> Path:
    return catalog / "patches" / f"{require_id(recipe_id, 'recipe_id')}.patch"


def load_shards(data: Mapping[str, Any], catalog: Path) -> list[Shard]:
    version = _require(data, "schema_version", "shards")
    if _require_json_integer(version, "shards schema_version") != SHARDS_SCHEMA_VERSION:
        raise MutationError(f"unsupported shards schema_version {version!r}; expected {SHARDS_SCHEMA_VERSION}")
    raw_shards = _require(data, "shards", "shards")
    if not isinstance(raw_shards, list) or not raw_shards:
        raise MutationError("shards must be a non-empty array")
    shards: list[Shard] = []
    seen_ids: set[str] = set()
    for index, raw in enumerate(raw_shards):
        context = f"shards[{index}]"
        if not isinstance(raw, Mapping):
            raise MutationError(f"{context} must be an object")
        shard_id = require_id(_require(raw, "shard_id", context), f"{context}.shard_id")
        if shard_id in seen_ids:
            raise MutationError(f"duplicate shard_id {shard_id!r}")
        seen_ids.add(shard_id)
        recipes = _require(raw, "recipes", context)
        if not isinstance(recipes, list) or not recipes:
            raise MutationError(f"{context}.recipes must be a non-empty array")
        recipe_ids = tuple(require_id(recipe, f"{context}.recipes") for recipe in recipes)
        if len(set(recipe_ids)) != len(recipe_ids):
            raise MutationError(f"{context}.recipes repeats a recipe")
        for recipe_id in recipe_ids:
            if not (catalog / f"{recipe_id}.json").is_file():
                raise MutationError(f"{context} names recipe {recipe_id!r} which is not in {catalog}")
        budget = _require_json_integer(_require(raw, "budget_seconds", context), f"{context}.budget_seconds")
        if budget <= 0 or budget > DEFAULT_BUDGET_SECONDS:
            raise MutationError(f"{context}.budget_seconds must be within 1..{DEFAULT_BUDGET_SECONDS}; got {budget}")
        shards.append(Shard(shard_id, recipe_ids, budget))
    return shards


def load_shards_file(path: Path, catalog: Path) -> list[Shard]:
    return load_shards(_read_json_object(path, "shards"), catalog)


@dataclass(frozen=True)
class FamilyVocabulary:
    axes: dict[str, tuple[str, ...]]
    dimensions: tuple[str, ...]


def load_family_vocabulary(rules_root: Path, family: str) -> FamilyVocabulary:
    """The domain axes of a family and every dimension a detector may name, read through the tracked data.

    Only the members this runner needs are read, so a manifest schema change that keeps the domain
    and dimension shapes stays consumable.
    """
    manifest_path = rules_root / "rules" / f"{require_id(family, 'family')}.json"
    if not manifest_path.is_file():
        raise MutationError(f"unknown family {family!r}: {manifest_path} does not exist")
    manifest = _read_json_object(manifest_path, "rule manifest")
    domain = _require(manifest, "domain", "rule manifest")
    if not isinstance(domain, Mapping) or not domain:
        raise MutationError(f"rule manifest {manifest_path} domain must be a non-empty object")
    axes: dict[str, tuple[str, ...]] = {}
    for axis, leaves in domain.items():
        if not isinstance(leaves, list) or not leaves or any(not isinstance(leaf, str) for leaf in leaves):
            raise MutationError(f"rule manifest {manifest_path} domain axis {axis!r} must list leaf strings")
        axes[str(axis)] = tuple(leaves)
    dimensions: list[str] = list(RUNNER_BUILTIN_DIMENSIONS)
    dimensions_root = rules_root / "dimensions"
    for dimension_file in sorted(dimensions_root.glob("*.json")):
        document = _read_json_object(dimension_file, "dimension manifest")
        declared = _require(document, "dimensions", "dimension manifest")
        if not isinstance(declared, list):
            raise MutationError(f"dimension manifest {dimension_file} dimensions must be an array")
        for entry in declared:
            if not isinstance(entry, Mapping):
                raise MutationError(f"dimension manifest {dimension_file} entries must be objects")
            dimensions.append(_require_string(_require(entry, "id", "dimension"), "dimension id"))
    return FamilyVocabulary(axes, tuple(dimensions))


def validate_recipe_against_rules(recipe: Recipe, rules_root: Path) -> None:
    vocabularies: dict[str, FamilyVocabulary] = {}
    for index, detector in enumerate(recipe.expected_detectors):
        context = f"recipe {recipe.recipe_id} expected_detectors[{index}]"
        if detector.family not in vocabularies:
            vocabularies[detector.family] = load_family_vocabulary(rules_root, detector.family)
        vocabulary = vocabularies[detector.family]
        expected_axes = set(vocabulary.axes)
        actual_axes = set(detector.case_coordinates)
        if expected_axes != actual_axes:
            raise MutationError(
                f"{context}.case_coordinates must name exactly the axes {sorted(expected_axes)}; got {sorted(actual_axes)}"
            )
        for axis, leaf in detector.case_coordinates.items():
            if leaf not in vocabulary.axes[axis]:
                raise MutationError(f"{context}.case_coordinates.{axis} has unknown leaf {leaf!r}")
        # Whether the named cell itself carries the dimension is resolved through the graph by the
        # C++ validator (test_type_completeness_history.h); Python holds no graph and does not re-derive it.
        if detector.dimension not in vocabulary.dimensions:
            raise MutationError(f"{context}.dimension {detector.dimension!r} is not a known dimension")


def validate_catalog(catalog: Path, rules_root: Path) -> list[Recipe]:
    if not catalog.is_dir():
        raise MutationError(f"mutation catalog {catalog} is not a directory")
    recipe_paths = sorted(path for path in catalog.glob("*.json") if path.name != "shards.json")
    if not recipe_paths:
        raise MutationError(f"mutation catalog {catalog} holds no recipe")
    recipes: list[Recipe] = []
    for path in recipe_paths:
        recipe = load_recipe_file(path)
        validate_recipe_against_rules(recipe, rules_root)
        patch = patch_path_for(catalog, recipe.recipe_id)
        if not patch.is_file():
            raise MutationError(f"recipe {recipe.recipe_id} has no patch at {patch}")
        recipes.append(recipe)
    shards = load_shards_file(catalog / "shards.json", catalog)
    scheduled = {recipe_id for shard in shards for recipe_id in shard.recipes}
    unscheduled = sorted(recipe.recipe_id for recipe in recipes if recipe.recipe_id not in scheduled)
    if unscheduled:
        raise MutationError(f"recipes are not scheduled by any shard: {unscheduled}")
    return recipes


@dataclass(frozen=True)
class DetectorResult:
    family: str
    case_id: str
    dimension: str
    state: DetectorState

    def to_dict(self) -> dict[str, Any]:
        return {
            "family": self.family,
            "case_id": self.case_id,
            "dimension": self.dimension,
            "state": self.state.value,
        }


def _cases_at(loaded: Report, coordinates: Mapping[str, Any]) -> list[Any]:
    return [case for case in loaded.cases if case.coordinates == dict(coordinates)]


def classify_detection(loaded: Report, recipe: Recipe, baseline: Report) -> tuple[Outcome, list[DetectorResult]]:
    """The single detector verdict.

    Every expected detector must pass on the unpatched baseline and fail on its named dimension once
    the patch is applied. A baseline that does not pass a detector's case makes the mutated
    observation unattributable: that is ``baseline_failed``, never ``detected``.
    """
    for document in (baseline, loaded):
        if document.is_structural_failure or document.raw.get("structural_failures"):
            return Outcome.STRUCTURAL_FAILURE, []
    results: list[DetectorResult] = []
    for detector in recipe.expected_detectors:
        matches = _cases_at(loaded, detector.case_coordinates) if loaded.family == detector.family else []
        baseline_matches = _cases_at(baseline, detector.case_coordinates) if baseline.family == detector.family else []
        if len(baseline_matches) != 1 or not baseline_matches[0].passed:
            case_id = baseline_matches[0].case_id if len(baseline_matches) == 1 else ""
            results.append(DetectorResult(detector.family, case_id, detector.dimension, DetectorState.BASELINE_FAILED))
            continue
        if len(matches) != 1:
            results.append(DetectorResult(detector.family, "", detector.dimension, DetectorState.ABSENT))
            continue
        case = matches[0]
        if case.passed:
            state = DetectorState.PASSED
        elif any(str(finding["dimension"]) == detector.dimension for finding in case.findings):
            state = DetectorState.FAILED_ON_DIMENSION
        else:
            state = DetectorState.FAILED_ON_OTHER_DIMENSION
        results.append(DetectorResult(detector.family, case.case_id, detector.dimension, state))
    if any(result.state is DetectorState.BASELINE_FAILED for result in results):
        return Outcome.BASELINE_FAILED, results
    outcome = Outcome.DETECTED if results and all(state_detects(r.state) for r in results) else Outcome.MISSED
    return outcome, results


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


RESULT_DIGEST_FIELDS = ("recipe_id", "develop_commit", "outcome", "detectors")


def result_digest(result: Mapping[str, Any]) -> str:
    return hashlib.sha256(
        canonical_json({field: result[field] for field in RESULT_DIGEST_FIELDS}).encode("utf-8")
    ).hexdigest()


def build_result(
    *,
    recipe_id: str,
    shard_id: str,
    outcome: Outcome,
    develop_commit: str,
    seed: int,
    capability_slice: Mapping[str, Any],
    report_path: str,
    detectors: Sequence[DetectorResult],
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "recipe_id": recipe_id,
        "shard_id": shard_id,
        "outcome": outcome.value,
        "develop_commit": develop_commit,
        "seed": seed,
        "capability_slice": dict(capability_slice),
        "report_path": report_path,
        "detectors": [detector.to_dict() for detector in detectors],
    }
    result["digest"] = result_digest(result)
    return result


def render_result(result: Mapping[str, Any]) -> str:
    return json.dumps(result, sort_keys=True, indent=2, ensure_ascii=False) + "\n"


def load_result(data: Mapping[str, Any], context: str) -> dict[str, Any]:
    version = _require(data, "schema_version", context)
    if _require_json_integer(version, f"{context} schema_version") != RESULT_SCHEMA_VERSION:
        raise MutationError(f"{context}: unsupported result schema_version {version!r}")
    for field in (
        "recipe_id",
        "shard_id",
        "outcome",
        "develop_commit",
        "seed",
        "capability_slice",
        "report_path",
        "detectors",
        "digest",
    ):
        _require(data, field, context)
    require_id(data["recipe_id"], f"{context} recipe_id")
    require_id(data["shard_id"], f"{context} shard_id")
    outcome = parse_outcome(data["outcome"], context)
    if outcome not in RECIPE_OUTCOMES:
        raise MutationError(f"{context}: outcome {outcome.value!r} is a summary outcome, not a recipe result")
    if not isinstance(data["develop_commit"], str) or COMMIT_PATTERN.match(data["develop_commit"]) is None:
        raise MutationError(f"{context}: develop_commit must be a 40-hex commit")
    detectors = data["detectors"]
    if not isinstance(detectors, list):
        raise MutationError(f"{context}: detectors must be an array")
    for index, detector in enumerate(detectors):
        if not isinstance(detector, Mapping):
            raise MutationError(f"{context}: detectors[{index}] must be an object")
        try:
            DetectorState(str(_require(detector, "state", f"{context} detectors[{index}]")))
        except ValueError as error:
            raise MutationError(f"{context}: detectors[{index}] has unknown state {detector.get('state')!r}") from error
    if outcome is Outcome.DETECTED and not detectors:
        raise MutationError(f"{context}: a detected result must name at least one detector")
    if result_digest(data) != data["digest"]:
        raise MutationError(f"{context}: digest does not match the result's content")
    return dict(data)


def summarize(result_roots: Iterable[Path]) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    for root in result_roots:
        # The run step writes results/<recipe_id>/mutation_result.json; that is the one layout read.
        paths = [root] if root.is_file() else sorted(root.glob(f"*/{RESULT_FILE_NAME}"))
        for path in paths:
            results.append(load_result(_read_json_object(path, "mutation result"), str(path)))
    counts = {outcome.value: 0 for outcome in Outcome}
    for result in results:
        counts[result["outcome"]] += 1
    results.sort(key=lambda result: (str(result["shard_id"]), str(result["recipe_id"])))
    # Zero results is not the best outcome, it is the worst kind of silence: no recipe reached a verdict.
    worst = max(
        (parse_outcome(result["outcome"], "summary") for result in results),
        key=exit_code_for,
        default=Outcome.NO_RESULTS,
    )
    return {
        "schema_version": RESULT_SCHEMA_VERSION,
        "counts": counts,
        "results": [{key: value for key, value in result.items() if key != "report_path"} for result in results],
        "worst_outcome": worst.value,
        "exit_code": exit_code_for(worst),
    }


@dataclass(frozen=True)
class RunOptions:
    recipe_id: str
    shard_id: str
    catalog: Path
    rules: Path
    repository: Path
    scratch: Path
    output: Path
    jobs: int = 4
    budget_seconds: int = DEFAULT_BUDGET_SECONDS
    seed: int = 0


class Toolchain:
    """git, the build wrapper, and the engine binary, as the run drives them.

    Every step takes an absolute monotonic ``deadline``; a step that outlives it raises
    :class:`subprocess.TimeoutExpired`, which the run reports as ``timeout``.
    """

    def clock(self) -> float:
        return time.monotonic()

    def _run(
        self, arguments: Sequence[str], cwd: Path, deadline: float, env: Optional[dict[str, str]] = None
    ) -> subprocess.CompletedProcess[bytes]:
        remaining = deadline - self.clock()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(list(arguments), 0)
        # Each step runs in its own session so that a step which outlives the budget is killed as a
        # whole process group: the build wrapper starts the compiler in a session of its own, and
        # killing only the wrapper would leave compilers running inside a worktree about to be removed.
        process = subprocess.Popen(
            list(arguments),
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,
        )
        try:
            output, _ = process.communicate(timeout=remaining)
        except subprocess.TimeoutExpired:
            _kill_process_group(process)
            raise
        return subprocess.CompletedProcess(list(arguments), process.returncode, output, None)

    def head_commit(self, repository: Path) -> str:
        completed = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "HEAD"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        commit = completed.stdout.decode("utf-8", "replace").strip()
        if completed.returncode != 0 or COMMIT_PATTERN.match(commit) is None:
            raise MutationError(
                f"cannot resolve HEAD of {repository}: {completed.stderr.decode('utf-8', 'replace').strip()}"
            )
        return commit

    def add_worktree(self, repository: Path, worktree: Path, commit: str, deadline: float) -> None:
        completed = self._run(["git", "worktree", "add", "--detach", str(worktree), commit], repository, deadline)
        if completed.returncode != 0:
            raise MutationError(f"git worktree add failed: {completed.stdout.decode('utf-8', 'replace').strip()}")

    def remove_worktree(self, repository: Path, worktree: Path) -> None:
        subprocess.run(
            ["git", "-C", str(repository), "worktree", "remove", "--force", str(worktree)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if worktree.exists():
            shutil.rmtree(worktree, ignore_errors=True)
        subprocess.run(
            ["git", "-C", str(repository), "worktree", "prune"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

    def apply_check(self, worktree: Path, patch: Path, deadline: float) -> Optional[str]:
        completed = self._run(["git", "apply", "--check", "--whitespace=nowarn", str(patch)], worktree, deadline)
        return None if completed.returncode == 0 else completed.stdout.decode("utf-8", "replace").strip()

    def apply(self, worktree: Path, patch: Path, deadline: float) -> None:
        completed = self._run(["git", "apply", "--whitespace=nowarn", str(patch)], worktree, deadline)
        if completed.returncode != 0:
            raise MutationError(
                f"git apply failed after a clean check: {completed.stdout.decode('utf-8', 'replace').strip()}"
            )

    def build(self, worktree: Path, jobs: int, deadline: float) -> Path:
        completed = self._run(
            [sys.executable, "scripts/agent_build.py", "--compiler-cache", "ccache", "--jobs", str(jobs)],
            worktree,
            deadline,
        )
        output = completed.stdout.decode("utf-8", "replace")
        if completed.returncode != 0:
            raise BuildFailed(output[-4000:])
        return binary_from_build_output(output, worktree)

    def run_matrix(
        self, binary: Path, family: str, catalog: Path, scratch: Path, report_path: Path, deadline: float
    ) -> None:
        env = dict(os.environ)
        env["FOUNDRY_TEST_SCRATCH"] = str(scratch)
        # The exit code is read from the report, never from the process: a missing or malformed report
        # is a structural failure whatever the process said.
        self._run(
            [
                str(binary),
                "--headless",
                "test",
                "completeness",
                "run",
                "--family",
                family,
                "--catalog",
                str(catalog),
                "--scratch",
                str(report_path.parent),
                "--report",
                str(report_path),
                "--tier",
                "scheduled",
            ],
            binary.parent.parent,
            deadline,
            env=env,
        )


# How long a step is given to stop after SIGTERM before its group is killed outright. The build
# wrapper runs the compiler in a session of its own and forwards SIGTERM to that session before
# killing it, so a SIGTERM to the wrapper's group is what reaches the compiler; SIGKILL would only
# reap the wrapper and orphan the compiler inside a worktree about to be removed.
STEP_TERMINATE_GRACE_SECONDS = 60.0


# The build wrapper's terminal verdict, always its last line (scripts/agent_build.README.md).
_BUILD_RESULT_LINE = re.compile(
    r"^\[agent-build\] RESULT: (?P<status>\S+) step=(?P<step>\S+) exit_code=(?P<exit_code>-?\d+) "
    r"binary=(?P<binary>.+?) binary_present=(?P<present>yes|no) child=\S+ invocation=\S+ log=\S+$"
)


def binary_from_build_output(output: str, worktree: Path) -> Path:
    """The binary the wrapper reports having produced; nothing is ever selected by globbing bin/.

    A checkout may hold several configurations (an editor next to a template_release), so the
    only binary that is known to belong to this build is the one the wrapper's RESULT line names.
    """
    lines = [line for line in output.splitlines() if line.strip()]
    match = _BUILD_RESULT_LINE.match(lines[-1].strip()) if lines else None
    if match is None:
        raise BuildFailed("the build wrapper ended without a RESULT line; its output is not a verdict")
    if match.group("status") != "success" or match.group("present") != "yes":
        raise BuildFailed(
            f"the build wrapper reported {match.group('status')} with binary_present={match.group('present')}"
        )
    binary = Path(match.group("binary"))
    if not binary.is_absolute():
        binary = worktree / binary
    try:
        binary.resolve().relative_to(worktree.resolve())
    except ValueError as error:
        raise BuildFailed(f"the build wrapper reported a binary outside the worktree: {binary}") from error
    if not binary.is_file():
        raise BuildFailed(f"the build wrapper reported a binary that does not exist: {binary}")
    return binary


def _kill_process_group(process: subprocess.Popen[bytes]) -> None:
    for signal_number, grace in ((signal.SIGTERM, STEP_TERMINATE_GRACE_SECONDS), (signal.SIGKILL, 10.0)):
        try:
            os.killpg(process.pid, signal_number)
        except (ProcessLookupError, PermissionError):
            pass
        try:
            process.communicate(timeout=grace)
            return
        except subprocess.TimeoutExpired:
            continue
    process.kill()


def _family_report_path(report_root: Path, family: str) -> Path:
    return report_root / f"report.{family}.json"


def _load_report_or_structural(path: Path) -> tuple[Optional[Report], str]:
    try:
        return load_report_file(path), ""
    except ReportError as error:
        return None, str(error)


def run_recipe(options: RunOptions, toolchain: Any) -> dict[str, Any]:
    """Apply, build, run, classify; always removes the worktree; always writes the result file."""
    recipe_id = require_id(options.recipe_id, "recipe_id")
    shard_id = require_id(options.shard_id, "shard_id")
    if options.budget_seconds <= 0 or options.budget_seconds > HARD_CAP_BUDGET_SECONDS:
        raise MutationError(f"budget_seconds must be within 1..{HARD_CAP_BUDGET_SECONDS}; got {options.budget_seconds}")
    # Every path is made absolute here: the steps below run in other working directories.
    catalog_root = options.catalog.resolve()
    rules_root = options.rules.resolve()
    repository = options.repository.resolve()
    recipe_path = catalog_root / f"{recipe_id}.json"
    if not recipe_path.is_file():
        raise MutationError(f"recipe {recipe_id!r} is not in {catalog_root}")
    recipe = load_recipe_file(recipe_path)
    validate_recipe_against_rules(recipe, rules_root)
    patch = patch_path_for(catalog_root, recipe_id)
    if not patch.is_file():
        raise MutationError(f"recipe {recipe_id} has no patch at {patch}")
    families = recipe.families
    if len(families) != 1:
        raise MutationError(f"recipe {recipe_id} must target exactly one family per run; got {list(families)}")
    family = families[0]

    scratch = options.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    worktree = scratch / f"worktree_{recipe_id}"
    baseline_worktree = scratch / f"baseline_worktree_{recipe_id}"
    report_root = scratch / f"matrix_{recipe_id}"
    report_path = _family_report_path(report_root, family)
    baseline_root = scratch / f"baseline_{recipe_id}"
    baseline_path = _family_report_path(baseline_root, family)
    catalog_relative = Path("modules") / "foundry_script" / "tests" / "type_completeness"
    clock: Callable[[], float] = toolchain.clock
    deadline = clock() + options.budget_seconds

    develop_commit = str(toolchain.head_commit(repository))
    if COMMIT_PATTERN.match(develop_commit) is None:
        raise MutationError(f"toolchain returned a non-commit {develop_commit!r}")

    outcome: Optional[Outcome] = None
    detectors: list[DetectorResult] = []
    capability_slice: dict[str, Any] = {"family": family, "paths": [], "broad_core": False}
    detail = ""
    try:
        # A scratch root may be reused across runs: a stale worktree or report from an earlier
        # invocation must never stand in for this one, so both are removed before anything starts.
        for stale_worktree in (worktree, baseline_worktree):
            if stale_worktree.exists():
                toolchain.remove_worktree(repository, stale_worktree)
        for old_report in (report_path, baseline_path):
            if old_report.exists():
                old_report.unlink()
        toolchain.add_worktree(repository, worktree, develop_commit, deadline)
        capabilities_path = worktree / catalog_relative / "capabilities.json"
        capability_slice = capability_slice_for_family(load_capabilities_file(capabilities_path), family).to_dict()
        stale = toolchain.apply_check(worktree, patch, deadline)
        if stale is not None:
            outcome, detail = Outcome.RECIPE_STALE, stale
        baseline: Optional[Report] = None
        if outcome is None:
            # The baseline is the unpatched commit in a detached worktree of its own, never the caller's
            # working checkout: the recorded SHA is then exactly what ran on both sides, whatever the
            # checkout holds uncommitted. One build invocation per worktree.
            toolchain.add_worktree(repository, baseline_worktree, develop_commit, deadline)
            try:
                baseline_binary = toolchain.build(baseline_worktree, options.jobs, deadline)
            except BuildFailed as error:
                outcome, detail = Outcome.BUILD_FAILED, f"baseline: {error}"
            else:
                if clock() > deadline:
                    raise subprocess.TimeoutExpired(["build"], options.budget_seconds)
                baseline_root.mkdir(parents=True, exist_ok=True)
                toolchain.run_matrix(
                    baseline_binary, family, baseline_worktree / catalog_relative, scratch, baseline_path, deadline
                )
                if clock() > deadline:
                    raise subprocess.TimeoutExpired(["run_matrix"], options.budget_seconds)
                baseline, baseline_detail = _load_report_or_structural(baseline_path)
                if baseline is None:
                    outcome, detail = Outcome.STRUCTURAL_FAILURE, f"baseline: {baseline_detail}"
                elif baseline.is_structural_failure or baseline.raw.get("structural_failures"):
                    outcome, detail = Outcome.STRUCTURAL_FAILURE, "baseline: the unpatched run is a structural failure"
                    baseline = None
        if outcome is None and baseline is not None:
            toolchain.apply(worktree, patch, deadline)
            try:
                binary = toolchain.build(worktree, options.jobs, deadline)
            except BuildFailed as error:
                outcome, detail = Outcome.BUILD_FAILED, str(error)
            else:
                if clock() > deadline:
                    raise subprocess.TimeoutExpired(["build"], options.budget_seconds)
                report_root.mkdir(parents=True, exist_ok=True)
                toolchain.run_matrix(binary, family, worktree / catalog_relative, scratch, report_path, deadline)
                if clock() > deadline:
                    raise subprocess.TimeoutExpired(["run_matrix"], options.budget_seconds)
                loaded, load_detail = _load_report_or_structural(report_path)
                if loaded is None:
                    outcome, detail = Outcome.STRUCTURAL_FAILURE, load_detail
                else:
                    outcome, detectors = classify_detection(loaded, recipe, baseline)
    except subprocess.TimeoutExpired as error:
        outcome, detail = Outcome.TIMEOUT, f"budget of {options.budget_seconds}s exceeded during {error.cmd}"
    finally:
        toolchain.remove_worktree(repository, worktree)
        toolchain.remove_worktree(repository, baseline_worktree)

    assert outcome is not None
    result = build_result(
        recipe_id=recipe_id,
        shard_id=shard_id,
        outcome=outcome,
        develop_commit=develop_commit,
        seed=options.seed,
        capability_slice=capability_slice,
        report_path=str(report_path),
        detectors=detectors,
    )
    options.output.mkdir(parents=True, exist_ok=True)
    (options.output / RESULT_FILE_NAME).write_text(render_result(result), encoding="utf-8")
    if detail:
        (options.output / "detail.txt").write_text(detail + "\n", encoding="utf-8")
    return result


def _print_result(result: Mapping[str, Any]) -> None:
    print(
        f"{result['outcome']} recipe={result['recipe_id']} shard={result['shard_id']} commit={result['develop_commit']}"
    )
    for detector in result["detectors"]:
        print(
            f"  {detector['state']:26} {detector['family']} {detector['case_id'] or '<absent>'} {detector['dimension']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mutation")
    commands = parser.add_subparsers(dest="command", required=True)

    validate = commands.add_parser("validate", help="Validate a mutation catalog against the rule catalog.")
    validate.add_argument("--catalog", required=True, help="Directory holding <recipe_id>.json, patches/, shards.json.")
    validate.add_argument("--rules", required=True, help="Type-completeness catalog root (rules/, dimensions/).")

    run = commands.add_parser("run", help="Apply one recipe in a disposable worktree, build, run, classify.")
    run.add_argument("--recipe", required=True)
    run.add_argument("--shard", required=True)
    run.add_argument("--catalog", required=True)
    run.add_argument("--rules", required=True)
    run.add_argument("--repository", required=True, help="Checkout whose HEAD is mutated.")
    run.add_argument("--scratch", required=True, help="Absolute FOUNDRY_TEST_SCRATCH directory owned by the run.")
    run.add_argument("--output", required=True, help="Directory receiving mutation_result.json.")
    run.add_argument("--jobs", type=int, default=4)
    run.add_argument("--budget-seconds", type=int, default=DEFAULT_BUDGET_SECONDS)
    run.add_argument("--seed", type=int, default=0)

    summarize_parser = commands.add_parser("summarize", help="Fold mutation_result.json files into one summary.")
    summarize_parser.add_argument("--results", required=True, nargs="+", help="Result files or directories to search.")
    summarize_parser.add_argument("--output", required=True)
    return parser


def main(argv: Optional[list[str]] = None, toolchain: Optional[Any] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        if arguments.command == "validate":
            recipes = validate_catalog(Path(arguments.catalog), Path(arguments.rules))
            for recipe in recipes:
                print(f"ok {recipe.recipe_id} detectors={len(recipe.expected_detectors)}")
            return 0
        if arguments.command == "run":
            options = RunOptions(
                recipe_id=arguments.recipe,
                shard_id=arguments.shard,
                catalog=Path(arguments.catalog),
                rules=Path(arguments.rules),
                repository=Path(arguments.repository),
                scratch=Path(arguments.scratch),
                output=Path(arguments.output),
                jobs=arguments.jobs,
                budget_seconds=arguments.budget_seconds,
                seed=arguments.seed,
            )
            result = run_recipe(options, toolchain if toolchain is not None else Toolchain())
            _print_result(result)
            return exit_code_for(parse_outcome(result["outcome"], "run"))
        summary = summarize(Path(entry) for entry in arguments.results)
        output = Path(arguments.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(summary, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        for outcome in Outcome:
            print(f"{outcome.value:20} {summary['counts'][outcome.value]}")
        return int(summary["exit_code"])
    except (MutationError, ReportError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_INVALID_INPUT


if __name__ == "__main__":
    sys.exit(main())
