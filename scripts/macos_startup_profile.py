#!/usr/bin/env python3
"""Deterministic macOS editor startup profiling for the Foundry editor.

Captures, parses and compares startup profiles of the macOS editor so that the
"editor blocks the run loop during launch" measurements are reproducible instead
of being re-derived by hand every session.

Three independent measurements are supported, each answering a different
question:

* ``sample``   - what share of main-thread samples sits inside
                 ``-[NSApplication _sendFinishLaunchingNotification]``
                 (magnitude, works on the stripped optimized binary).
* ``timeline`` - Instruments ``Time Profiler`` capture, giving per-sample
                 timestamps and main run loop iteration events, so the longest
                 uninterrupted non-servicing block can be measured in
                 milliseconds instead of inferred.
* ``phases``   - ``--benchmark`` marks, i.e. the engine's own phase timings.

Everything is stdlib only. Run ``--help`` on any subcommand for details.
"""

from __future__ import annotations

import argparse
import json
import os
import platform as platform_module
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
AC_SAMPLE_WINDOW_SECONDS = 20.0

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


def parse_runloop_events(path: Path) -> dict[str, Any]:
    """Main run loop iteration events -> the real 'not servicing' gap measurement."""
    strings: dict[str, str] = {}
    booleans: dict[str, str] = {}
    funcs: dict[str, str] = {}
    times: dict[str, int] = {}
    iterations: list[int] = []
    origin: int | None = None

    for row in _iter_rows(path):
        timestamp: int | None = None
        interval_type = ""
        is_main = ""
        func = ""
        short_string_index = 0
        for child in row:
            tag = child.tag
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
        origin = timestamp if origin is None else min(origin, timestamp)
        if is_main == "Yes" and interval_type == "individual_iteration" and func == "START":
            iterations.append(timestamp)

    if not iterations or origin is None:
        return {"main_runloop_iterations": 0}

    iterations.sort()
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

        if not args.skip_time_profile:
            log("  exporting time-profile")
            profile_xml = export_table(trace_path, "time-profile", out_dir / f"timeprofile-run{index}.xml")
            run.update(parse_time_profile(profile_xml, args.bucket_ms))
        runs.append(run)
        log(
            f"  first main run loop iteration at {run.get('time_to_first_main_iteration_ms')} ms, "
            f"longest non-servicing gap {run.get('longest_non_servicing_gap_ms')} ms"
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
    }
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


def evaluate_acceptance(summary: dict[str, Any], max_blocked_percent: float, max_block_ms: float) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    warnings: list[str] = []
    sample_block = summary.get("sample")
    if sample_block:
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
    if timeline_block:
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
        contiguous = timeline_block.get("longest_contiguous_blocked_ms", {}).get("median")
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
    return {
        "issue": "cafecito-games/Foundry#2090",
        "checks": checks,
        "warnings": warnings,
        "passed": all(check["passed"] for check in checks) if checks else False,
    }


def command_check(args: argparse.Namespace) -> int:
    summary = json.loads(Path(args.summary).expanduser().resolve().read_text())
    acceptance = evaluate_acceptance(summary, args.max_blocked_percent, args.max_block_ms)
    print("== acceptance gate (cafecito-games/Foundry#2090) ==")
    print_context(summary)
    print()
    if not acceptance["checks"]:
        print("no measurements in summary; nothing to check")
        return 2
    for warning in acceptance["warnings"]:
        print(f"WARNING: {warning}")
    for check in acceptance["checks"]:
        state = "PASS" if check["passed"] else "FAIL"
        print(f"[{state}] {check['criterion']}")
        print(f"        measured {check['value']} (source: {check['source']})")
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
    ("phases: Main::Setup", ("phases", "median", "[Startup] Main::Setup", "median"), "s"),
    ("phases: Main::Setup2", ("phases", "median", "[Startup] Main::Setup2", "median"), "s"),
    ("phases: Main::Start", ("phases", "median", "[Startup] Main::Start", "median"), "s"),
]


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
    capture.add_argument("--runs", type=int, default=3, help="number of runs (default 3; a single run is not a trend)")
    capture.add_argument("--duration", type=float, default=20.0, help="sampling window in seconds (default 20)")
    capture.add_argument("--interval", type=int, default=1, help="sampling interval in milliseconds (default 1)")
    capture.set_defaults(func=command_capture)

    timeline = subparsers.add_parser("timeline", help="capture an Instruments trace and derive the per-bucket timeline")
    add_common(timeline)
    timeline.add_argument("--runs", type=int, default=1, help="number of traces (default 1; traces are expensive)")
    timeline.add_argument("--time-limit", type=float, default=20.0, help="recording length in seconds (default 20)")
    timeline.add_argument("--bucket-ms", type=float, default=100.0, help="timeline bucket size (default 100 ms)")
    timeline.add_argument("--skip-time-profile", action="store_true", help="only parse run loop events (much faster)")
    timeline.set_defaults(func=command_timeline)

    phases = subparsers.add_parser("phases", help="capture engine `--benchmark` marks")
    add_common(phases)
    phases.add_argument("--runs", type=int, default=3, help="number of runs (default 3)")
    phases.add_argument("--quit-after", type=int, default=300, help="iterations before the editor exits (default 300)")
    phases.set_defaults(func=command_phases)

    every = subparsers.add_parser(
        "all", help="capture + timeline + phases into one directory, then run the acceptance gate"
    )
    add_common(every)
    every.add_argument("--runs", type=int, default=3)
    every.add_argument("--duration", type=float, default=20.0)
    every.add_argument("--skip-timeline", action="store_true")
    every.add_argument("--skip-phases", action="store_true")
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

    check = subparsers.add_parser("check", help="evaluate the #2090 acceptance criteria against a summary.json")
    check.add_argument("summary")
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
