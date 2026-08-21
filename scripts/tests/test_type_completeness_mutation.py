#!/usr/bin/env python3
"""Unit tests for scripts/type_completeness/mutation.py.

Run with: python3 -m unittest discover -s scripts/tests -p "test_type_completeness_*.py"
"""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any, Optional

from test_type_completeness_comparator import RUNNER_REPORT_TEXT, RUNNER_TIMEOUT_REPORT_TEXT, _load

report = _load("report")
mutation = _load("mutation")

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "type_completeness"
TRACKED_MUTATIONS = REPO_ROOT / "modules" / "foundry_script" / "tests" / "type_completeness" / "mutations"
TRACKED_RULES = REPO_ROOT / "modules" / "foundry_script" / "tests" / "type_completeness"
TRACKED_RECIPE_ID = "union_membership_drops_last_alternative"

# The coordinates of the failed case in the hand-derived runner fixture of the comparator tests.
FAILED_CASE_COORDINATES = {"destination": "variable", "surface": "text"}
PASSED_CASE_COORDINATES = {"destination": "member", "surface": "text"}


def _recipe(detectors: Optional[list[dict[str, Any]]] = None, **overrides: Any) -> dict[str, Any]:
    base: dict[str, Any] = {
        "schema_version": 1,
        "recipe_id": "fixture_recipe",
        "description": "Fixture recipe over the comparator's runner report.",
        "escape_class": "skip_runtime_boundary_check",
        "expected_detectors": detectors
        if detectors is not None
        else [
            {
                "family": "union_destination_membership",
                "case_coordinates": dict(FAILED_CASE_COORDINATES),
                "dimension": "destination",
            }
        ],
    }
    base.update(overrides)
    return base


def _load_recipe(data: dict[str, Any]) -> Any:
    return mutation.load_recipe(data, "fixture_recipe")


