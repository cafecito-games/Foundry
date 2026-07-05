#!/usr/bin/env python3
"""Agent-friendly Foundry build wrapper for Linux cloud environments."""

from __future__ import annotations

import argparse
import importlib.util
import os
import queue
import shlex
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG = Path("/tmp/foundry-build.log")
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


def write_status(log_file, message: str) -> None:
    line = f"{message}\n"
    sys.stdout.write(line)
    sys.stdout.flush()
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
    env: dict[str, str] | None = None,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    mode = "a" if append_log else "w"
    with log_path.open(mode, encoding="utf-8") as log_file:
        if append_log:
            log_file.write("\n")
        write_status(log_file, f"[agent-build] {label}: {shlex.join(command)}")
        write_status(log_file, f"[agent-build] log: {log_path}")

        started = time.monotonic()
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
                sys.stdout.write(item)
                sys.stdout.flush()
                log_file.write(item)
                log_file.flush()
                stripped = item.strip()
                if stripped:
                    last_output = stripped

            now = time.monotonic()
            if now >= next_heartbeat and proc.poll() is None:
                elapsed = format_duration(now - started)
                write_status(
                    log_file,
                    f"[agent-build] {label} still running after {elapsed}; last output: {last_output}",
                )
                next_heartbeat = now + heartbeat

        exit_code = proc.wait()
        reader.join(timeout=1)
        elapsed = format_duration(time.monotonic() - started)
        write_status(log_file, f"[agent-build] {label} exited with {exit_code} after {elapsed}")
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
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="Parallel SCons jobs. Default: CPU count.")
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG, help=f"Build log path. Default: {DEFAULT_LOG}.")
    parser.add_argument("--append-log", action="store_true", help="Append to the log instead of replacing it.")
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


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if scons_prefix() is None:
        print(
            "[agent-build] SCons is not available. Install SCons for python3 or make the `scons` executable "
            "available in PATH.",
            file=sys.stderr,
        )
        return 127

    build_exit = run_logged_command(
        build_command(args),
        label="build",
        log_path=args.log,
        heartbeat=args.heartbeat,
        append_log=args.append_log,
    )
    if build_exit != 0:
        return build_exit

    if not DEFAULT_BINARY.exists():
        if args.test:
            print(f"[agent-build] expected binary is missing after build: {DEFAULT_BINARY}", file=sys.stderr)
            return 127
        print(f"[agent-build] build command succeeded; expected binary is not present yet: {DEFAULT_BINARY}")
        return 0

    if not args.test:
        print(f"[agent-build] built binary: {DEFAULT_BINARY}")
        return 0

    return run_logged_command(
        test_command(args),
        label="test",
        log_path=args.log,
        heartbeat=args.heartbeat,
        append_log=True,
        env=test_environment(args),
    )


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
