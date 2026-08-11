#!/usr/bin/env python3
"""Deterministic macOS editor startup profiling for the Foundry editor.

Captures, parses and compares startup profiles of the macOS editor so that the
"editor blocks the run loop during launch" measurements are reproducible instead
of being re-derived by hand every session.

Four independent measurements are supported, each answering a different
question:

* ``sample``   - what share of main-thread samples sits inside
                 ``-[NSApplication _sendFinishLaunchingNotification]``
                 (magnitude, works on the stripped optimized binary).
* ``timeline`` - Instruments ``Time Profiler`` capture, giving per-sample
                 timestamps and main run loop iteration events, so the longest
                 uninterrupted non-servicing block can be measured in
                 milliseconds instead of inferred.
* ``phases``   - ``--benchmark`` marks, i.e. the engine's own phase timings.
* ``window``   - milliseconds from spawning the editor to its first on-screen
                 window, polled from the CoreGraphics window list. Geometry and
                 owner pid only, never window titles or pixels.

Everything is stdlib only. Run ``--help`` on any subcommand for details.
"""

from __future__ import annotations

import argparse
import json
import os
import platform as platform_module
import plistlib
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Iterator, NoReturn, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "foundry.macos_startup_profile/1"

BINARY_FLAVORS = {
    "release": "bin/foundry.macos.editor.arm64",
    "dev": "bin/foundry.macos.editor.dev.arm64",
}

# The AppKit frame that brackets `applicationDidFinishLaunching:`, which is where
# `OS_MacOS_NSApp::start_main()` runs the whole engine + editor boot inline
# (platform/macos/os_macos.mm:1291).
BLOCK_FRAME = "-[NSApplication _sendFinishLaunchingNotification]"

# Acceptance thresholds from cafecito-games/Foundry#2090.
DEFAULT_MAX_BLOCKED_PERCENT = 8.0
DEFAULT_MAX_BLOCK_MS = 250.0
DEFAULT_MAX_WINDOW_MS = 500.0
AC_SAMPLE_WINDOW_SECONDS = 20.0

# The startup interval cafecito-games/Foundry#2097 is defined over. The editor emits exactly one of
# each marker per process, from `platform/macos/startup_markers_macos.{h,mm}`, under its own
# subsystem so the Metal driver's Points of Interest in the same trace cannot be mistaken for them.
STARTUP_SIGNPOST_SUBSYSTEM = "org.cafecito.foundry.startup"
MARKER_FIRST_WINDOW = "FoundryFirstWindowVisible"
MARKER_FIRST_MAIN_ITERATION = "FoundryFirstMainIteration"

# Why an exit code of its own: a capture whose markers are missing, doubled, reversed or spread
# across processes measured *nothing*. Reporting that as a pass (0) or as a bound violation (1)
# would both be wrong, and the first of those silently green-lights an unmeasured change.
EXIT_MEASUREMENT_INVALID = 2

# A window smaller than this is a helper surface (a status item, a tooltip host), not the
# application window the user is waiting for.
MIN_WINDOW_SIDE_PX = 32.0

# Leaf symbols that mean "main thread parked", not "main thread working".
IDLE_LEAF_SYMBOLS = frozenset(
    {
        "__semwait_signal",
        "mach_msg2_trap",
        "__psynch_cvwait",
        "__ulock_wait",
        "__ulock_wait2",
        "kevent",
        "kevent_id",
        "poll",
        "select",
        "__workq_kernreturn",
        "semaphore_wait_trap",
        "semaphore_timedwait_trap",
        "read",
        "__select",
    }
)
IDLE_INCLUSIVE_SYMBOLS = frozenset({"nanosleep", "__semwait_signal", "mach_msg2_trap"})

DEFAULT_OUTPUT_ROOT = Path.home() / ".foundry-startup-profiles"

CALL_GRAPH_HEADER = "Call graph:"
BINARY_IMAGES_HEADER = "Binary Images:"
THREAD_LINE = re.compile(r"^(\s+)(\d+) Thread_(\d+)(?::\s*(.*?))?\s*$")
FRAME_LINE = re.compile(r"^([ +!:|]*)(\d+) (.*)$")
LOAD_OFFSET = re.compile(r"load address 0x[0-9a-f]+ \+ (0x[0-9a-f]+)")


# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def median(values: Sequence[float]) -> float:
    return float(statistics.median(values)) if values else 0.0


def spread(values: Sequence[float]) -> dict[str, float]:
    if not values:
        return {"median": 0.0, "min": 0.0, "max": 0.0, "runs": 0}
    return {
        "median": round(float(statistics.median(values)), 4),
        "min": round(float(min(values)), 4),
        "max": round(float(max(values)), 4),
        "runs": len(values),
    }


def log(message: str) -> None:
    print(f"[startup-profile] {message}", flush=True)


def fail(message: str) -> NoReturn:
    print(f"[startup-profile] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(2)


def positive_int(value: str) -> int:
    """A run count of zero would record an empty measurement, which reads as a passing zero."""
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError(f"must be at least 1, got {parsed}")
    return parsed


def resolve_binary(value: str) -> Path:
    if value in BINARY_FLAVORS:
        candidate = REPO_ROOT / BINARY_FLAVORS[value]
    else:
        candidate = Path(value).expanduser().resolve()
    if not candidate.is_file():
        fail(f"binary not found: {candidate}")
    return candidate


def binary_flavor(path: Path) -> str:
    return "dev" if ".dev." in path.name else "release"


def binary_metadata(path: Path) -> dict[str, Any]:
    version = ""
    try:
        completed = subprocess.run([str(path), "--version"], capture_output=True, text=True, timeout=60)
        version = completed.stdout.strip().splitlines()[0] if completed.stdout.strip() else ""
    except (OSError, subprocess.SubprocessError, IndexError):
        version = ""
    stat = path.stat()
    return {
        "path": str(path),
        "flavor": binary_flavor(path),
        "name": path.name,
        "version": version,
        "size_bytes": stat.st_size,
        "mtime": datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(timespec="seconds"),
    }


def host_metadata() -> dict[str, Any]:
    return {
        "machine": platform_module.machine(),
        "release": platform_module.mac_ver()[0] or platform_module.release(),
        "cpu_count": os.cpu_count(),
        "node": platform_module.node(),
    }


def machine_load() -> dict[str, float]:
    """Load average around the capture, recorded so a busy machine cannot be mistaken for a delta.

    Every metric here is sensitive to machine load: the same two binaries measured while a build
    and two test suites were running reported a worst run-loop gap of 1184/1165 ms instead of
    837/842 ms. The absolute milliseconds move by tens of percent while the before/after ratios
    hold, so a pair captured under different loads is not a pair. `compare` warns on it.
    """
    one, five, fifteen = os.getloadavg()
    return {"load_average_1m": round(one, 2), "load_average_5m": round(five, 2), "load_average_15m": round(fifteen, 2)}


def git_revision() -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            timeout=20,
        )
        return completed.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def make_output_dir(explicit: str | None, label: str) -> Path:
    if explicit:
        out = Path(explicit).expanduser().resolve()
    else:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out = DEFAULT_OUTPUT_ROOT / f"{label}-{stamp}"
    out.mkdir(parents=True, exist_ok=True)
    return out


def editor_command(binary: Path, project: Path, extra_global: Sequence[str] = ()) -> list[str]:
    # `--benchmark`, `--benchmark-file` and `--quit-after` are GLOBAL flags and
    # must precede the subcommand; `editor open` rejects them otherwise.
    return [str(binary), *extra_global, "editor", "open", "--project", str(project)]


def launch_editor(binary: Path, project: Path, log_path: Path, extra_global: Sequence[str] = ()):
    handle = log_path.open("wb")
    process = subprocess.Popen(
        editor_command(binary, project, extra_global),
        cwd=str(REPO_ROOT),
        stdout=handle,
        stderr=subprocess.STDOUT,
    )
    return process, handle


def stop_editor(process: subprocess.Popen[bytes], grace_seconds: float = 8.0) -> None:
    """SIGINT is the graceful quit path for the macOS editor; SIGKILL is a backstop."""
    if process.poll() is not None:
        return
    try:
        process.send_signal(signal.SIGINT)
    except OSError:
        return
    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return
        time.sleep(0.2)
    try:
        process.kill()
    except OSError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass


def warm_caches(binary: Path, project: Path, log_path: Path) -> None:
    """One throwaway launch so the doc cache, shader cache and imports are warm."""
    log("warming caches (one throwaway launch)")
    process, handle = launch_editor(binary, project, log_path, ["--quit-after", "400"])
    try:
        process.wait(timeout=600)
    except subprocess.TimeoutExpired:
        stop_editor(process)
    finally:
        handle.close()


# --------------------------------------------------------------------------- #
# Time to first on-screen window
# --------------------------------------------------------------------------- #

# `CGWindowListCopyWindowInfo` options, from CGWindow.h.
_CG_WINDOW_LIST_ON_SCREEN_ONLY = 1 << 0
_CG_NULL_WINDOW_ID = 0
# `CFPropertyListCreateData` format, from CFPropertyList.h.
_CF_PROPERTY_LIST_BINARY_FORMAT_V1_0 = 200


