"""Unit tests for report.py (stdlib unittest; runnable under pytest too)."""

import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import report  # noqa: E402

REPORT_PATH = Path(__file__).resolve().parent / "report.py"


class BuildRowsTest(unittest.TestCase):
    def test_pairs_computes_overhead_net_of_floor(self):
        data = {
            "gdscript:_baseline/empty_loop": 100.0,
            "gdscript:trait_call/baseline": 200.0,
            "gdscript:trait_call/feature": 300.0,
        }
        rows = report.build_rows(data)
        row = {r["case"]: r for r in rows}["trait_call"]
        # net feature = 300-100=200, net baseline = 200-100=100 -> 100% overhead.
        self.assertEqual(row["overhead_percent"], 100.0)
        self.assertEqual(row["overhead_abs"], 100.0)

    def test_no_floor_key_uses_zero_floor(self):
        data = {
            "gdscript:trait_call/baseline": 200.0,
            "gdscript:trait_call/feature": 300.0,
        }
        rows = report.build_rows(data)
        row = {r["case"]: r for r in rows}["trait_call"]
        self.assertEqual(row["overhead_abs"], 100.0)
        self.assertEqual(row["overhead_percent"], 50.0)

    def test_baseline_case_is_excluded_from_rows(self):
        data = {
            "gdscript:_baseline/empty_loop": 100.0,
            "gdscript:trait_call/baseline": 200.0,
            "gdscript:trait_call/feature": 300.0,
        }
        rows = report.build_rows(data)
        self.assertNotIn("_baseline", {r["case"] for r in rows})

    def test_incomplete_pair_is_skipped(self):
        data = {
            "gdscript:trait_call/feature": 300.0,
        }
        self.assertEqual(report.build_rows(data), [])

    def test_rows_are_sorted_by_case(self):
        data = {
            "gdscript:zeta/baseline": 100.0,
            "gdscript:zeta/feature": 110.0,
            "gdscript:alpha/baseline": 100.0,
            "gdscript:alpha/feature": 110.0,
        }
        rows = report.build_rows(data)
        self.assertEqual([r["case"] for r in rows], ["alpha", "zeta"])

    def test_multiple_cases_share_one_floor(self):
        data = {
            "gdscript:_baseline/empty_loop": 50.0,
            "gdscript:a/baseline": 150.0,
            "gdscript:a/feature": 250.0,
            "gdscript:b/baseline": 100.0,
            "gdscript:b/feature": 100.0,
        }
        rows = {r["case"]: r for r in report.build_rows(data)}
        # a: net baseline 100, net feature 200 -> 100%.
        self.assertEqual(rows["a"]["overhead_percent"], 100.0)
        # b: net baseline 50, net feature 50 -> 0%.
        self.assertEqual(rows["b"]["overhead_percent"], 0.0)


