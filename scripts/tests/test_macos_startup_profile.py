"""Unit tests for `scripts/macos_startup_profile.py`.

Covers the pure decision logic that the window measurement rests on: which entry in the
CoreGraphics window list counts as "the application window is on screen". Getting this wrong
silently reports either a status item or a window that never appeared, so it is worth pinning
without launching an editor.
"""

from __future__ import annotations

import importlib.util
import itertools
import json
import tempfile
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

    def test_ignores_windows_explicitly_marked_off_screen(self):
        self.assertFalse(macos_startup_profile._has_visible_window([window(42, on_screen=False)], 42))

    def test_accepts_windows_that_omit_the_optional_on_screen_key(self):
        # `kCGWindowIsOnscreen` is optional, and the query is already on-screen-only, so a missing
        # key must not be read as "not visible" — that would report no window for a visible one.
        entry = window(42)
        del entry["kCGWindowIsOnscreen"]
        self.assertTrue(macos_startup_profile._has_visible_window([entry], 42))

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
    def summary(self, *loads: float) -> dict[str, Any]:
        return {"machine_load": {f"m{index}": {"load_average_1m": value} for index, value in enumerate(loads)}}

    def test_no_warning_when_load_is_comparable(self):
        self.assertIsNone(macos_startup_profile.load_mismatch_warning(self.summary(1.2), self.summary(1.9)))

    def test_warns_when_one_capture_ran_on_a_busy_machine(self):
        warning = macos_startup_profile.load_mismatch_warning(self.summary(0.8), self.summary(11.4))
        assert warning is not None
        self.assertIn("machine load varies materially", warning)

    def test_warns_symmetrically(self):
        self.assertIsNotNone(macos_startup_profile.load_mismatch_warning(self.summary(11.4), self.summary(0.8)))

    def test_warns_when_one_campaign_drifted_across_its_own_measurements(self):
        # `all` runs four measurements minutes apart; a build starting halfway through is exactly
        # the case that produced two wrong conclusions before this check existed.
        drifted = self.summary(0.9, 1.1, 9.7, 8.4)
        self.assertIsNotNone(macos_startup_profile.load_mismatch_warning(drifted, self.summary(1.0)))

    def test_summaries_without_load_are_not_flagged(self):
        self.assertIsNone(macos_startup_profile.load_mismatch_warning({}, self.summary(1.0)))
        self.assertIsNone(macos_startup_profile.load_mismatch_warning(self.summary(1.0), {}))

    def test_reads_the_older_single_load_shape(self):
        legacy = {"machine_load": {"load_average_1m": 0.7, "load_average_5m": 0.8, "load_average_15m": 0.9}}
        self.assertEqual(macos_startup_profile.load_averages(legacy), [0.7])
        self.assertIsNotNone(macos_startup_profile.load_mismatch_warning(legacy, self.summary(9.9)))

    def test_records_one_load_per_measurement(self):
        summary: dict[str, Any] = {}
        macos_startup_profile.record_load(summary, "sample")
        macos_startup_profile.record_load(summary, "window")
        self.assertEqual(sorted(summary["machine_load"]), ["sample", "window"])
        self.assertEqual(len(macos_startup_profile.load_averages(summary)), 2)


MS = 1_000_000  # nanoseconds per millisecond, the unit `xctrace` exports event times in.