def _load_window_list_api():
    """CoreGraphics window list, reached through ctypes so the script stays stdlib only.

    Only window geometry, layer and owner pid are ever read. Window *titles* are deliberately
    not touched: they can carry the contents of whatever the user has open, and reading them
    would additionally require Screen Recording permission. Geometry alone needs no permission.
    """
    import ctypes
    import ctypes.util

    core_foundation = ctypes.CDLL(ctypes.util.find_library("CoreFoundation"))
    core_graphics = ctypes.CDLL(ctypes.util.find_library("CoreGraphics"))

    core_graphics.CGWindowListCopyWindowInfo.restype = ctypes.c_void_p
    core_graphics.CGWindowListCopyWindowInfo.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    core_foundation.CFPropertyListCreateData.restype = ctypes.c_void_p
    core_foundation.CFPropertyListCreateData.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_void_p,
    ]
    core_foundation.CFDataGetBytePtr.restype = ctypes.c_void_p
    core_foundation.CFDataGetBytePtr.argtypes = [ctypes.c_void_p]
    core_foundation.CFDataGetLength.restype = ctypes.c_long
    core_foundation.CFDataGetLength.argtypes = [ctypes.c_void_p]
    core_foundation.CFRelease.argtypes = [ctypes.c_void_p]

    def on_screen_windows() -> list[dict[str, Any]]:
        info = core_graphics.CGWindowListCopyWindowInfo(_CG_WINDOW_LIST_ON_SCREEN_ONLY, _CG_NULL_WINDOW_ID)
        if not info:
            return []
        data = None
        try:
            data = core_foundation.CFPropertyListCreateData(None, info, _CF_PROPERTY_LIST_BINARY_FORMAT_V1_0, 0, None)
            if not data:
                return []
            length = core_foundation.CFDataGetLength(data)
            raw = ctypes.string_at(core_foundation.CFDataGetBytePtr(data), length)
        finally:
            if data:
                core_foundation.CFRelease(data)
            core_foundation.CFRelease(info)
        parsed: list[dict[str, Any]] = plistlib.loads(raw)
        return parsed

    return on_screen_windows


def _has_visible_window(windows: Sequence[dict[str, Any]], pid: int) -> bool:
    for window in windows:
        if window.get("kCGWindowOwnerPID") != pid:
            continue
        if window.get("kCGWindowLayer") != 0:
            # Non-zero layers are panels, menus and status items, not the application window.
            continue
        if window.get("kCGWindowIsOnscreen") is False:
            # The key is optional in CoreGraphics' dictionaries and the query is already
            # on-screen-only, so absence means "no opinion", not "not visible".
            continue
        bounds = window.get("kCGWindowBounds") or {}
        if float(bounds.get("Width", 0)) > MIN_WINDOW_SIDE_PX and float(bounds.get("Height", 0)) > MIN_WINDOW_SIDE_PX:
            return True
    return False


def measure_first_window_ms(
    binary: Path,
    project: Path,
    log_path: Path,
    poll_interval_ms: float,
    timeout_seconds: float,
) -> float:
    """Milliseconds from spawning the editor to its first window being on screen.

    The clock starts at `Popen`, so the number includes dyld and everything before the engine
    runs — which is what the user experiences, and what the 500 ms criterion on #2090 is about.
    """
    on_screen_windows = _load_window_list_api()
    interval = poll_interval_ms / 1000.0

    started = time.monotonic()
    process, handle = launch_editor(binary, project, log_path)
    try:
        deadline = started + timeout_seconds
        while time.monotonic() < deadline:
            if _has_visible_window(on_screen_windows(), process.pid):
                return (time.monotonic() - started) * 1000.0
            if process.poll() is not None:
                fail(f"editor exited before showing a window (exit {process.returncode}); see {log_path}")
            time.sleep(interval)
        fail(f"no window appeared within {timeout_seconds}s; see {log_path}")
    finally:
        stop_editor(process)
        handle.close()


def command_window(args: argparse.Namespace) -> int:
    binary = resolve_binary(args.binary)
    project = Path(args.project).expanduser().resolve()
    out_dir = make_output_dir(args.out, args.label)

    if args.warm:
        warm_caches(binary, project, out_dir / "warmup.log")

    runs: list[float] = []
    for index in range(1, args.runs + 1):
        log(f"window run {index}/{args.runs}")
        runs.append(
            measure_first_window_ms(
                binary,
                project,
                out_dir / f"window-run{index}.log",
                args.poll_interval_ms,
                args.timeout,
            )
        )

    summary = load_or_new_summary(out_dir, args.label, binary, project, args.cache_state)
    summary["window"] = {
        "poll_interval_ms": args.poll_interval_ms,
        "min_window_side_px": MIN_WINDOW_SIDE_PX,
        "runs": runs,
        "time_to_first_window_ms": spread(runs),
    }
    record_load(summary, "window")
    write_summary(out_dir, summary)
    print_window_summary(summary)
    return 0


# --------------------------------------------------------------------------- #
# `sample` call-graph parsing
# --------------------------------------------------------------------------- #


class SampleNode:
    __slots__ = ("count", "symbol", "module", "children")

    def __init__(self, count: int, symbol: str, module: str) -> None:
        self.count = count
        self.symbol = symbol
        self.module = module
        self.children: list[SampleNode] = []


def _split_frame(rest: str) -> tuple[str, str]:
    """Split a frame description into (symbol, module)."""
    module = ""
    match = re.search(r"\s{2}\(in ([^)]+)\)", rest)
    if match:
        module = match.group(1)
        symbol = rest[: match.start()].strip()
    else:
        symbol = re.split(r"\s{2}\[0x", rest)[0].strip()
    if symbol == "???":
        offset = LOAD_OFFSET.search(rest)
        if offset:
            symbol = f"???+{offset.group(1)}"
    return symbol, module


def parse_sample_file(path: Path) -> dict[str, Any]:
    """Parse `/usr/bin/sample` output into per-thread call trees."""
    text = path.read_text(errors="replace").splitlines()
    header: dict[str, str] = {}
    for line in text[:30]:
        match = re.match(r"^([A-Z][A-Za-z /]+):\s+(.*)$", line)
        if match:
            header[match.group(1)] = match.group(2).strip()

    try:
        start = text.index(CALL_GRAPH_HEADER) + 1
    except ValueError:
        fail(f"{path} does not look like `sample` output (no '{CALL_GRAPH_HEADER}')")

    threads: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    stack: list[tuple[int, SampleNode]] = []

    for line in text[start:]:
        if line.startswith(BINARY_IMAGES_HEADER):
            break
        if not line.strip():
            continue
        thread_match = THREAD_LINE.match(line)
        if thread_match:
            root = SampleNode(int(thread_match.group(2)), "<thread>", "")
            current = {
                "tid": thread_match.group(3),
                "name": (thread_match.group(4) or "").strip(),
                "samples": int(thread_match.group(2)),
                "root": root,
            }
            threads.append(current)
            stack = [(0, root)]
            continue
        if current is None:
            continue
        frame_match = FRAME_LINE.match(line)
        if not frame_match:
            continue
        depth = len(frame_match.group(1)) // 2 - 1
        if depth < 0:
            continue
        symbol, module = _split_frame(frame_match.group(3))
        node = SampleNode(int(frame_match.group(2)), symbol, module)
        while stack and stack[-1][0] >= depth + 1:
            stack.pop()
        if not stack:
            stack = [(0, current["root"])]
        stack[-1][1].children.append(node)
        stack.append((depth + 1, node))

    return {"header": header, "threads": threads}


def walk(node: SampleNode) -> Iterator[SampleNode]:
    yield node
    for child in node.children:
        yield from walk(child)


def inclusive_outermost(root: SampleNode, predicate) -> int:
    """Inclusive count of matching frames, counting only the outermost occurrence."""
    total = 0
    pending = [root]
    while pending:
        node = pending.pop()
        if node is not root and predicate(node):
            total += node.count
            continue
        pending.extend(node.children)
    return total


def flat_self_times(root: SampleNode) -> list[tuple[str, int]]:
    buckets: dict[str, int] = {}
    for node in walk(root):
        if node.symbol == "<thread>":
            continue
        self_count = node.count - sum(child.count for child in node.children)
        if self_count <= 0:
            continue
        buckets[node.symbol] = buckets.get(node.symbol, 0) + self_count
    return sorted(buckets.items(), key=lambda item: item[1], reverse=True)


def heaviest_chain(root: SampleNode, limit: int = 40) -> list[str]:
    chain: list[str] = []
    node = root
    while node.children and len(chain) < limit:
        node = max(node.children, key=lambda child: child.count)
        chain.append(f"{node.count} {node.symbol}")
    return chain


