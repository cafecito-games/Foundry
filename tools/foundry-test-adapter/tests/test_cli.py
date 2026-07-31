"""The stable command-line surface and JSON result document."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest

from _reports import point, report
from _support import FIXTURES, ROOT, ScratchTestCase

NESTED = str(FIXTURES / "valid" / "discovery" / "nested.jsonl")
CAPABILITIES = str(FIXTURES / "valid" / "capabilities" / "minimal.json")
MANIFEST = str(FIXTURES / "manifest.json")


def run(*arguments):
    environment = dict(os.environ)
    environment["PYTHONPATH"] = os.pathsep.join(
        [str(ROOT / "src")] + ([environment["PYTHONPATH"]] if "PYTHONPATH" in environment else [])
    )
    return subprocess.run(
        [sys.executable, "-m", "foundry_test_adapter.cli", *arguments],
        capture_output=True,
        text=True,
        env=environment,
    )


class CommandSurfaceTests(ScratchTestCase):
    def test_capabilities_json_result_document(self) -> None:
        completed = run("capabilities", CAPABILITIES, "--exit-code", "0", "--format", "json")
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual(
            {
                "validator": "foundry-test-adapter",
                "validator_version": "1.0.0",
                "protocol_version": 1,
                "artifact": "capabilities",
                "valid": True,
                "complete": True,
                "classification": "conforming",
                "violations": [],
            },
            document,
        )

    def test_violation_entries_carry_the_documented_keys(self) -> None:
        artifact = self.write("discovery.jsonl", '{"protocol":"foundry-test-adapter","version":1,'
                                                 '"event":"discovery_start","root":"res://tests"}\n'
                                                 '{"protocol":"foundry-test-adapter","version":1,'
                                                 '"event":"discovery_start","root":"res://tests"}\n')
        completed = run("discovery", artifact, "--format", "json")
        self.assertEqual(1, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual("discovery", document["artifact"])
        for violation in document["violations"]:
            self.assertEqual({"code", "message", "file", "line", "column", "path"}, set(violation))

    def test_text_mode_writes_diagnostics_to_stderr(self) -> None:
        completed = run("capabilities", self.write("capabilities.json", "{\n"))
        self.assertEqual(1, completed.returncode)
        self.assertEqual("", completed.stdout)
        self.assertIn("capabilities.json", completed.stderr)

    def test_report_operation_with_discovery_and_selections(self) -> None:
        artifact = self.write("report.tap", report(1, point(1, test_id="test-c")))
        completed = run("report", artifact, "--discovery", NESTED, "--select", "suite-b",
                        "--exit-code", "0", "--format", "json")
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertEqual("conforming", json.loads(completed.stdout)["classification"])

    def test_a_selection_identifier_may_be_spelled_like_a_separator(self) -> None:
        # `--select --` selects the opaque ID `--`; it never begins framework arguments.
        artifact = self.write("report.tap", report(1, point(1, test_id="test-c")))
        completed = run("report", artifact, "--discovery", NESTED, "--select", "--", "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["report.selection"], [entry["code"] for entry in document["violations"]])
        self.assertIn("'--'", document["violations"][0]["message"])

    def test_a_selection_identifier_may_be_spelled_like_an_option(self) -> None:
        artifact = self.write("report.tap", report(1, point(1, test_id="test-c")))
        completed = run("report", artifact, "--discovery", NESTED, "--select", "--report", "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertIn("'--report'", document["violations"][0]["message"])

    def test_cancelled_and_exit_code_are_mutually_exclusive(self) -> None:
        artifact = self.write("report.tap", report(3, point(1)))
        completed = run("report", artifact, "--cancelled", "--exit-code", "1", "--format", "json")
        self.assertEqual(2, completed.returncode)
        self.assertEqual("", completed.stdout)

    def test_cancelled_report_exits_zero(self) -> None:
        artifact = self.write("report.tap", report(3, point(1)))
        completed = run("report", artifact, "--cancelled", "--format", "json")
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual("cancelled", document["classification"])
        self.assertFalse(document["complete"])

    def test_selections_require_a_discovery_context(self) -> None:
        artifact = self.write("report.tap", report(1, point(1)))
        completed = run("report", artifact, "--select", "test-a", "--format", "json")
        self.assertEqual(2, completed.returncode)
        self.assertEqual("", completed.stdout)

    def test_invalid_syntax_never_emits_a_json_document(self) -> None:
        completed = run("capabilities", "--format", "json")
        self.assertEqual(2, completed.returncode)
        self.assertEqual("", completed.stdout)
        self.assertIn("usage", completed.stderr)

    def test_unknown_operation_exits_two(self) -> None:
        self.assertEqual(2, run("verify", CAPABILITIES).returncode)

    def test_a_missing_artifact_is_a_validation_result_not_an_io_failure(self) -> None:
        completed = run("capabilities", self.missing("absent.json"), "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["artifact.missing"], [entry["code"] for entry in document["violations"]])

    def test_an_unreadable_primary_artifact_exits_two(self) -> None:
        directory = str(self.scratch / "as_directory")
        os.mkdir(directory)
        completed = run("capabilities", directory, "--format", "json")
        self.assertEqual(2, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["artifact.read"], [entry["code"] for entry in document["violations"]])
        self.assertFalse(document["valid"])
        self.assertFalse(document["complete"])
        self.assertEqual("invalid", document["classification"])

    def test_a_missing_discovery_context_keeps_report_completeness(self) -> None:
        artifact = self.write("report.tap", report(1, point(1)))
        completed = run("report", artifact, "--discovery", self.missing("absent.jsonl"), "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["artifact.missing"], [entry["code"] for entry in document["violations"]])
        self.assertTrue(document["complete"])
        self.assertEqual("invalid", document["classification"])

    def test_an_encoded_discovery_context_suppresses_selection_checks(self) -> None:
        context = self.write("discovery.jsonl", b"\xef\xbb\xbf{}\n")
        artifact = self.write("report.tap", report(1, point(1)))
        completed = run("report", artifact, "--discovery", context, "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["artifact.encoding"], [entry["code"] for entry in document["violations"]])

    def test_a_nonconforming_discovery_context_reports_only_selection(self) -> None:
        context = self.write("discovery.jsonl",
                             '{"protocol":"foundry-test-adapter","version":1,"event":"discovery_start",'
                             '"root":"res://tests"}\n')
        artifact = self.write("report.tap", report(1, point(1)))
        completed = run("report", artifact, "--discovery", context, "--format", "json")
        self.assertEqual(1, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["report.selection"], [entry["code"] for entry in document["violations"]])

    def test_an_unreadable_discovery_context_exits_two(self) -> None:
        context = str(self.scratch / "context_directory")
        os.mkdir(context)
        artifact = self.write("report.tap", report(1, point(1)))
        completed = run("report", artifact, "--discovery", context, "--format", "json")
        self.assertEqual(2, completed.returncode)
        document = json.loads(completed.stdout)
        self.assertEqual(["artifact.read"], [entry["code"] for entry in document["violations"]])

    def test_fixtures_operation_runs_the_normative_manifest(self) -> None:
        completed = run("fixtures", MANIFEST)
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertIn("0 mismatch(es)", completed.stdout)

    def test_fixtures_operation_reports_every_mismatch(self) -> None:
        with open(MANIFEST, encoding="utf-8") as handle:
            manifest = json.load(handle)
        for index, entry in enumerate(manifest["fixtures"]):
            entry["artifact"] = str(FIXTURES / entry["artifact"])
            if entry["discovery"] is not None:
                entry["discovery"] = str(FIXTURES / entry["discovery"])
            if index < 3:
                entry["expected"]["valid"] = not entry["expected"]["valid"]
        broken = self.write("manifest.json", json.dumps(manifest))
        completed = run("fixtures", broken)
        self.assertEqual(1, completed.returncode)
        self.assertEqual(3, len(completed.stderr.strip().splitlines()))


if __name__ == "__main__":
    unittest.main()
