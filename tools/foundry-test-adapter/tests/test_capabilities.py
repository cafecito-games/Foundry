"""Behavior of the capabilities validator."""

from __future__ import annotations

import json
import unittest

from _support import ScratchTestCase
from foundry_test_adapter.json_artifacts import validate_capabilities

DOCUMENT = {
    "protocol": "foundry-test-adapter",
    "supported_versions": [1],
    "framework": {"id": "fixture", "name": "Fixture", "version": "1.0.0"},
    "extensions": [],
}


def document(**overrides):
    return json.dumps(dict(DOCUMENT, **overrides)) + "\n"


class CapabilitiesTests(ScratchTestCase):
    def test_minimal_document_conforms(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document()), process_exit=0)
        self.assertTrue(result.valid)
        self.assertTrue(result.complete)
        self.assertEqual("conforming", result.classification)
        self.assertEqual((), result.violations)

    def test_multi_version_document_conforms(self) -> None:
        result = validate_capabilities(
            self.write("capabilities.json", document(supported_versions=[1, 3])), process_exit=0
        )
        self.assertTrue(result.valid)

    def test_unsorted_versions_report_version_order(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document(supported_versions=[3, 1])))
        self.assertEqual(("capabilities.version_order",), result.codes)
        self.assertTrue(result.complete)
        self.assertEqual("unsupported", result.classification)

    def test_missing_v1_is_unsupported(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document(supported_versions=[2])))
        self.assertEqual(("capabilities.version_order",), result.codes)
        self.assertEqual("unsupported", result.classification)

    def test_schema_failures_are_reported_together(self) -> None:
        broken = json.dumps({"protocol": "other", "supported_versions": [], "framework": {}, "extensions": []}) + "\n"
        result = validate_capabilities(self.write("capabilities.json", broken))
        self.assertEqual({"capabilities.schema"}, set(result.codes))
        self.assertGreater(len(result.violations), 1)
        self.assertTrue(all(violation.path for violation in result.violations))

    def test_malformed_json_suppresses_schema_checks(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", "{\n"))
        self.assertEqual(("capabilities.json",), result.codes)
        self.assertFalse(result.complete)

    def test_non_standard_json_constants_are_rejected(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", '{"supported_versions": [NaN]}\n'))
        self.assertEqual(("capabilities.json",), result.codes)

    def test_missing_terminal_line_feed_is_incomplete(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document().rstrip("\n")))
        self.assertEqual(("capabilities.line_ending",), result.codes)
        self.assertFalse(result.complete)

    def test_crlf_is_a_line_ending_violation(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document().replace("\n", "\r\n")))
        self.assertEqual(("capabilities.line_ending",), result.codes)
        self.assertFalse(result.complete)

    def test_byte_order_mark_is_an_encoding_violation(self) -> None:
        result = validate_capabilities(
            self.write("capabilities.json", b"\xef\xbb\xbf" + document().encode("utf-8"))
        )
        self.assertEqual(("artifact.encoding",), result.codes)
        self.assertFalse(result.valid)
        self.assertFalse(result.complete)
        self.assertEqual("unsupported", result.classification)

    def test_invalid_utf8_is_an_encoding_violation(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", b'{"protocol": "\xff"}\n'))
        self.assertEqual(("artifact.encoding",), result.codes)

    def test_missing_artifact_is_unsupported(self) -> None:
        result = validate_capabilities(self.missing("absent.json"))
        self.assertEqual(("artifact.missing",), result.codes)
        self.assertEqual("unsupported", result.classification)

    def test_nonzero_exit_on_a_valid_document_is_invalid(self) -> None:
        result = validate_capabilities(self.write("capabilities.json", document()), process_exit=1)
        self.assertEqual(("capabilities.exit",), result.codes)
        self.assertTrue(result.complete)
        self.assertEqual("invalid", result.classification)


if __name__ == "__main__":
    unittest.main()
