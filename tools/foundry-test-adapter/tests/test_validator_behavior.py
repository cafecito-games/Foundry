# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
"""Behavior of the validator on inputs that are easier to state in code than as fixtures."""

from __future__ import annotations

import json
import threading
import unittest

from foundry_test_adapter import (
    ALL_VIOLATION_CODES,
    PROTOCOL_NAME,
    TapReport,
    Violation,
    ViolationCode,
    expected_leaf_ids,
    fixtures_root,
    utf16_length,
    utf16_offset_to_index,
    validate_discovery_stream,
    validate_run,
    validate_tap_report,
)


def _record(**fields: object) -> str:
    payload = {"protocol": PROTOCOL_NAME, "version": 1}
    payload.update(fields)
    return json.dumps(payload)


def _suite(identifier: str, parent: object = None, runnable: bool = True) -> str:
    return _record(
        event="suite",
        id=identifier,
        label=identifier,
        parent_id=parent,
        path="res://tests/example.fs",
        range=None,
        runnable=runnable,
        skipped=False,
        skip_reason=None,
    )


def _test(identifier: str, parent: object = None, runnable: bool = True, item_range: object = None) -> str:
    return _record(
        event="test",
        id=identifier,
        label=identifier,
        parent_id=parent,
        path="res://tests/example.fs",
        range=item_range,
        runnable=runnable,
        skipped=False,
        skip_reason=None,
        case_key=None,
    )


def _stream(*records: str) -> str:
    suites = len([record for record in records if '"event": "suite"' in record])
    tests = len([record for record in records if '"event": "test"' in record])
    end = _record(event="discovery_end", suite_count=suites, test_count=tests, error_count=0)
    return "\n".join((_record(event="discovery_start", root="res://tests"),) + records + (end,)) + "\n"


class Utf16PositionTests(unittest.TestCase):
    def test_astral_characters_count_as_two_code_units(self) -> None:
        self.assertEqual(1, utf16_length("a"))
        self.assertEqual(2, utf16_length("\U0001f600"))
        self.assertEqual(4, utf16_length("a\U0001f600b"))

    def test_offsets_convert_back_to_string_indices(self) -> None:
        text = "a\U0001f600b"
        self.assertEqual(0, utf16_offset_to_index(text, 0))
        self.assertEqual(1, utf16_offset_to_index(text, 1))
        self.assertEqual(2, utf16_offset_to_index(text, 3))
        self.assertEqual(3, utf16_offset_to_index(text, 4))

    def test_an_offset_inside_a_surrogate_pair_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            utf16_offset_to_index("a\U0001f600b", 2)

    def test_a_range_past_an_astral_character_is_accepted(self) -> None:
        emoji_range = {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 4}}
        result = validate_discovery_stream(_stream(_test("t:emoji \U0001f600", item_range=emoji_range)))
        self.assertTrue(result.conforms, msg=str(result.violations))
        item = result.item_by_id("t:emoji \U0001f600")
        assert item is not None and item.range is not None
        self.assertEqual((0, 4), item.range.end.as_tuple())


class ViolationRegistryTests(unittest.TestCase):
    def test_an_unregistered_code_cannot_be_reported(self) -> None:
        with self.assertRaises(ValueError):
            Violation("totally_made_up", "message")

    def test_every_registered_code_is_a_plain_identifier(self) -> None:
        self.assertIn(ViolationCode.MISSING_FIELD, ALL_VIOLATION_CODES)
        for code in ALL_VIOLATION_CODES:
            self.assertRegex(code, r"^[a-z][a-z0-9_]*$")


class DiscoveryEdgeCaseTests(unittest.TestCase):
    def test_an_empty_stream_is_not_an_empty_test_suite(self) -> None:
        result = validate_discovery_stream("")
        self.assertFalse(result.complete)
        self.assertEqual(
            [ViolationCode.EMPTY_STREAM],
            [violation.code for violation in result.violations],
        )

    def test_blank_separator_lines_are_ignored(self) -> None:
        result = validate_discovery_stream(_stream(_suite("S1"), _test("S1::a", "S1")).replace("\n", "\n\n"))
        self.assertTrue(result.conforms, msg=str(result.violations))

    def test_a_parent_must_precede_its_child(self) -> None:
        reordered = _stream(_test("S1::a", "S1"), _suite("S1"))
        result = validate_discovery_stream(reordered)
        self.assertIn(ViolationCode.UNKNOWN_PARENT, [violation.code for violation in result.violations])


class SelectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.discovery = validate_discovery_stream(
            _stream(
                _suite("S1"),
                _test("S1::a", "S1"),
                _test("S1::b", "S1"),
                _suite("S2"),
                _test("S2::c", "S2", runnable=False),
            )
        )
        self.assertTrue(self.discovery.conforms, msg=str(self.discovery.violations))

    def test_no_selection_resolves_to_every_runnable_leaf(self) -> None:
        leaves, violations = expected_leaf_ids(self.discovery, [])
        self.assertEqual([], violations)
        self.assertEqual(["S1::a", "S1::b"], leaves)

    def test_selecting_a_leaf_twice_runs_it_once(self) -> None:
        leaves, violations = expected_leaf_ids(self.discovery, ["S1::a", "S1::a"])
        self.assertEqual([], violations)
        self.assertEqual(["S1::a"], leaves)

    def test_a_non_runnable_selection_is_rejected(self) -> None:
        leaves, violations = expected_leaf_ids(self.discovery, ["S2::c"])
        self.assertEqual([], leaves)
        self.assertEqual([ViolationCode.SELECTION_NOT_RUNNABLE], [violation.code for violation in violations])

    def test_a_cancelled_run_does_not_report_the_unrun_tests_as_missing(self) -> None:
        report = validate_tap_report(
            "TAP version 13\n"
            "# foundry-test-adapter: 1\n"
            "1..2\n"
            "ok 1 - a\n"
            "  ---\n"
            "  _foundry:\n"
            '    id: "S1::a"\n'
            "    duration_ms: 1\n"
            '    status_detail: ""\n'
            "  ...\n"
        )
        self.assertFalse(report.complete)
        codes = [violation.code for violation in validate_run(self.discovery, report, [])]
        self.assertNotIn(ViolationCode.MISSING_RESULT_ID, codes)
        self.assertNotIn(ViolationCode.UNEXPECTED_RESULT_ID, codes)


if __name__ == "__main__":
    unittest.main()


class MalformedInputTerminationTests(unittest.TestCase):
    """A non-conforming artifact must still produce violations rather than hang or lie."""

    def test_a_repeated_bail_out_keeps_the_first_failure_message(self) -> None:
        report = validate_tap_report(
            (fixtures_root() / "tap/invalid/repeated_bail_out.tap").read_text(encoding="utf-8")
        )
        self.assertTrue(report.bailed_out)
        self.assertEqual("Test host terminated", report.bail_message)
        self.assertEqual(
            [ViolationCode.CONTENT_AFTER_BAIL_OUT],
            [violation.code for violation in report.violations],
        )

    def test_an_unterminated_diagnostic_block_is_not_counted_as_a_result(self) -> None:
        report = validate_tap_report(
            (fixtures_root() / "tap/invalid/truncated_diagnostic_block.tap").read_text(encoding="utf-8")
        )
        self.assertEqual(["id-1"], report.point_ids())
        self.assertFalse(report.complete)

    def test_a_duplicated_identifier_cannot_make_selection_loop_forever(self) -> None:
        # A repeated identifier can make an item its own descendant. Traversal must
        # terminate and hand the caller the violations it already collected.
        discovery = validate_discovery_stream(
            (fixtures_root() / "discovery/invalid/duplicate_id.jsonl").read_text(encoding="utf-8")
        )
        self.assertIn(ViolationCode.DUPLICATE_ID, [violation.code for violation in discovery.violations])

        resolved: list[list[str]] = []
        worker = threading.Thread(target=lambda: resolved.append(expected_leaf_ids(discovery, ["S:math"])[0]))
        worker.daemon = True
        worker.start()
        # A correct implementation returns immediately; the timeout only bounds how
        # long a regression is allowed to spin before the test reports it.
        worker.join(5)
        self.assertFalse(worker.is_alive(), msg="Selection resolution did not terminate")
        self.assertEqual(1, len(resolved))
        self.assertLessEqual(len(resolved[0]), len(discovery.items))


class SkipSemanticsTests(unittest.TestCase):
    """Skip state is discovery-owned and must survive into the report unchanged."""

    def _report(self, point_line: str) -> TapReport:
        return validate_tap_report(
            "TAP version 13\n"
            "# foundry-test-adapter: 1\n"
            "1..1\n" + point_line + "  ---\n"
            "  _foundry:\n"
            '    id: "S1::a"\n'
            "    duration_ms: 0\n"
            '    status_detail: ""\n'
            "  ...\n"
        )

    def test_a_failing_point_cannot_be_a_skip(self) -> None:
        report = self._report("not ok 1 - a # SKIP pending\n")
        codes = [violation.code for violation in report.violations]
        self.assertIn(ViolationCode.INVALID_DIRECTIVE, codes)
        self.assertIn(ViolationCode.MISSING_FIELD, codes)

    def test_a_skip_requires_a_reason(self) -> None:
        report = self._report("ok 1 - a # SKIP\n")
        self.assertEqual(
            [ViolationCode.INVALID_DIRECTIVE],
            [violation.code for violation in report.violations],
        )

    def test_a_report_may_not_change_the_discovered_skip_reason(self) -> None:
        discovery = validate_discovery_stream(
            (fixtures_root() / "run/discovery_with_skip.jsonl").read_text(encoding="utf-8")
        )
        self.assertTrue(discovery.conforms, msg=str(discovery.violations))
        report = self._report("ok 1 - a # SKIP a different reason\n")
        self.assertTrue(report.conforms, msg=str(report.violations))
        self.assertEqual(
            [ViolationCode.SKIP_STATE_MISMATCH],
            [violation.code for violation in validate_run(discovery, report, [])],
        )


class ParentIdentityTests(unittest.TestCase):
    def test_a_record_cannot_be_its_own_parent(self) -> None:
        # Self-parenting would otherwise satisfy the parent check and then hide the
        # item from every run, because an item that is its own child is never a leaf.
        result = validate_discovery_stream(
            (fixtures_root() / "discovery/invalid/self_parented.jsonl").read_text(encoding="utf-8")
        )
        self.assertEqual(
            [ViolationCode.UNKNOWN_PARENT],
            [violation.code for violation in result.violations],
        )
