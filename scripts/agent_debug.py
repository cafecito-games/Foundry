#!/usr/bin/env python3
"""Agent-friendly GDB launcher for the Linux Foundry binary."""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = REPO_ROOT / "bin" / "foundry.linuxbsd.editor.dev.x86_64"
DEFAULT_LOG = Path("/tmp/foundry-build.log")
BUILD_SCRIPT = REPO_ROOT / "scripts" / "agent_build.py"


def resolve_executable(name: str) -> str | None:
    if os.sep in name:
        return name if Path(name).exists() else None
    return shutil.which(name)


def build_first(args: argparse.Namespace) -> int:
    command = [sys.executable, str(BUILD_SCRIPT)]

    if args.dev_build:
        command.append("--dev-build")
    elif args.dev_mode:
        command.append("--dev-mode")
    if args.jobs is not None:
        command.extend(["--jobs", str(args.jobs)])
    command.extend(["--log", str(args.log)])

    print(f"[agent-debug] build-first: {shlex.join(command)}", flush=True)
    return subprocess.call(command, cwd=REPO_ROOT)


def foundry_args(args: argparse.Namespace) -> list[str]:
    extra_args = list(args.extra_foundry_args)
    if extra_args and extra_args[0] == "--":
        extra_args = extra_args[1:]

    command = []
    if not args.no_headless:
        command.append("--headless")
    command.extend(["test", "run"])
    for case_filter in args.test_case or []:
        command.extend(["--case", case_filter])
    command.append("--force-colors")
    command.extend(extra_args)
    return command


def gdb_command(args: argparse.Namespace, gdb_path: str) -> list[str]:
    command = [gdb_path, "-q"]
    if args.batch:
        command.extend(
            [
                "--batch",
                "-ex",
                "set pagination off",
                "-ex",
                "run",
                "-ex",
                "bt",
                "-ex",
                "thread apply all bt",
            ]
        )
    command.extend(["--args", str(args.binary)])
    command.extend(foundry_args(args))
    return command


def debug_environment(args: argparse.Namespace) -> dict[str, str]:
    env = os.environ.copy()
    if args.display is not None:
        env["DISPLAY"] = args.display
    elif "DISPLAY" not in env:
        env["DISPLAY"] = ":1"
    return env


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Launch GDB for the Linux Foundry binary with the usual test-runner arguments."
    )
    parser.add_argument(
        "--case",
        dest="test_case",
        action="append",
        help=(
            "Run a focused doctest case filter under GDB. Repeatable: every occurrence is "
            "retained and forwarded, and the selected tests are the union of all supplied "
            "patterns (matches any)."
        ),
    )
    parser.add_argument("--build-first", action="store_true", help="Build Foundry before launching GDB.")
    parser.add_argument(
        "--dev-build",
        action="store_true",
        help="With --build-first, use faster dev_build=yes instead of the default CI-style dev_mode=yes build.",
    )
    parser.add_argument(
        "--dev-mode",
        action="store_true",
        help="Compatibility no-op for --build-first; dev_mode=yes is now the default.",
    )
    parser.add_argument("--jobs", type=int, help="With --build-first, set the SCons job count.")
    parser.add_argument(
        "--log",
        type=Path,
        default=DEFAULT_LOG,
        help=f"Build log path for --build-first. Default: {DEFAULT_LOG}.",
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=DEFAULT_BINARY,
        help=f"Foundry binary path. Default: {DEFAULT_BINARY}.",
    )
    parser.add_argument("--gdb", default="gdb", help="GDB executable name or path. Default: gdb.")
    parser.add_argument("--batch", action="store_true", help="Run non-interactively and print GDB backtraces.")
    parser.add_argument("--no-headless", action="store_true", help="Do not pass --headless to Foundry.")
    parser.add_argument(
        "--display",
        help="DISPLAY value for Foundry under GDB. Defaults to the existing DISPLAY, or :1 if unset.",
    )
    parser.add_argument(
        "extra_foundry_args",
        nargs=argparse.REMAINDER,
        help="Extra Foundry arguments appended after the default test command. Use `--` before these args.",
    )
    args = parser.parse_args(argv)
    if args.dev_mode and args.dev_build:
        parser.error("--dev-mode and --dev-build cannot be combined")
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be at least 1")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.build_first:
        build_exit = build_first(args)
        if build_exit != 0:
            return build_exit

    gdb_path = resolve_executable(args.gdb)
    if not gdb_path:
        print(
            "[agent-debug] gdb was not found. Install it in the cloud image, for example with `apt install gdb`, "
            "or pass --gdb /path/to/gdb.",
            file=sys.stderr,
        )
        return 127

    if not args.binary.exists():
        print(
            f"[agent-debug] Foundry binary does not exist: {args.binary}. "
            "Run `python3 scripts/agent_build.py` first, or pass --build-first.",
            file=sys.stderr,
        )
        return 127

    command = gdb_command(args, gdb_path)
    print(f"[agent-debug] running: {shlex.join(command)}", flush=True)
    if not args.batch:
        print("[agent-debug] common GDB commands: run, bt, thread apply all bt, quit", flush=True)
    return subprocess.call(command, cwd=REPO_ROOT, env=debug_environment(args))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
