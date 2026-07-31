"""Behavior of the strict TAP13 report parser and lifecycle validator."""

from __future__ import annotations

import unittest

from _reports import PREAMBLE, block, point, report
from _support import ScratchTestCase
from foundry_test_adapter.tap13 import validate_report


class ReportTests(ScratchTestCase):
    def validate(self, content, **kwargs):
        return validate_report(self.write("report.tap", content), **kwargs)

    def test_empty_plan_conforms(self) -> None:
        result = self.validate(report(0), process_exit=0)
        self.assertTrue(result.valid)
        self.assertTrue(result.complete)
        self.assertEqual("conforming", result.classification)

    def test_passing_report_conforms(self) -> None:
        result = self.validate(report(1, point(1)), process_exit=0)
        self.assertTrue(result.valid)
        self.assertEqual("conforming", result.classification)

    def test_failing_report_is_test_failures(self) -> None:
        content = report(
            1, point(1, ok=False, message="expected 4, got 5", location=("res://tests/math_tests.fs", 5, 1))
        )
        result = self.validate(content, process_exit=1)
        self.assertTrue(result.valid)
        self.assertEqual("test_failures", result.classification)

    def test_every_non_empty_status_detail_is_accepted(self) -> None:
        for detail in ("discovery_error", "runtime_error", "timed_out", "aborted", "setup_error"):
            with self.subTest(detail=detail):
                content = report(1, point(1, ok=False, status_detail=detail, message="failed"))
                result = self.validate(content, process_exit=1)
                self.assertTrue(result.valid, result.codes)

    def test_colliding_labels_are_allowed(self) -> None:
        content = report(2, point(1, test_id="test-a"), point(2, test_id="test-b"))
        self.assertTrue(self.validate(content, process_exit=0).valid)

    def test_skip_requires_a_reason(self) -> None:
        self.assertTrue(self.validate(report(1, point(1, skip_reason="pending")), process_exit=0).valid)
        bad = PREAMBLE + "1..1\nok 1 - MathTests.adds numbers # SKIP\n" + block()
        self.assertEqual(("report.directive",), self.validate(bad).codes)

    def test_todo_directives_are_outside_the_profile(self) -> None:
        content = PREAMBLE + "1..1\nok 1 - MathTests.adds numbers # TODO later\n" + block()
        self.assertEqual(("report.directive",), self.validate(content).codes)

    def test_header_failure_stops_parsing(self) -> None:
        content = "TAP version 12\n# foundry-test-adapter: 1\n1..0\n"
        result = self.validate(content)
        self.assertEqual(("report.header",), result.codes)
        self.assertFalse(result.complete)

    def test_adapter_comment_failure_stops_parsing(self) -> None:
        content = "TAP version 13\n# foundry-test-adapter: 2\n1..0\n"
        self.assertEqual(("report.adapter_version",), self.validate(content).codes)

    def test_late_plan_is_rejected(self) -> None:
        content = PREAMBLE + point(1) + "1..1\n"
        self.assertEqual({"report.plan", "report.incomplete"}, set(self.validate(content).codes))

    def test_point_numbering_must_be_contiguous(self) -> None:
        content = report(2, point(1), point(3, test_id="test-b"))
        self.assertEqual(("report.point",), self.validate(content).codes)

    def test_points_beyond_the_plan_are_rejected(self) -> None:
        content = report(1, point(1), point(2, test_id="test-b"))
        self.assertIn("report.point", self.validate(content).codes)

    def test_missing_yaml_block_leaves_the_plan_unsatisfied(self) -> None:
        content = PREAMBLE + "1..1\nok 1 - MathTests.adds numbers\n"
        result = self.validate(content)
        self.assertEqual({"report.yaml", "report.incomplete"}, set(result.codes))
        self.assertFalse(result.complete)

    def test_malformed_yaml_is_reported_once(self) -> None:
        content = PREAMBLE + "1..1\nok 1 - MathTests.adds numbers\n  ---\n  _foundry: [oops\n  ...\n"
        self.assertEqual(("report.yaml",), self.validate(content).codes)

    def test_yaml_sequences_are_rejected(self) -> None:
        content = PREAMBLE + "1..1\nok 1 - MathTests.adds numbers\n  ---\n  - one\n  ...\n"
        self.assertEqual(("report.yaml",), self.validate(content).codes)

    def test_metadata_requirements(self) -> None:
        cases = {
            "missing id": PREAMBLE + "1..1\nok 1 - L\n  ---\n  _foundry:\n"
            '    duration_ms: 1\n    status_detail: ""\n  ...\n',
            "negative duration": report(1, point(1, duration=-1)),
            "boolean duration": report(1, point(1, duration="true")),
            "unknown detail": report(1, point(1, ok=False, status_detail="exploded", message="boom")),
            "missing message": report(1, point(1, ok=False)),
        }
        for name, content in cases.items():
            with self.subTest(case=name):
                self.assertIn("report.metadata", self.validate(content).codes)

    def test_non_empty_detail_requires_not_ok(self) -> None:
        content = report(1, point(1, status_detail="runtime_error"))
        self.assertEqual(("report.status",), self.validate(content).codes)

    def test_skip_directive_requires_ok(self) -> None:
        content = report(1, point(1, ok=False, skip_reason="pending", message="unreachable"))
        self.assertEqual(("report.status",), self.validate(content).codes)

    def test_source_locations_are_one_based(self) -> None:
        content = report(1, point(1, ok=False, message="failed", location=("res://tests/math_tests.fs", 0, 1)))
        self.assertEqual(("report.location",), self.validate(content).codes)

    def test_duplicate_point_identifiers_are_rejected(self) -> None:
        content = report(2, point(1), point(2))
        self.assertEqual(("report.duplicate_id",), self.validate(content).codes)

    def test_unsatisfied_plan_is_infrastructure_failure(self) -> None:
        result = self.validate(report(3, point(1)))
        self.assertEqual(("report.incomplete",), result.codes)
        self.assertFalse(result.complete)
        self.assertEqual("infrastructure_failure", result.classification)

    def test_bailout_before_the_plan_is_a_valid_lifecycle(self) -> None:
        result = self.validate(PREAMBLE + "Bail out! cannot enumerate\n", process_exit=2)
        self.assertTrue(result.valid)
        self.assertFalse(result.complete)
        self.assertEqual("infrastructure_failure", result.classification)

    def test_bailout_after_a_partial_plan_is_a_valid_lifecycle(self) -> None:
        content = report(3, point(1)) + "Bail out! the runtime stopped\n"
        result = self.validate(content, process_exit=2)
        self.assertTrue(result.valid)
        self.assertFalse(result.complete)

    def test_bailout_after_a_satisfied_plan_is_trailing_content(self) -> None:
        content = report(1, point(1)) + "Bail out! too late\n"
        result = self.validate(content)
        self.assertEqual(("report.bailout",), result.codes)
        self.assertTrue(result.complete)
        self.assertEqual("invalid", result.classification)

    def test_content_after_a_bailout_is_rejected(self) -> None:
        content = report(3, point(1)) + "Bail out! stopped\n# trailing\n"
        self.assertEqual(("report.bailout",), self.validate(content).codes)

    def test_line_endings_must_be_terminal_line_feeds(self) -> None:
        for content in (
            report(1, point(1)).replace("\n", "\r\n"),
            report(1, point(1)).rstrip("\n"),
        ):
            with self.subTest(content=content[:20]):
                result = self.validate(content)
                self.assertEqual({"report.line_ending", "report.incomplete"}, set(result.codes))
                self.assertFalse(result.complete)
                self.assertEqual("infrastructure_failure", result.classification)

    def test_encoding_failures_stop_validation(self) -> None:
        result = self.validate(b"\xef\xbb\xbf" + report(1, point(1)).encode("utf-8"))
        self.assertEqual(("artifact.encoding",), result.codes)
        self.assertEqual("infrastructure_failure", result.classification)

    def test_missing_report_is_infrastructure_failure(self) -> None:
        result = validate_report(self.missing("absent.tap"))
        self.assertEqual(("artifact.missing",), result.codes)
        self.assertEqual("infrastructure_failure", result.classification)

    def test_an_oversized_plan_number_is_a_structural_violation(self) -> None:
        # CPython refuses to convert very long digit strings, so the parser must reject the
        # line rather than raise out of the validator.
        content = "TAP version 13\n# foundry-test-adapter: 1\n1.." + ("9" * 5000) + "\n"
        result = self.validate(content)
        self.assertEqual({"report.plan", "report.incomplete"}, set(result.codes))

    def test_an_oversized_point_number_is_a_structural_violation(self) -> None:
        content = "TAP version 13\n# foundry-test-adapter: 1\n1..1\nok " + ("9" * 5000) + " - L\n  ---\n  ...\n"
        self.assertIn("report.point", self.validate(content).codes)

    def test_yaml_tags_that_fail_to_construct_report_report_yaml(self) -> None:
        for body in (
            '  message: !!timestamp "not-a-date"\n',
            '  _foundry:\n    id: "a"\n    duration_ms: ' + ("9" * 5000) + "\n",
        ):
            with self.subTest(body=body[:24]):
                content = PREAMBLE + "1..1\nok 1 - L\n  ---\n" + body + "  ...\n"
                self.assertEqual(("report.yaml",), self.validate(content).codes)

    def test_a_satisfied_plan_stays_complete_when_extra_points_follow(self) -> None:
        content = report(1, point(1, test_id="test-a"), point(2, test_id="test-b"))
        result = self.validate(content)
        self.assertEqual(("report.point",), result.codes)
        self.assertTrue(result.complete)
        self.assertEqual("invalid", result.classification)

    def test_wrong_supplied_exit_is_invalid(self) -> None:
        result = self.validate(report(1, point(1)), process_exit=1)
        self.assertEqual(("report.exit",), result.codes)
        self.assertTrue(result.complete)
        self.assertEqual("invalid", result.classification)