def analyze_sample(path: Path, duration_seconds: float) -> dict[str, Any]:
    parsed = parse_sample_file(path)
    main = next(
        (thread for thread in parsed["threads"] if "Main Thread" in (thread["name"] or "")),
        None,
    )
    if main is None:
        fail(f"{path}: no main thread found in call graph")
    root: SampleNode = main["root"]
    total = int(main["samples"])

    blocked = inclusive_outermost(root, lambda node: node.symbol == BLOCK_FRAME)
    idle = inclusive_outermost(
        root,
        lambda node: node.symbol in IDLE_INCLUSIVE_SYMBOLS or (not node.children and node.symbol in IDLE_LEAF_SYMBOLS),
    )

    samples_per_second = total / duration_seconds if duration_seconds else 0.0
    blocked_pct = 100.0 * blocked / total if total else 0.0
    return {
        "profile": str(path),
        "duration_seconds": duration_seconds,
        "main_thread_samples": total,
        "samples_per_second": round(samples_per_second, 1),
        "blocked_samples": blocked,
        "blocked_percent": round(blocked_pct, 2),
        "blocked_ms_estimate": round((blocked / samples_per_second * 1000.0) if samples_per_second else 0.0, 1),
        "idle_samples": idle,
        "idle_percent": round(100.0 * idle / total, 2) if total else 0.0,
        "top_self_symbols": [
            {"symbol": symbol, "samples": count, "percent": round(100.0 * count / total, 2)}
            for symbol, count in flat_self_times(root)[:15]
        ],
        "heaviest_chain": heaviest_chain(root, 25),
    }


def analyze_sample_phases(path: Path) -> dict[str, Any]:
    """Share of the blocked window taken by each boot phase (dev build only)."""
    parsed = parse_sample_file(path)
    main = next(
        (thread for thread in parsed["threads"] if "Main Thread" in (thread["name"] or "")),
        None,
    )
    if main is None:
        fail(f"{path}: no main thread found in call graph")
    root: SampleNode = main["root"]

    blocked_nodes: list[SampleNode] = []
    pending = [root]
    while pending:
        node = pending.pop()
        if node.symbol == BLOCK_FRAME:
            blocked_nodes.append(node)
            continue
        pending.extend(node.children)
    blocked_total = sum(node.count for node in blocked_nodes)

    phase_patterns = {
        "Main::setup": re.compile(r"^Main::setup\b"),
        "Main::setup2": re.compile(r"^Main::setup2\b"),
        "Main::start": re.compile(r"^Main::start\b"),
        "main_loop->initialize (SceneTree::initialize)": re.compile(r"^SceneTree::initialize\b"),
        "EditorNode::EditorNode": re.compile(r"^EditorNode::EditorNode\b"),
        "RenderingServerDefault::init": re.compile(r"^RenderingServerDefault::init\b"),
        "Node::_propagate_enter_tree": re.compile(r"^Node::_propagate_enter_tree\b"),
    }
    phases: dict[str, int] = {}
    for name, pattern in phase_patterns.items():
        count = 0
        for blocked_node in blocked_nodes:
            count += inclusive_outermost(blocked_node, lambda node: bool(pattern.match(node.symbol)))
            if pattern.match(blocked_node.symbol):
                count += blocked_node.count
        phases[name] = count

    return {
        "profile": str(path),
        "blocked_samples": blocked_total,
        "note": (
            "Shares are inclusive and some rows nest inside others "
            "(Main::setup contains Main::setup2, Main::start contains EditorNode::EditorNode, "
            "SceneTree::initialize contains Node::_propagate_enter_tree). "
            "Main::setup + Main::start + SceneTree::initialize are the disjoint top-level segments."
        ),
        "phases": [
            {
                "phase": name,
                "samples": count,
                "percent_of_blocked": round(100.0 * count / blocked_total, 1) if blocked_total else 0.0,
            }
            for name, count in sorted(phases.items(), key=lambda item: item[1], reverse=True)
        ],
    }


# --------------------------------------------------------------------------- #
# `sample` capture
# --------------------------------------------------------------------------- #


def capture_sample_run(
    binary: Path,
    project: Path,
    out_dir: Path,
    index: int,
    duration: float,
    interval: int,
) -> dict[str, Any]:
    raw = out_dir / f"sample-run{index}.txt"
    editor_log = out_dir / f"sample-run{index}.editor.log"
    sample_log = out_dir / f"sample-run{index}.sample.log"

    # `-wait` must be armed BEFORE the process exists, otherwise the whole
    # startup block is missed and only the idle main loop is captured.
    with sample_log.open("wb") as sample_handle:
        sampler = subprocess.Popen(
            [
                "/usr/bin/sample",
                binary.name,
                str(duration),
                str(interval),
                "-wait",
                "-mayDie",
                "-f",
                str(raw),
            ],
            stdout=sample_handle,
            stderr=subprocess.STDOUT,
        )
        time.sleep(1.0)
        process, handle = launch_editor(binary, project, editor_log)
        try:
            sampler.wait(timeout=duration + 300)
        except subprocess.TimeoutExpired:
            sampler.kill()
        finally:
            stop_editor(process)
            handle.close()

    if not raw.is_file():
        fail(f"sample produced no output at {raw}; see {sample_log}")
    result = analyze_sample(raw, duration)
    result["editor_log"] = str(editor_log)
    return result


def command_capture(args: argparse.Namespace) -> int:
    binary = resolve_binary(args.binary)
    project = Path(args.project).expanduser().resolve()
    if not (project / "project.foundry").is_file():
        fail(f"{project} has no project.foundry")
    out_dir = make_output_dir(args.out, args.label)

    if args.warm:
        warm_caches(binary, project, out_dir / "warmup.log")

    runs: list[dict[str, Any]] = []
    for index in range(1, args.runs + 1):
        log(f"sample run {index}/{args.runs} ({args.duration}s window)")
        runs.append(capture_sample_run(binary, project, out_dir, index, args.duration, args.interval))
        log(
            f"  blocked {runs[-1]['blocked_percent']}% "
            f"(~{runs[-1]['blocked_ms_estimate']} ms), idle {runs[-1]['idle_percent']}%"
        )

    summary = load_or_new_summary(out_dir, args.label, binary, project, args.cache_state)
    summary["sample"] = {
        "duration_seconds": args.duration,
        "interval_ms": args.interval,
        "runs": runs,
        "blocked_percent": spread([run["blocked_percent"] for run in runs]),
        "blocked_ms_estimate": spread([run["blocked_ms_estimate"] for run in runs]),
        "idle_percent": spread([run["idle_percent"] for run in runs]),
    }
    record_load(summary, "sample")
    write_summary(out_dir, summary)
    print_sample_summary(summary)
    return 0


# --------------------------------------------------------------------------- #
# Instruments (`xctrace`) capture and parsing
# --------------------------------------------------------------------------- #


def xctrace_available() -> bool:
    return shutil.which("xcrun") is not None


def record_trace(binary: Path, project: Path, trace_path: Path, time_limit: float, log_path: Path) -> None:
    if trace_path.exists():
        shutil.rmtree(trace_path)
    command = [
        "xcrun",
        "xctrace",
        "record",
        "--template",
        "Time Profiler",
        "--output",
        str(trace_path),
        "--time-limit",
        f"{int(time_limit)}s",
        "--target-stdout",
        "-",
        "--launch",
        "--",
        str(binary),
        "editor",
        "open",
        "--project",
        str(project),
    ]
    with log_path.open("wb") as handle:
        subprocess.run(command, cwd=str(REPO_ROOT), stdout=handle, stderr=subprocess.STDOUT)
    subprocess.run(["/usr/bin/pkill", "-f", binary.name], capture_output=True)
    if not trace_path.exists():
        fail(f"xctrace produced no trace at {trace_path}; see {log_path}")


def export_table(trace_path: Path, schema: str, destination: Path) -> Path:
    xpath = f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'
    with destination.open("wb") as handle:
        completed = subprocess.run(
            ["xcrun", "xctrace", "export", "--input", str(trace_path), "--xpath", xpath],
            stdout=handle,
            stderr=subprocess.PIPE,
        )
    if completed.returncode != 0:
        fail(f"xctrace export of '{schema}' failed: {completed.stderr.decode(errors='replace')}")
    return destination


def _iter_rows(path: Path) -> Iterator[ElementTree.Element]:
    for event, element in ElementTree.iterparse(str(path), events=("end",)):
        if element.tag == "row":
            yield element
            element.clear()