class CompareTest(unittest.TestCase):
    def test_compare_flags_regression(self):
        old = {"gdscript:trait_call/feature": 100.0}
        new = {"gdscript:trait_call/feature": 130.0}
        regressed = report.compare(old, new, tolerance_percent=10.0)
        self.assertTrue(any(r["regressed"] for r in regressed))

    def test_compare_within_tolerance_is_not_regression(self):
        old = {"gdscript:trait_call/feature": 100.0}
        new = {"gdscript:trait_call/feature": 105.0}
        results = report.compare(old, new, tolerance_percent=10.0)
        self.assertFalse(any(r["regressed"] for r in results))

    def test_compare_improvement_is_not_regression(self):
        old = {"gdscript:trait_call/feature": 100.0}
        new = {"gdscript:trait_call/feature": 70.0}
        results = report.compare(old, new, tolerance_percent=10.0)
        self.assertEqual(results[0]["delta_percent"], -30.0)
        self.assertFalse(results[0]["regressed"])

    def test_compare_skips_missing_or_nonpositive_old_keys(self):
        old = {"gdscript:a/feature": 0.0}
        new = {
            "gdscript:a/feature": 100.0,
            "gdscript:b/feature": 100.0,
        }
        results = report.compare(old, new, tolerance_percent=10.0)
        self.assertEqual(results, [])

    def test_compare_exactly_at_tolerance_is_not_regression(self):
        old = {"gdscript:a/feature": 100.0}
        new = {"gdscript:a/feature": 110.0}
        results = report.compare(old, new, tolerance_percent=10.0)
        self.assertFalse(results[0]["regressed"])

    def test_compare_flags_benchmark_dropped_from_new_run(self):
        old = {"gdscript:a/feature": 100.0, "gdscript:b/feature": 100.0}
        new = {"gdscript:a/feature": 100.0}
        results = report.compare(old, new, tolerance_percent=10.0)
        by_key = {r["key"]: r for r in results}
        self.assertTrue(by_key["gdscript:b/feature"]["missing"])
        self.assertTrue(by_key["gdscript:b/feature"]["regressed"])
        self.assertIsNone(by_key["gdscript:b/feature"]["new_us"])
        self.assertFalse(by_key["gdscript:a/feature"]["missing"])

    def test_main_compare_nonzero_when_benchmark_dropped(self):
        # Exercised through main() to confirm the dropped key fails the gate.
        results = report.compare(
            {"gdscript:a/feature": 100.0, "gdscript:b/feature": 100.0},
            {"gdscript:a/feature": 100.0},
        )
        self.assertTrue(any(r["regressed"] for r in results))

    def test_compare_uses_exact_delta_not_rounded(self):
        # 1000 -> 1100.4 is a 10.04% increase: over a 10% tolerance even though
        # the displayed delta rounds to 10.0. The regression must still flag.
        old = {"gdscript:a/feature": 1000.0}
        new = {"gdscript:a/feature": 1100.4}
        results = report.compare(old, new, tolerance_percent=10.0)
        self.assertEqual(results[0]["delta_percent"], 10.0)
        self.assertTrue(results[0]["regressed"])


class CliTest(unittest.TestCase):
    def _write_json(self, payload):
        handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, dir=tempfile.gettempdir())
        json.dump(payload, handle)
        handle.close()
        self.addCleanup(os.unlink, handle.name)
        return handle.name

    def test_main_renders_table(self):
        path = self._write_json(
            {
                "gdscript:_baseline/empty_loop": 100.0,
                "gdscript:trait_call/baseline": 200.0,
                "gdscript:trait_call/feature": 300.0,
            }
        )
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = report.main([path])
        self.assertEqual(code, 0)
        self.assertIn("trait_call", buffer.getvalue())

    def test_main_compare_returns_nonzero_on_regression(self):
        old = self._write_json({"gdscript:trait_call/feature": 100.0})
        new = self._write_json({"gdscript:trait_call/feature": 130.0})
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = report.main(["--compare", old, new])
        self.assertEqual(code, 1)
        self.assertIn("REGRESSION", buffer.getvalue())

    def test_main_compare_returns_zero_when_clean(self):
        old = self._write_json({"gdscript:trait_call/feature": 100.0})
        new = self._write_json({"gdscript:trait_call/feature": 102.0})
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = report.main(["--compare", old, new])
        self.assertEqual(code, 0)

    def test_main_custom_tolerance(self):
        old = self._write_json({"gdscript:trait_call/feature": 100.0})
        new = self._write_json({"gdscript:trait_call/feature": 130.0})
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = report.main(["--compare", old, new, "--tolerance", "50"])
        self.assertEqual(code, 0)

    def test_cli_subprocess_exit_code_on_regression(self):
        old = self._write_json({"gdscript:trait_call/feature": 100.0})
        new = self._write_json({"gdscript:trait_call/feature": 130.0})
        result = subprocess.run(
            [sys.executable, str(REPORT_PATH), "--compare", old, new],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)


if __name__ == "__main__":
    unittest.main()