class SignpostTableBuilder:
    """Builds an `os-signpost` export shaped like a real one.

    Every element in a real export carries an `id` the first time a value appears and a `ref` to
    that id afterwards, and the `process` column is *defined* nested inside `thread` while the row's
    own `process` column is only a `ref`. A parser that only reads a row's direct children never
    sees the definition, so the fixtures reproduce that structure rather than a flattened one.
    """

    def __init__(self) -> None:
        self._ids = itertools.count(1)
        self._rows: list[str] = []
        self._process_ids: dict[int, int] = {}

    def add(
        self,
        time_ns: int,
        name: str,
        *,
        pid: int = 4242,
        process: str = "foundry.macos.editor.arm64",
        subsystem: str | None = None,
        category: str = "PointsOfInterest",
    ) -> SignpostTableBuilder:
        if subsystem is None:
            subsystem = macos_startup_profile.STARTUP_SIGNPOST_SUBSYSTEM
        new = next(self._ids)
        if pid in self._process_ids:
            thread = f'<thread ref="{self._process_ids[pid] - 1}"/>'
            process_column = f'<process ref="{self._process_ids[pid]}"/>'
        else:
            thread_id = next(self._ids)
            process_id = next(self._ids)
            self._process_ids[pid] = process_id
            thread = (
                f'<thread id="{thread_id}" fmt="Main Thread"><tid id="{next(self._ids)}" fmt="0x1">1</tid>'
                f'<process id="{process_id}" fmt="{process} ({pid})">'
                f'<pid id="{next(self._ids)}" fmt="{pid}">{pid}</pid></process></thread>'
            )
            process_column = f'<process ref="{process_id}"/>'
        self._rows.append(
            f'<row><event-time id="{new}" fmt="t">{time_ns}</event-time>'
            f"{thread}{process_column}"
            f'<event-type id="{next(self._ids)}" fmt="Event">Event</event-type>'
            f'<string id="{next(self._ids)}" fmt="Process">Process</string>'
            f'<os-signpost-identifier id="{next(self._ids)}" fmt="E">1</os-signpost-identifier>'
            f'<signpost-name id="{next(self._ids)}" fmt="{name}">{name}</signpost-name>'
            f"<sentinel/><sentinel/>"
            f'<subsystem id="{next(self._ids)}" fmt="{subsystem}">{subsystem}</subsystem>'
            f'<category id="{next(self._ids)}" fmt="{category}">{category}</category>'
            f"<sentinel/></row>"
        )
        return self

    def write(self, directory: Path, filename: str = "signposts.xml") -> Path:
        path = directory / filename
        path.write_text(
            '<?xml version="1.0"?>\n<trace-query-result>\n<node xpath="x">'
            '<schema name="os-signpost"/>' + "\n".join(self._rows) + "</node>\n</trace-query-result>\n"
        )
        return path


class RunloopTableBuilder:
    """Builds a `runloop-events` export shaped like a real one.

    Unlike `os-signpost`, this table has no process column: the emitting process is reachable only
    through the `thread` column, and every row after the first refers to its thread by `ref`. A
    parser that wants to attribute an iteration to a process has to resolve that indirection.
    """

    def __init__(self) -> None:
        self._ids = itertools.count(1)
        self._rows: list[str] = []
        self._threads: dict[tuple[int, str], int] = {}
        self._processes: dict[int, int] = {}

    def add_iteration(
        self, time_ns: int, *, pid: int = 4242, is_main: bool = True, thread_name: str = "Main Thread"
    ) -> RunloopTableBuilder:
        key = (pid, thread_name)
        if key in self._threads:
            thread = f'<thread ref="{self._threads[key]}"/>'
        else:
            thread_id = next(self._ids)
            self._threads[key] = thread_id
            if pid in self._processes:
                # A process is defined once, by whichever thread appears first, and every later
                # thread of that process refers to it. In a real export the *main* thread is
                # usually not the first one seen, so this is the shape that matters most.
                process = f'<process ref="{self._processes[pid]}"/>'
            else:
                process_id = next(self._ids)
                self._processes[pid] = process_id
                process = (
                    f'<process id="{process_id}" fmt="foundry ({pid})">'
                    f'<pid id="{next(self._ids)}" fmt="{pid}">{pid}</pid>'
                    f'<device-session id="{next(self._ids)}" fmt="TODO">TODO</device-session></process>'
                )
            thread = (
                f'<thread id="{thread_id}" fmt="{thread_name} (foundry, pid: {pid})">'
                f'<tid id="{next(self._ids)}" fmt="0x1">1</tid>{process}</thread>'
            )
        self._rows.append(
            f'<row><event-time id="{next(self._ids)}" fmt="t">{time_ns}</event-time>'
            f'<string id="{next(self._ids)}" fmt="recorded">recorded</string>'
            f'<short-string id="{next(self._ids)}" fmt="individual_iteration">individual_iteration</short-string>'
            f'<kdebug-func id="{next(self._ids)}" fmt="START">1</kdebug-func>'
            f'<short-string id="{next(self._ids)}" fmt="r1">r1</short-string>'
            f'<uint64 id="{next(self._ids)}" fmt="1">1</uint64>'
            f'<medium-length-string id="{next(self._ids)}" fmt="m">kCFRunLoopDefaultMode</medium-length-string>'
            f'<boolean id="{next(self._ids)}" fmt="{"Yes" if is_main else "No"}">{int(is_main)}</boolean>'
            f"{thread}</row>"
        )
        return self

    def write(self, directory: Path, filename: str = "runloop.xml") -> Path:
        path = directory / filename
        path.write_text(
            '<?xml version="1.0"?>\n<trace-query-result>\n<node xpath="x">'
            '<schema name="runloop-events"/>' + "\n".join(self._rows) + "</node>\n</trace-query-result>\n"
        )
        return path


