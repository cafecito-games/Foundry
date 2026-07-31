"""Runnable-leaf plan reconstruction and report cross-checking."""

from __future__ import annotations

import unittest

from _reports import point, report
from _support import FIXTURES, ScratchTestCase
from foundry_test_adapter.json_artifacts import validate_discovery
from foundry_test_adapter.selection import SelectionError, build_leaf_plan
from foundry_test_adapter.tap13 import validate_report

NESTED = FIXTURES / "valid" / "discovery" / "nested.jsonl"
WITH_ERRORS = FIXTURES / "valid" / "discovery" / "with-errors.jsonl"
WITH_SKIP = FIXTURES / "valid" / "discovery" / "with-skip.jsonl"


def model(path):
    result, discovery = validate_discovery(str(path))
    if not result.valid:
        raise AssertionError("fixture {} is not conforming: {}".format(path, result.codes))
    return discovery


class PlanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.model = model(NESTED)

    def test_run_all_uses_every_runnable_leaf_in_discovery_order(self) -> None:
        plan = build_leaf_plan(self.model)
        self.assertEqual(("test-a", "test-b", "test-c"), tuple(item.id for item in plan))

    def test_exact_selection(self) -> None:
        self.assertEqual(("test-b",), tuple(item.id for item in build_leaf_plan(self.model, ["test-b"])))

    def test_suite_selection_expands_to_runnable_descendants(self) -> None:
        self.assertEqual(("test-c",), tuple(item.id for item in build_leaf_plan(self.model, ["suite-b"])))

    def test_overlapping_selections_deduplicate_and_keep_discovery_order(self) -> None:
        plan = build_leaf_plan(self.model, ["test-b", "suite-a", "test-a", "test-b"])
        self.assertEqual(("test-a", "test-b"), tuple(item.id for item in plan))

    def test_rejected_selections(self) -> None:
        cases = {
            "unknown": "test-missing",
            "non-runnable": "test-d",
            "discovery error": None,
        }
        for name, selection in cases.items():
            with self.subTest(case=name):
                target = self.model if selection is not None else model(WITH_ERRORS)
                identifier = selection if selection is not None else "error-a"
                with self.assertRaises(SelectionError):
                    build_leaf_plan(target, [identifier])

    def test_selecting_an_empty_suite_is_rejected(self) -> None:
        empty = model(FIXTURES / "valid" / "discovery" / "empty.jsonl")
        with self.assertRaises(SelectionError):
            build_leaf_plan(empty, ["suite-a"])


class ReportCorrelationTests(ScratchTestCase):
    def validate(self, content, discovery_path, **kwargs):
        return validate_report(self.write("report.tap", content), discovery=model(discovery_path), **kwargs)

    def test_matching_report_conforms(self) -> None:
        content = report(
            3,
            point(1, test_id="test-a"),
            point(2, test_id="test-b"),
            point(3, test_id="test-c"),
        )
        self.assertTrue(self.validate(content, NESTED, process_exit=0).valid)

    def test_unknown_point_identifier_is_a_selection_violation(self) -> None:
        content = report(
            3,
            point(1, test_id="test-a"),
            point(2, test_id="test-b"),
            point(3, test_id="test-z"),
        )
        self.assertEqual(("report.selection",), self.validate(content, NESTED).codes)

    def test_wrong_order_is_reported_separately(self) -> None:
        content = report(
            3,
            point(1, test_id="test-b"),
            point(2, test_id="test-a"),
            point(3, test_id="test-c"),
        )
        self.assertEqual(("report.order",), self.validate(content, NESTED).codes)

    def test_plan_length_must_match_the_selection(self) -> None:
        content = report(1, point(1, test_id="test-a"))
        self.assertEqual(("report.selection",), self.validate(content, NESTED).codes)

    def test_standalone_discovery_error_identifiers_are_rejected(self) -> None:
        content = report(1, point(1, ok=False, test_id="error-a", status_detail="discovery_error",
                                  message="the suite could not be indexed"))
        self.assertEqual(("report.selection",), self.validate(content, WITH_ERRORS).codes)

    def test_discovery_error_status_may_carry_a_planned_leaf(self) -> None:
        content = report(1, point(1, ok=False, test_id="test-a", status_detail="discovery_error",
                                  message="the planned leaf could not be reloaded"))
        result = self.validate(content, NESTED, selections=["test-a"], process_exit=1)
        self.assertTrue(result.valid, result.codes)

    def test_skip_state_and_reason_must_match_discovery(self) -> None:
        honored = report(
            2,
            point(1, test_id="test-s1", skip_reason="pending upstream fix"),
            point(2, test_id="test-s2"),
        )
        self.assertTrue(self.validate(honored, WITH_SKIP, process_exit=0).valid)

        ignored = report(2, point(1, test_id="test-s1"), point(2, test_id="test-s2"))
        self.assertEqual(("report.skip",), self.validate(ignored, WITH_SKIP).codes)

        mismatched = report(
            2,
            point(1, test_id="test-s1", skip_reason="different reason"),
            point(2, test_id="test-s2"),
        )
        self.assertEqual(("report.skip",), self.validate(mismatched, WITH_SKIP).codes)

        unexpected = report(
            2,
            point(1, test_id="test-s1", skip_reason="pending upstream fix"),
            point(2, test_id="test-s2", skip_reason="unexpected"),
        )
        self.assertEqual(("report.skip",), self.validate(unexpected, WITH_SKIP).codes)

    def test_an_interrupted_report_only_needs_to_be_a_plan_prefix(self) -> None:
        content = report(3, point(1, test_id="test-a")) + "Bail out! stopped\n"
        self.assertTrue(self.validate(content, NESTED, process_exit=2).valid)

        wrong_prefix = report(3, point(1, test_id="test-c")) + "Bail out! stopped\n"
        self.assertEqual(("report.selection",), self.validate(wrong_prefix, NESTED, process_exit=2).codes)


if __name__ == "__main__":
    unittest.main()
