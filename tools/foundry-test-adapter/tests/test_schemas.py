"""The checked-in schemas are normative Draft 2020-12 documents."""

from __future__ import annotations

import json
import unittest

from _support import PROTOCOL
from jsonschema import Draft202012Validator

CAPABILITIES = {
    "protocol": "foundry-test-adapter",
    "supported_versions": [1],
    "framework": {"id": "fixture", "name": "Fixture", "version": "1.0.0"},
    "extensions": [],
}

RANGE = {"start": {"line": 0, "character": 0}, "end": {"line": 20, "character": 0}}

RECORDS = {
    "discovery_start": {"event": "discovery_start", "root": "res://tests"},
    "suite": {
        "event": "suite",
        "id": "suite-a",
        "label": "MathTests",
        "parent_id": None,
        "path": "res://tests/math_tests.fs",
        "range": RANGE,
        "runnable": True,
        "skipped": False,
        "skip_reason": None,
    },
    "test": {
        "event": "test",
        "id": "test-a",
        "label": "adds numbers",
        "parent_id": "suite-a",
        "path": "res://tests/math_tests.fs",
        "range": RANGE,
        "runnable": True,
        "skipped": False,
        "skip_reason": None,
        "case_key": None,
    },
    "discovery_error": {
        "event": "discovery_error",
        "id": "error-a",
        "label": "BrokenTests discovery",
        "parent_id": None,
        "message": "Unable to index suite",
        "path": None,
        "range": None,
    },
    "discovery_end": {"event": "discovery_end", "suite_count": 1, "test_count": 1, "error_count": 1},
}


def load(name):
    value = json.loads((PROTOCOL / name).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError("{} must contain an object".format(name))
    return value


def envelope(fields):
    document = {"protocol": "foundry-test-adapter", "version": 1}
    document.update(fields)
    return document


class SchemaTests(unittest.TestCase):
    def test_schemas_are_valid_draft_2020_12(self) -> None:
        for name in ("capabilities.schema.json", "discovery-record.schema.json"):
            Draft202012Validator.check_schema(load(name))

    def test_schema_identifiers_are_the_normative_urns(self) -> None:
        self.assertEqual("urn:foundry:test-adapter:v1:capabilities", load("capabilities.schema.json")["$id"])
        self.assertEqual(
            "urn:foundry:test-adapter:v1:discovery-record", load("discovery-record.schema.json")["$id"]
        )

    def test_minimal_capabilities_validate(self) -> None:
        validator = Draft202012Validator(load("capabilities.schema.json"))
        self.assertEqual([], list(validator.iter_errors(CAPABILITIES)))

    def test_every_record_event_validates(self) -> None:
        validator = Draft202012Validator(load("discovery-record.schema.json"))
        for event, fields in RECORDS.items():
            with self.subTest(event=event):
                self.assertEqual([], list(validator.iter_errors(envelope(fields))))

    def test_schemas_tolerate_additive_properties(self) -> None:
        capabilities = Draft202012Validator(load("capabilities.schema.json"))
        document = dict(CAPABILITIES, future_field=1)
        self.assertEqual([], list(capabilities.iter_errors(document)))

        records = Draft202012Validator(load("discovery-record.schema.json"))
        record = envelope(dict(RECORDS["test"], tags=["fast"]))
        self.assertEqual([], list(records.iter_errors(record)))

    def test_control_characters_are_rejected_in_identity_fields(self) -> None:
        validator = Draft202012Validator(load("discovery-record.schema.json"))
        record = envelope(dict(RECORDS["test"], label="two\nlines"))
        self.assertNotEqual([], list(validator.iter_errors(record)))


if __name__ == "__main__":
    unittest.main()
