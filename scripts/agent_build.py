#!/usr/bin/env python3
"""Agent-friendly Foundry build wrapper for local and cloud environments."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
import platform as platform_module
import queue
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from collections.abc import Iterator, Mapping
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NamedTuple, TextIO, cast

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CACHE_PATH = Path.home() / ".scons_cache"
DEFAULT_TEST_SCRATCH = REPO_ROOT / ".test_scratch"
SUPPORTED_SCONS_PLATFORMS = ("linuxbsd", "macos")
DEFAULT_CCACHE_PATH = Path.home() / ".cache" / "foundry-ccache"
SUPPORTED_BUILD_BACKENDS = ("scons", "ninja")
SUPPORTED_COMPILER_CACHES = ("auto", "none", "ccache")
NINJA_OWNED_SCONS_KEYS = frozenset(
    {
        "cache_path",
        "c_compiler_launcher",
        "cpp_compiler_launcher",
        "debug_paths_relative",
        "ninja",
        "ninja_auto_run",
        "ninja_dir",
        "ninja_file",
    }
)
SUPPORTED_SCONS_TARGETS = ("editor", "template_debug", "template_release")
# The companion settings each target implies. Building a target with the wrong companions verifies a
# different configuration than the one named, so the wrapper owns them instead of the caller.
TARGET_COMPANION_SETTINGS: dict[str, dict[str, str]] = {
    "editor": {"dev_mode": "yes", "dev_build": "yes", "tests": "yes"},
    "template_debug": {"dev_mode": "no", "dev_build": "no", "tests": "no"},
    "template_release": {"dev_mode": "no", "dev_build": "no", "tests": "no"},
}
COMPANION_SETTING_ORDER = ("dev_mode", "dev_build", "tests")
TELEMETRY_TIMEOUT_SECONDS = 5.0
RESULT_PREFIX = "[agent-build] RESULT:"
RESULT_STATUSES = (
    "success",
    "build-failure",
    "generation-failure",
    "binary-missing",
    "test-failure",
    "tooling-missing",
    "interrupted",
)
RESULT_STEPS = ("startup", "generate", "build", "test")
# How a build or test child ended, as reported by the terminal verdict.
CHILD_DISPOSITIONS = ("none", "exited", "terminated", "killed", "escaped")
# How long an interrupted child is given to stop on SIGTERM before it is killed outright.
CHILD_TERMINATION_GRACE_SECONDS = 10.0
MISSING_BINARY_EXIT_CODE = 1
TOOLING_MISSING_EXIT_CODE = 127
INTERRUPTED_EXIT_CODE = 130
# The falsy spellings SCons accepts for a boolean build setting, mirroring SCons' own BoolVariable
# table. Missing one here makes the wrapper predict the wrong binary name for a build that disabled
# the setting, and then resolve a stale binary from an earlier configuration.
SCONS_FALSE_VALUES = frozenset({"0", "f", "false", "n", "no", "none", "off"})
JOBS_ENVIRONMENT_VARIABLE = "FOUNDRY_BUILD_JOBS"
DEFAULT_CGROUP_ROOT = Path("/sys/fs/cgroup")
DEFAULT_PROC_CGROUP = Path("/proc/self/cgroup")
BUILD_DESCRIPTION_EXCLUDED_DIRECTORIES = frozenset(
    {".foundry", ".git", ".ninja", ".test_scratch", ".worktrees", "__pycache__", "bin", "build", "out"}
)
BUILD_GRAPH_UNTRACKED_SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".glsl",
        ".h",
        ".hh",
        ".hpp",
        ".inc",
        ".inl",
        ".json",
        ".m",
        ".mm",
        ".otf",
        ".po",
        ".py",
        ".s",
        ".svg",
        ".toml",
        ".ttf",
        ".woff",
        ".woff2",
        ".xml",
        ".yaml",
        ".yml",
    }
)


class JobSelection(NamedTuple):
    """Build concurrency together with the input that determined it."""

    jobs: int
    source: str


def read_cgroup_v2_cpu_limit(directory: Path) -> float | None:
    """Effective CPU count from a cgroup v2 `cpu.max` file, or None when unlimited."""
    try:
        fields = (directory / "cpu.max").read_text(encoding="utf-8").split()
    except OSError:
        return None
    if len(fields) != 2 or fields[0] == "max":
        return None
    try:
        quota = int(fields[0])
        period = int(fields[1])
    except ValueError:
        return None
    if quota <= 0 or period <= 0:
        return None
    return quota / period


def read_cgroup_v1_cpu_limit(directory: Path) -> float | None:
    """Effective CPU count from cgroup v1 CFS quota files, or None when unlimited."""
    try:
        quota = int((directory / "cpu.cfs_quota_us").read_text(encoding="utf-8").strip())
        period = int((directory / "cpu.cfs_period_us").read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None
    if quota <= 0 or period <= 0:
        return None
    return quota / period


def cgroup_v2_directories(cgroup_root: Path, proc_cgroup: Path) -> list[Path]:
    """The cgroup mount root plus every directory down to this process' own cgroup."""
    directories = [cgroup_root]
    try:
        lines = proc_cgroup.read_text(encoding="utf-8").splitlines()
    except OSError:
        return directories
    relative = next((line.partition("::")[2] for line in lines if line.startswith("0::")), "")
    directory = cgroup_root
    for part in relative.split("/"):
        if not part or part in (".", ".."):
            continue
        directory = directory / part
        directories.append(directory)
    return directories


def cgroup_v1_directories(cgroup_root: Path, proc_cgroup: Path) -> list[Path]:
    """Every cgroup v1 CPU controller directory that applies to this process.

    A container's quota can live either at the controller mount root (when the mount is already
    scoped to the container) or under the process-specific path from `/proc/self/cgroup`.
    """
    bases = [cgroup_root / "cpu"]
    relative = ""
    try:
        lines = proc_cgroup.read_text(encoding="utf-8").splitlines()
    except OSError:
        lines = []
    for line in lines:
        _, _, remainder = line.partition(":")
        controllers, _, path = remainder.partition(":")
        if "cpu" not in controllers.split(","):
            continue
        mount = cgroup_root / controllers
        if mount not in bases:
            bases.append(mount)
        relative = path
        break

    directories: list[Path] = []
    for base in bases:
        directories.append(base)
        directory = base
        for part in relative.split("/"):
            if not part or part in (".", ".."):
                continue
            directory = directory / part
            directories.append(directory)
    return directories


def cgroup_cpu_limit(
    cgroup_root: Path = DEFAULT_CGROUP_ROOT,
    proc_cgroup: Path = DEFAULT_PROC_CGROUP,
) -> float | None:
    """The tightest CPU quota that applies to this process, or None when unconstrained."""
    readings = [read_cgroup_v2_cpu_limit(directory) for directory in cgroup_v2_directories(cgroup_root, proc_cgroup)]
    readings += [read_cgroup_v1_cpu_limit(directory) for directory in cgroup_v1_directories(cgroup_root, proc_cgroup)]
    limits = [limit for limit in readings if limit is not None]
    return min(limits) if limits else None


def resolve_job_selection(
    explicit_jobs: int | None,
    environment: Mapping[str, str],
    host_cpu_count: int | None,
    cgroup_root: Path = DEFAULT_CGROUP_ROOT,
    proc_cgroup: Path = DEFAULT_PROC_CGROUP,
) -> JobSelection:
    """Pick build concurrency from --jobs, the environment override, or the execution environment.

    A container can see every host CPU while being allowed only a fraction of them, so the
    host CPU count alone overcommits the workspace and makes builds fail to spawn processes.
    """
    if explicit_jobs is not None:
        if explicit_jobs < 1:
            raise ValueError("--jobs must be at least 1")
        return JobSelection(explicit_jobs, "--jobs")

    override = environment.get(JOBS_ENVIRONMENT_VARIABLE)
    if override is not None:
        try:
            requested = int(override.strip())
        except ValueError:
            requested = 0
        if requested < 1:
            raise ValueError(f"{JOBS_ENVIRONMENT_VARIABLE} must be a positive integer, got {override!r}")
        return JobSelection(requested, JOBS_ENVIRONMENT_VARIABLE)

    host_jobs = max(1, host_cpu_count or 1)
    quota = cgroup_cpu_limit(cgroup_root, proc_cgroup)
    if quota is not None:
        quota_jobs = max(1, int(quota))
        if quota_jobs < host_jobs:
            return JobSelection(quota_jobs, "cgroup-cpu-quota")
    return JobSelection(host_jobs, "host-cpu-count")