class OutcomeVocabularyTests(unittest.TestCase):
    def test_every_outcome_has_a_distinct_exit_code(self) -> None:
        codes = [mutation.exit_code_for(outcome) for outcome in mutation.Outcome]
        self.assertEqual(len(codes), len(set(codes)))
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.DETECTED), 0)
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.MISSED), 1)
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.TIMEOUT), 3)
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.RECIPE_STALE), 4)
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.BUILD_FAILED), 5)
        self.assertEqual(mutation.exit_code_for(mutation.Outcome.STRUCTURAL_FAILURE), 6)
        self.assertNotIn(mutation.EXIT_INVALID_INPUT, codes)

    def test_summarize_handles_every_outcome_member(self) -> None:
        for outcome in mutation.Outcome:
            with self.subTest(outcome=outcome.value), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result = mutation.build_result(
                    recipe_id="fixture_recipe",
                    shard_id="fixture_shard",
                    outcome=outcome,
                    develop_commit="a" * 40,
                    seed=0,
                    capability_slice={"family": "f", "paths": ["p"], "broad_core": False},
                    report_path="",
                    detectors=[
                        mutation.DetectorResult("f", "case", "dimension", mutation.DetectorState.FAILED_ON_DIMENSION)
                    ],
                )
                (root / "mutation_result.json").write_text(mutation.render_result(result), encoding="utf-8")
                code = mutation.main(["summarize", "--results", str(root), "--output", str(root / "summary.json")])
                self.assertEqual(code, mutation.exit_code_for(outcome))
                summary = json.loads((root / "summary.json").read_text())
                self.assertEqual(summary["counts"][outcome.value], 1)
                self.assertEqual(sorted(summary["counts"]), sorted(member.value for member in mutation.Outcome))

    def test_summarize_rejects_an_unknown_outcome_by_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = json.loads(
                mutation.render_result(
                    mutation.build_result(
                        recipe_id="fixture_recipe",
                        shard_id="fixture_shard",
                        outcome=mutation.Outcome.DETECTED,
                        develop_commit="a" * 40,
                        seed=0,
                        capability_slice={"family": "f", "paths": ["p"], "broad_core": False},
                        report_path="",
                        detectors=[],
                    )
                )
            )
            result["outcome"] = "surprise"
            (root / "mutation_result.json").write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaises(mutation.MutationError) as raised:
                mutation.summarize([root])
            self.assertIn("surprise", str(raised.exception))
            self.assertEqual(
                mutation.main(["summarize", "--results", str(root), "--output", str(root / "s.json")]),
                mutation.EXIT_INVALID_INPUT,
            )

    def test_summarize_rejects_a_result_whose_digest_does_not_match(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = json.loads(
                mutation.render_result(
                    mutation.build_result(
                        recipe_id="fixture_recipe",
                        shard_id="fixture_shard",
                        outcome=mutation.Outcome.DETECTED,
                        develop_commit="a" * 40,
                        seed=0,
                        capability_slice={"family": "f", "paths": ["p"], "broad_core": False},
                        report_path="",
                        detectors=[],
                    )
                )
            )
            result["outcome"] = mutation.Outcome.MISSED.value
            (root / "mutation_result.json").write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaises(mutation.MutationError) as raised:
                mutation.summarize([root])
            self.assertIn("digest", str(raised.exception))


class ClassifyDetectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.report = report.load_report(json.loads(RUNNER_REPORT_TEXT))

    def test_detector_failing_on_the_named_dimension_is_detected(self) -> None:
        outcome, states = mutation.classify_detection(self.report, _load_recipe(_recipe()))
        self.assertEqual(outcome, mutation.Outcome.DETECTED)
        self.assertEqual([state.state for state in states], [mutation.DetectorState.FAILED_ON_DIMENSION])
        self.assertEqual(states[0].case_id, "case_union_store_variable")

    def test_detector_that_passes_is_missed(self) -> None:
        recipe = _load_recipe(
            _recipe(
                [
                    {
                        "family": "union_destination_membership",
                        "case_coordinates": dict(PASSED_CASE_COORDINATES),
                        "dimension": "destination",
                    }
                ]
            )
        )
        outcome, states = mutation.classify_detection(self.report, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertEqual(states[0].state, mutation.DetectorState.PASSED)
        self.assertEqual(states[0].case_id, "case_union_store_member")

    def test_detector_absent_from_the_report_is_missed(self) -> None:
        recipe = _load_recipe(
            _recipe(
                [
                    {
                        "family": "union_destination_membership",
                        "case_coordinates": {"destination": "nowhere", "surface": "text"},
                        "dimension": "destination",
                    }
                ]
            )
        )
        outcome, states = mutation.classify_detection(self.report, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertEqual(states[0].state, mutation.DetectorState.ABSENT)
        self.assertEqual(states[0].case_id, "")

    def test_detector_failing_only_on_another_dimension_is_missed(self) -> None:
        recipe = _load_recipe(
            _recipe(
                [
                    {
                        "family": "union_destination_membership",
                        "case_coordinates": dict(FAILED_CASE_COORDINATES),
                        "dimension": "runtime_status",
                    }
                ]
            )
        )
        outcome, states = mutation.classify_detection(self.report, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertEqual(states[0].state, mutation.DetectorState.FAILED_ON_OTHER_DIMENSION)

    def test_one_missed_detector_among_detected_ones_is_missed(self) -> None:
        recipe = _load_recipe(
            _recipe(
                [
                    {
                        "family": "union_destination_membership",
                        "case_coordinates": dict(FAILED_CASE_COORDINATES),
                        "dimension": "destination",
                    },
                    {
                        "family": "union_destination_membership",
                        "case_coordinates": dict(PASSED_CASE_COORDINATES),
                        "dimension": "destination",
                    },
                ]
            )
        )
        outcome, states = mutation.classify_detection(self.report, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertEqual(
            [state.state for state in states],
            [mutation.DetectorState.FAILED_ON_DIMENSION, mutation.DetectorState.PASSED],
        )

    def test_structural_failure_report_is_never_detected(self) -> None:
        timed_out = report.load_report(json.loads(RUNNER_TIMEOUT_REPORT_TEXT))
        outcome, states = mutation.classify_detection(timed_out, _load_recipe(_recipe()))
        self.assertEqual(outcome, mutation.Outcome.STRUCTURAL_FAILURE)
        self.assertEqual(states, [])

    def test_non_empty_structural_failures_with_a_mismatch_outcome_is_structural(self) -> None:
        document = json.loads(RUNNER_REPORT_TEXT)
        document["structural_failures"] = [{"stage": "witness", "detail": "x"}]
        outcome, _ = mutation.classify_detection(report.load_report(document), _load_recipe(_recipe()))
        self.assertEqual(outcome, mutation.Outcome.STRUCTURAL_FAILURE)

    def test_detector_for_another_family_than_the_report_is_absent(self) -> None:
        recipe = _load_recipe(
            _recipe(
                [
                    {
                        "family": "other_family",
                        "case_coordinates": dict(FAILED_CASE_COORDINATES),
                        "dimension": "destination",
                    }
                ]
            )
        )
        outcome, states = mutation.classify_detection(self.report, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertEqual(states[0].state, mutation.DetectorState.ABSENT)

    def test_every_detector_state_is_classified(self) -> None:
        self.assertEqual(
            {state for state in mutation.DetectorState},
            {
                mutation.DetectorState.FAILED_ON_DIMENSION,
                mutation.DetectorState.PASSED,
                mutation.DetectorState.ABSENT,
                mutation.DetectorState.FAILED_ON_OTHER_DIMENSION,
            },
        )
        for state in mutation.DetectorState:
            self.assertEqual(state is mutation.DetectorState.FAILED_ON_DIMENSION, mutation.state_detects(state))


class RealReportTests(unittest.TestCase):
    """The byte-faithful report the real `test completeness run` wrote for the tracked union family."""

    def test_the_real_passed_report_loads_and_misses_the_tracked_recipe(self) -> None:
        loaded = report.load_report_file(FIXTURES / "runner_report_union_passed.json")
        self.assertTrue(loaded.is_clean)
        self.assertEqual(len(loaded.cases), 40)
        recipe = mutation.load_recipe_file(TRACKED_MUTATIONS / f"{TRACKED_RECIPE_ID}.json")
        outcome, states = mutation.classify_detection(loaded, recipe)
        self.assertEqual(outcome, mutation.Outcome.MISSED)
        self.assertTrue(states)
        self.assertTrue(all(state.state is mutation.DetectorState.PASSED for state in states))

    def test_the_real_mutated_report_detects_the_tracked_recipe(self) -> None:
        loaded = report.load_report_file(FIXTURES / "runner_report_union_mutated.json")
        self.assertEqual(loaded.outcome, "product_mismatch")
        recipe = mutation.load_recipe_file(TRACKED_MUTATIONS / f"{TRACKED_RECIPE_ID}.json")
        outcome, states = mutation.classify_detection(loaded, recipe)
        self.assertEqual(outcome, mutation.Outcome.DETECTED)
        self.assertEqual(len(states), len(recipe.expected_detectors))

    def test_the_real_mutated_report_reaches_the_whole_run_flow(self) -> None:
        fake = FakeToolchain(report_text=(FIXTURES / "runner_report_union_mutated.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = mutation.run_recipe(_run_options(root), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.DETECTED.value)


class RecipeValidationTests(unittest.TestCase):
    def test_the_tracked_catalog_validates(self) -> None:
        self.assertEqual(
            mutation.main(["validate", "--catalog", str(TRACKED_MUTATIONS), "--rules", str(TRACKED_RULES)]), 0
        )

    def test_the_tracked_patch_applies_to_the_checkout(self) -> None:
        if shutil.which("git") is None:
            self.skipTest("git is unavailable")
        patch = mutation.patch_path_for(TRACKED_MUTATIONS, TRACKED_RECIPE_ID)
        completed = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "apply", "--check", "--whitespace=nowarn", str(patch)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", "replace"))

    def test_every_tracked_shard_is_within_budget_and_names_tracked_recipes(self) -> None:
        shards = mutation.load_shards_file(TRACKED_MUTATIONS / "shards.json", TRACKED_MUTATIONS)
        self.assertTrue(shards)
        for shard in shards:
            self.assertLessEqual(shard.budget_seconds, mutation.DEFAULT_BUDGET_SECONDS)
            for recipe_id in shard.recipes:
                self.assertTrue((TRACKED_MUTATIONS / f"{recipe_id}.json").is_file())

    def test_the_valid_fixture_recipe_loads(self) -> None:
        recipe = mutation.load_recipe_file(FIXTURES / "recipe_valid.json")
        self.assertEqual(recipe.recipe_id, "recipe_valid")
        self.assertEqual(len(recipe.expected_detectors), 1)

    def test_each_malformed_fixture_is_rejected_by_name(self) -> None:
        malformed = sorted(FIXTURES.glob("recipe_malformed_*.json"))
        self.assertTrue(malformed)
        for path in malformed:
            with self.subTest(fixture=path.name):
                with self.assertRaises(mutation.MutationError):
                    mutation.load_recipe_file(path)

    def test_validate_exits_2_for_a_malformed_recipe_in_a_catalog(self) -> None:
        for path in sorted(FIXTURES.glob("recipe_malformed_*.json")):
            with self.subTest(fixture=path.name), tempfile.TemporaryDirectory() as directory:
                catalog = Path(directory)
                (catalog / "patches").mkdir()
                shutil.copy(path, catalog / path.name)
                (catalog / "patches" / f"{path.stem}.patch").write_text("", encoding="utf-8")
                (catalog / "shards.json").write_text(
                    json.dumps(
                        {
                            "schema_version": 1,
                            "shards": [{"shard_id": "s", "recipes": [path.stem], "budget_seconds": 60}],
                        }
                    ),
                    encoding="utf-8",
                )
                code = mutation.main(["validate", "--catalog", str(catalog), "--rules", str(TRACKED_RULES)])
                self.assertEqual(code, mutation.EXIT_INVALID_INPUT)

    def test_validate_rejects_unknown_family_and_dimension(self) -> None:
        for field, value in (("family", "no_such_family"), ("dimension", "no_such_dimension")):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                catalog = Path(directory)
                (catalog / "patches").mkdir()
                recipe = json.loads((FIXTURES / "recipe_valid.json").read_text())
                recipe["recipe_id"] = "bad"
                recipe["expected_detectors"][0][field] = value
                (catalog / "bad.json").write_text(json.dumps(recipe), encoding="utf-8")
                (catalog / "patches" / "bad.patch").write_text("", encoding="utf-8")
                (catalog / "shards.json").write_text(
                    json.dumps(
                        {"schema_version": 1, "shards": [{"shard_id": "s", "recipes": ["bad"], "budget_seconds": 60}]}
                    ),
                    encoding="utf-8",
                )
                code = mutation.main(["validate", "--catalog", str(catalog), "--rules", str(TRACKED_RULES)])
                self.assertEqual(code, mutation.EXIT_INVALID_INPUT)

    def test_validate_rejects_partial_coordinates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            catalog = Path(directory)
            (catalog / "patches").mkdir()
            recipe = json.loads((FIXTURES / "recipe_valid.json").read_text())
            recipe["recipe_id"] = "partial"
            del recipe["expected_detectors"][0]["case_coordinates"]["surface"]
            (catalog / "partial.json").write_text(json.dumps(recipe), encoding="utf-8")
            (catalog / "patches" / "partial.patch").write_text("", encoding="utf-8")
            (catalog / "shards.json").write_text(
                json.dumps(
                    {"schema_version": 1, "shards": [{"shard_id": "s", "recipes": ["partial"], "budget_seconds": 60}]}
                ),
                encoding="utf-8",
            )
            code = mutation.main(["validate", "--catalog", str(catalog), "--rules", str(TRACKED_RULES)])
            self.assertEqual(code, mutation.EXIT_INVALID_INPUT)

    def test_validate_rejects_a_shard_over_budget_or_naming_an_unknown_recipe(self) -> None:
        for shards in (
            {"schema_version": 1, "shards": [{"shard_id": "s", "recipes": ["recipe_valid"], "budget_seconds": 1801}]},
            {"schema_version": 1, "shards": [{"shard_id": "s", "recipes": ["missing"], "budget_seconds": 60}]},
            {
                "schema_version": 1,
                "shards": [{"shard_id": "Bad Id", "recipes": ["recipe_valid"], "budget_seconds": 60}],
            },
            {"schema_version": 1, "shards": []},
        ):
            with self.subTest(shards=shards), tempfile.TemporaryDirectory() as directory:
                catalog = Path(directory)
                (catalog / "patches").mkdir()
                shutil.copy(FIXTURES / "recipe_valid.json", catalog / "recipe_valid.json")
                (catalog / "patches" / "recipe_valid.patch").write_text("", encoding="utf-8")
                (catalog / "shards.json").write_text(json.dumps(shards), encoding="utf-8")
                code = mutation.main(["validate", "--catalog", str(catalog), "--rules", str(TRACKED_RULES)])
                self.assertEqual(code, mutation.EXIT_INVALID_INPUT)

    def test_recipe_id_must_match_the_grammar_before_any_path_is_built(self) -> None:
        for bad in ("../escape", "Upper", "", "a" * 65, "with space", "dash-id"):
            with self.subTest(recipe_id=bad):
                with self.assertRaises(mutation.MutationError):
                    mutation.patch_path_for(TRACKED_MUTATIONS, bad)
                with self.assertRaises(mutation.MutationError):
                    _load_recipe(_recipe(recipe_id=bad))

    def test_recipe_file_name_must_equal_its_id(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "other_name.json"
            path.write_text(json.dumps(_recipe()), encoding="utf-8")
            with self.assertRaises(mutation.MutationError):
                mutation.load_recipe_file(path)

    def test_schema_version_must_be_a_json_integer(self) -> None:
        for version in (1.0, "1", True, 2):
            with self.subTest(version=version):
                with self.assertRaises(mutation.MutationError):
                    _load_recipe(_recipe(schema_version=version))

    def test_malformed_recipe_is_never_treated_as_absent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture_recipe.json"
            path.write_text("{not json", encoding="utf-8")
            with self.assertRaises(mutation.MutationError):
                mutation.load_recipe_file(path)


class FakeToolchain:
    """Stands in for git, the build wrapper, and the engine binary, with scripted failures."""

    def __init__(
        self,
        report_text: Optional[str] = None,
        stale: bool = False,
        build_fails: bool = False,
        step_seconds: float = 0.0,
        report_missing: bool = False,
    ) -> None:
        self.report_text = report_text if report_text is not None else RUNNER_REPORT_TEXT
        self.stale = stale
        self.build_fails = build_fails
        self.step_seconds = step_seconds
        self.report_missing = report_missing
        self.calls: list[str] = []
        self.now = 0.0
        self.removed: list[Path] = []
        self.build_invocations = 0

    def clock(self) -> float:
        return self.now

    def _tick(self, name: str) -> None:
        self.calls.append(name)
        self.now += self.step_seconds

    def head_commit(self, repository: Path) -> str:
        self._tick("head_commit")
        return "f" * 40

    def add_worktree(self, repository: Path, worktree: Path, commit: str, deadline: float) -> None:
        self._tick("add_worktree")
        worktree.mkdir(parents=True, exist_ok=True)
        capabilities = worktree / "modules" / "foundry_script" / "tests" / "type_completeness"
        capabilities.mkdir(parents=True, exist_ok=True)
        shutil.copy(TRACKED_RULES / "capabilities.json", capabilities / "capabilities.json")

    def remove_worktree(self, repository: Path, worktree: Path) -> None:
        self.calls.append("remove_worktree")
        self.removed.append(worktree)

    def apply_check(self, worktree: Path, patch: Path, deadline: float) -> Optional[str]:
        self._tick("apply_check")
        return "error: patch does not apply" if self.stale else None

    def apply(self, worktree: Path, patch: Path, deadline: float) -> None:
        self._tick("apply")

    def build(self, worktree: Path, jobs: int, deadline: float) -> Path:
        self._tick("build")
        self.build_invocations += 1
        if self.build_fails:
            raise mutation.BuildFailed("scons returned 2")
        return worktree / "bin" / "foundry.fake"

    def run_matrix(
        self, binary: Path, family: str, catalog: Path, scratch: Path, report_path: Path, deadline: float
    ) -> None:
        self._tick("run_matrix")
        if self.report_missing:
            return
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(self.report_text, encoding="utf-8")


def _run_options(root: Path, **overrides: Any) -> Any:
    options = {
        "recipe_id": TRACKED_RECIPE_ID,
        "shard_id": "union_core",
        "catalog": TRACKED_MUTATIONS,
        "rules": TRACKED_RULES,
        "repository": REPO_ROOT,
        "scratch": root / "scratch",
        "output": root / "out",
        "jobs": 2,
        "budget_seconds": 600,
        "seed": 0,
    }
    options.update(overrides)
    return mutation.RunOptions(**options)


class RunFlowTests(unittest.TestCase):
    def test_a_stale_patch_is_recipe_stale_and_never_builds(self) -> None:
        fake = FakeToolchain(stale=True)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = mutation.run_recipe(_run_options(root), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.RECIPE_STALE.value)
            self.assertNotIn("build", fake.calls)
            self.assertEqual(fake.calls[-1], "remove_worktree")

    def test_a_failed_build_is_build_failed(self) -> None:
        fake = FakeToolchain(build_fails=True)
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.BUILD_FAILED.value)
            self.assertNotIn("run_matrix", fake.calls)

    def test_exactly_one_build_invocation_per_worktree(self) -> None:
        fake = FakeToolchain()
        with tempfile.TemporaryDirectory() as directory:
            mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(fake.build_invocations, 1)
            self.assertEqual(fake.calls.count("add_worktree"), 1)

    def test_a_missing_report_is_structural_not_absent(self) -> None:
        fake = FakeToolchain(report_missing=True)
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.STRUCTURAL_FAILURE.value)

    def test_a_stale_report_from_an_earlier_run_is_never_reused(self) -> None:
        fake = FakeToolchain(report_missing=True)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            options = _run_options(root)
            stale = (
                options.scratch.resolve() / f"matrix_{TRACKED_RECIPE_ID}" / "report.union_destination_membership.json"
            )
            stale.parent.mkdir(parents=True)
            stale.write_text((FIXTURES / "runner_report_union_mutated.json").read_text(), encoding="utf-8")
            result = mutation.run_recipe(options, fake)
            self.assertEqual(result["outcome"], mutation.Outcome.STRUCTURAL_FAILURE.value)
            self.assertFalse(stale.exists())

    def test_a_leftover_worktree_is_removed_before_a_new_one_is_added(self) -> None:
        fake = FakeToolchain()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            options = _run_options(root)
            (options.scratch / f"worktree_{TRACKED_RECIPE_ID}").mkdir(parents=True)
            mutation.run_recipe(options, fake)
            self.assertEqual(fake.calls.index("remove_worktree"), fake.calls.index("add_worktree") - 1)

    def test_an_unparsable_report_is_structural(self) -> None:
        fake = FakeToolchain(report_text="{not json")
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.STRUCTURAL_FAILURE.value)

    def test_a_report_rejected_by_load_report_is_structural(self) -> None:
        document = json.loads(RUNNER_REPORT_TEXT)
        document["schema_version"] = 99.0
        fake = FakeToolchain(report_text=json.dumps(document))
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.STRUCTURAL_FAILURE.value)

    def test_a_timed_out_report_is_structural(self) -> None:
        fake = FakeToolchain(report_text=RUNNER_TIMEOUT_REPORT_TEXT)
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.STRUCTURAL_FAILURE.value)

    def test_the_worktree_is_removed_even_when_a_step_raises(self) -> None:
        fake = FakeToolchain()

        def explode(*arguments: Any, **keywords: Any) -> None:
            raise RuntimeError("boom")

        fake.apply = explode  # type: ignore[method-assign]
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(RuntimeError):
                mutation.run_recipe(_run_options(Path(directory)), fake)
            self.assertEqual(fake.calls[-1], "remove_worktree")

    def test_budget_exhaustion_is_timeout_with_the_partial_report_published(self) -> None:
        fake = FakeToolchain(step_seconds=100.0)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = mutation.run_recipe(_run_options(root, budget_seconds=250), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.TIMEOUT.value)
            self.assertNotIn("run_matrix", fake.calls)
            self.assertTrue(result["report_path"].startswith(str((root / "scratch").resolve())))

    def test_budget_exhaustion_after_the_matrix_ran_is_still_timeout(self) -> None:
        fake = FakeToolchain(step_seconds=100.0)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = mutation.run_recipe(_run_options(root, budget_seconds=550), fake)
            self.assertEqual(result["outcome"], mutation.Outcome.TIMEOUT.value)
            self.assertIn("run_matrix", fake.calls)
            self.assertTrue(Path(result["report_path"]).is_file())

    def test_budget_above_the_hard_cap_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(mutation.MutationError):
                mutation.run_recipe(_run_options(Path(directory), budget_seconds=2401), FakeToolchain())

    def test_detected_result_file_is_byte_identical_across_runs(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            result_a = mutation.run_recipe(_run_options(Path(first)), FakeToolchain())
            result_b = mutation.run_recipe(_run_options(Path(second)), FakeToolchain())
            text_a = (Path(first) / "out" / "mutation_result.json").read_bytes()
            text_b = (Path(second) / "out" / "mutation_result.json").read_bytes()
            self.assertEqual(
                {key: value for key, value in result_a.items() if key != "report_path"},
                {key: value for key, value in result_b.items() if key != "report_path"},
            )
            self.assertNotEqual(result_a["report_path"], result_b["report_path"])
            # The only byte that may differ is the report path.
            self.assertEqual(
                text_a.replace(result_a["report_path"].encode("utf-8"), b"X"),
                text_b.replace(result_b["report_path"].encode("utf-8"), b"X"),
            )

    def test_result_digest_covers_only_the_specified_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = mutation.run_recipe(_run_options(Path(directory)), FakeToolchain())
            expected = hashlib.sha256(
                json.dumps(
                    {
                        "recipe_id": result["recipe_id"],
                        "develop_commit": result["develop_commit"],
                        "outcome": result["outcome"],
                        "detectors": result["detectors"],
                    },
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=False,
                ).encode("utf-8")
            ).hexdigest()
            self.assertEqual(result["digest"], expected)
            changed = copy.deepcopy(result)
            changed["report_path"] = "/elsewhere"
            self.assertEqual(mutation.result_digest(changed), expected)

    def test_result_file_matches_the_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = mutation.run_recipe(_run_options(root), FakeToolchain())
            self.assertEqual(
                sorted(result),
                sorted(
                    [
                        "schema_version",
                        "recipe_id",
                        "shard_id",
                        "outcome",
                        "develop_commit",
                        "seed",
                        "capability_slice",
                        "report_path",
                        "detectors",
                        "digest",
                    ]
                ),
            )
            self.assertEqual(result["schema_version"], 1)
            self.assertEqual(result["develop_commit"], "f" * 40)
            self.assertEqual(result["capability_slice"]["family"], "union_destination_membership")
            self.assertTrue(result["capability_slice"]["paths"])
            for detector in result["detectors"]:
                self.assertEqual(sorted(detector), ["case_id", "dimension", "family", "state"])
            self.assertEqual(json.loads((root / "out" / "mutation_result.json").read_text(encoding="utf-8")), result)

    def test_run_exit_code_is_the_outcome_code(self) -> None:
        fake = FakeToolchain(stale=True)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            code = mutation.main(
                [
                    "run",
                    "--recipe",
                    TRACKED_RECIPE_ID,
                    "--shard",
                    "union_core",
                    "--catalog",
                    str(TRACKED_MUTATIONS),
                    "--rules",
                    str(TRACKED_RULES),
                    "--repository",
                    str(REPO_ROOT),
                    "--scratch",
                    str(root / "scratch"),
                    "--output",
                    str(root / "out"),
                    "--jobs",
                    "1",
                ],
                toolchain=fake,
            )
            self.assertEqual(code, mutation.exit_code_for(mutation.Outcome.RECIPE_STALE))

    def test_unknown_recipe_is_invalid_input(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            code = mutation.main(
                [
                    "run",
                    "--recipe",
                    "no_such_recipe",
                    "--shard",
                    "union_core",
                    "--catalog",
                    str(TRACKED_MUTATIONS),
                    "--rules",
                    str(TRACKED_RULES),
                    "--repository",
                    str(REPO_ROOT),
                    "--scratch",
                    str(root / "scratch"),
                    "--output",
                    str(root / "out"),
                ],
                toolchain=FakeToolchain(),
            )
            self.assertEqual(code, mutation.EXIT_INVALID_INPUT)


if __name__ == "__main__":
    unittest.main()
