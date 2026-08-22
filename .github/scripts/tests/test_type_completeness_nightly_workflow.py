"""Structural contracts over the type-completeness nightly workflow.

Every assertion goes through `workflow_graph`: the workflow is parsed, never read as text.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / ".github/scripts"))

import workflow_graph  # noqa: E402

sys.path.insert(0, str(REPO_ROOT))
from scripts.type_completeness import cadence  # noqa: E402

WORKFLOW = REPO_ROOT / ".github/workflows/type_completeness_nightly.yml"
SHARDS = REPO_ROOT / "modules/foundry_script/tests/type_completeness/mutations/shards.json"
JOB = "mutation-shard"

# Wall clock the job spends outside the recipe loop: checkout, Python and build tooling install, and
# catalog validation before it, the summary and the artifact upload after it. Generous on purpose -
# the point is that a shard's own budgets can never claim the whole job.
SETUP_SECONDS = 300
TEARDOWN_SECONDS = 120


class TypeCompletenessNightlyTests(unittest.TestCase):
    workflow: workflow_graph.Workflow
    shards: list[dict[str, Any]]

    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = workflow_graph.load(WORKFLOW)
        cls.shards = json.loads(SHARDS.read_text(encoding="utf-8"))["shards"]

    def test_runs_on_a_daily_schedule_and_manual_dispatch_only(self) -> None:
        triggers = self.workflow.triggers()
        self.assertEqual(sorted(triggers), ["schedule", "workflow_dispatch"])
        schedule = self.workflow.trigger("schedule")
        self.assertEqual(len(schedule), 1)
        minute, hour, day_of_month, month, day_of_week = schedule[0]["cron"].split()
        self.assertTrue(minute.isdigit() and hour.isdigit())
        self.assertEqual((day_of_month, month, day_of_week), ("*", "*", "*"))

    def test_one_matrix_cell_per_tracked_shard(self) -> None:
        cells = self.workflow.matrix_cells(JOB)
        self.assertEqual([cell["shard_id"] for cell in cells], [shard["shard_id"] for shard in self.shards])
        for cell, shard in zip(cells, self.shards):
            self.assertEqual(cell["recipes"].split(","), shard["recipes"])
            self.assertEqual(cell["budget_seconds"], shard["budget_seconds"])
            self.assertLessEqual(shard["budget_seconds"], 1800)

    def test_artifact_name_is_what_the_cadence_check_looks_for(self) -> None:
        upload = self.workflow.step_with(JOB, "Upload mutation results")
        self.assertEqual(
            upload["name"],
            cadence.ARTIFACT_NAME_TEMPLATE.format(shard_id="${{ matrix.shard_id }}", run_id="${{ github.run_id }}"),
        )

    def test_shard_job_has_the_forty_minute_timeout(self) -> None:
        self.assertEqual(self.workflow.job_key(JOB, "timeout-minutes"), 40)
        self.assertFalse(self.workflow.strategy(JOB)["fail-fast"])

    def test_every_shard_fits_its_recipes_inside_the_job_timeout(self) -> None:
        # A shard whose recipes can outlast the job has no failure mode a reader can trust: the
        # runner kills the job mid-recipe, so a legitimate timeout in the first recipe silently
        # starves every later one instead of each writing its own terminal verdict.
        job_seconds = self.workflow.job_key(JOB, "timeout-minutes") * 60
        for shard in self.shards:
            with self.subTest(shard=shard["shard_id"]):
                recipe_seconds = len(shard["recipes"]) * shard["budget_seconds"]
                self.assertLessEqual(SETUP_SECONDS + recipe_seconds + TEARDOWN_SECONDS, job_seconds)

    def test_no_shard_is_empty_and_every_recipe_is_scheduled_exactly_once(self) -> None:
        scheduled: list[str] = []
        for shard in self.shards:
            self.assertTrue(shard["recipes"])
            scheduled.extend(shard["recipes"])
        self.assertEqual(sorted(scheduled), sorted(set(scheduled)))
        catalog = {path.stem for path in (SHARDS.parent).glob("*.json") if path.name != SHARDS.name}
        self.assertEqual(set(scheduled), catalog)

    def test_results_are_uploaded_per_shard_and_run_with_fourteen_day_retention(self) -> None:
        upload = self.workflow.step_with(JOB, "Upload mutation results")
        self.assertEqual(upload["name"], "type-completeness-mutation-${{ matrix.shard_id }}-${{ github.run_id }}")
        self.assertEqual(upload["retention-days"], 14)
        self.assertTrue(upload["overwrite"])
        self.assertEqual(self.workflow.step_if(JOB, "Upload mutation results"), "always()")
        self.assertEqual(self.workflow.step_if(JOB, "Summarize the shard"), "always()")

    def test_validation_precedes_the_run_and_the_summary_follows_it(self) -> None:
        names = [
            "Validate the mutation catalog",
            "Run the shard's recipes",
            "Summarize the shard",
            "Upload mutation results",
        ]
        indices = [self.workflow.step_index(JOB, name) for name in names]
        self.assertEqual(indices, sorted(indices))

    def test_the_checkout_keeps_full_history_for_the_disposable_worktree(self) -> None:
        self.assertEqual(self.workflow.step_with(JOB, "Checkout")["fetch-depth"], 0)


if __name__ == "__main__":
    unittest.main()
