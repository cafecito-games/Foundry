#!/usr/bin/env python3
"""Run a command repeatedly and append per-run benchmark records as JSONL."""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import resource
import sys
import time
from pathlib import Path
from typing import Sequence


@dataclasses.dataclass(frozen=True)
class BenchmarkResult:
    version: int
    label: str
    repetition: int
    command: Sequence[str]
    exit_code: int
    wall_ms: float
    user_ms: float
    system_ms: float
    max_rss_kib: int
    input_blocks: int
    output_blocks: int
    error: str | None = None


def _exit_code(status: int) -> int:
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    if os.WIFSIGNALED(status):
        return -os.WTERMSIG(status)
    return status


def _max_rss_kib(usage: resource.struct_rusage) -> int:
    max_rss = int(usage.ru_maxrss)
    return max_rss // 1024 if sys.platform == "darwin" else max_rss


def run_once(command: Sequence[str], *, label: str, repetition: int) -> BenchmarkResult:
    """Run one child process and return its wall-clock and resource metrics."""
    if not command:
        raise ValueError("command must not be empty")

    started = time.monotonic_ns()
    try:
        pid = os.posix_spawnp(command[0], list(command), os.environ)
    except OSError as exc:
        wall_ms = (time.monotonic_ns() - started) / 1_000_000
        reason = "not found" if isinstance(exc, FileNotFoundError) else (exc.strerror or type(exc).__name__)
        return BenchmarkResult(
            version=1,
            label=label,
            repetition=repetition,
            command=tuple(command),
            exit_code=127,
            wall_ms=wall_ms,
            user_ms=0.0,
            system_ms=0.0,
            max_rss_kib=0,
            input_blocks=0,
            output_blocks=0,
            error=f"could not execute {command[0]!r}: {reason}",
        )
    _, status, usage = os.wait4(pid, 0)
    wall_ms = (time.monotonic_ns() - started) / 1_000_000

    return BenchmarkResult(
        version=1,
        label=label,
        repetition=repetition,
        command=tuple(command),
        exit_code=_exit_code(status),
        wall_ms=wall_ms,
        user_ms=usage.ru_utime * 1_000,
        system_ms=usage.ru_stime * 1_000,
        max_rss_kib=_max_rss_kib(usage),
        input_blocks=int(usage.ru_inblock),
        output_blocks=int(usage.ru_oublock),
    )


def _result_json(result: BenchmarkResult) -> str:
    return json.dumps(dataclasses.asdict(result), sort_keys=True)


def write_result(output: Path, result: BenchmarkResult) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("a", encoding="utf-8") as result_file:
        result_file.write(_result_json(result) + "\n")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True, help="Name recorded with each result.")
    parser.add_argument("--output", required=True, type=Path, help="JSONL output path.")
    parser.add_argument("--repeat", type=int, default=3, help="Number of repetitions (default: 3).")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command to benchmark.")
    args = parser.parse_args(argv)
    if args.repeat < 1:
        parser.error("--repeat must be at least 1")
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    if not args.command:
        parser.error("a command is required")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    for repetition in range(1, args.repeat + 1):
        result = run_once(args.command, label=args.label, repetition=repetition)
        write_result(args.output, result)
        print(_result_json(result), flush=True)
        if result.error is not None:
            print(result.error, file=sys.stderr, flush=True)
        if result.exit_code != 0:
            return 128 - result.exit_code if result.exit_code < 0 else result.exit_code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