class RunloopProcessAttributionTests(unittest.TestCase):
    """Iterations must be attributed to the process that emitted the markers.

    A trace records the launched editor *and its children*. Measuring one process's markers against
    another's run loop would silently combine unrelated timelines, which can turn a real stall into
    a passing number.
    """

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_all_iterations_are_returned_when_no_process_is_requested(self):
        builder = RunloopTableBuilder().add_iteration(100 * MS, pid=1).add_iteration(200 * MS, pid=2)
        iterations, _ = macos_startup_profile.scan_runloop_iterations(builder.write(self.tmp))
        self.assertEqual(iterations, [100 * MS, 200 * MS])

    def test_iterations_from_other_processes_are_excluded(self):
        builder = (
            RunloopTableBuilder()
            .add_iteration(100 * MS, pid=4242)
            .add_iteration(150 * MS, pid=999)
            .add_iteration(200 * MS, pid=4242)
        )
        iterations, _ = macos_startup_profile.scan_runloop_iterations(builder.write(self.tmp), pid=4242)
        self.assertEqual(iterations, [100 * MS, 200 * MS])

    def test_a_thread_referenced_by_ref_still_resolves_to_its_process(self):
        # Only the first row per thread carries the process; the rest are `ref`s.
        builder = RunloopTableBuilder()
        for offset in range(4):
            builder.add_iteration((100 + offset * 10) * MS, pid=4242)
        builder.add_iteration(300 * MS, pid=999)
        iterations, _ = macos_startup_profile.scan_runloop_iterations(builder.write(self.tmp), pid=4242)
        self.assertEqual(len(iterations), 4)

    def test_a_thread_whose_process_is_a_reference_still_resolves(self):
        # The shape that matters in practice: an event thread appears first and defines the process,
        # then the main thread refers to it. Resolving only the nested definition drops every main
        # thread iteration and silently reports the whole interval as one stall.
        builder = (
            RunloopTableBuilder()
            .add_iteration(50 * MS, pid=4242, is_main=False, thread_name="com.apple.NSEventThread")
            .add_iteration(100 * MS, pid=4242, thread_name="Main Thread")
            .add_iteration(200 * MS, pid=4242, thread_name="Main Thread")
        )
        iterations, _ = macos_startup_profile.scan_runloop_iterations(builder.write(self.tmp), pid=4242)
        self.assertEqual(iterations, [100 * MS, 200 * MS])

    def test_non_main_runloop_iterations_are_still_excluded(self):
        builder = RunloopTableBuilder().add_iteration(100 * MS).add_iteration(200 * MS, is_main=False)
        iterations, _ = macos_startup_profile.scan_runloop_iterations(builder.write(self.tmp), pid=4242)
        self.assertEqual(iterations, [100 * MS])

    def test_the_interval_uses_only_the_marker_process_run_loop(self):
        signposts = (
            SignpostTableBuilder()
            .add(400 * MS, macos_startup_profile.MARKER_FIRST_WINDOW, pid=4242)
            .add(1400 * MS, macos_startup_profile.MARKER_FIRST_MAIN_ITERATION, pid=4242)
        ).write(self.tmp)
        # The measured process stalls for the whole interval; a busy sibling services constantly.
        runloop = RunloopTableBuilder().add_iteration(100 * MS, pid=4242)
        for step in range(10):
            runloop.add_iteration((400 + step * 100) * MS, pid=999)
        measured = macos_startup_profile.measure_startup_interval(runloop.write(self.tmp), signposts)
        self.assertIsNone(measured["invalid_reason"])
        # Without process attribution the sibling's iterations would mask this as ten 100 ms gaps.
        self.assertEqual(measured["worst_clipped_gap_ms"], 1000.0)