class CancellationTests(ScratchTestCase):
    def validate(self, content, **kwargs):
        kwargs.setdefault("cancelled", True)
        return validate_report(self.write("report.tap", content), **kwargs)

    def assert_cancelled(self, content) -> None:
        result = self.validate(content)
        self.assertTrue(result.valid, result.codes)
        self.assertFalse(result.complete)
        self.assertEqual("cancelled", result.classification)

    def test_valid_prefixes(self) -> None:
        self.assert_cancelled("")
        self.assert_cancelled("TAP version 13\n")
        self.assert_cancelled(PREAMBLE)
        self.assert_cancelled(report(3))
        self.assert_cancelled(report(3, point(1)))

    def test_missing_output_is_a_valid_cancellation(self) -> None:
        result = validate_report(self.missing("absent.tap"), cancelled=True)
        self.assertTrue(result.valid)
        self.assertEqual("cancelled", result.classification)

    def test_incomplete_trailing_units_are_discarded(self) -> None:
        content = report(3, point(1)) + 'ok 2 - MathTests.adds numbers\n  ---\n  _foundry:\n    id: "test-b"'
        self.assert_cancelled(content)

    def test_bytes_after_the_final_line_feed_are_discarded(self) -> None:
        self.assert_cancelled(report(3, point(1)) + "ok 2 - partial")

    def test_a_satisfied_plan_is_not_a_cancellation(self) -> None:
        result = self.validate(report(1, point(1)))
        self.assertEqual(("report.cancellation",), result.codes)
        self.assertEqual("invalid", result.classification)

    def test_a_bailout_is_not_a_cancellation(self) -> None:
        result = self.validate(report(3) + "Bail out! stopped\n")
        self.assertIn("report.cancellation", result.codes)

    def test_a_completed_malformed_unit_is_reported(self) -> None:
        result = self.validate(PREAMBLE + "1..3\ngarbage\n")
        self.assertEqual({"report.cancellation", "report.point"}, set(result.codes))

    def test_a_supplied_child_exit_is_invalid(self) -> None:
        result = self.validate(report(3, point(1)), process_exit=1)
        self.assertEqual(("report.cancellation",), result.codes)


if __name__ == "__main__":
    unittest.main()
