#!/usr/bin/env python3
"""Agent-friendly Foundry build wrapper for Linux cloud environments."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import queue
import shlex
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import TextIO

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG = Path("/tmp/foundry-build.log")
DEFAULT_PROGRESS_LOG = Path("/tmp/foundry-build-progress.jsonl")
DEFAULT_BINARY = REPO_ROOT / "bin" / "foundry.linuxbsd.editor.dev.x86_64"
DEFAULT_CACHE_PATH = Path.home() / ".scons_cache"


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


class ProgressReporter:
    def __init__(
        self,
        *,
        phase: str,
        progress_path: Path | None,
        append_progress: bool,
        stdout_jsonl: bool,
        started: float,
    ) -> None:
        self.phase = phase
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
            self.progress_file = self.progress_path.open(mode, encoding="utf-8")
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


def build_command(args: argparse.Namespace) -> list[str]:
    build_modes = ["dev_build=yes"] if args.dev_build else ["dev_mode=yes", "dev_build=yes"]
    prefix = scons_prefix()
    if prefix is None:
        raise RuntimeError("SCons is not available")
    command = prefix + [
        "platform=linuxbsd",
        "target=editor",
        *build_modes,
        "tests=yes",
        "module_text_server_fb_enabled=yes",
        f"cache_path={DEFAULT_CACHE_PATH}",
        f"-j{args.jobs}",
    ]
    command.extend(args.scons_arg)
    return command


def test_command(args: argparse.Namespace) -> list[str]:
    command = [str(DEFAULT_BINARY), "--headless", "test", "run"]
    if args.test_case:
        command.extend(["--case", args.test_case])
    command.append("--force-colors")
    return command


def test_environment(args: argparse.Namespace) -> dict[str, str]:
    env = os.environ.copy()
    if args.display:
        env["DISPLAY"] = args.display
    return env


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build Foundry with stable logging and progress output for cloud agents."
    )
    parser.add_argument("--test", action="store_true", help="Run the Foundry test suite after a successful build.")
    parser.add_argument("--case", dest="test_case", help="Run a focused doctest case after building. Implies --test.")
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
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="Parallel SCons jobs. Default: CPU count.",
    )
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG, help=f"Build log path. Default: {DEFAULT_LOG}.")
    parser.add_argument("--append-log", action="store_true", help="Append to the log instead of replacing it.")
    parser.add_argument(
        "--progress-file",
        type=Path,
        default=DEFAULT_PROGRESS_LOG,
        help=f"Write command progress events as JSONL. Default: {DEFAULT_PROGRESS_LOG}.",
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
        default=":1",
        help="DISPLAY value for post-build test runs. Default: :1.",
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
    return args


def progress_path_from_args(args: argparse.Namespace) -> Path | None:
    if args.no_progress_file:
        return None
    return args.progress_file


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if scons_prefix() is None:
        print(
            "[agent-build] SCons is not available. Install SCons for python3 or make the `scons` executable "
            "available in PATH.",
            file=sys.stderr,
        )
        return 127

    progress_path = progress_path_from_args(args)
    progress_stdout_jsonl = args.progress_format == "jsonl"
    human_stream = sys.stderr if progress_stdout_jsonl else sys.stdout

    build_exit = run_logged_command(
        build_command(args),
        label="build",
        log_path=args.log,
        heartbeat=args.heartbeat,
        append_log=args.append_log,
        progress_path=progress_path,
        append_progress=args.append_progress,
        progress_stdout_jsonl=progress_stdout_jsonl,
    )
    if build_exit != 0:
        return build_exit

    if not DEFAULT_BINARY.exists():
        if args.test:
            print(f"[agent-build] expected binary is missing after build: {DEFAULT_BINARY}", file=sys.stderr)
            return 127
        print(
            f"[agent-build] build command succeeded; expected binary is not present yet: {DEFAULT_BINARY}",
            file=human_stream,
        )
        return 0

    if not args.test:
        print(f"[agent-build] built binary: {DEFAULT_BINARY}", file=human_stream)
        return 0

    return run_logged_command(
        test_command(args),
        label="test",
        log_path=args.log,
        heartbeat=args.heartbeat,
        append_log=True,
        progress_path=progress_path,
        append_progress=True,
        progress_stdout_jsonl=progress_stdout_jsonl,
        env=test_environment(args),
    )


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