def parse_time_profile(path: Path, bucket_ms: float) -> dict[str, Any]:
    """Per-sample timeline of main-thread samples inside the blocked frame."""
    frame_names: dict[str, str] = {}
    thread_is_main: dict[str, bool] = {}
    time_values: dict[str, int] = {}

    samples: list[tuple[int, bool]] = []  # (nanoseconds, inside blocked frame)

    for row in _iter_rows(path):
        timestamp: int | None = None
        is_main = False
        blocked = False
        for child in row:
            if child.tag == "sample-time":
                identifier = child.get("id")
                reference = child.get("ref")
                if identifier is not None and child.text:
                    time_values[identifier] = int(child.text)
                    timestamp = time_values[identifier]
                elif reference is not None:
                    timestamp = time_values.get(reference)
            elif child.tag == "thread":
                identifier = child.get("id")
                reference = child.get("ref")
                if identifier is not None:
                    thread_is_main[identifier] = "Main Thread" in (child.get("fmt") or "")
                    is_main = thread_is_main[identifier]
                elif reference is not None:
                    is_main = thread_is_main.get(reference, False)
            elif child.tag == "tagged-backtrace":
                for frame in child.iter("frame"):
                    identifier = frame.get("id")
                    if identifier is not None:
                        frame_names[identifier] = frame.get("name") or ""
                        name = frame_names[identifier]
                    else:
                        name = frame_names.get(frame.get("ref") or "", "")
                    if name == BLOCK_FRAME:
                        blocked = True
        if timestamp is None or not is_main:
            continue
        samples.append((timestamp, blocked))

    samples.sort(key=lambda item: item[0])
    if not samples:
        return {"time_profile_samples": 0}

    origin = samples[0][0]
    total = len(samples)
    blocked_total = sum(1 for _, blocked in samples if blocked)

    bucket_ns = int(bucket_ms * 1_000_000)
    buckets: dict[int, list[int]] = {}
    for timestamp, blocked in samples:
        index = (timestamp - origin) // bucket_ns
        entry = buckets.setdefault(index, [0, 0])
        entry[0] += 1
        entry[1] += 1 if blocked else 0

    timeline = [
        {
            "start_ms": round(index * bucket_ms, 1),
            "main_samples": entry[0],
            "blocked_samples": entry[1],
            "blocked_percent": round(100.0 * entry[1] / entry[0], 1) if entry[0] else 0.0,
        }
        for index, entry in sorted(buckets.items())
    ]

    blocked_times = [timestamp for timestamp, blocked in samples if blocked]
    contiguous_ms = 0.0
    first_blocked_ms = None
    last_blocked_ms = None
    if blocked_times:
        first_blocked_ms = round((blocked_times[0] - origin) / 1e6, 1)
        last_blocked_ms = round((blocked_times[-1] - origin) / 1e6, 1)
        run_start = blocked_times[0]
        previous = blocked_times[0]
        for timestamp in blocked_times[1:]:
            if timestamp - previous > 50_000_000:  # >50 ms gap ends the run
                contiguous_ms = max(contiguous_ms, (previous - run_start) / 1e6)
                run_start = timestamp
            previous = timestamp
        contiguous_ms = max(contiguous_ms, (previous - run_start) / 1e6)

    return {
        "time_profile_samples": total,
        "blocked_samples": blocked_total,
        # Instruments only samples RUNNING threads, so this share is of running
        # main-thread samples and is not comparable with the `sample` percentage.
        "blocked_percent_of_running_main_samples": round(100.0 * blocked_total / total, 2),
        "first_blocked_ms": first_blocked_ms,
        "last_blocked_ms": last_blocked_ms,
        "longest_contiguous_blocked_ms": round(contiguous_ms, 1),
        "bucket_ms": bucket_ms,
        "timeline": timeline,
    }


def scan_runloop_iterations(path: Path, pid: int | None = None) -> tuple[list[int], int | None]:
    """Absolute start timestamps of every main run loop iteration, plus the trace origin.

    Returned in trace nanoseconds rather than as offsets, because the #2097 interval is defined by
    signpost timestamps from a different exported table and the two only align on the raw clock.

    `pid` restricts the result to one process. A trace records the launched editor *and* its
    children, so measuring one process's markers against another process's run loop would combine
    unrelated timelines — and a busy sibling servicing its own run loop would mask a real stall in
    the process being measured. This table has no process column: the emitting process is reachable
    only through `thread`, which every row after the first refers to by `ref`.
    """
    strings: dict[str, str] = {}
    booleans: dict[str, str] = {}
    funcs: dict[str, str] = {}
    times: dict[str, int] = {}
    thread_pids: dict[str, int] = {}
    process_pids: dict[str, int] = {}
    iterations: list[int] = []
    origin: int | None = None

    for row in _iter_rows(path):
        timestamp: int | None = None
        interval_type = ""
        is_main = ""
        func = ""
        row_pid: int | None = None
        short_string_index = 0
        for child in row:
            tag = child.tag
            if tag == "thread":
                identifier = child.get("id")
                if identifier is None:
                    row_pid = thread_pids.get(child.get("ref") or "")
                else:
                    process = child.find("process")
                    if process is not None:
                        process_id = process.get("id")
                        pid_element = process.find("pid")
                        if process_id is not None and pid_element is not None and pid_element.text:
                            process_pids[process_id] = int(pid_element.text)
                            thread_pids[identifier] = process_pids[process_id]
                        elif process_id is None:
                            # Only the first thread of a process nests the definition; the rest —
                            # including, typically, the main thread — refer to it.
                            resolved = process_pids.get(process.get("ref") or "")
                            if resolved is not None:
                                thread_pids[identifier] = resolved
                    row_pid = thread_pids.get(identifier)
            if tag == "event-time":
                identifier = child.get("id")
                reference = child.get("ref")
                if identifier is not None and child.text:
                    times[identifier] = int(child.text)
                    timestamp = times[identifier]
                elif reference is not None:
                    timestamp = times.get(reference)
            elif tag == "short-string":
                identifier = child.get("id")
                if identifier is not None:
                    strings[identifier] = child.get("fmt") or ""
                    value = strings[identifier]
                else:
                    value = strings.get(child.get("ref") or "", "")
                if short_string_index == 0:
                    interval_type = value
                short_string_index += 1
            elif tag == "boolean":
                identifier = child.get("id")
                if identifier is not None:
                    booleans[identifier] = child.get("fmt") or ""
                    is_main = booleans[identifier]
                else:
                    is_main = booleans.get(child.get("ref") or "", "")
            elif tag == "kdebug-func":
                identifier = child.get("id")
                if identifier is not None:
                    funcs[identifier] = child.get("fmt") or ""
                    func = funcs[identifier]
                else:
                    func = funcs.get(child.get("ref") or "", "")
        if timestamp is None:
            continue
        # The origin stays trace-wide: it anchors the legacy offsets, which predate this filter.
        origin = timestamp if origin is None else min(origin, timestamp)
        if pid is not None and row_pid != pid:
            continue
        if is_main == "Yes" and interval_type == "individual_iteration" and func == "START":
            iterations.append(timestamp)

    iterations.sort()
    return iterations, origin


def parse_runloop_events(path: Path) -> dict[str, Any]:
    """Main run loop iteration events -> the real 'not servicing' gap measurement."""
    iterations, origin = scan_runloop_iterations(path)
    if not iterations or origin is None:
        return {"main_runloop_iterations": 0}

    gaps = [
        {
            "start_ms": round((iterations[index] - origin) / 1e6, 1),
            "gap_ms": round((iterations[index + 1] - iterations[index]) / 1e6, 1),
        }
        for index in range(len(iterations) - 1)
    ]
    worst = sorted(gaps, key=lambda entry: entry["gap_ms"], reverse=True)[:10]
    first_iteration_ms = round((iterations[0] - origin) / 1e6, 1)
    return {
        "main_runloop_iterations": len(iterations),
        "time_to_first_main_iteration_ms": first_iteration_ms,
        "longest_non_servicing_gap_ms": max([first_iteration_ms] + [entry["gap_ms"] for entry in gaps]),
        "longest_gap_between_iterations_ms": max(entry["gap_ms"] for entry in gaps) if gaps else 0.0,
        "worst_gaps": worst,
        "gaps_over_250ms": [entry for entry in gaps if entry["gap_ms"] > DEFAULT_MAX_BLOCK_MS],
    }


# --------------------------------------------------------------------------- #
# #2097 startup interval: `[first window visible, first Main::iteration entry)`
# --------------------------------------------------------------------------- #


def parse_startup_markers(path: Path) -> dict[str, Any]:
    """Extract the two Foundry startup markers from an exported `os-signpost` table.

    An export reuses values by reference: a column carries `id` the first time a value appears and
    `ref` afterwards. The `process` column is *defined* nested inside `thread` and only referenced
    at row level, so ids are registered from the whole row subtree while column values are read from
    the row's direct children.
    """
    names: dict[str, str] = {}
    subsystems: dict[str, str] = {}
    processes: dict[str, int] = {}
    times: dict[str, int] = {}
    # marker name -> pid -> timestamps, so a duplicate is distinguishable from a second process.
    found: dict[str, dict[int, list[int]]] = {MARKER_FIRST_WINDOW: {}, MARKER_FIRST_MAIN_ITERATION: {}}

    for row in _iter_rows(path):
        # Register definitions from anywhere in the row, including nested `process` inside `thread`.
        for element in row.iter():
            identifier = element.get("id")
            if identifier is None:
                continue
            if element.tag == "signpost-name":
                names[identifier] = element.get("fmt") or (element.text or "")
            elif element.tag == "subsystem":
                subsystems[identifier] = element.get("fmt") or (element.text or "")
            elif element.tag == "event-time" and element.text:
                times[identifier] = int(element.text)
            elif element.tag == "process":
                pid_element = element.find("pid")
                if pid_element is not None and pid_element.text:
                    processes[identifier] = int(pid_element.text)

        def value(tag: str, table: dict[str, Any]) -> Any:
            element = row.find(tag)
            if element is None:
                return None
            identifier = element.get("id")
            return table.get(identifier if identifier is not None else (element.get("ref") or ""))

        name = value("signpost-name", names)
        if name not in found:
            continue
        if value("subsystem", subsystems) != STARTUP_SIGNPOST_SUBSYSTEM:
            continue  # e.g. the Metal driver's own Points of Interest in the same trace.
        timestamp = value("event-time", times)
        pid = value("process", processes)
        if timestamp is None or pid is None:
            continue
        found[name].setdefault(pid, []).append(timestamp)

    return _resolve_startup_markers(found)