def resolve_compiler_cache(args: argparse.Namespace) -> str:
    if args.compiler_cache != "auto":
        return str(args.compiler_cache)
    return "ccache" if args.backend == "ninja" else "none"


def _set_ccache_boolean(env: dict[str, str], name: str, *, enabled: bool) -> None:
    positive = f"CCACHE_{name}"
    negative = f"CCACHE_NO{name}"
    env.pop(negative if enabled else positive, None)
    env[positive if enabled else negative] = "1"


class OutputPaths(NamedTuple):
    log: Path
    progress: Path


def _filename_slug(name: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", name).strip("._-")[:80].rstrip("._-")
    return slug or "worktree"


def worktree_identity(repo_root: Path) -> str:
    resolved = str(repo_root.resolve())
    digest = hashlib.sha256(resolved.encode("utf-8")).hexdigest()[:10]
    return f"{_filename_slug(repo_root.name)}-{digest}"


def default_output_paths(repo_root: Path, temp_root: Path = Path("/tmp")) -> OutputPaths:
    identity = worktree_identity(repo_root)
    return OutputPaths(
        log=temp_root / f"foundry-build-{identity}.log",
        progress=temp_root / f"foundry-build-{identity}-progress.jsonl",
    )


DEFAULT_OUTPUT_PATHS = default_output_paths(REPO_ROOT)
DEFAULT_LOG = DEFAULT_OUTPUT_PATHS.log
DEFAULT_PROGRESS_LOG = DEFAULT_OUTPUT_PATHS.progress


class BuildTarget(NamedTuple):
    scons_platform: str
    binary_path: Path
    default_display: str | None
    scons_target: str = "editor"


class NinjaState(NamedTuple):
    directory: Path
    file: Path


def format_duration(seconds: float) -> str:
    total = max(0, int(seconds))
    minutes, secs = divmod(total, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}h{minutes:02d}m{secs:02d}s"
    if minutes:
        return f"{minutes}m{secs:02d}s"
    return f"{secs}s"


def timestamp_utc() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def new_invocation_id() -> str:
    return str(uuid.uuid4())


def ccache_stats_log_path(invocation_id: str, *, temp_root: Path | None = None) -> Path:
    root = temp_root if temp_root is not None else Path(tempfile.gettempdir())
    return root / f"foundry-ccache-{invocation_id}.stats"


def read_git_commit(
    repo_root: Path = REPO_ROOT,
    *,
    run=subprocess.run,
) -> tuple[str, str | None]:
    try:
        completed = run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=TELEMETRY_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return "unknown", f"git commit probe timed out after {TELEMETRY_TIMEOUT_SECONDS:g} seconds"
    except OSError as exc:
        return "unknown", f"could not execute git commit probe: {exc}"
    except Exception as exc:
        return "unknown", f"unexpected git commit probe failure: {type(exc).__name__}: {exc}"
    if completed.returncode != 0:
        error = completed.stderr.strip() or f"git exited with {completed.returncode}"
        return "unknown", error
    commit = completed.stdout.strip()
    if not commit:
        return "unknown", "git commit probe returned empty output"
    return commit, None


def read_ccache_stats(
    env: dict[str, str],
    *,
    run=subprocess.run,
) -> dict[str, object]:
    command = [
        "ccache",
        "--print-log-stats" if env.get("CCACHE_STATSLOG") else "--print-stats",
        "--format=json",
    ]
    try:
        completed = run(
            command,
            env=env,
            check=False,
            capture_output=True,
            text=True,
            timeout=TELEMETRY_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return {"error": f"ccache statistics probe timed out after {TELEMETRY_TIMEOUT_SECONDS:g} seconds"}
    except OSError as exc:
        return {"error": f"could not execute ccache statistics probe: {exc}"}
    if completed.returncode != 0:
        return {"error": completed.stderr.strip() or f"ccache exited with {completed.returncode}"}
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        return {"error": f"invalid ccache statistics JSON: {exc}"}
    return payload if isinstance(payload, dict) else {"error": "ccache statistics were not a JSON object"}


def read_ccache_stats_best_effort(env: dict[str, str]) -> dict[str, object]:
    try:
        return read_ccache_stats(env)
    except Exception as exc:
        return {"error": f"unexpected ccache statistics failure: {type(exc).__name__}: {exc}"}


def stats_delta(before: dict[str, object], after: dict[str, object]) -> dict[str, int | float]:
    delta: dict[str, int | float] = {}
    for key, after_value in after.items():
        before_value = before.get(key)
        if (
            isinstance(after_value, (int, float))
            and not isinstance(after_value, bool)
            and isinstance(before_value, (int, float))
            and not isinstance(before_value, bool)
        ):
            delta[key] = after_value - before_value
    return delta


def append_progress_record(
    path: Path | None,
    event: str,
    *,
    mode: str = "a",
    stdout_jsonl: bool = False,
    **fields: object,
) -> None:
    if path is None and not stdout_jsonl:
        return
    payload = {"version": 1, "event": event, "timestamp": timestamp_utc(), **fields}
    line = json.dumps(payload, sort_keys=True)
    if path is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open(mode, encoding="utf-8") as progress_file:
            progress_file.write(line + "\n")
    if stdout_jsonl:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()


def binary_identity(path: Path | None) -> dict[str, object] | None:
    """What the file at `path` is right now, or None when there is no such file.

    A caller comparing the identity recorded before a build with the one recorded after it can tell
    whether this invocation actually relinked the binary it is about to run.
    """
    if path is None:
        return None
    try:
        if not path.is_file():
            return None
        stats = path.stat()
    except OSError:
        return None
    return {"path": str(path), "size": stats.st_size, "mtime_ns": stats.st_mtime_ns}


def built_binary_identities(target: BuildTarget) -> dict[str, dict[str, object]]:
    """Pre-build identities of every binary this target's build could resolve to, keyed by path.

    Build settings the wrapper cannot reconstruct rename the binary, so the file a run ends up
    reporting is not always the one whose name the wrapper predicted. Recording the whole directory
    keeps `binary_changed` an honest comparison of one path against its own earlier state.
    """
    identities: dict[str, dict[str, object]] = {}
    candidates = [target.binary_path]
    try:
        candidates += sorted(target.binary_path.parent.glob(f"foundry.{target.scons_platform}.{target.scons_target}*"))
    except OSError:
        pass
    for candidate in candidates:
        identity = binary_identity(candidate)
        if identity is not None:
            identities[str(candidate)] = identity
    return identities


class ProgressReporter:
    def __init__(
        self,
        *,
        phase: str,
        invocation_id: str,
        progress_path: Path | None,
        append_progress: bool,
        stdout_jsonl: bool,
        started: float,
    ) -> None:
        self.phase = phase
        self.invocation_id = invocation_id
        self.progress_path = progress_path
        self.append_progress = append_progress
        self.stdout_jsonl = stdout_jsonl
        self.started = started
        self.sequence = 0
        self.progress_file: TextIO | None = None

    def __enter__(self) -> ProgressReporter:
        if self.progress_path is not None:
            self.progress_path.parent.mkdir(parents=True, exist_ok=True)
            mode = "a" if self.append_progress else "w"
            self.progress_file = cast(TextIO, self.progress_path.open(mode, encoding="utf-8"))
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback) -> None:
        if self.progress_file is not None:
            self.progress_file.close()

    def emit(self, event: str, **fields) -> None:
        self.sequence += 1
        payload = {
            "version": 1,
            "event": event,
            "phase": self.phase,
            "invocation_id": self.invocation_id,
            "sequence": self.sequence,
            "timestamp": timestamp_utc(),
            "elapsed_ms": int((time.monotonic() - self.started) * 1000),
        }
        payload.update(fields)
        line = json.dumps(payload, sort_keys=True)
        if self.progress_file is not None:
            self.progress_file.write(f"{line}\n")
            self.progress_file.flush()
        if self.stdout_jsonl:
            sys.stdout.write(f"{line}\n")
            sys.stdout.flush()


def write_status(log_file, message: str, *, stream: TextIO | None = None) -> None:
    if stream is None:
        stream = sys.stdout
    line = f"{message}\n"
    stream.write(line)
    stream.flush()
    log_file.write(line)
    log_file.flush()


def stream_output(pipe, output_queue: queue.Queue[str | None]) -> None:
    try:
        for line in pipe:
            output_queue.put(line)
    finally:
        output_queue.put(None)


class StopSignalDeferral:
    """Whether a stop signal must be held, and the one being held."""

    def __init__(self) -> None:
        self.active = False
        self.pending: int | None = None


STOP_SIGNALS = StopSignalDeferral()


def raise_interrupt(signal_number: int, frame: object) -> None:
    """Route a supervisor's stop signal into the wrapper's ordinary interrupt path."""
    if STOP_SIGNALS.active:
        STOP_SIGNALS.pending = signal_number
        return
    raise KeyboardInterrupt(f"signal {signal_number}")


def install_stop_signal_handlers() -> dict[int, Any]:
    """Handle the signals a supervisor stops a build with, and report what they replaced.

    A stop signal is delivered to the wrapper alone — its build runs in a separate session — so
    without a handler the wrapper dies leaving the build running and no verdict written at all.
    `SIGINT` is handled explicitly, rather than left to the interpreter's default, so that it can be
    deferred over the sections where an interrupt would lose track of a running build.
    """
    previous: dict[int, Any] = {}
    for name in ("SIGINT", "SIGTERM", "SIGHUP"):
        number = getattr(signal, name, None)
        if number is None:
            continue
        try:
            previous[number] = signal.signal(number, raise_interrupt)
        except (OSError, ValueError):
            continue
    return previous


@contextlib.contextmanager
def deferred_stop_signals(*, deliver: bool = True) -> Iterator[None]:
    """Hold stop signals for the length of a section, then deliver one that arrived.

    Creating the build process is such a section: an interrupt raised inside `Popen` leaves a
    started, separately-sessioned child that no handle refers to, which is unstoppable and therefore
    the very orphan this shutdown path exists to prevent. Deferring costs only the microseconds the
    launch takes, and the signal is honored immediately afterwards.

    Stopping that build is the other: an impatient second signal must not abort the shutdown and let
    the wrapper report a verdict over a build that is still running. `deliver=False` drops the held
    signal there, because the shutdown it would ask for is already under way.
    """
    STOP_SIGNALS.active = True
    try:
        yield
    finally:
        STOP_SIGNALS.active = False
        pending = STOP_SIGNALS.pending
        STOP_SIGNALS.pending = None
    if pending is not None and deliver:
        raise KeyboardInterrupt(f"signal {pending}")


def child_group_id(proc: subprocess.Popen[str]) -> int | None:
    """The child's process group, or None where the platform has none to address."""
    if os.name == "nt":
        return None
    try:
        pgid = os.getpgid(proc.pid)
    except OSError:
        return None
    # The child is expected to lead its own group; sharing the wrapper's would make a shutdown
    # signal reach the wrapper itself.
    return None if pgid == os.getpgrp() else pgid


def signal_child_group(proc: subprocess.Popen[str], pgid: int | None, signal_number: int) -> None:
    """Signal the child's whole process group, so a build's compilers go with it.

    The child is started in its own session, so signaling only the child would leave the compiler
    processes it spawned running and still writing objects.
    """
    if pgid is not None:
        try:
            os.killpg(pgid, signal_number)
            return
        except ProcessLookupError:
            return
        except OSError:
            pass
    try:
        if signal_number == getattr(signal, "SIGKILL", None):
            proc.kill()
        else:
            proc.terminate()
    except (OSError, ValueError):
        pass


def child_group_is_running(pgid: int | None) -> bool:
    """Whether any process of the group is still there — a reaped leader proves nothing."""
    if pgid is None:
        return False
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except OSError:
        return True
    return True


def wait_for_child(proc: subprocess.Popen[str], timeout: float) -> int | None:
    """Wait up to `timeout` for the child, ignoring further interrupts, and report its status.

    A second interrupt arriving while the wrapper is shutting a build down must not abandon the
    wait: that is exactly how an orphan escapes. `None` means the child outlived the timeout.
    """
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return proc.poll()
        try:
            return proc.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            return proc.poll()
        except KeyboardInterrupt:
            continue


def wait_for_child_group(pgid: int | None, timeout: float) -> bool:
    """Wait up to `timeout` for the group to empty out, and report whether it did.

    The build driver's own exit is not proof that the build stopped: a compiler it spawned can
    outlive it and keep writing objects, so the group is what has to be observed gone.
    """
    deadline = time.monotonic() + timeout
    while child_group_is_running(pgid):
        if time.monotonic() >= deadline:
            return False
        try:
            time.sleep(0.05)
        except KeyboardInterrupt:
            continue
    return True


def terminate_child_group(
    proc: subprocess.Popen[str],
    pgid: int | None,
    *,
    grace_seconds: float | None = None,
) -> tuple[str, int | None]:
    """Stop an interrupted run's child process group and wait for it to actually be gone.

    Returns the group's disposition and the driver's exit status: `exited` when the build had
    already finished on its own, `terminated` when SIGTERM was enough, `killed` when SIGKILL was
    needed, and `escaped` when something survived even that — the one case where later writes to the
    build tree are still possible.
    """
    grace = CHILD_TERMINATION_GRACE_SECONDS if grace_seconds is None else grace_seconds
    if proc.poll() is not None and not child_group_is_running(pgid):
        return "exited", proc.returncode

    signal_child_group(proc, pgid, signal.SIGTERM)
    started = time.monotonic()
    status = wait_for_child(proc, grace)
    if status is not None and wait_for_child_group(pgid, max(0.0, grace - (time.monotonic() - started))):
        return "terminated", status

    signal_child_group(proc, pgid, getattr(signal, "SIGKILL", signal.SIGTERM))
    started = time.monotonic()
    if status is None:
        status = wait_for_child(proc, grace)
    if status is not None and wait_for_child_group(pgid, max(0.0, grace - (time.monotonic() - started))):
        return "killed", status
    return "escaped", status


def run_logged_command(
    command: list[str],
    *,
    label: str,
    invocation_id: str,
    log_path: Path,
    heartbeat: float,
    append_log: bool,
    progress_path: Path | None = None,
    append_progress: bool = False,
    progress_stdout_jsonl: bool = False,
    env: dict[str, str] | None = None,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    mode = "a" if append_log else "w"
    human_stream = sys.stderr if progress_stdout_jsonl else sys.stdout
    with log_path.open(mode, encoding="utf-8") as log_file:
        if append_log:
            log_file.write("\n")

        started = time.monotonic()
        with ProgressReporter(
            phase=label,
            invocation_id=invocation_id,
            progress_path=progress_path,
            append_progress=append_progress,
            stdout_jsonl=progress_stdout_jsonl,
            started=started,
        ) as progress:
            progress.emit(
                "command_start",
                command=command,
                command_text=shlex.join(command),
                log_path=str(log_path),
                progress_path=str(progress_path) if progress_path is not None else None,
            )
            write_status(log_file, f"[agent-build] {label}: {shlex.join(command)}", stream=human_stream)
            write_status(log_file, f"[agent-build] log: {log_path}", stream=human_stream)
            if progress_path is not None:
                write_status(log_file, f"[agent-build] progress: {progress_path}", stream=human_stream)

            proc: subprocess.Popen[str] | None = None
            pgid: int | None = None
            # Everything from process creation on is guarded: an interrupt landing between the
            # launch and the output loop would otherwise leave the build running unattended.
            try:
                with deferred_stop_signals():
                    proc = subprocess.Popen(
                        command,
                        cwd=REPO_ROOT,
                        env=env,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1,
                        start_new_session=os.name != "nt",
                    )
                    pgid = child_group_id(proc)
                assert proc.stdout is not None

                output_queue: queue.Queue[str | None] = queue.Queue()
                reader = threading.Thread(target=stream_output, args=(proc.stdout, output_queue), daemon=True)
                reader.start()

                last_output = "(no output yet)"
                output_index = 0
                next_heartbeat = time.monotonic() + heartbeat if heartbeat > 0 else float("inf")
                stream_done = False

                while not stream_done:
                    try:
                        item = output_queue.get(timeout=0.25)
                    except queue.Empty:
                        item = ""

                    if item is None:
                        stream_done = True
                    elif item:
                        human_stream.write(item)
                        human_stream.flush()
                        log_file.write(item)
                        log_file.flush()
                        stripped = item.strip()
                        if stripped:
                            output_index += 1
                            last_output = stripped
                            progress.emit("command_output", output_index=output_index, line=stripped)

                    now = time.monotonic()
                    if now >= next_heartbeat and proc.poll() is None:
                        elapsed = format_duration(now - started)
                        progress.emit("command_heartbeat", elapsed=elapsed, last_output=last_output)
                        write_status(
                            log_file,
                            f"[agent-build] {label} still running after {elapsed}; last output: {last_output}",
                            stream=human_stream,
                        )
                        next_heartbeat = now + heartbeat

                exit_code = proc.wait()
            except BaseException:
                if proc is None:
                    raise
                # The child owns the build tree, so it has to be gone before the run is declared
                # over; otherwise it keeps writing artifacts the terminal verdict has described.
                # Signals are held throughout, so an impatient second one cannot cut the shutdown
                # short and produce exactly the verdict-over-a-live-build this prevents.
                with deferred_stop_signals(deliver=False):
                    disposition, child_status = terminate_child_group(proc, pgid)
                    RESULT_CONTEXT.child_disposition = disposition
                    RESULT_CONTEXT.child_exit_code = child_status
                    progress.emit(
                        "command_end",
                        duration_ms=int((time.monotonic() - started) * 1000),
                        exit_code=child_status,
                        status="interrupted",
                        child_disposition=disposition,
                    )
                    write_status(
                        log_file,
                        f"[agent-build] {label} interrupted; child process group {disposition}"
                        + ("" if child_status is None else f" with status {child_status}"),
                        stream=human_stream,
                    )
                    if disposition == "escaped":
                        write_status(
                            log_file,
                            f"[agent-build] warning: the {label} child survived SIGKILL and may still be "
                            "writing build artifacts",
                            stream=human_stream,
                        )
                raise
            RESULT_CONTEXT.child_disposition = "exited"
            RESULT_CONTEXT.child_exit_code = exit_code
            reader.join(timeout=1)
            duration = time.monotonic() - started
            elapsed = format_duration(duration)
            progress.emit(
                "command_end",
                duration_ms=int(duration * 1000),
                exit_code=exit_code,
                status="success" if exit_code == 0 else "failed",
            )
            write_status(
                log_file,
                f"[agent-build] {label} exited with {exit_code} after {elapsed}",
                stream=human_stream,
            )
            return exit_code


class ResultContext:
    """The values `emit_result` needs, kept current so an interrupt can still report a verdict."""

    def __init__(self) -> None:
        self.step = "startup"
        self.exit_code = INTERRUPTED_EXIT_CODE
        self.binary_path = Path()
        self.invocation_id = ""
        self.log_path = DEFAULT_LOG
        self.human_stream: TextIO = sys.stdout
        self.progress_path: Path | None = None
        self.progress_stdout_jsonl = False
        self.binaries_before: dict[str, dict[str, object]] = {}
        self.started = time.monotonic()
        self.child_disposition = "none"
        self.child_exit_code: int | None = None


RESULT_CONTEXT = ResultContext()


def emit_result(status: str, *, step: str, exit_code: int, context: ResultContext) -> int:
    """Write the terminal verdict — the `RESULT:` line and the `run_end` record — exactly once.

    A process exit code is discarded by a pipe, a background launch, or any trailing command in the
    same shell invocation, so the verdict is also written as the last line of the build log and as
    the last record of the progress stream, where a caller can always recover it.
    """
    assert status in RESULT_STATUSES, f"unknown result status {status!r}"
    assert step in RESULT_STEPS, f"unknown result step {step!r}"
    assert context.child_disposition in CHILD_DISPOSITIONS, f"unknown child disposition {context.child_disposition!r}"
    binary_path = context.binary_path
    line = (
        f"{RESULT_PREFIX} {status} step={step} exit_code={exit_code} binary={binary_path} "
        f"binary_present={'yes' if binary_path.is_file() else 'no'} child={context.child_disposition} "
        f"invocation={context.invocation_id} log={context.log_path}"
    )
    context.human_stream.write(f"{line}\n")
    context.human_stream.flush()
    try:
        context.log_path.parent.mkdir(parents=True, exist_ok=True)
        with context.log_path.open("a", encoding="utf-8") as log_file:
            log_file.write(f"{line}\n")
    except OSError as exc:
        print(f"[agent-build] warning: could not append the result line to {context.log_path}: {exc}", file=sys.stderr)
    binary_after = binary_identity(binary_path)
    binary_before = context.binaries_before.get(str(binary_path))
    try:
        append_progress_record(
            context.progress_path,
            "run_end",
            stdout_jsonl=context.progress_stdout_jsonl,
            invocation_id=context.invocation_id,
            status=status,
            step=step,
            exit_code=exit_code,
            duration_ms=int((time.monotonic() - context.started) * 1000),
            binary_path=str(binary_path),
            binary_after=binary_after,
            binary_before=binary_before,
            binary_changed=binary_after != binary_before,
            child_disposition=context.child_disposition,
            child_exit_code=context.child_exit_code,
        )
    except Exception as exc:
        print(f"[agent-build] warning: could not emit the run_end record: {exc}", file=sys.stderr)
    return exit_code


def announce_invocation(
    invocation_id: str,
    *,
    progress_path: Path | None,
    log_path: Path,
    human_stream: TextIO,
) -> None:
    """Announce this run's identity so a caller can wait for its records specifically.

    Without an announced id a caller reading the progress stream from outside cannot tell this
    invocation's records from a previous one's.
    """
    line = (
        f"[agent-build] invocation: {invocation_id} "
        f"progress: {progress_path if progress_path is not None else 'none'} log: {log_path}"
    )
    human_stream.write(f"{line}\n")
    human_stream.flush()
    try:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("a", encoding="utf-8") as log_file:
            log_file.write(f"{line}\n")
    except OSError:
        pass


def scons_prefix() -> list[str] | None:
    if importlib.util.find_spec("SCons") is not None:
        return [sys.executable, "-m", "SCons"]
    scons = shutil.which("scons")
    if scons:
        return [scons]
    return None


def host_scons_platform(sys_platform: str | None = None) -> str:
    host = sys_platform if sys_platform is not None else sys.platform
    if host == "darwin":
        return "macos"
    if host.startswith("linux"):
        return "linuxbsd"
    raise RuntimeError(
        f"Unsupported host platform {host!r}; pass --platform with one of: {', '.join(SUPPORTED_SCONS_PLATFORMS)}."
    )


def normalize_arch(machine: str | None = None) -> str:
    arch = (machine if machine is not None else platform_module.machine()).lower()
    if arch in ("amd64", "x64"):
        return "x86_64"
    if arch == "aarch64":
        return "arm64"
    return arch


def arch_from_args(args: argparse.Namespace) -> str:
    for raw_arg in reversed(args.scons_arg):
        key, separator, value = raw_arg.partition("=")
        if key == "arch" and separator and value:
            return str(value)
    return normalize_arch()


def binary_suffix(args: argparse.Namespace, scons_platform: str) -> str:
    """The binary suffix SCons will produce, mirroring the suffix assembled in SConstruct.

    Raw SCons arguments can rename the produced binary, and the wrapper has to look for the file
    the build actually writes rather than the default name.
    """
    suffix = f".{scons_platform}.{args.target}"
    if _is_scons_true(effective_companion_setting(args, "dev_build")):
        suffix += ".dev"
    if _raw_scons_setting(args, "precision") == "double":
        suffix += ".double"
    suffix += f".{arch_from_args(args)}"
    if _scons_boolean_setting(args, "threads") is False:
        suffix += ".nothreads"
    extra_suffix = _raw_scons_setting(args, "extra_suffix")
    if extra_suffix:
        suffix += f".{extra_suffix}"
    return suffix


def resolve_linked_binary(target: BuildTarget) -> Path | None:
    """The binary this build produced, or None when the build left none.

    Platform configuration appends suffixes the wrapper cannot reconstruct from its own arguments
    (sanitizers, fuzzer instrumentation, alternate toolchains), so an absent expected path falls back
    to the sole editor binary in the same directory rather than declaring a successful build broken.
    """
    if target.binary_path.exists():
        return target.binary_path
    try:
        candidates = sorted(
            path
            for path in target.binary_path.parent.glob(f"foundry.{target.scons_platform}.{target.scons_target}*")
            if path.is_file()
        )
    except OSError:
        return None
    return candidates[0] if len(candidates) == 1 else None


def resolve_build_target(args: argparse.Namespace) -> BuildTarget:
    scons_platform = host_scons_platform() if args.platform == "auto" else args.platform
    binary_path = REPO_ROOT / "bin" / f"foundry{binary_suffix(args, scons_platform)}"
    default_display = ":1" if scons_platform == "linuxbsd" else None
    return BuildTarget(
        scons_platform=scons_platform,
        binary_path=binary_path,
        default_display=default_display,
        scons_target=args.target,
    )


def target_companion_settings(args: argparse.Namespace) -> dict[str, str]:
    """The companion settings implied by the selected target, before any `--scons-arg` override."""
    settings = dict(TARGET_COMPANION_SETTINGS[args.target])
    if args.dev_build:
        settings["dev_mode"] = "no"
    return settings


def effective_companion_setting(args: argparse.Namespace, name: str) -> str:
    """The value SCons will end up seeing for a companion setting.

    `--scons-arg` is placed after the wrapper's own settings on the command line, so an explicit
    override wins; the wrapper has to predict the same value to find the binary the build writes.
    """
    override = _raw_scons_setting(args, name)
    return override if override is not None else target_companion_settings(args)[name]


def wrapper_scons_settings(args: argparse.Namespace) -> list[str]:
    """The target and its companion settings, in command order."""
    companions = target_companion_settings(args)
    return [f"target={args.target}", *(f"{name}={companions[name]}" for name in COMPANION_SETTING_ORDER)]


def _raw_scons_setting(args: argparse.Namespace, name: str) -> str | None:
    value = None
    for raw_arg in args.scons_arg:
        key, separator, candidate = raw_arg.lstrip("-").partition("=")
        if separator and key.strip().replace("-", "_").lower() == name:
            value = candidate
    return value


def _is_scons_true(value: str) -> bool:
    return value.strip().lower() not in SCONS_FALSE_VALUES


def _scons_boolean_setting(args: argparse.Namespace, name: str) -> bool | None:
    value = _raw_scons_setting(args, name)
    if value is None:
        return None
    return _is_scons_true(value)


def _resolve_build_input(value: str, repo_root: Path) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else repo_root / path


def _repository_files(repo_root: Path) -> list[Path]:
    if not repo_root.is_dir():
        return []

    def _git_files(*arguments: str) -> list[Path] | None:
        try:
            completed = subprocess.run(
                ["git", "ls-files", "-z", *arguments],
                cwd=repo_root,
                check=False,
                capture_output=True,
                timeout=TELEMETRY_TIMEOUT_SECONDS,
            )
        except (OSError, subprocess.TimeoutExpired):
            return None
        if completed.returncode != 0:
            return None
        return [Path(os.fsdecode(item)) for item in completed.stdout.split(b"\0") if item]

    def _is_excluded(path: Path) -> bool:
        return any(part in BUILD_DESCRIPTION_EXCLUDED_DIRECTORIES for part in path.parts[:-1])

    tracked_files = _git_files("--cached")
    untracked_files = _git_files("--others", "--exclude-standard")
    if tracked_files is not None and untracked_files is not None:
        inventory = {path for path in tracked_files if not _is_excluded(path)}
        inventory.update(
            path
            for path in untracked_files
            if not _is_excluded(path)
            and (path.name in ("SConstruct", "SCsub") or path.suffix.lower() in BUILD_GRAPH_UNTRACKED_SUFFIXES)
        )
        return [repo_root / path for path in sorted(inventory)]

    repository_files: list[Path] = []
    for root, directory_names, file_names in os.walk(repo_root):
        directory_names[:] = sorted(
            name for name in directory_names if name not in BUILD_DESCRIPTION_EXCLUDED_DIRECTORIES
        )
        root_path = Path(root)
        for file_name in sorted(file_names):
            if ".gen." in file_name or file_name.startswith(".scons") or file_name.endswith(".uid"):
                continue
            repository_files.append(root_path / file_name)
    return repository_files


def _selected_build_description_inputs(args: argparse.Namespace, repo_root: Path) -> list[tuple[str, Path]]:
    selected = [("custom", repo_root / "custom.py")]
    profile = _raw_scons_setting(args, "profile")
    if profile:
        profile_path = _resolve_build_input(profile, repo_root)
        selected.extend(("profile", candidate) for candidate in (profile_path, Path(f"{profile_path}.py")))
    build_profile = _raw_scons_setting(args, "build_profile")
    if build_profile:
        selected.append(("build_profile", _resolve_build_input(build_profile, repo_root)))
    return selected


def _hash_build_description(hasher, identity: str, path: Path) -> None:
    hasher.update(identity.encode("utf-8", errors="surrogateescape"))
    hasher.update(b"\0")
    try:
        with path.open("rb") as source:
            hasher.update(b"file\0")
            while chunk := source.read(1024 * 1024):
                hasher.update(chunk)
    except FileNotFoundError:
        hasher.update(b"missing\0")
    except OSError as exc:
        hasher.update(f"unreadable:{exc.errno}\0".encode("ascii"))


def build_description_fingerprint(args: argparse.Namespace, repo_root: Path = REPO_ROOT) -> str:
    hasher = hashlib.sha256()
    if not repo_root.is_dir():
        hasher.update(b"missing-repository-root\0")
    for path in _repository_files(repo_root):
        relative_path = path.relative_to(repo_root).as_posix()
        hasher.update(f"repository-file:{relative_path}".encode("utf-8", errors="surrogateescape"))
        hasher.update(b"\0")
        hasher.update(b"present\0" if os.path.lexists(path) else b"missing\0")
        if path.name in ("SConstruct", "SCsub") or path.suffix == ".py":
            _hash_build_description(hasher, f"repository:{relative_path}", path)
    for source, path in _selected_build_description_inputs(args, repo_root):
        _hash_build_description(hasher, f"selected:{source}:{path.resolve(strict=False)}", path)
    return hasher.hexdigest()


def build_configuration_payload(
    args: argparse.Namespace,
    target: BuildTarget,
    *,
    repo_root: Path = REPO_ROOT,
) -> dict[str, object]:
    return {
        "platform": target.scons_platform,
        "arch": arch_from_args(args),
        "target": args.target,
        "settings": wrapper_scons_settings(args),
        "module_text_server_fb_enabled": True,
        "scons_arg": list(args.scons_arg),
        "build_description_fingerprint": build_description_fingerprint(args, repo_root),
    }


def resolve_ninja_state(
    args: argparse.Namespace,
    target: BuildTarget,
    *,
    repo_root: Path = REPO_ROOT,
) -> NinjaState:
    payload = build_configuration_payload(args, target, repo_root=repo_root)
    serialized = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    key = hashlib.sha256(serialized.encode("utf-8")).hexdigest()[:16]
    directory = repo_root / ".ninja" / "agent-build" / key
    return NinjaState(directory=directory, file=directory / "build.ninja")


def ninja_generation_command(args: argparse.Namespace, target: BuildTarget, state: NinjaState) -> list[str]:
    prefix = scons_prefix()
    if prefix is None:
        raise RuntimeError("SCons is not available")
    return prefix + [
        f"platform={target.scons_platform}",
        *wrapper_scons_settings(args),
        "module_text_server_fb_enabled=yes",
        "cache_path=",
        "c_compiler_launcher=ccache",
        "cpp_compiler_launcher=ccache",
        "debug_paths_relative=yes",
        "ninja=yes",
        "ninja_auto_run=no",
        f"ninja_file={state.file}",
        f"ninja_dir={state.directory}",
        *args.scons_arg,
    ]


def ninja_build_command(state: NinjaState, jobs: int) -> list[str]:
    ninja = shutil.which("ninja")
    if ninja is None:
        raise RuntimeError("Ninja is not available")
    ninja_file = os.path.relpath(state.file, REPO_ROOT)
    return [ninja, "-f", ninja_file, f"-j{jobs}"]


def build_command(args: argparse.Namespace, target: BuildTarget | None = None) -> list[str]:
    if target is None:
        target = resolve_build_target(args)
    compiler_cache = resolve_compiler_cache(args)
    cache_args = [f"cache_path={DEFAULT_CACHE_PATH}"]
    if compiler_cache == "ccache":
        cache_args = [
            "cache_path=",
            "c_compiler_launcher=ccache",
            "cpp_compiler_launcher=ccache",
            "debug_paths_relative=yes",
        ]
    prefix = scons_prefix()
    if prefix is None:
        raise RuntimeError("SCons is not available")
    command = prefix + [
        f"platform={target.scons_platform}",
        *wrapper_scons_settings(args),
        "module_text_server_fb_enabled=yes",
    ]
    if compiler_cache == "none":
        command.extend(cache_args)
    command.append(f"-j{args.jobs}")
    command.extend(args.scons_arg)
    if compiler_cache == "ccache":
        command.extend(cache_args)
    return command


def test_command(args: argparse.Namespace, target: BuildTarget | None = None) -> list[str]:
    if target is None:
        target = resolve_build_target(args)
    command = [str(target.binary_path), "--headless", "test", "run"]
    for case_filter in args.test_case or []:
        command.extend(["--case", case_filter])
    for suite_filter in args.test_suite or []:
        command.extend(["--suite", suite_filter])
    command.append("--force-colors")
    return command


def build_environment(
    args: argparse.Namespace,
    compiler_cache: str,
    *,
    repo_root: Path = REPO_ROOT,
    ccache_stats_log: Path | None = None,
) -> dict[str, str]:
    env = os.environ.copy()
    env.pop("CCACHE", None)
    if compiler_cache == "ccache":
        for variable in list(env):
            if variable.startswith("CCACHE_"):
                env.pop(variable)
        for variable in ("SCONS_CACHE", "SCONS_CACHE_LIMIT"):
            env.pop(variable, None)
        ccache_dir = args.ccache_dir.expanduser()
        if not ccache_dir.is_absolute():
            ccache_dir = ccache_dir.resolve()
        env["CCACHE_DIR"] = str(ccache_dir)
        env["CCACHE_BASEDIR"] = str(repo_root.resolve())
        env["CCACHE_NAMESPACE"] = "foundry"
        # Ignore user and cache-local configuration; this environment is the complete cache policy.
        env["CCACHE_CONFIGPATH"] = os.devnull
        env["CCACHE_COMPILERCHECK"] = "content"
        # ccache booleans are true whenever present, so CCACHE_NO* is required to force false.
        _set_ccache_boolean(env, "HASHDIR", enabled=True)
        _set_ccache_boolean(env, "HARDLINK", enabled=False)
        _set_ccache_boolean(env, "FILECLONE", enabled=args.ccache_file_clone)
        _set_ccache_boolean(env, "COMPRESS", enabled=not args.ccache_file_clone)
        _set_ccache_boolean(env, "DISABLE", enabled=False)
        _set_ccache_boolean(env, "RECACHE", enabled=False)
        _set_ccache_boolean(env, "READONLY", enabled=False)
        _set_ccache_boolean(env, "READONLY_DIRECT", enabled=False)
        if ccache_stats_log is not None:
            env["CCACHE_STATSLOG"] = str(ccache_stats_log)
    return env


def test_environment(args: argparse.Namespace, target: BuildTarget | None = None) -> dict[str, str]:
    if target is None:
        target = resolve_build_target(args)
    env = os.environ.copy()
    if args.display is not None:
        env["DISPLAY"] = args.display
    elif target.default_display is not None:
        env["DISPLAY"] = target.default_display
    env.setdefault("FOUNDRY_TEST_SCRATCH", str(DEFAULT_TEST_SCRATCH))
    return env


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build Foundry with stable logging and progress output for cloud agents."
    )
    parser.add_argument("--test", action="store_true", help="Run the Foundry test suite after a successful build.")
    parser.add_argument(
        "--case",
        dest="test_case",
        action="append",
        help=(
            "Run a focused doctest case filter after building. Repeatable: every occurrence "
            "is retained and forwarded, and the selected tests are the union of all supplied "
            "patterns (matches any). Implies --test."
        ),
    )
    parser.add_argument(
        "--suite",
        dest="test_suite",
        action="append",
        help=(
            "Run a focused doctest suite filter after building. Repeatable, and combines with "
            "--case: the selected tests are the union of every supplied case and suite pattern. "
            "Implies --test."
        ),
    )
    parser.add_argument(
        "--platform",
        choices=["auto", *SUPPORTED_SCONS_PLATFORMS],
        default="auto",
        help="SCons platform to build. Default: auto-detect from the host OS.",
    )
    parser.add_argument(
        "--target",
        choices=SUPPORTED_SCONS_TARGETS,
        default="editor",
        help=(
            "SCons target to build, together with the companion settings it implies: editor is "
            "dev_mode=yes dev_build=yes tests=yes, and both templates are dev_mode=no dev_build=no "
            "tests=no. Default: editor."
        ),
    )
    parser.add_argument(
        "--dev-build",
        action="store_true",
        help="Use faster dev_build=yes instead of the default CI-style dev_mode=yes build.",
    )
    parser.add_argument(
        "--dev-mode",
        action="store_true",
        help="Compatibility no-op; dev_mode=yes is now the default.",
    )
    parser.add_argument(
        "--backend",
        choices=SUPPORTED_BUILD_BACKENDS,
        default="scons",
        help="Build execution backend. Default: scons.",
    )
    parser.add_argument(
        "--compiler-cache",
        choices=SUPPORTED_COMPILER_CACHES,
        default="auto",
        help="Compiler cache. auto selects ccache for Ninja and none for native SCons.",
    )
    parser.add_argument(
        "--ccache-dir",
        type=Path,
        default=DEFAULT_CCACHE_PATH,
        help=f"Shared Foundry ccache directory. Default: {DEFAULT_CCACHE_PATH}.",
    )
    parser.add_argument(
        "--ccache-file-clone",
        action="store_true",
        help="Use ccache file cloning when supported; disables compressed cache storage.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=None,
        help=(
            "Parallel build jobs. Default: the effective cgroup CPU quota when one applies, "
            f"otherwise the host CPU count. Set {JOBS_ENVIRONMENT_VARIABLE} to override the default."
        ),
    )
    parser.add_argument(
        "--log",
        type=Path,
        default=DEFAULT_LOG,
        help=f"Build log path. Default for this worktree: {DEFAULT_LOG}.",
    )
    parser.add_argument("--append-log", action="store_true", help="Append to the log instead of replacing it.")
    parser.add_argument(
        "--invocation-id",
        default=None,
        help=(
            "Identity to stamp on every progress record of this run. Supply one to wait for this "
            "specific invocation's run_end record. Default: a freshly generated UUID."
        ),
    )
    parser.add_argument(
        "--progress-file",
        type=Path,
        default=DEFAULT_PROGRESS_LOG,
        help=f"Write command progress events as JSONL. Default for this worktree: {DEFAULT_PROGRESS_LOG}.",
    )
    parser.add_argument(
        "--no-progress-file",
        action="store_true",
        help="Disable the default JSONL progress file.",
    )
    parser.add_argument(
        "--append-progress",
        action="store_true",
        help="Append to the progress file instead of replacing it.",
    )
    parser.add_argument(
        "--progress-format",
        choices=["text", "jsonl"],
        default="text",
        help="Use jsonl to emit machine-readable progress events on stdout. Default: text.",
    )
    parser.add_argument(
        "--heartbeat",
        type=float,
        default=60.0,
        help="Seconds between progress heartbeats when the command is still running. Default: 60.",
    )
    parser.add_argument(
        "--display",
        help="DISPLAY value for post-build test runs. Default: :1 on Linux, unset on macOS.",
    )
    parser.add_argument(
        "--scons-arg",
        action="append",
        default=[],
        help="Append one extra raw argument to the SCons command. Repeat as needed.",
    )
    args = parser.parse_args(argv)
    if args.dev_mode and args.dev_build:
        parser.error("--dev-mode and --dev-build cannot be combined")
    if _raw_scons_setting(args, "target") is not None:
        parser.error(
            "--scons-arg target=... is not accepted; select the target with "
            f"--target {{{','.join(SUPPORTED_SCONS_TARGETS)}}}"
        )
    if args.dev_build and args.target != "editor":
        parser.error(f"--dev-build applies to --target editor, not --target {args.target}")
    if args.test_case or args.test_suite:
        args.test = True
    if args.test and not _is_scons_true(effective_companion_setting(args, "tests")):
        parser.error(
            f"--target {args.target} builds tests=no, so there is no test runner to run; "
            "use --target editor or add --scons-arg tests=yes"
        )
    try:
        job_selection = resolve_job_selection(args.jobs, os.environ, os.cpu_count())
    except ValueError as exc:
        parser.error(str(exc))
    args.jobs = job_selection.jobs
    args.jobs_source = job_selection.source
    if args.heartbeat < 0:
        parser.error("--heartbeat must be non-negative")
    if args.backend == "ninja":
        if args.compiler_cache == "none":
            parser.error(
                "--backend ninja requires ccache; remove --compiler-cache none or use "
                "--backend scons --compiler-cache none"
            )
        for raw_arg in args.scons_arg:
            key = raw_arg.lstrip("-").partition("=")[0].strip().replace("-", "_").lower()
            if key in NINJA_OWNED_SCONS_KEYS:
                parser.error(f"--backend ninja owns the SCons setting {key!r}; remove that --scons-arg")
    return args


def progress_path_from_args(args: argparse.Namespace) -> Path | None:
    if args.no_progress_file:
        return None
    progress_file: Path | None = args.progress_file
    return progress_file


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    progress_path = progress_path_from_args(args)
    progress_stdout_jsonl = args.progress_format == "jsonl"
    human_stream = sys.stderr if progress_stdout_jsonl else sys.stdout
    invocation_id = args.invocation_id or new_invocation_id()

    RESULT_CONTEXT.step = "startup"
    RESULT_CONTEXT.exit_code = INTERRUPTED_EXIT_CODE
    RESULT_CONTEXT.binary_path = Path()
    RESULT_CONTEXT.invocation_id = invocation_id
    RESULT_CONTEXT.log_path = args.log
    RESULT_CONTEXT.human_stream = human_stream
    RESULT_CONTEXT.progress_path = progress_path
    RESULT_CONTEXT.progress_stdout_jsonl = progress_stdout_jsonl
    RESULT_CONTEXT.binaries_before = {}
    RESULT_CONTEXT.started = time.monotonic()
    RESULT_CONTEXT.child_disposition = "none"
    RESULT_CONTEXT.child_exit_code = None

    def fail_at_startup(exit_code: int, error: str) -> int:
        print(f"[agent-build] {error}", file=sys.stderr)
        return emit_result("tooling-missing", step="startup", exit_code=exit_code, context=RESULT_CONTEXT)

    resolved_target: BuildTarget | None
    try:
        resolved_target = resolve_build_target(args)
    except RuntimeError as exc:
        resolved_target = None
        target_error: str | None = str(exc)
    else:
        target_error = None
        RESULT_CONTEXT.binary_path = resolved_target.binary_path
        RESULT_CONTEXT.binaries_before = built_binary_identities(resolved_target)

    git_commit, git_commit_error = read_git_commit()

    # The progress stream is truncated and identified before any tooling check, so a caller waiting
    # on this invocation's records can never match a previous invocation's, not even in the window
    # before the first build command starts and not on the paths that abort during startup.
    try:
        append_progress_record(
            progress_path,
            "run_start",
            mode="a" if args.append_progress else "w",
            stdout_jsonl=progress_stdout_jsonl,
            invocation_id=invocation_id,
            argv=list(argv),
            pid=os.getpid(),
            worktree=str(REPO_ROOT),
            backend=args.backend,
            compiler_cache=resolve_compiler_cache(args),
            platform=resolved_target.scons_platform if resolved_target is not None else args.platform,
            arch=arch_from_args(args),
            target=args.target,
            jobs=args.jobs,
            jobs_source=args.jobs_source,
            git_commit=git_commit,
            log_path=str(args.log),
            binary_path=str(resolved_target.binary_path) if resolved_target is not None else None,
            binary_before=(
                RESULT_CONTEXT.binaries_before.get(str(resolved_target.binary_path))
                if resolved_target is not None
                else None
            ),
        )
    except Exception as exc:
        print(f"[agent-build] warning: could not emit the run_start record: {exc}", file=sys.stderr)
    announce_invocation(invocation_id, progress_path=progress_path, log_path=args.log, human_stream=human_stream)

    if resolved_target is None:
        assert target_error is not None
        return fail_at_startup(TOOLING_MISSING_EXIT_CODE, target_error)
    target = resolved_target

    if scons_prefix() is None:
        return fail_at_startup(
            TOOLING_MISSING_EXIT_CODE,
            "SCons is not available. Install SCons for python3 or make the `scons` executable available in PATH.",
        )

    if args.backend == "ninja" and shutil.which("ninja") is None:
        return fail_at_startup(
            TOOLING_MISSING_EXIT_CODE,
            "Ninja is required for --backend ninja but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.",
        )

    compiler_cache = resolve_compiler_cache(args)
    if compiler_cache == "ccache" and shutil.which("ccache") is None:
        return fail_at_startup(
            TOOLING_MISSING_EXIT_CODE,
            "ccache is required for this build mode but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.",
        )

    print(f"[agent-build] jobs: {args.jobs} (source: {args.jobs_source})", file=human_stream)

    stats_log_path = ccache_stats_log_path(invocation_id) if compiler_cache == "ccache" else None
    build_env = build_environment(args, compiler_cache, ccache_stats_log=stats_log_path)
    cache_before = read_ccache_stats_best_effort(build_env) if compiler_cache == "ccache" else {}
    generation_command_args: list[str] | None = None
    if args.backend == "ninja":
        ninja_state = resolve_ninja_state(args, target)
        build_command_args = ninja_build_command(ninja_state, args.jobs)
        if not ninja_state.file.exists():
            generation_command_args = ninja_generation_command(args, target, ninja_state)
    else:
        ninja_state = None
        build_command_args = build_command(args, target)
    if compiler_cache == "ccache":
        cache_dir: str | None = build_env.get("CCACHE_DIR")
        cache_policy = "wrapper-managed-ccache"
        cache_source = "--ccache-dir"
        cache_stats_source = "per-invocation-stats-log"
    else:
        cache_dir = next(
            (
                argument.partition("=")[2]
                for argument in reversed(build_command_args)
                if argument.startswith("cache_path=")
            ),
            None,
        )
        cache_policy = "native-scons-cache"
        cache_source = "effective-build-command"
        cache_stats_source = "disabled"

    build_started = time.monotonic()
    build_exit: int | None = None
    build_status = "error"
    build_error: str | None = None
    generation_exit: int | None = None
    if generation_command_args is not None:
        generation_status = "pending"
    elif args.backend == "ninja":
        generation_status = "skipped-existing"
    else:
        generation_status = "not-applicable"
    generation_error: str | None = None
    active_step = "build"
    try:
        append_log = args.append_log
        # `run_start` already opened the progress file with the caller's truncation choice, so every
        # later record appends to it.
        append_progress = True
        if generation_command_args is not None:
            assert ninja_state is not None
            active_step = "generate"
            RESULT_CONTEXT.step = active_step
            ninja_state.directory.mkdir(parents=True, exist_ok=True)
            generation_exit = run_logged_command(
                generation_command_args,
                label="generate",
                invocation_id=invocation_id,
                log_path=args.log,
                heartbeat=args.heartbeat,
                append_log=append_log,
                progress_path=progress_path,
                append_progress=append_progress,
                progress_stdout_jsonl=progress_stdout_jsonl,
                env=build_env,
            )
            generation_status = "success" if generation_exit == 0 else "failed"
            if generation_exit != 0:
                build_exit = generation_exit
                build_status = "failed"
            else:
                append_log = True

        if build_exit is None:
            active_step = "build"
            RESULT_CONTEXT.step = active_step
            build_exit = run_logged_command(
                build_command_args,
                label="build",
                invocation_id=invocation_id,
                log_path=args.log,
                heartbeat=args.heartbeat,
                append_log=append_log,
                progress_path=progress_path,
                append_progress=append_progress,
                progress_stdout_jsonl=progress_stdout_jsonl,
                env=build_env,
            )
            build_status = "success" if build_exit == 0 else "failed"

    except OSError as exc:
        build_exit = 127
        build_error = f"{type(exc).__name__}: {exc}"
        if active_step == "generate":
            generation_status = "error"
            generation_error = build_error
        print(f"[agent-build] failed to start build command: {exc}", file=sys.stderr)
    except BaseException as exc:
        build_status = "interrupted" if isinstance(exc, (KeyboardInterrupt, SystemExit)) else "error"
        build_error = f"{type(exc).__name__}: {exc}"
        if active_step == "generate":
            generation_status = build_status
            generation_error = build_error
        raise
    finally:
        build_duration = time.monotonic() - build_started
        cache_after = read_ccache_stats_best_effort(build_env) if compiler_cache == "ccache" else {}
        cache_delta = stats_delta(cache_before, cache_after)
        cache_stats_errors = {
            stage: str(stats["error"])
            for stage, stats in (("before", cache_before), ("after", cache_after))
            if "error" in stats
        }
        cache_stats_status = "error" if cache_stats_errors else ("ok" if compiler_cache == "ccache" else "disabled")
        summary_fields: dict[str, Any] = {
            "phase": "build",
            "invocation_id": invocation_id,
            "backend": args.backend,
            "compiler_cache": compiler_cache,
            "worktree": str(REPO_ROOT),
            "git_commit": git_commit,
            "git_commit_error": git_commit_error,
            "platform": target.scons_platform,
            "arch": arch_from_args(args),
            "target": target.scons_target,
            "jobs": args.jobs,
            "jobs_source": args.jobs_source,
            "build_command": build_command_args,
            "generation_command": generation_command_args,
            "generation_status": generation_status,
            "generation_error": generation_error,
            "generation_exit_code": generation_exit,
            "duration_ms": int(build_duration * 1000),
            "status": build_status,
            "error": build_error,
            "exit_code": build_exit,
            "cache_dir": cache_dir,
            "cache_policy": cache_policy,
            "cache_source": cache_source,
            "cache_stats_source": cache_stats_source,
            "ccache_stats_log": str(stats_log_path) if stats_log_path is not None else None,
            "cache_stats_status": cache_stats_status,
            "cache_stats_error": cache_stats_errors or None,
            "cache_stats_before": cache_before,
            "cache_stats_after": cache_after,
            "cache_delta": cache_delta,
            "log_path": str(args.log),
            "binary_path": str(target.binary_path),
            "binary_after": binary_identity(target.binary_path),
        }
        try:
            append_progress_record(
                progress_path,
                "build_summary",
                stdout_jsonl=progress_stdout_jsonl,
                **summary_fields,
            )
        except Exception as exc:
            print(f"[agent-build] warning: could not emit build summary: {exc}", file=sys.stderr)
        print(
            f"[agent-build] summary: backend={args.backend} cache={compiler_cache} "
            f"jobs={args.jobs}({args.jobs_source}) duration={format_duration(build_duration)} log={args.log}",
            file=human_stream,
        )
        if stats_log_path is not None:
            try:
                stats_log_path.unlink(missing_ok=True)
            except OSError as exc:
                print(
                    f"[agent-build] warning: could not remove ccache stats log {stats_log_path}: {exc}", file=sys.stderr
                )

    assert build_exit is not None

    def report(status: str, step: str, exit_code: int) -> int:
        RESULT_CONTEXT.binary_path = target.binary_path
        return emit_result(status, step=step, exit_code=exit_code, context=RESULT_CONTEXT)

    if build_exit != 0:
        if generation_status in ("failed", "error"):
            return report("generation-failure", "generate", build_exit)
        return report("build-failure", "build", build_exit)

    linked_binary = resolve_linked_binary(target)
    if linked_binary is None:
        print(f"[agent-build] expected binary is missing after build: {target.binary_path}", file=sys.stderr)
        return report("binary-missing", "build", MISSING_BINARY_EXIT_CODE)
    target = target._replace(binary_path=linked_binary)
    RESULT_CONTEXT.binary_path = linked_binary

    if not args.test:
        print(f"[agent-build] built binary: {linked_binary}", file=human_stream)
        return report("success", "build", 0)

    RESULT_CONTEXT.step = "test"
    try:
        test_exit = run_logged_command(
            test_command(args, target),
            label="test",
            invocation_id=invocation_id,
            log_path=args.log,
            heartbeat=args.heartbeat,
            append_log=True,
            progress_path=progress_path,
            append_progress=True,
            progress_stdout_jsonl=progress_stdout_jsonl,
            env=test_environment(args, target),
        )
    except OSError as exc:
        print(f"[agent-build] failed to start the test command: {exc}", file=sys.stderr)
        return report("test-failure", "test", TOOLING_MISSING_EXIT_CODE)
    return report("test-failure" if test_exit else "success", "test", test_exit)


def run(argv: list[str]) -> int:
    """Run the wrapper, turning an interrupt into a defined verdict instead of a traceback."""
    previous_handlers = install_stop_signal_handlers()
    try:
        return main(argv)
    except KeyboardInterrupt:
        return emit_result(
            "interrupted",
            step=RESULT_CONTEXT.step,
            exit_code=INTERRUPTED_EXIT_CODE,
            context=RESULT_CONTEXT,
        )
    finally:
        for number, handler in previous_handlers.items():
            try:
                signal.signal(number, handler)
            except (OSError, ValueError):
                pass


if __name__ == "__main__":
    raise SystemExit(run(sys.argv[1:]))
