# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
"""Behavior of the validator on inputs that are easier to state in code than as fixtures."""

from __future__ import annotations

import json
import unittest

from foundry_test_adapter import (
    ALL_VIOLATION_CODES,
    PROTOCOL_NAME,
    Violation,
    ViolationCode,
    expected_leaf_ids,
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