def _resolve_startup_markers(found: dict[str, dict[int, list[int]]]) -> dict[str, Any]:
    def invalid(reason: str) -> dict[str, Any]:
        return {
            "invalid_reason": reason,
            "first_window_ns": None,
            "first_main_iteration_ns": None,
            "interval_ms": None,
            "pid": None,
        }

    windows = found[MARKER_FIRST_WINDOW]
    iterations = found[MARKER_FIRST_MAIN_ITERATION]
    # A trace can contain unrelated processes (a helper, a previous editor). The measured process is
    # the one that emitted both markers; anything else is noise rather than a broken capture.
    shared = sorted(set(windows) & set(iterations))
    if not shared:
        return invalid("cross-process" if windows and iterations else "missing")
    if len(shared) > 1:
        return invalid("cross-process")

    pid = shared[0]
    if len(windows[pid]) > 1 or len(iterations[pid]) > 1:
        return invalid("duplicate")

    first_window = windows[pid][0]
    first_iteration = iterations[pid][0]
    if first_iteration <= first_window:
        # Includes the coincident case: a zero-length interval measures nothing.
        return invalid("reversed")

    return {
        "invalid_reason": None,
        "first_window_ns": first_window,
        "first_main_iteration_ns": first_iteration,
        "interval_ms": (first_iteration - first_window) / 1e6,
        "pid": pid,
    }


def clip_gaps_to_interval(iterations_ns: Sequence[int], start_ns: int, end_ns: int) -> list[dict[str, float]]:
    """Stretches inside `[start_ns, end_ns)` during which the main run loop began no iteration.

    Expressed as the spans between successive boundaries — the interval start, every iteration
    strictly inside it, and the interval end. That definition clips for free at both edges: a stall
    that began before the window is charged only from the window, and one still running at the first
    `Main::iteration()` is charged only up to it. Pre-window and post-startup work are excluded by
    construction rather than by a separate filter that could disagree with it.

    Values are kept at full precision rather than rounded for readability. The gate is a strict
    `<= 250.0 ms`, and rounding to a tenth first would let a real 250.04 ms gap record as 250.0 and
    pass — a false green on the one number the criterion is defined on. Rounding happens at display.
    """
    boundaries = [start_ns] + [value for value in sorted(iterations_ns) if start_ns < value < end_ns] + [end_ns]
    return [
        {
            "offset_ms": (boundaries[index] - start_ns) / 1e6,
            "gap_ms": (boundaries[index + 1] - boundaries[index]) / 1e6,
        }
        for index in range(len(boundaries) - 1)
    ]


def measure_startup_interval(runloop_xml: Path, signpost_xml: Path) -> dict[str, Any]:
    """One run's #2097 measurement: markers, the interval they bound, and the clipped gaps."""
    markers = parse_startup_markers(signpost_xml)
    result: dict[str, Any] = dict(markers)
    if markers["invalid_reason"] is not None:
        result["clipped_gaps"] = []
        result["worst_clipped_gap_ms"] = None
        return result

    # Same process as the markers: see `scan_runloop_iterations`.
    iterations, _ = scan_runloop_iterations(runloop_xml, pid=markers["pid"])
    gaps = clip_gaps_to_interval(iterations, markers["first_window_ns"], markers["first_main_iteration_ns"])
    result["clipped_gaps"] = sorted(gaps, key=lambda entry: entry["gap_ms"], reverse=True)[:10]
    result["worst_clipped_gap_ms"] = max((entry["gap_ms"] for entry in gaps), default=0.0)
    result["main_runloop_iterations_in_interval"] = max(len(gaps) - 1, 0)
    return result


