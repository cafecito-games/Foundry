"""Tests for `.github/scripts/workflow_graph.py`.

The helper backs every workflow contract, so a bug that made it return an empty
mapping instead of raising would make every contract pass while asserting
nothing. These tests exercise it against a fixture workflow and pin the
raise-on-missing behavior explicitly.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import workflow_graph  # noqa: E402
from workflow_graph import MissingWorkflowElement  # noqa: E402

FIXTURE_WORKFLOW = """
name: Fixture
on:
  workflow_call:
    inputs:
      checkout-ref:
        type: string
        default: ""
  workflow_dispatch:

jobs:
  preflight:
    name: Parse request
    runs-on: ubuntu-24.04
    outputs:
      authorized: ${{ steps.plan.outputs.authorized }}
    steps:
      - name: Checkout
        uses: actions/checkout@v6
        with:
          ref: ${{ inputs.checkout-ref || github.sha }}
      - name: Plan
        id: plan
        run: echo plan
      - run: echo unnamed

  build:
    needs: preflight
    if: >-
      ${{ needs.preflight.outputs.authorized == 'true'
        && github.event_name != 'push' }}
    runs-on: ubuntu-24.04
    concurrency:
      group: fixture-${{ github.sha }}
    strategy:
      fail-fast: false
      matrix:
        os: [linux, macos]
        include:
          - os: linux
            arch: x86_64
          - os: macos
            arch: arm64
    steps:
      - name: Compile
        if: ${{ matrix.os == 'linux' }}
        run: echo compile

  publish:
    needs:
      - preflight
      - build
    uses: ./.github/workflows/fixture_reusable.yml
    with:
      checkout-ref: ${{ github.sha }}
"""


class WorkflowGraphTests(unittest.TestCase):
    workflow: workflow_graph.Workflow
    _directory: tempfile.TemporaryDirectory[str]

    @classmethod
    def setUpClass(cls) -> None:
        cls._directory = tempfile.TemporaryDirectory()
        path = Path(cls._directory.name) / "fixture.yml"
        path.write_text(FIXTURE_WORKFLOW, encoding="utf-8")
        cls.workflow = workflow_graph.load(path)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._directory.cleanup()

    def test_jobs_returns_every_top_level_job(self) -> None:
        self.assertEqual(["preflight", "build", "publish"], self.workflow.job_names())
        self.assertEqual(0, self.workflow.job_index("preflight"))
        self.assertEqual(2, self.workflow.job_index("publish"))

    def test_needs_normalizes_scalar_and_list_forms(self) -> None:
        self.assertEqual(("preflight",), self.workflow.needs("build"))
        self.assertEqual(("preflight", "build"), self.workflow.needs("publish"))
        self.assertFalse(self.workflow.has_needs("preflight"))

    def test_job_if_collapses_wrapped_expression_whitespace(self) -> None:
        self.assertEqual(
            "${{ needs.preflight.outputs.authorized == 'true' && github.event_name != 'push' }}",
            self.workflow.job_if("build"),
        )

    def test_step_names_preserves_declaration_order(self) -> None:
        self.assertEqual(["Checkout", "Plan", None], self.workflow.step_names("preflight"))
        self.assertLess(
            self.workflow.step_index("preflight", "Checkout"),
            self.workflow.step_index("preflight", "Plan"),
        )

    def test_step_accessors_return_the_declared_values(self) -> None:
        self.assertEqual("actions/checkout@v6", self.workflow.step_key("preflight", "Checkout", "uses"))
        self.assertEqual(
            {"ref": "${{ inputs.checkout-ref || github.sha }}"},
            self.workflow.step_with("preflight", "Checkout"),
        )
        self.assertEqual("echo plan", self.workflow.step_run("preflight", "Plan"))
        self.assertEqual("${{ matrix.os == 'linux' }}", self.workflow.step_if("build", "Compile"))

    def test_matrix_returns_every_dimension_and_value(self) -> None:
        self.assertEqual(
            {"os": ["linux", "macos"], "arch": ["x86_64", "arm64"]},
            self.workflow.matrix("build"),
        )
        self.assertEqual(
            [{"os": "linux", "arch": "x86_64"}, {"os": "macos", "arch": "arm64"}],
            self.workflow.matrix_cells("build"),
        )

    def test_job_level_accessors_return_the_declared_values(self) -> None:
        self.assertEqual({"authorized": "${{ steps.plan.outputs.authorized }}"}, self.workflow.outputs("preflight"))
        self.assertEqual({"group": "fixture-${{ github.sha }}"}, self.workflow.concurrency("build"))
        self.assertIs(False, self.workflow.strategy("build")["fail-fast"])
        self.assertEqual("./.github/workflows/fixture_reusable.yml", self.workflow.uses("publish"))
        self.assertEqual({"checkout-ref": "${{ github.sha }}"}, self.workflow.with_inputs("publish"))
        self.assertIn("checkout-ref", self.workflow.workflow_call_inputs())
        self.assertEqual(["workflow_call", "workflow_dispatch"], list(self.workflow.triggers()))

    def test_all_steps_yields_every_step_of_every_job_with_steps(self) -> None:
        owners = [job for job, _ in self.workflow.all_steps()]
        self.assertEqual(["preflight", "preflight", "preflight", "build"], owners)

    def test_scalars_walks_parsed_values_not_file_text(self) -> None:
        scalars = set(self.workflow.scalars())
        self.assertIn("actions/checkout@v6", scalars)
        self.assertIn("./.github/workflows/fixture_reusable.yml", scalars)
        self.assertNotIn("cafecito-games/Foundry-Android", scalars)

    def test_missing_job_raises_rather_than_returning_empty(self) -> None:
        for accessor in (
            self.workflow.job,
            self.workflow.needs,
            self.workflow.job_if,
            self.workflow.steps,
            self.workflow.step_names,
            self.workflow.matrix,
            self.workflow.matrix_cells,
            self.workflow.uses,
            self.workflow.outputs,
            self.workflow.job_index,
        ):
            with self.subTest(accessor=accessor.__name__):
                with self.assertRaises(MissingWorkflowElement):
                    accessor("no-such-job")

    def test_missing_key_raises_rather_than_returning_empty(self) -> None:
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.needs("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.job_if("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.matrix("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.concurrency("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.uses("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.with_inputs("preflight")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.step_with("preflight", "Plan")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.step_if("preflight", "Plan")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.action_inputs()

    def test_missing_step_raises_rather_than_returning_empty(self) -> None:
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.step("preflight", "No Such Step")
        with self.assertRaises(MissingWorkflowElement):
            self.workflow.step_index("preflight", "No Such Step")

    def test_a_workflow_without_jobs_raises(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            empty = Path(directory) / "empty.yml"
            empty.write_text("name: Empty\njobs:\n", encoding="utf-8")
            with self.assertRaises(MissingWorkflowElement):
                workflow_graph.load(empty).jobs()

            scalar = Path(directory) / "scalar.yml"
            scalar.write_text("just-a-string\n", encoding="utf-8")
            with self.assertRaises(MissingWorkflowElement):
                workflow_graph.load(scalar)

    def test_normalize_expression_collapses_but_does_not_evaluate(self) -> None:
        self.assertEqual(
            "${{ always() && a == 'b' }}",
            workflow_graph.normalize_expression("${{\n  always()\n  && a == 'b'\n}}"),
        )


if __name__ == "__main__":
    unittest.main()
