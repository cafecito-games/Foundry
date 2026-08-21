#!/usr/bin/env python3
"""Cross-checks of the mined-history records against the report the real runner wrote.

The C++ validator under test_type_completeness_history.h is the authority over the records; these
tests prove the part the validator cannot see: that the coordinates a record maps to are executed
by a real `test completeness run` on both surfaces and that the report covers the record's required
dimensions.

Run with: python3 -m unittest discover -s scripts/tests -p "test_type_completeness_*.py"
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path
from typing import Any

from test_type_completeness_comparator import _load

report = _load("report")

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "type_completeness"
HISTORY = REPO_ROOT / "modules" / "foundry_script" / "tests" / "type_completeness" / "history"
ID_PATTERN = re.compile(r"^[a-z0-9_]{1,64}$")


def _records() -> list[tuple[Path, dict[str, Any]]]:
    return [
        (path, json.loads(path.read_text(encoding="utf-8")))
        for path in sorted(HISTORY.glob("*.json"))
        if path.name != "schema.json"
    ]


class HistoryRecordTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema = json.loads((HISTORY / "schema.json").read_text(encoding="utf-8"))
        self.records = _records()
        self.assertTrue(self.records)
        self.report = report.load_report_file(FIXTURES / "runner_report_union_passed.json")

    def test_every_record_is_keyed_by_its_file_name(self) -> None:
        for path, record in self.records:
            with self.subTest(record=path.name):
                self.assertRegex(path.stem, ID_PATTERN)
                self.assertEqual(record["item_id"], path.stem)
                self.assertIn(record["disposition"], self.schema["dispositions"])
                self.assertIn(record["reverification"]["result"], self.schema["reverification_results"])
                self.assertRegex(record["reverification"]["commit"], r"^[0-9a-f]{40}$")

    def test_mapped_coordinates_are_executed_on_both_surfaces_with_their_dimensions(self) -> None:
        coverage = self.report.raw["coverage_by_dimension"]
        mapped = 0
        for path, record in self.records:
            source = record.get("seed") if record["disposition"] == "seeded" else record["mapping"]
            if "case_coordinates" not in source:
                continue
            mapped += 1
            with self.subTest(record=path.name):
                self.assertEqual(source["family"], self.report.family)
                for surface in ("text", "bytecode"):
                    coordinates = dict(source["case_coordinates"], surface=surface)
                    matches = [case for case in self.report.cases if case.coordinates == coordinates]
                    self.assertEqual(len(matches), 1, f"{coordinates} executed {len(matches)} times")
                for dimension in source["required_dimensions"]:
                    self.assertGreater(coverage.get(dimension, 0.0), 0.0, dimension)
        self.assertGreater(mapped, 0)

    def test_still_failing_records_keep_their_probe_under_history_evidence(self) -> None:
        for path, record in self.records:
            if record["reverification"]["result"] != "still_failing":
                continue
            with self.subTest(record=path.name):
                evidence = REPO_ROOT / record["reverification"]["evidence_path"]
                self.assertEqual(evidence.parent, HISTORY / "evidence")
                self.assertEqual(evidence.stem, path.stem)
                self.assertTrue(evidence.is_file())


if __name__ == "__main__":
    unittest.main()
