"""Behavior of the discovery stream validator."""

from __future__ import annotations

import json
import unittest

from _support import ScratchTestCase
from foundry_test_adapter.json_artifacts import validate_discovery


def record(**fields):
    document = {"protocol": "foundry-test-adapter", "version": 1}
    document.update(fields)
    return json.dumps(document, separators=(",", ":"))


def item_range(start_line=0, start_character=0, end_line=20, end_character=0):
    return {
        "start": {"line": start_line, "character": start_character},
        "end": {"line": end_line, "character": end_character},
    }


def start(root="res://tests"):
    return record(event="discovery_start", root=root)


def end(suites=0, tests=0, errors=0):
    return record(event="discovery_end", suite_count=suites, test_count=tests, error_count=errors)


def suite(identifier="suite-a", label="MathTests", **overrides):
    fields = {
        "event": "suite",
        "id": identifier,
        "label": label,
        "parent_id": None,
        "path": "res://tests/math_tests.fs",
        "range": item_range(),
        "runnable": True,
        "skipped": False,
        "skip_reason": None,
    }
    fields.update(overrides)
    return record(**fields)


def test_record(identifier="test-a", label="adds numbers", parent="suite-a", **overrides):
    fields = {
        "event": "test",
        "id": identifier,
        "label": label,
        "parent_id": parent,
        "path": "res://tests/math_tests.fs",
        "range": item_range(4, 0, 6, 1),
        "runnable": True,
        "skipped": False,
        "skip_reason": None,
        "case_key": None,
    }
    fields.update(overrides)
    return record(**fields)


def stream(*lines):
    return "".join(line + "\n" for line in lines)