def summarize_startup_intervals(runs: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """The `startup_interval` summary block, including the across-run maximum the gate is defined on.

    Per-run data alone would force every consumer of `summary.json` to reimplement the acceptance
    calculation to learn the one number that decides it, so the aggregate is recorded alongside.

    Invalid runs are excluded from the maximum rather than folded in as zeros: a missing marker
    means "unmeasured", and a fabricated 0.0 would drag the aggregate toward a pass. The capture is
    rejected separately on the invalid reason, so nothing is lost by leaving them out here.
    """
    valid = [run for run in runs if run.get("invalid_reason") is None]
    worst = [float(run.get("worst_clipped_gap_ms") or 0.0) for run in valid]
    return {
        "runs": list(runs),
        "runs_measured": len(runs),
        "valid_runs": len(valid),
        "max_clipped_gap_ms": max(worst) if worst else None,
        "interval_ms": spread([float(run.get("interval_ms") or 0.0) for run in valid]),
    }


def command_timeline(args: argparse.Namespace) -> int:
    if not xctrace_available():
        fail("xcrun/xctrace not found; install Xcode command line tools")
    binary = resolve_binary(args.binary)
    project = Path(args.project).expanduser().resolve()
    out_dir = make_output_dir(args.out, args.label)

    if args.warm:
        warm_caches(binary, project, out_dir / "warmup.log")

    runs: list[dict[str, Any]] = []
    for index in range(1, args.runs + 1):
        trace_path = out_dir / f"timeline-run{index}.trace"
        log(f"timeline run {index}/{args.runs}: recording {args.time_limit}s Time Profiler trace")
        record_trace(binary, project, trace_path, args.time_limit, out_dir / f"timeline-run{index}.log")

        log("  exporting runloop-events")
        runloop_xml = export_table(trace_path, "runloop-events", out_dir / f"runloop-run{index}.xml")
        run: dict[str, Any] = {"trace": str(trace_path)}
        run.update(parse_runloop_events(runloop_xml))

        # The Time Profiler template records `os-signpost` on the same trace clock as
        # `runloop-events`, so the #2097 interval needs no separate capture to align against.
        log("  exporting os-signpost")
        signpost_xml = export_table(trace_path, "os-signpost", out_dir / f"signposts-run{index}.xml")
        interval = measure_startup_interval(runloop_xml, signpost_xml)
        run["startup_interval"] = interval
        if interval["invalid_reason"] is not None:
            log(f"  WARNING: startup markers {interval['invalid_reason']}; #2097 interval unmeasurable this run")

        if not args.skip_time_profile:
            log("  exporting time-profile")
            profile_xml = export_table(trace_path, "time-profile", out_dir / f"timeprofile-run{index}.xml")
            run.update(parse_time_profile(profile_xml, args.bucket_ms))
        runs.append(run)
        log(
            f"  first main run loop iteration at {run.get('time_to_first_main_iteration_ms')} ms, "
            f"longest non-servicing gap {run.get('longest_non_servicing_gap_ms')} ms"
        )
        if interval["invalid_reason"] is None:
            log(
                f"  #2097 interval {interval['interval_ms']:.1f} ms, "
                f"worst clipped gap {interval['worst_clipped_gap_ms']:.1f} ms"
            )

    summary = load_or_new_summary(out_dir, args.label, binary, project, args.cache_state)
    summary["timeline"] = {
        "time_limit_seconds": args.time_limit,
        "bucket_ms": args.bucket_ms,
        "runs": runs,
        "time_to_first_main_iteration_ms": spread([run.get("time_to_first_main_iteration_ms", 0.0) for run in runs]),
        "longest_non_servicing_gap_ms": spread([run.get("longest_non_servicing_gap_ms", 0.0) for run in runs]),
        "blocked_percent_of_running_main_samples": spread(
            [run.get("blocked_percent_of_running_main_samples", 0.0) for run in runs]
        ),
        "longest_contiguous_blocked_ms": spread([run.get("longest_contiguous_blocked_ms", 0.0) for run in runs]),
        # Per-run data is kept alongside the aggregate rather than replaced by a spread: the #2097
        # gate is a hard maximum, and a median would make the criterion unable to see one bad run.
        "startup_interval": summarize_startup_intervals([run["startup_interval"] for run in runs]),
    }
    record_load(summary, "timeline")
    write_summary(out_dir, summary)
    print_timeline_summary(summary)
    return 0


# --------------------------------------------------------------------------- #
# `--benchmark` phases
# --------------------------------------------------------------------------- #


def command_phases(args: argparse.Namespace) -> int:
    binary = resolve_binary(args.binary)
    project = Path(args.project).expanduser().resolve()
    out_dir = make_output_dir(args.out, args.label)

    if args.warm:
        warm_caches(binary, project, out_dir / "warmup.log")

    runs: list[dict[str, float]] = []
    for index in range(1, args.runs + 1):
        bench_path = out_dir / f"benchmark-run{index}.json"
        log(f"benchmark run {index}/{args.runs}")
        process, handle = launch_editor(
            binary,
            project,
            out_dir / f"benchmark-run{index}.log",
            [
                "--benchmark",
                "--benchmark-file",
                str(bench_path),
                "--quit-after",
                str(args.quit_after),
            ],
        )
        try:
            process.wait(timeout=900)
        except subprocess.TimeoutExpired:
            stop_editor(process)
        finally:
            handle.close()
        if not bench_path.is_file():
            fail(f"no benchmark file written at {bench_path} (the run must exit cleanly)")
        runs.append({key: float(value) for key, value in json.loads(bench_path.read_text()).items()})

    keys = sorted({key for run in runs for key in run})
    aggregate = {key: spread([run[key] for run in runs if key in run]) for key in keys}

    summary = load_or_new_summary(out_dir, args.label, binary, project, args.cache_state)
    summary["phases"] = {"quit_after": args.quit_after, "runs": runs, "median": aggregate}
    record_load(summary, "phases")
    write_summary(out_dir, summary)
    print_phases_summary(summary)
    return 0


# --------------------------------------------------------------------------- #
# Summary IO, reporting, comparison and the acceptance gate
# --------------------------------------------------------------------------- #


def summary_path(out_dir: Path) -> Path:
    return out_dir / "summary.json"


def load_or_new_summary(out_dir: Path, label: str, binary: Path, project: Path, cache_state: str) -> dict[str, Any]:
    path = summary_path(out_dir)
    summary: dict[str, Any]
    if path.is_file():
        summary = json.loads(path.read_text())
    else:
        summary = {"schema": SCHEMA, "created": now_iso()}
    summary.update(
        {
            "label": label,
            "updated": now_iso(),
            "binary": binary_metadata(binary),
            "project": str(project),
            "cache_state": cache_state,
            "host": host_metadata(),
            "git_revision": git_revision(),
        }
    )
    return summary


def record_load(summary: dict[str, Any], measurement: str) -> None:
    """Load is recorded per measurement: `all` runs four of them, minutes apart."""
    loads = summary.setdefault("machine_load", {})
    if not isinstance(loads, dict) or "load_average_1m" in loads:
        # A summary from before per-measurement recording; start over rather than merge shapes.
        loads = {}
        summary["machine_load"] = loads
    loads[measurement] = machine_load()


def load_averages(summary: dict[str, Any]) -> list[float]:
    """Every 1-minute load average recorded for this summary, whatever the record's vintage."""
    loads = summary.get("machine_load")
    if not isinstance(loads, dict):
        return []
    if "load_average_1m" in loads:
        return [float(loads["load_average_1m"])]
    return [
        float(entry["load_average_1m"])
        for entry in loads.values()
        if isinstance(entry, dict) and "load_average_1m" in entry
    ]


def write_summary(out_dir: Path, summary: dict[str, Any]) -> None:
    summary["acceptance"] = evaluate_acceptance(summary, DEFAULT_MAX_BLOCKED_PERCENT, DEFAULT_MAX_BLOCK_MS)
    summary_path(out_dir).write_text(json.dumps(summary, indent=2) + "\n")
    log(f"summary: {summary_path(out_dir)}")


def print_context(summary: dict[str, Any]) -> None:
    binary = summary.get("binary", {})
    print(f"binary       : {binary.get('name')} ({binary.get('flavor')})")
    if binary.get("version"):
        print(f"version      : {binary['version']}")
    print(f"built        : {binary.get('mtime')}")
    print(f"project      : {summary.get('project')}")
    print(f"cache state  : {summary.get('cache_state')}")
    loads = load_averages(summary)
    if loads:
        print(f"machine load : {min(loads)} to {max(loads)} (1m average, across measurements)")
    print(f"tree revision: {summary.get('git_revision')}")


def print_sample_summary(summary: dict[str, Any]) -> None:
    block = summary.get("sample")
    if not block:
        return
    print()
    print("== sample (main-thread call graph, aggregate) ==")
    print_context(summary)
    print(f"window       : {block['duration_seconds']}s x {block['runs'][0]['main_thread_samples']} samples (run 1)")
    print()
    print(f"{'run':>4}  {'main samples':>12}  {'samples/s':>9}  {'blocked %':>9}  {'blocked ms':>10}  {'idle %':>7}")
    for index, run in enumerate(block["runs"], start=1):
        print(
            f"{index:>4}  {run['main_thread_samples']:>12}  {run['samples_per_second']:>9}  "
            f"{run['blocked_percent']:>9}  {run['blocked_ms_estimate']:>10}  {run['idle_percent']:>7}"
        )
    stats = block["blocked_percent"]
    milliseconds = block["blocked_ms_estimate"]
    print(
        f"median blocked: {stats['median']}% (min {stats['min']}, max {stats['max']}) "
        f"~= {milliseconds['median']} ms (min {milliseconds['min']}, max {milliseconds['max']})"
    )


def print_timeline_summary(summary: dict[str, Any]) -> None:
    block = summary.get("timeline")
    if not block:
        return
    print()
    print("== timeline (Instruments Time Profiler) ==")
    print_context(summary)
    for index, run in enumerate(block["runs"], start=1):
        print()
        print(f"run {index}:")
        print(f"  main run loop iterations        : {run.get('main_runloop_iterations')}")
        print(f"  first main run loop iteration   : {run.get('time_to_first_main_iteration_ms')} ms after trace start")
        print(f"  longest non-servicing gap       : {run.get('longest_non_servicing_gap_ms')} ms")
        for gap in run.get("worst_gaps", [])[:5]:
            print(f"      gap {gap['gap_ms']:>8} ms starting at {gap['start_ms']} ms")
        _print_startup_interval(run.get("startup_interval"))
        if "blocked_percent_of_running_main_samples" in run:
            print(f"  running main samples inside the blocked frame: {run['blocked_percent_of_running_main_samples']}%")
            print(f"  longest contiguous blocked run  : {run['longest_contiguous_blocked_ms']} ms")
            print(f"  blocked window                  : {run['first_blocked_ms']} ms .. {run['last_blocked_ms']} ms")
            print(
                f"  per-{run['bucket_ms']:.0f}ms timeline (only buckets with blocked samples, and the first idle bucket):"
            )
            printed_idle = False
            for bucket in run["timeline"]:
                if bucket["blocked_samples"]:
                    printed_idle = False
                    print(
                        f"      t={bucket['start_ms']:>8} ms  main={bucket['main_samples']:>4}  "
                        f"blocked={bucket['blocked_samples']:>4}  ({bucket['blocked_percent']}%)"
                    )
                elif not printed_idle:
                    printed_idle = True
                    print(f"      t={bucket['start_ms']:>8} ms  main={bucket['main_samples']:>4}  blocked=   0  (0.0%)")


def _print_startup_interval(interval: dict[str, Any] | None) -> None:
    if not interval:
        return
    print("  #2097 startup interval [first window, first Main::iteration):")
    if interval.get("invalid_reason") is not None:
        print(f"      UNMEASURABLE: startup markers {interval['invalid_reason']}")
        return
    print(f"      duration                    : {interval['interval_ms']:.1f} ms (pid {interval['pid']})")
    print(f"      worst clipped gap           : {interval['worst_clipped_gap_ms']:.1f} ms")
    print(f"      run loop iterations inside  : {interval.get('main_runloop_iterations_in_interval')}")
    for gap in interval.get("clipped_gaps", [])[:5]:
        print(f"      clipped gap {gap['gap_ms']:>8.1f} ms at +{gap['offset_ms']:.1f} ms")


def print_phases_summary(summary: dict[str, Any]) -> None:
    block = summary.get("phases")
    if not block:
        return
    print()
    print("== benchmark phases (engine marks) ==")
    print_context(summary)
    rows = sorted(block["median"].items(), key=lambda item: item[1]["median"], reverse=True)
    print(f"{'phase':<48}{'median s':>10}{'min':>10}{'max':>10}")
    for name, stats in rows:
        if stats["median"] < 0.001:
            continue
        print(f"{name:<48}{stats['median']:>10.4f}{stats['min']:>10.4f}{stats['max']:>10.4f}")
    instrumented = sum(
        block["median"][key]["median"]
        for key in ("[Startup] Main::Setup", "[Startup] Main::Setup2", "[Startup] Main::Start")
        if key in block["median"]
    )
    print()
    print(f"instrumented setup+start total: {instrumented:.3f} s")
    print(
        "note: main_loop->initialize() (SceneTree::initialize) and the first-frame "
        "CallQueue::flush() carry no benchmark marks, so they are invisible here (#2090 AC5)."
    )


def print_window_summary(summary: dict[str, Any]) -> None:
    block = summary.get("window")
    if not block:
        return
    print()
    print("== window (time from spawn to first on-screen window) ==")
    print_context(summary)
    stats = block["time_to_first_window_ms"]
    print(f"runs         : {', '.join(f'{value:.1f}' for value in block['runs'])} ms")
    print(f"median       : {stats['median']:.1f} ms (min {stats['min']:.1f}, max {stats['max']:.1f})")
    print(f"poll interval: {block['poll_interval_ms']} ms")


def measured(stats: Any) -> bool:
    """False for a spread that aggregates nothing, so a fabricated 0.0 is never gated on."""
    return isinstance(stats, dict) and stats.get("runs", 0) >= 1


def evaluate_acceptance(summary: dict[str, Any], max_blocked_percent: float, max_block_ms: float) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    warnings: list[str] = []
    sample_block = summary.get("sample")
    if sample_block and measured(sample_block.get("blocked_percent")):
        value = sample_block["blocked_percent"]["median"]
        # The percentage criterion is a share of a capture window, so it is only
        # meaningful against the window length the issue names.
        window = sample_block.get("duration_seconds")
        if window != AC_SAMPLE_WINDOW_SECONDS:
            warnings.append(
                f"sample window is {window}s, but the {max_blocked_percent}% criterion is "
                f"defined for a {AC_SAMPLE_WINDOW_SECONDS}s capture; the percentage is not comparable"
            )
        checks.append(
            {
                "criterion": f"median main-thread samples under {BLOCK_FRAME} < {max_blocked_percent}%",
                "value": value,
                "threshold": max_blocked_percent,
                "passed": value < max_blocked_percent,
                "source": "sample",
            }
        )
    timeline_block = summary.get("timeline")
    if timeline_block and measured(timeline_block.get("longest_non_servicing_gap_ms")):
        value = timeline_block["longest_non_servicing_gap_ms"]["median"]
        checks.append(
            {
                "criterion": f"longest main run loop non-servicing gap <= {max_block_ms} ms",
                "value": value,
                "threshold": max_block_ms,
                "passed": value <= max_block_ms,
                "source": "timeline/runloop-events",
            }
        )
        contiguous_stats = timeline_block.get("longest_contiguous_blocked_ms")
        contiguous = contiguous_stats.get("median") if measured(contiguous_stats) else None
        if contiguous is not None:
            checks.append(
                {
                    "criterion": f"longest contiguous run inside {BLOCK_FRAME} <= {max_block_ms} ms",
                    "value": contiguous,
                    "threshold": max_block_ms,
                    "passed": contiguous <= max_block_ms,
                    "source": "timeline/time-profile",
                }
            )
    window_block = summary.get("window")
    if window_block and measured(window_block.get("time_to_first_window_ms")):
        value = window_block["time_to_first_window_ms"]["median"]
        checks.append(
            {
                "criterion": f"time from spawn to first on-screen window <= {DEFAULT_MAX_WINDOW_MS} ms",
                "value": value,
                "threshold": DEFAULT_MAX_WINDOW_MS,
                "passed": value <= DEFAULT_MAX_WINDOW_MS,
                "source": "window",
            }
        )
    return {
        "issue": "cafecito-games/Foundry#2090",
        "checks": checks,
        "warnings": warnings,
        "passed": all(check["passed"] for check in checks) if checks else False,
    }


def evaluate_acceptance_2097(summary: dict[str, Any], max_block_ms: float = DEFAULT_MAX_BLOCK_MS) -> dict[str, Any]:
    """The #2097 gate: the worst clipped gap in *every* run, not the median of the runs.

    #2090 gates on a median, which is the right shape for a "typical launch" claim but the wrong one
    here: "no gap exceeds 250 ms" is a hard maximum, and a median lets one bad capture in three pass
    unnoticed.
    """
    runs = (summary.get("timeline") or {}).get("startup_interval", {}).get("runs") or []
    invalid_runs = [
        {"run": index + 1, "reason": run.get("invalid_reason")}
        for index, run in enumerate(runs)
        if run.get("invalid_reason") is not None
    ]
    if not runs or invalid_runs:
        return {
            "issue": "cafecito-games/Foundry#2097",
            "invalid": True,
            "invalid_runs": invalid_runs,
            "reason": "no startup interval measurements in summary" if not runs else "invalid markers",
            "max_clipped_gap_ms": None,
            "threshold": max_block_ms,
            "runs": len(runs),
            "passed": False,
        }

    worst = [float(run.get("worst_clipped_gap_ms") or 0.0) for run in runs]
    maximum = max(worst)
    return {
        "issue": "cafecito-games/Foundry#2097",
        "invalid": False,
        "invalid_runs": [],
        "reason": None,
        "criterion": f"max clipped non-servicing gap in [first window, first Main::iteration) <= {max_block_ms} ms",
        "max_clipped_gap_ms": maximum,
        "per_run_worst_ms": worst,
        "threshold": max_block_ms,
        "runs": len(runs),
        "passed": maximum <= max_block_ms,
    }


def command_check(args: argparse.Namespace) -> int:
    summary = json.loads(Path(args.summary).expanduser().resolve().read_text())
    if getattr(args, "criteria", "2090") == "2097":
        return _print_2097_check(summary, args.max_block_ms)
    acceptance = evaluate_acceptance(summary, args.max_blocked_percent, args.max_block_ms)
    print("== acceptance gate (cafecito-games/Foundry#2090) ==")
    print_context(summary)
    print()
    if not acceptance["checks"]:
        print("no measurements in summary; nothing to check")
        return EXIT_MEASUREMENT_INVALID
    for warning in acceptance["warnings"]:
        print(f"WARNING: {warning}")
    for check in acceptance["checks"]:
        state = "PASS" if check["passed"] else "FAIL"
        print(f"[{state}] {check['criterion']}")
        print(f"        measured {check['value']} (source: {check['source']})")
    print()
    print("RESULT:", "PASS" if acceptance["passed"] else "FAIL")
    return 0 if acceptance["passed"] else 1


def _print_2097_check(summary: dict[str, Any], max_block_ms: float) -> int:
    acceptance = evaluate_acceptance_2097(summary, max_block_ms)
    print("== acceptance gate (cafecito-games/Foundry#2097) ==")
    print_context(summary)
    print()
    if acceptance["invalid"]:
        print("INVALID MEASUREMENT: the startup interval could not be established.")
        if not acceptance["invalid_runs"]:
            print(f"  {acceptance['reason']}")
        for entry in acceptance["invalid_runs"]:
            print(f"  run {entry['run']}: {entry['reason']} startup markers")
        print()
        print("RESULT: INVALID")
        return EXIT_MEASUREMENT_INVALID
    for index, value in enumerate(acceptance["per_run_worst_ms"], start=1):
        state = "PASS" if value <= max_block_ms else "FAIL"
        print(f"[{state}] run {index}: worst clipped gap {value:.1f} ms")
    print()
    state = "PASS" if acceptance["passed"] else "FAIL"
    print(f"[{state}] {acceptance['criterion']}")
    print(f"        measured {acceptance['max_clipped_gap_ms']:.1f} ms (max across {acceptance['runs']} runs)")
    print()
    print("RESULT:", "PASS" if acceptance["passed"] else "FAIL")
    return 0 if acceptance["passed"] else 1


def _metric(summary: dict[str, Any], path: Sequence[str]) -> float | None:
    node: Any = summary
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return None
        node = node[key]
    return float(node) if isinstance(node, (int, float)) else None


COMPARE_METRICS = [
    ("sample: blocked % of main-thread samples", ("sample", "blocked_percent", "median"), "%"),
    ("sample: blocked wall-clock estimate", ("sample", "blocked_ms_estimate", "median"), "ms"),
    ("sample: idle % of main-thread samples", ("sample", "idle_percent", "median"), "%"),
    (
        "timeline: time to first main run loop iteration",
        ("timeline", "time_to_first_main_iteration_ms", "median"),
        "ms",
    ),
    ("timeline: longest non-servicing gap", ("timeline", "longest_non_servicing_gap_ms", "median"), "ms"),
    ("timeline: longest contiguous blocked run", ("timeline", "longest_contiguous_blocked_ms", "median"), "ms"),
    ("window: time to first on-screen window", ("window", "time_to_first_window_ms", "median"), "ms"),
    ("phases: Main::Setup", ("phases", "median", "[Startup] Main::Setup", "median"), "s"),
    ("phases: Main::Setup2", ("phases", "median", "[Startup] Main::Setup2", "median"), "s"),
    ("phases: Main::Start", ("phases", "median", "[Startup] Main::Start", "median"), "s"),
]


# Absolute timings move by tens of percent between an idle and a busy machine, so a pair captured
# either side of that difference measures the load, not the change.
LOAD_MISMATCH_THRESHOLD = 1.5


def load_mismatch_warning(baseline: dict[str, Any], candidate: dict[str, Any]) -> str | None:
    """Warn when any two of the recorded loads are far enough apart to explain a delta by itself.

    The comparison spans both summaries, so it also catches a single campaign whose own
    measurements ran under changing load — `all` runs four of them over several minutes.
    """
    before = load_averages(baseline)
    after = load_averages(candidate)
    if not before or not after:
        # Summaries written before load recording existed; nothing to compare.
        return None
    low = min(min(before), min(after))
    high = max(max(before), max(after))
    if high - low < LOAD_MISMATCH_THRESHOLD:
        return None
    return (
        f"machine load varies materially across these measurements ({low} to {high}, 1m average) - "
        "absolute milliseconds are not comparable across that difference; re-capture both on an idle machine"
    )


def command_compare(args: argparse.Namespace) -> int:
    baseline = json.loads(Path(args.baseline).expanduser().resolve().read_text())
    candidate = json.loads(Path(args.candidate).expanduser().resolve().read_text())

    warnings: list[str] = []
    if baseline.get("binary", {}).get("flavor") != candidate.get("binary", {}).get("flavor"):
        warnings.append("binary flavors differ - these numbers are NOT comparable")
    if baseline.get("cache_state") != candidate.get("cache_state"):
        warnings.append("cache states differ - these numbers are NOT comparable")
    if baseline.get("project") != candidate.get("project"):
        warnings.append("projects differ - these numbers are NOT comparable")
    load_warning = load_mismatch_warning(baseline, candidate)
    if load_warning:
        warnings.append(load_warning)

    print("== comparison ==")
    print(
        f"baseline : {baseline.get('label')} ({baseline.get('binary', {}).get('name')}, {baseline.get('cache_state')} cache, rev {baseline.get('git_revision')})"
    )
    print(
        f"candidate: {candidate.get('label')} ({candidate.get('binary', {}).get('name')}, {candidate.get('cache_state')} cache, rev {candidate.get('git_revision')})"
    )
    for warning in warnings:
        print(f"WARNING: {warning}")
    print()
    print(f"{'metric':<52}{'baseline':>12}{'candidate':>12}{'delta':>12}{'':>4}")
    for name, path, unit in COMPARE_METRICS:
        before = _metric(baseline, path)
        after = _metric(candidate, path)
        if before is None or after is None:
            continue
        delta = after - before
        percent = f"{100.0 * delta / before:+.1f}%" if before else "n/a"
        print(f"{name:<52}{before:>12.3f}{after:>12.3f}{delta:>+12.3f}  {unit} {percent}")

    print()
    acceptance = evaluate_acceptance(candidate, args.max_blocked_percent, args.max_block_ms)
    for check in acceptance["checks"]:
        print(f"[{'PASS' if check['passed'] else 'FAIL'}] {check['criterion']} -> {check['value']}")
    print("RESULT:", "PASS" if acceptance["passed"] else "FAIL")
    if warnings:
        return 2
    return 0 if acceptance["passed"] else 1


def command_analyze(args: argparse.Namespace) -> int:
    target = Path(args.path).expanduser().resolve()
    if target.is_dir() and target.suffix == ".trace":
        runloop_xml = target.parent / f"{target.stem}-runloop.xml"
        export_table(target, "runloop-events", runloop_xml)
        result: dict[str, Any] = parse_runloop_events(runloop_xml)
        if not args.skip_time_profile:
            profile_xml = target.parent / f"{target.stem}-timeprofile.xml"
            export_table(target, "time-profile", profile_xml)
            result.update(parse_time_profile(profile_xml, args.bucket_ms))
        print(json.dumps(result, indent=2))
        return 0
    if target.suffix == ".xml":
        text = target.read_text(errors="replace")[:4000]
        if "runloop-events" in text:
            print(json.dumps(parse_runloop_events(target), indent=2))
        else:
            print(json.dumps(parse_time_profile(target, args.bucket_ms), indent=2))
        return 0

    result = analyze_sample(target, args.duration)
    if args.phases:
        result["phase_shares"] = analyze_sample_phases(target)
    print(json.dumps(result, indent=2))
    return 0


def command_all(args: argparse.Namespace) -> int:
    out_dir = make_output_dir(args.out, args.label)
    shared = [
        "--project",
        args.project,
        "--label",
        args.label,
        "--out",
        str(out_dir),
        "--cache-state",
        args.cache_state,
    ]

    capture_args = build_parser().parse_args(
        ["capture", "--binary", args.binary, "--runs", str(args.runs), "--duration", str(args.duration), *shared]
        + ([] if args.warm else ["--no-warm"])
    )
    command_capture(capture_args)

    if not args.skip_timeline:
        timeline_args = build_parser().parse_args(
            [
                "timeline",
                "--binary",
                args.binary,
                "--runs",
                "1",
                "--time-limit",
                str(args.duration),
                *shared,
                "--no-warm",
            ]
        )
        command_timeline(timeline_args)

    if not args.skip_phases:
        phases_args = build_parser().parse_args(
            ["phases", "--binary", args.binary, "--runs", str(args.runs), *shared, "--no-warm"]
        )
        command_phases(phases_args)

    if not args.skip_window:
        window_args = build_parser().parse_args(
            ["window", "--binary", args.binary, "--runs", str(args.runs), *shared, "--no-warm"]
        )
        command_window(window_args)

    check_args = build_parser().parse_args(["check", str(summary_path(out_dir))])
    return command_check(check_args)


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #


def add_common(parser: argparse.ArgumentParser, *, needs_project: bool = True) -> None:
    if needs_project:
        parser.add_argument(
            "--project", required=True, help="path to a project directory (must contain project.foundry)"
        )
    parser.add_argument(
        "--binary",
        default="release",
        help="'release' (optimized, stripped - trust timings not symbols), 'dev' (symbolized, "
        "proportions only), or an explicit path",
    )
    parser.add_argument(
        "--label", default="baseline", help="label recorded in the summary and used in the output dir name"
    )
    parser.add_argument(
        "--out", default=None, help=f"output directory (default: {DEFAULT_OUTPUT_ROOT}/<label>-<timestamp>)"
    )
    parser.add_argument(
        "--cache-state",
        default="warm",
        choices=("warm", "cold", "unknown"),
        help="recorded in the summary; comparisons across differing cache states are refused",
    )
    parser.add_argument(
        "--warm", dest="warm", action="store_true", default=True, help="run one throwaway launch first (default)"
    )
    parser.add_argument("--no-warm", dest="warm", action="store_false", help="skip the throwaway warm-up launch")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="macos_startup_profile.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture", help="capture `sample -wait` startup profiles (magnitude)")
    add_common(capture)
    capture.add_argument(
        "--runs", type=positive_int, default=3, help="number of runs (default 3; a single run is not a trend)"
    )
    capture.add_argument("--duration", type=float, default=20.0, help="sampling window in seconds (default 20)")
    capture.add_argument("--interval", type=int, default=1, help="sampling interval in milliseconds (default 1)")
    capture.set_defaults(func=command_capture)

    timeline = subparsers.add_parser("timeline", help="capture an Instruments trace and derive the per-bucket timeline")
    add_common(timeline)
    timeline.add_argument(
        "--runs", type=positive_int, default=1, help="number of traces (default 1; traces are expensive)"
    )
    timeline.add_argument("--time-limit", type=float, default=20.0, help="recording length in seconds (default 20)")
    timeline.add_argument("--bucket-ms", type=float, default=100.0, help="timeline bucket size (default 100 ms)")
    timeline.add_argument("--skip-time-profile", action="store_true", help="only parse run loop events (much faster)")
    timeline.set_defaults(func=command_timeline)

    phases = subparsers.add_parser("phases", help="capture engine `--benchmark` marks")
    add_common(phases)
    phases.add_argument("--runs", type=positive_int, default=3, help="number of runs (default 3)")
    phases.add_argument("--quit-after", type=int, default=300, help="iterations before the editor exits (default 300)")
    phases.set_defaults(func=command_phases)

    window = subparsers.add_parser("window", help="measure time from spawn to the first on-screen window")
    add_common(window)
    window.add_argument("--runs", type=positive_int, default=3, help="number of launches (default 3)")
    window.add_argument("--poll-interval-ms", type=float, default=2.0, help="window list poll interval (default 2 ms)")
    window.add_argument("--timeout", type=float, default=60.0, help="give up after this many seconds (default 60)")
    window.set_defaults(func=command_window)

    every = subparsers.add_parser(
        "all", help="capture + timeline + phases + window into one directory, then run the acceptance gate"
    )
    add_common(every)
    every.add_argument("--runs", type=positive_int, default=3)
    every.add_argument("--duration", type=float, default=20.0)
    every.add_argument("--skip-timeline", action="store_true")
    every.add_argument("--skip-phases", action="store_true")
    every.add_argument("--skip-window", action="store_true")
    every.set_defaults(func=command_all)

    analyze = subparsers.add_parser(
        "analyze", help="parse an existing artifact (sample .txt, exported .xml, or .trace)"
    )
    analyze.add_argument("path")
    analyze.add_argument(
        "--duration", type=float, default=20.0, help="sampling window used for the .txt profile (for rate math)"
    )
    analyze.add_argument("--bucket-ms", type=float, default=100.0)
    analyze.add_argument(
        "--phases", action="store_true", help="also break the blocked window down by boot phase (dev build only)"
    )
    analyze.add_argument("--skip-time-profile", action="store_true")
    analyze.set_defaults(func=command_analyze)

    compare = subparsers.add_parser("compare", help="compare two summary.json files and re-run the acceptance gate")
    compare.add_argument("baseline")
    compare.add_argument("candidate")
    compare.add_argument("--max-blocked-percent", type=float, default=DEFAULT_MAX_BLOCKED_PERCENT)
    compare.add_argument("--max-block-ms", type=float, default=DEFAULT_MAX_BLOCK_MS)
    compare.set_defaults(func=command_compare)

    check = subparsers.add_parser("check", help="evaluate an acceptance criteria set against a summary.json")
    check.add_argument("summary")
    check.add_argument(
        "--criteria",
        choices=("2090", "2097"),
        default="2090",
        help=(
            "'2090' (default) keeps the historical median-based gate; '2097' gates the maximum "
            "clipped non-servicing gap in the first-window to first-Main::iteration interval across "
            f"every run, and exits {EXIT_MEASUREMENT_INVALID} when the markers make it unmeasurable"
        ),
    )
    check.add_argument("--max-blocked-percent", type=float, default=DEFAULT_MAX_BLOCKED_PERCENT)
    check.add_argument("--max-block-ms", type=float, default=DEFAULT_MAX_BLOCK_MS)
    check.set_defaults(func=command_check)

    return parser


def main(argv: Iterable[str] | None = None) -> int:
    if sys.platform != "darwin":
        fail("this tool measures the macOS editor and only runs on macOS")
    args = build_parser().parse_args(list(argv) if argv is not None else None)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