class StartupMarkerParsingTests(unittest.TestCase):
    """`FoundryFirstWindowVisible` / `FoundryFirstMainIteration` extraction from an export."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def parse(self, builder: SignpostTableBuilder) -> dict[str, Any]:
        markers: dict[str, Any] = macos_startup_profile.parse_startup_markers(builder.write(self.tmp))
        return markers

    def both_markers(self, *, window_ns: int = 400 * MS, iteration_ns: int = 1400 * MS) -> SignpostTableBuilder:
        return (
            SignpostTableBuilder()
            .add(window_ns, macos_startup_profile.MARKER_FIRST_WINDOW)
            .add(iteration_ns, macos_startup_profile.MARKER_FIRST_MAIN_ITERATION)
        )

    def test_extracts_both_markers_and_the_interval(self):
        markers = self.parse(self.both_markers())
        self.assertIsNone(markers["invalid_reason"])
        self.assertEqual(markers["first_window_ns"], 400 * MS)
        self.assertEqual(markers["first_main_iteration_ns"], 1400 * MS)
        self.assertEqual(markers["interval_ms"], 1000.0)
        self.assertEqual(markers["pid"], 4242)

    def test_ignores_signposts_from_other_subsystems(self):
        # The Metal driver emits Points of Interest under its own subsystem in the very same trace.
        builder = self.both_markers()
        builder.add(500 * MS, "create_pipeline", subsystem="org.cafecito.foundry.metal")
        markers = self.parse(builder)
        self.assertIsNone(markers["invalid_reason"])
        self.assertEqual(markers["first_window_ns"], 400 * MS)

    def test_ignores_a_same_named_marker_from_another_process(self):
        builder = self.both_markers()
        builder.add(50 * MS, macos_startup_profile.MARKER_FIRST_WINDOW, pid=999, process="other.tool")
        markers = self.parse(builder)
        self.assertIsNone(markers["invalid_reason"])
        self.assertEqual(markers["pid"], 4242)
        self.assertEqual(markers["first_window_ns"], 400 * MS)

    def test_markers_split_across_two_processes_are_invalid(self):
        builder = SignpostTableBuilder()
        builder.add(400 * MS, macos_startup_profile.MARKER_FIRST_WINDOW, pid=1)
        builder.add(1400 * MS, macos_startup_profile.MARKER_FIRST_MAIN_ITERATION, pid=2)
        self.assertEqual(self.parse(builder)["invalid_reason"], "cross-process")

    def test_a_missing_marker_is_invalid(self):
        only_window = SignpostTableBuilder().add(400 * MS, macos_startup_profile.MARKER_FIRST_WINDOW)
        self.assertEqual(self.parse(only_window)["invalid_reason"], "missing")
        self.assertEqual(self.parse(SignpostTableBuilder())["invalid_reason"], "missing")

    def test_a_duplicated_marker_is_invalid(self):
        builder = self.both_markers()
        builder.add(600 * MS, macos_startup_profile.MARKER_FIRST_WINDOW)
        self.assertEqual(self.parse(builder)["invalid_reason"], "duplicate")

    def test_reversed_markers_are_invalid(self):
        builder = self.both_markers(window_ns=1400 * MS, iteration_ns=400 * MS)
        self.assertEqual(self.parse(builder)["invalid_reason"], "reversed")

    def test_coincident_markers_are_reversed_not_a_zero_length_interval(self):
        # A zero-length interval measures nothing; reporting it as a pass would be a false green.
        builder = self.both_markers(window_ns=400 * MS, iteration_ns=400 * MS)
        self.assertEqual(self.parse(builder)["invalid_reason"], "reversed")


class GapClippingTests(unittest.TestCase):
    """Gaps intersected with `[first window visible, first Main::iteration entry)`."""

    def clip(self, iterations_ms: list[float], start_ms: float, end_ms: float) -> list[float]:
        gaps = macos_startup_profile.clip_gaps_to_interval(
            [int(value * MS) for value in iterations_ms], int(start_ms * MS), int(end_ms * MS)
        )
        return [gap["gap_ms"] for gap in gaps]

    def test_a_gap_wholly_inside_the_interval_is_measured_whole(self):
        self.assertEqual(self.clip([100, 500, 1500], 400, 2000), [100.0, 1000.0, 500.0])

    def test_a_gap_spanning_the_window_marker_is_clipped_at_the_marker(self):
        # The run loop stalls from 100 ms to 900 ms, but the window only appears at 400 ms. Only the
        # 500 ms the user could actually see counts; charging the whole 800 ms would reject
        # pre-window work the criterion deliberately excludes.
        self.assertEqual(self.clip([100, 900], 400, 1000), [500.0, 100.0])

    def test_work_after_the_first_main_iteration_is_excluded(self):
        # A 5 s post-startup stall must not leak into a startup criterion.
        self.assertEqual(self.clip([400, 600, 6000], 400, 1000), [200.0, 400.0])

    def test_a_stall_straddling_both_boundaries_is_clipped_to_the_interval(self):
        self.assertEqual(self.clip([0, 9000], 400, 1000), [600.0])

    def test_iterations_exactly_on_the_boundaries_do_not_create_empty_gaps(self):
        self.assertEqual(self.clip([400, 700, 1000], 400, 1000), [300.0, 300.0])

    def test_offsets_are_reported_relative_to_the_window_marker(self):
        gaps = macos_startup_profile.clip_gaps_to_interval(
            [int(400 * MS), int(900 * MS)], int(400 * MS), int(1000 * MS)
        )
        self.assertEqual([gap["offset_ms"] for gap in gaps], [0.0, 500.0])

    def test_a_gap_just_over_the_bound_is_not_rounded_into_a_pass(self):
        # 250.04 ms rounded to one decimal is 250.0, which satisfies a `<= 250.0` gate. Rounding
        # before the comparison therefore turns a real violation into a false green, so the measured
        # value has to keep full precision and round only for display.
        gaps = macos_startup_profile.clip_gaps_to_interval([], 0, 250_040_000)
        self.assertGreater(gaps[0]["gap_ms"], macos_startup_profile.DEFAULT_MAX_BLOCK_MS)

    def test_a_gap_just_under_the_bound_still_passes(self):
        gaps = macos_startup_profile.clip_gaps_to_interval([], 0, 249_960_000)
        self.assertLess(gaps[0]["gap_ms"], macos_startup_profile.DEFAULT_MAX_BLOCK_MS)

    def test_sub_tenth_millisecond_overruns_reach_the_gate(self):
        runs = [{"worst_clipped_gap_ms": 250.04, "invalid_reason": None, "interval_ms": 1000.0}]
        acceptance = macos_startup_profile.evaluate_acceptance_2097({"timeline": {"startup_interval": {"runs": runs}}})
        self.assertFalse(acceptance["passed"])

    def test_unsorted_input_is_handled(self):
        self.assertEqual(self.clip([1500, 100, 500], 400, 2000), [100.0, 1000.0, 500.0])


class Criteria2097Tests(unittest.TestCase):
    """The #2097 gate is a hard maximum across every capture, not a median."""

    def summary(self, *runs: dict[str, Any]) -> dict[str, Any]:
        return {"timeline": {"runs": list(runs), "startup_interval": {"runs": list(runs)}}}

    def run_entry(self, worst_ms: float, *, invalid: str | None = None) -> dict[str, Any]:
        return {"worst_clipped_gap_ms": worst_ms, "invalid_reason": invalid, "interval_ms": 1000.0}

    def test_passes_when_every_run_is_under_the_bound(self):
        acceptance = macos_startup_profile.evaluate_acceptance_2097(
            self.summary(self.run_entry(120.0), self.run_entry(240.0), self.run_entry(90.0))
        )
        self.assertTrue(acceptance["passed"])
        self.assertFalse(acceptance["invalid"])
        self.assertEqual(acceptance["max_clipped_gap_ms"], 240.0)

    def test_one_bad_run_fails_even_when_the_median_passes(self):
        # Median would be 100 ms and pass; the criterion is a hard maximum, so this must fail.
        acceptance = macos_startup_profile.evaluate_acceptance_2097(
            self.summary(self.run_entry(90.0), self.run_entry(100.0), self.run_entry(610.0))
        )
        self.assertFalse(acceptance["passed"])
        self.assertEqual(acceptance["max_clipped_gap_ms"], 610.0)

    def test_the_bound_is_inclusive_at_exactly_250_ms(self):
        self.assertTrue(macos_startup_profile.evaluate_acceptance_2097(self.summary(self.run_entry(250.0)))["passed"])
        self.assertFalse(macos_startup_profile.evaluate_acceptance_2097(self.summary(self.run_entry(250.1)))["passed"])

    def test_an_invalid_run_invalidates_the_whole_measurement(self):
        acceptance = macos_startup_profile.evaluate_acceptance_2097(
            self.summary(self.run_entry(90.0), self.run_entry(0.0, invalid="missing"))
        )
        self.assertTrue(acceptance["invalid"])
        self.assertFalse(acceptance["passed"])

    def test_a_run_with_no_measured_gap_is_invalid_rather_than_a_passing_zero(self):
        # `check` reads whatever summary.json it is handed — an older schema, a truncated file, a
        # hand-edited one. A run that claims to be valid but carries no gap measured nothing, and
        # reading the absent value as 0.0 ms would report PASS for a capture with no data in it.
        runs = [{"invalid_reason": None, "interval_ms": 1000.0}]
        acceptance = macos_startup_profile.evaluate_acceptance_2097({"timeline": {"startup_interval": {"runs": runs}}})
        self.assertTrue(acceptance["invalid"])
        self.assertFalse(acceptance["passed"])

    def test_an_explicit_null_gap_is_also_invalid(self):
        runs = [{"invalid_reason": None, "worst_clipped_gap_ms": None, "interval_ms": 1000.0}]
        acceptance = macos_startup_profile.evaluate_acceptance_2097({"timeline": {"startup_interval": {"runs": runs}}})
        self.assertTrue(acceptance["invalid"])

    def test_a_genuine_zero_gap_is_still_a_valid_measurement(self):
        # Zero is a legitimate result (the run loop serviced every turn); only *absence* is invalid.
        runs = [{"invalid_reason": None, "worst_clipped_gap_ms": 0.0, "interval_ms": 1000.0}]
        acceptance = macos_startup_profile.evaluate_acceptance_2097({"timeline": {"startup_interval": {"runs": runs}}})
        self.assertFalse(acceptance["invalid"])
        self.assertTrue(acceptance["passed"])

    def test_no_runs_at_all_is_invalid_rather_than_a_pass(self):
        acceptance = macos_startup_profile.evaluate_acceptance_2097(self.summary())
        self.assertTrue(acceptance["invalid"])
        self.assertFalse(acceptance["passed"])

    def test_a_summary_without_the_interval_block_is_invalid(self):
        acceptance = macos_startup_profile.evaluate_acceptance_2097({})
        self.assertTrue(acceptance["invalid"])
        self.assertFalse(acceptance["passed"])


