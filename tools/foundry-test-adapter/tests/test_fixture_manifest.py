"""Every normative fixture produces the expected validator result."""

from __future__ import annotations

import io
import json
import unittest

from _support import FIXTURES
from foundry_test_adapter.cli import main
from foundry_test_adapter.diagnostics import CODE_REGISTRY

MANIFEST_PATH = FIXTURES / "manifest.json"
with MANIFEST_PATH.open(encoding="utf-8") as _handle:
    MANIFEST = json.load(_handle)

CLASSIFICATIONS = {
    "conforming",
    "discovery_failures",
    "test_failures",
    "infrastructure_failure",
    "cancelled",
    "unsupported",
    "invalid",
}


class ManifestTests(unittest.TestCase):
    def test_every_manifest_entry_matches(self) -> None:
        stdout, stderr = io.StringIO(), io.StringIO()
        exit_code = main(["fixtures", str(MANIFEST_PATH)], stdout, stderr)
        self.assertEqual(0, exit_code, stderr.getvalue())
        self.assertIn("{} fixture(s) checked".format(len(MANIFEST["fixtures"])), stdout.getvalue())

    def test_manifest_entries_have_stable_unique_identifiers(self) -> None:
        identifiers = [entry["id"] for entry in MANIFEST["fixtures"]]
        self.assertEqual(sorted(set(identifiers)), sorted(identifiers))

    def test_manifest_entries_use_the_closed_registries(self) -> None:
        for entry in MANIFEST["fixtures"]:
            with self.subTest(fixture=entry["id"]):
                self.assertIn(entry["operation"], ("capabilities", "discovery", "report"))
                self.assertIn(entry["expected"]["classification"], CLASSIFICATIONS)
                self.assertIn(entry["expected"]["validator_exit"], (0, 1, 2))
                for code in entry["expected"]["codes"]:
                    self.assertIn(code, CODE_REGISTRY)

    def test_referenced_artifacts_exist_unless_deliberately_missing(self) -> None:
        for entry in MANIFEST["fixtures"]:
            referenced = [entry["artifact"]] + ([entry["discovery"]] if entry["discovery"] else [])
            missing = [name for name in referenced if not (FIXTURES / name).exists()]
            expects_missing = "artifact.missing" in entry["expected"]["codes"] or entry["cancelled"]
            with self.subTest(fixture=entry["id"]):
                if expects_missing:
                    continue
                self.assertEqual([], missing)

    def test_every_lifecycle_row_is_covered(self) -> None:
        observed = {
            (entry["operation"], entry["expected"]["valid"], entry["expected"]["complete"],
             entry["expected"]["classification"])
            for entry in MANIFEST["fixtures"]
        }
        required = {
            ("capabilities", True, True, "conforming"),
            ("capabilities", False, True, "unsupported"),
            ("capabilities", False, False, "unsupported"),
            ("capabilities", False, True, "invalid"),
            ("discovery", True, True, "conforming"),
            ("discovery", True, True, "discovery_failures"),
            ("discovery", False, True, "invalid"),
            ("discovery", False, False, "infrastructure_failure"),
            ("report", True, True, "conforming"),
            ("report", True, True, "test_failures"),
            ("report", True, False, "infrastructure_failure"),
            ("report", True, False, "cancelled"),
            ("report", False, False, "infrastructure_failure"),
            ("report", False, True, "invalid"),
            ("report", False, False, "invalid"),
        }
        self.assertEqual(set(), required - observed)

    def test_every_registry_code_is_exercised(self) -> None:
        exercised = set()
        for entry in MANIFEST["fixtures"]:
            exercised.update(entry["expected"]["codes"])
        # `artifact.read` requires a path that exists but cannot be read, which is not
        # expressible as a checked-in file; the CLI tests cover it with a directory.
        self.assertEqual({"artifact.read"}, set(CODE_REGISTRY) - exercised)


if __name__ == "__main__":
    unittest.main()
