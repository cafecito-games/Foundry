#!/usr/bin/env python3
"""Agent-friendly Foundry build wrapper for local and cloud environments."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import platform as platform_module
import queue
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple, TextIO, cast

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
TELEMETRY_TIMEOUT_SECONDS = 5.0
BUILD_DESCRIPTION_EXCLUDED_DIRECTORIES = frozenset(
    {".foundry", ".git", ".ninja", ".test_scratch", ".worktrees", "__pycache__", "bin", "build", "out"}
)


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
    stdout_jsonl: bool = False,
    **fields: object,
) -> None:
    if path is None and not stdout_jsonl:
        return
    payload = {"version": 1, "event": event, "timestamp": timestamp_utc(), **fields}
    line = json.dumps(payload, sort_keys=True)
    if path is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8") as progress_file:
            progress_file.write(line + "\n")
    if stdout_jsonl:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()


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


def resolve_build_target(args: argparse.Namespace) -> BuildTarget:
    scons_platform = host_scons_platform() if args.platform == "auto" else args.platform
    arch = arch_from_args(args)
    binary_path = REPO_ROOT / "bin" / f"foundry.{scons_platform}.editor.dev.{arch}"
    default_display = ":1" if scons_platform == "linuxbsd" else None
    return BuildTarget(scons_platform=scons_platform, binary_path=binary_path, default_display=default_display)


def build_modes(args: argparse.Namespace) -> list[str]:
    return ["dev_build=yes"] if args.dev_build else ["dev_mode=yes", "dev_build=yes"]


def _raw_scons_setting(args: argparse.Namespace, name: str) -> str | None:
    value = None
    for raw_arg in args.scons_arg:
        key, separator, candidate = raw_arg.lstrip("-").partition("=")
        if separator and key.strip().replace("-", "_").lower() == name:
            value = candidate
    return value


def _resolve_build_input(value: str, repo_root: Path) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else repo_root / path


def _repository_build_descriptions(repo_root: Path) -> list[Path]:
    if not repo_root.is_dir():
        return []
    descriptions: list[Path] = []
    for root, directory_names, file_names in os.walk(repo_root):
        directory_names[:] = sorted(
            name for name in directory_names if name not in BUILD_DESCRIPTION_EXCLUDED_DIRECTORIES
        )
        root_path = Path(root)
        for file_name in sorted(file_names):
            if file_name == "SConstruct" or file_name == "SCsub" or file_name.endswith(".py"):
                descriptions.append(root_path / file_name)
    return descriptions


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
    for path in _repository_build_descriptions(repo_root):
        relative_path = path.relative_to(repo_root).as_posix()
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
        "target": "editor",
        "modes": build_modes(args),
        "tests": True,
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
        "target=editor",
        *build_modes(args),
        "tests=yes",
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
        "target=editor",
        *build_modes(args),
        "tests=yes",
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
    if args.test_case:
        command.extend(["--case", args.test_case])
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
    parser.add_argument("--case", dest="test_case", help="Run a focused doctest case after building. Implies --test.")
    parser.add_argument(
        "--platform",
        choices=["auto", *SUPPORTED_SCONS_PLATFORMS],
        default="auto",
        help="SCons platform to build. Default: auto-detect from the host OS.",
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
        default=os.cpu_count() or 1,
        help="Parallel SCons jobs. Default: CPU count.",
    )
    parser.add_argument(
        "--log",
        type=Path,
        default=DEFAULT_LOG,
        help=f"Build log path. Default for this worktree: {DEFAULT_LOG}.",
    )
    parser.add_argument("--append-log", action="store_true", help="Append to the log instead of replacing it.")
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
    if args.test_case:
        args.test = True
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
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
    try:
        target = resolve_build_target(args)
    except RuntimeError as exc:
        print(f"[agent-build] {exc}", file=sys.stderr)
        return 2

    if scons_prefix() is None:
        print(
            "[agent-build] SCons is not available. Install SCons for python3 or make the `scons` executable "
            "available in PATH.",
            file=sys.stderr,
        )
        return 127

    if args.backend == "ninja" and shutil.which("ninja") is None:
        print(
            "[agent-build] Ninja is required for --backend ninja but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.",
            file=sys.stderr,
        )
        return 127

    compiler_cache = resolve_compiler_cache(args)
    if compiler_cache == "ccache" and shutil.which("ccache") is None:
        print(
            "[agent-build] ccache is required for this build mode but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.",
            file=sys.stderr,
        )
        return 127

    progress_path = progress_path_from_args(args)
    progress_stdout_jsonl = args.progress_format == "jsonl"
    human_stream = sys.stderr if progress_stdout_jsonl else sys.stdout

    invocation_id = new_invocation_id()
    git_commit, git_commit_error = read_git_commit()
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
        append_progress = args.append_progress
        if generation_command_args is not None:
            assert ninja_state is not None
            active_step = "generate"
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
                append_progress = True

        if build_exit is None:
            active_step = "build"
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
        summary_fields: dict[str, object] = {
            "phase": "build",
            "invocation_id": invocation_id,
            "backend": args.backend,
            "compiler_cache": compiler_cache,
            "worktree": str(REPO_ROOT),
            "git_commit": git_commit,
            "git_commit_error": git_commit_error,
            "platform": target.scons_platform,
            "arch": arch_from_args(args),
            "jobs": args.jobs,
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
            f"duration={format_duration(build_duration)} log={args.log}",
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
    if build_exit != 0:
        return build_exit

    if not target.binary_path.exists():
        if args.test:
            print(f"[agent-build] expected binary is missing after build: {target.binary_path}", file=sys.stderr)
            return 127
        print(
            f"[agent-build] build command succeeded; expected binary is not present yet: {target.binary_path}",
            file=human_stream,
        )
        return 0

    if not args.test:
        print(f"[agent-build] built binary: {target.binary_path}", file=human_stream)
        return 0

    return run_logged_command(
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


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