class SummaryAggregateTests(unittest.TestCase):
    """#2097 requires the summary artifact to record the across-run maximum, not just per-run data.

    A consumer reading summary.json should not have to reimplement the acceptance calculation to
    learn the number the gate is actually defined on.
    """

    def build(self, *worst: float | None) -> dict[str, Any]:
        runs: list[dict[str, Any]] = [
            {
                "worst_clipped_gap_ms": value,
                "invalid_reason": None if value is not None else "missing",
                "interval_ms": 1000.0,
            }
            for value in worst
        ]
        block: dict[str, Any] = macos_startup_profile.summarize_startup_intervals(runs)
        return block

    def test_records_the_maximum_across_runs(self):
        block = self.build(120.0, 610.0, 90.0)
        self.assertEqual(block["max_clipped_gap_ms"], 610.0)
        self.assertEqual(len(block["runs"]), 3)

    def test_records_how_many_runs_were_valid(self):
        block = self.build(120.0, None, 90.0)
        self.assertEqual(block["valid_runs"], 2)
        self.assertEqual(block["runs_measured"], 3)

    def test_the_maximum_ignores_invalid_runs_rather_than_reading_them_as_zero(self):
        # A missing marker means "unmeasured", not "0 ms"; folding it in as a zero would be a
        # fabricated data point. The gate rejects the capture separately on the invalid reason.
        block = self.build(300.0, None)
        self.assertEqual(block["max_clipped_gap_ms"], 300.0)

    def test_no_valid_run_leaves_the_maximum_unset_rather_than_zero(self):
        block = self.build(None, None)
        self.assertIsNone(block["max_clipped_gap_ms"])
        self.assertEqual(block["valid_runs"], 0)

    def test_the_recorded_maximum_agrees_with_the_gate(self):
        runs = [
            {"worst_clipped_gap_ms": value, "invalid_reason": None, "interval_ms": 1000.0}
            for value in (120.0, 610.0, 90.0)
        ]
        block = macos_startup_profile.summarize_startup_intervals(runs)
        gate = macos_startup_profile.evaluate_acceptance_2097({"timeline": {"startup_interval": block}})
        self.assertEqual(block["max_clipped_gap_ms"], gate["max_clipped_gap_ms"])


