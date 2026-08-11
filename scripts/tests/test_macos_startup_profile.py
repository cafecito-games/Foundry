"""Unit tests for `scripts/macos_startup_profile.py`.

Covers the pure decision logic that the window measurement rests on: which entry in the
CoreGraphics window list counts as "the application window is on screen". Getting this wrong
silently reports either a status item or a window that never appeared, so it is worth pinning
without launching an editor.
"""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "scripts" / "macos_startup_profile.py"

_spec = importlib.util.spec_from_file_location("macos_startup_profile", MODULE_PATH)
assert _spec is not None and _spec.loader is not None
macos_startup_profile = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(macos_startup_profile)


def window(
    pid: int, *, layer: int = 0, width: float = 1280, height: float = 800, on_screen: bool = True
) -> dict[str, Any]:
    return {
        "kCGWindowOwnerPID": pid,
        "kCGWindowLayer": layer,
        "kCGWindowIsOnscreen": on_screen,
        "kCGWindowBounds": {"X": 0.0, "Y": 0.0, "Width": width, "Height": height},
    }


class HasVisibleWindowTests(unittest.TestCase):
    def test_reports_an_on_screen_application_window(self):
        self.assertTrue(macos_startup_profile._has_visible_window([window(42)], 42))

    def test_ignores_windows_owned_by_other_processes(self):
        self.assertFalse(macos_startup_profile._has_visible_window([window(99)], 42))

    def test_ignores_panels_and_status_items_above_layer_zero(self):
        self.assertFalse(macos_startup_profile._has_visible_window([window(42, layer=25)], 42))

    def test_ignores_windows_that_are_not_on_screen_yet(self):
        self.assertFalse(macos_startup_profile._has_visible_window([window(42, on_screen=False)], 42))

    def test_ignores_windows_smaller_than_the_minimum_side(self):
        side = macos_startup_profile.MIN_WINDOW_SIDE_PX
        self.assertFalse(macos_startup_profile._has_visible_window([window(42, width=side, height=side)], 42))
        self.assertTrue(macos_startup_profile._has_visible_window([window(42, width=side + 1, height=side + 1)], 42))

    def test_tolerates_entries_without_bounds(self):
        borderless = {"kCGWindowOwnerPID": 42, "kCGWindowLayer": 0, "kCGWindowIsOnscreen": True}
        self.assertFalse(macos_startup_profile._has_visible_window([borderless], 42))

    def test_finds_the_application_window_among_unrelated_entries(self):
        windows = [window(1, layer=25), window(99), window(42, on_screen=False), window(42)]
        self.assertTrue(macos_startup_profile._has_visible_window(windows, 42))


class AcceptanceGateTests(unittest.TestCase):
    def summary(self, median_ms: float) -> dict[str, Any]:
        return {"window": {"time_to_first_window_ms": macos_startup_profile.spread([median_ms])}}

    def test_window_criterion_passes_under_the_bound(self):
        acceptance = macos_startup_profile.evaluate_acceptance(self.summary(420.0), 8.0, 250.0)
        checks = [check for check in acceptance["checks"] if check["source"] == "window"]
        self.assertEqual(len(checks), 1)
        self.assertTrue(checks[0]["passed"])

    def test_window_criterion_fails_over_the_bound(self):
        acceptance = macos_startup_profile.evaluate_acceptance(self.summary(773.7), 8.0, 250.0)
        checks = [check for check in acceptance["checks"] if check["source"] == "window"]
        self.assertEqual(len(checks), 1)
        self.assertFalse(checks[0]["passed"])
        self.assertFalse(acceptance["passed"])

    def test_no_window_measurement_adds_no_criterion(self):
        acceptance = macos_startup_profile.evaluate_acceptance({}, 8.0, 250.0)
        self.assertEqual([check for check in acceptance["checks"] if check["source"] == "window"], [])


class EmptyMeasurementTests(unittest.TestCase):
    """A measurement that aggregates nothing must never be gated on as if it were a zero."""

    def test_run_count_of_zero_is_rejected_at_the_command_line(self):
        with self.assertRaises(Exception):
            macos_startup_profile.positive_int("0")
        with self.assertRaises(Exception):
            macos_startup_profile.positive_int("-1")
        self.assertEqual(macos_startup_profile.positive_int("1"), 1)

    def test_empty_window_measurement_adds_no_criterion(self):
        empty = {"window": {"time_to_first_window_ms": macos_startup_profile.spread([])}}
        acceptance = macos_startup_profile.evaluate_acceptance(empty, 8.0, 250.0)
        self.assertEqual([check for check in acceptance["checks"] if check["source"] == "window"], [])
        self.assertFalse(acceptance["passed"])

    def test_empty_sample_measurement_adds_no_criterion(self):
        empty = {"sample": {"blocked_percent": macos_startup_profile.spread([]), "duration_seconds": 20.0}}
        acceptance = macos_startup_profile.evaluate_acceptance(empty, 8.0, 250.0)
        self.assertEqual([check for check in acceptance["checks"] if check["source"] == "sample"], [])

    def test_a_real_single_run_measurement_still_counts(self):
        one = {"window": {"time_to_first_window_ms": macos_startup_profile.spread([412.0])}}
        acceptance = macos_startup_profile.evaluate_acceptance(one, 8.0, 250.0)
        checks = [check for check in acceptance["checks"] if check["source"] == "window"]
        self.assertEqual(len(checks), 1)
        self.assertTrue(checks[0]["passed"])


class LoadMismatchTests(unittest.TestCase):
    def summary(self, load: float | None) -> dict[str, Any]:
        return {} if load is None else {"machine_load": {"load_average_1m": load}}

    def test_no_warning_when_load_is_comparable(self):
        self.assertIsNone(macos_startup_profile.load_mismatch_warning(self.summary(1.2), self.summary(1.9)))

    def test_warns_when_one_capture_ran_on_a_busy_machine(self):
        warning = macos_startup_profile.load_mismatch_warning(self.summary(0.8), self.summary(11.4))
        self.assertIsNotNone(warning)
        assert warning is not None
        self.assertIn("machine load differs materially", warning)

    def test_warns_symmetrically(self):
        self.assertIsNotNone(macos_startup_profile.load_mismatch_warning(self.summary(11.4), self.summary(0.8)))

    def test_summaries_without_load_are_not_flagged(self):
        self.assertIsNone(macos_startup_profile.load_mismatch_warning(self.summary(None), self.summary(1.0)))
        self.assertIsNone(macos_startup_profile.load_mismatch_warning(self.summary(1.0), self.summary(None)))


if __name__ == "__main__":
    unittest.main()