class DiscoveryTests(ScratchTestCase):
    def validate(self, content, process_exit=None, name="discovery.jsonl"):
        return validate_discovery(self.write(name, content), process_exit)

    def test_empty_stream_conforms(self) -> None:
        result, model = self.validate(stream(start(), end()), 0)
        self.assertTrue(result.valid)
        self.assertTrue(result.complete)
        self.assertEqual("conforming", result.classification)
        self.assertEqual((), model.items)
        self.assertEqual("res://tests", model.root)

    def test_nested_stream_preserves_order(self) -> None:
        result, model = self.validate(
            stream(start(), suite(), test_record(), test_record("test-b", case_key="case-2"), end(1, 2, 0)), 0
        )
        self.assertTrue(result.valid)
        self.assertEqual(["suite-a", "test-a", "test-b"], [item.id for item in model.items])

    def test_recoverable_errors_exit_one(self) -> None:
        content = stream(
            start(),
            suite(),
            record(
                event="discovery_error",
                id="error-a",
                label="BrokenTests discovery",
                parent_id=None,
                message="Unable to index suite",
                path=None,
                range=None,
            ),
            end(1, 0, 1),
        )
        result, _ = self.validate(content, 1)
        self.assertTrue(result.valid)
        self.assertEqual("discovery_failures", result.classification)

    def test_wrong_exit_for_a_valid_stream_is_invalid(self) -> None:
        result, _ = self.validate(stream(start(), end()), 1)
        self.assertEqual(("discovery.exit",), result.codes)
        self.assertEqual("invalid", result.classification)
        self.assertTrue(result.complete)

    def test_malformed_line_does_not_stop_the_stream(self) -> None:
        result, model = self.validate(stream(start(), "{oops", suite(), end(1, 0, 0)))
        self.assertEqual(("discovery.json",), result.codes)
        self.assertEqual(["suite-a"], [item.id for item in model.items])

    def test_schema_failure_skips_that_record_only(self) -> None:
        result, model = self.validate(stream(start(), record(event="module"), suite(), end(1, 0, 0)))
        self.assertEqual({"discovery.schema"}, set(result.codes))
        self.assertEqual(["suite-a"], [item.id for item in model.items])

    def test_line_separator_inside_a_string_is_not_a_record_boundary(self) -> None:
        # U+2028 terminates a line for str.splitlines() but never for JSONL.
        content = stream(start(), suite(label="Math\u2028Tests"), end(1, 0, 0))
        result, model = self.validate(content)
        self.assertEqual((), result.codes)
        self.assertEqual(1, len(model.items))

    def test_missing_end_is_infrastructure_failure(self) -> None:
        result, _ = self.validate(stream(start(), suite()))
        self.assertEqual({"discovery.end", "discovery.incomplete"}, set(result.codes))
        self.assertFalse(result.complete)
        self.assertEqual("infrastructure_failure", result.classification)

    def test_truncated_final_record_is_incomplete(self) -> None:
        result, _ = self.validate(stream(start(), suite()) + test_record())
        self.assertIn("discovery.line_ending", result.codes)
        self.assertIn("discovery.incomplete", result.codes)
        self.assertFalse(result.complete)

    def test_duplicate_identifiers_are_rejected(self) -> None:
        result, _ = self.validate(stream(start(), suite(), suite(), end(2, 0, 0)))
        self.assertEqual(("discovery.duplicate_id",), result.codes)

    def test_parent_must_be_a_previously_emitted_suite(self) -> None:
        for content in (
            stream(start(), test_record(parent="suite-missing"), end(0, 1, 0)),
            stream(start(), suite(), test_record(), test_record("test-b", parent="test-a"), end(1, 2, 0)),
            stream(start(), test_record(), suite(), end(1, 1, 0)),
        ):
            with self.subTest(content=content):
                result, _ = self.validate(content)
                self.assertIn("discovery.parent", result.codes)

    def test_non_canonical_paths_are_rejected(self) -> None:
        result, _ = self.validate(
            stream(start(), suite(path="res://tests/../tests/a.fs"), end(1, 0, 0))
        )
        self.assertEqual(("discovery.path",), result.codes)

    def test_range_requires_a_path_and_forward_order(self) -> None:
        without_path, _ = self.validate(stream(start(), suite(path=None), end(1, 0, 0)))
        self.assertEqual(("discovery.range",), without_path.codes)
        reversed_range, _ = self.validate(
            stream(start(), suite(range=item_range(9, 0, 2, 0)), end(1, 0, 0))
        )
        self.assertEqual(("discovery.range",), reversed_range.codes)

    def test_skip_state_invariants(self) -> None:
        cases = (
            stream(start(), suite(), test_record(skipped=True), end(1, 1, 0)),
            stream(start(), suite(), test_record(skip_reason="pending"), end(1, 1, 0)),
            stream(start(), suite(), test_record(runnable=False, skipped=True, skip_reason="x"), end(1, 1, 0)),
            stream(start(), suite(skipped=True, skip_reason="disabled"), test_record(), end(1, 1, 0)),
        )
        for content in cases:
            with self.subTest(content=content):
                result, _ = self.validate(content)
                self.assertIn("discovery.skip", result.codes)

    def test_counts_must_match_emitted_records(self) -> None:
        result, _ = self.validate(stream(start(), suite(), end(2, 0, 0)))
        self.assertEqual(("discovery.counts",), result.codes)

    def test_counts_are_suppressed_when_a_record_was_rejected(self) -> None:
        result, _ = self.validate(stream(start(), "{oops", suite(), end(9, 9, 9)))
        self.assertEqual(("discovery.json",), result.codes)

    def test_encoding_failures_stop_validation(self) -> None:
        for content in (b"\xef\xbb\xbf{}\n", b'{"protocol":"\xff"}\n'):
            with self.subTest(content=content):
                result, model = self.validate(content)
                self.assertEqual(("artifact.encoding",), result.codes)
                self.assertEqual("infrastructure_failure", result.classification)
                self.assertIsNone(model)

    def test_missing_artifact_is_infrastructure_failure(self) -> None:
        result, model = validate_discovery(self.missing("absent.jsonl"))
        self.assertEqual(("artifact.missing",), result.codes)
        self.assertEqual("infrastructure_failure", result.classification)
        self.assertIsNone(model)


if __name__ == "__main__":
    unittest.main()