class CheckCommandExitCodeTests(unittest.TestCase):
    """`check` must exit 2 for an unmeasurable capture, never 0."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def check(self, summary: dict[str, Any], *extra: str) -> int:
        path = self.tmp / "summary.json"
        path.write_text(json.dumps(summary))
        exit_code: int = macos_startup_profile.main(["check", str(path), *extra])
        return exit_code

    def interval_summary(self, *worst: float, invalid: str | None = None) -> dict[str, Any]:
        runs: list[dict[str, Any]] = [
            {"worst_clipped_gap_ms": value, "invalid_reason": None, "interval_ms": 1000.0} for value in worst
        ]
        if invalid is not None:
            runs.append({"worst_clipped_gap_ms": 0.0, "invalid_reason": invalid, "interval_ms": 0.0})
        return {"timeline": {"startup_interval": {"runs": runs}}}

    def test_passing_capture_exits_zero(self):
        self.assertEqual(self.check(self.interval_summary(120.0, 200.0), "--criteria", "2097"), 0)

    def test_failing_capture_exits_one(self):
        self.assertEqual(self.check(self.interval_summary(120.0, 700.0), "--criteria", "2097"), 1)

    def test_missing_markers_exit_two(self):
        self.assertEqual(self.check(self.interval_summary(120.0, invalid="missing"), "--criteria", "2097"), 2)

    def test_duplicate_markers_exit_two(self):
        self.assertEqual(self.check(self.interval_summary(120.0, invalid="duplicate"), "--criteria", "2097"), 2)

    def test_reversed_markers_exit_two(self):
        self.assertEqual(self.check(self.interval_summary(120.0, invalid="reversed"), "--criteria", "2097"), 2)

    def test_cross_process_markers_exit_two(self):
        self.assertEqual(self.check(self.interval_summary(120.0, invalid="cross-process"), "--criteria", "2097"), 2)

    def test_a_summary_with_no_interval_block_exits_two(self):
        self.assertEqual(self.check({"timeline": {}}, "--criteria", "2097"), 2)

    def test_a_run_carrying_no_measured_gap_exits_two(self):
        summary = {"timeline": {"startup_interval": {"runs": [{"invalid_reason": None, "interval_ms": 1000.0}]}}}
        self.assertEqual(self.check(summary, "--criteria", "2097"), 2)


class LegacyCriteriaPreservationTests(unittest.TestCase):
    """The #2090 gate must keep its exact behavior for historical comparisons."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def legacy_summary(self) -> dict[str, Any]:
        return {
            "window": {"time_to_first_window_ms": macos_startup_profile.spread([434.7])},
            "timeline": {
                "longest_non_servicing_gap_ms": macos_startup_profile.spread([606.9]),
                "longest_contiguous_blocked_ms": macos_startup_profile.spread([180.0]),
            },
        }

    def test_default_criteria_still_evaluate_the_2090_checks(self):
        acceptance = macos_startup_profile.evaluate_acceptance(self.legacy_summary(), 8.0, 250.0)
        self.assertEqual(acceptance["issue"], "cafecito-games/Foundry#2090")
        sources = sorted(check["source"] for check in acceptance["checks"])
        self.assertEqual(sources, ["timeline/runloop-events", "timeline/time-profile", "window"])

    def test_the_2090_gate_still_uses_the_median_not_a_maximum(self):
        # Two good runs and one bad one: the median passes, and that is the documented #2090
        # behavior. #2097 deliberately differs, so this pins the two apart.
        summary = {"timeline": {"longest_non_servicing_gap_ms": macos_startup_profile.spread([90.0, 100.0, 610.0])}}
        checks = macos_startup_profile.evaluate_acceptance(summary, 8.0, 250.0)["checks"]
        gap_check = [check for check in checks if check["source"] == "timeline/runloop-events"][0]
        self.assertEqual(gap_check["value"], 100.0)
        self.assertTrue(gap_check["passed"])

    def test_startup_interval_data_does_not_leak_into_the_2090_gate(self):
        summary = self.legacy_summary()
        summary["timeline"]["startup_interval"] = {
            "runs": [{"worst_clipped_gap_ms": 900.0, "invalid_reason": None, "interval_ms": 1000.0}]
        }
        with_interval = macos_startup_profile.evaluate_acceptance(summary, 8.0, 250.0)
        del summary["timeline"]["startup_interval"]
        without_interval = macos_startup_profile.evaluate_acceptance(summary, 8.0, 250.0)
        self.assertEqual(with_interval, without_interval)


if __name__ == "__main__":
    unittest.main()
