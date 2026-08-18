#!/usr/bin/env python3
"""Decide which files the static-checks workflow hands to `prek`.

A pull request is checked over its own diff so the gate stays fast. Every other
trigger sweeps the whole tree, because a finding in a file no pull request touches
is otherwise never reported and survives on `develop` indefinitely.
"""

from __future__ import annotations

import argparse
import shlex
import sys

ALL_FILES = "--all-files"
CHANGED_FILES_EVENT = "pull_request"


def resolve_hook_scope(event_name: str, changed_files: list[str]) -> str:
    """Return the `prek` arguments for `event_name` over `changed_files`.

    An empty changed-file set falls back to the full sweep: `--files` with no
    operand is an error, and a pull request whose diff lists nothing is degenerate
    enough that checking more than strictly necessary is the safe answer.
    """

    if event_name != CHANGED_FILES_EVENT:
        return ALL_FILES

    paths = [path for path in (raw.strip() for raw in changed_files) if path]
    if not paths:
        return ALL_FILES
    return "--files " + " ".join(shlex.quote(f"./{path}") for path in paths)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Resolve the file scope for a static-checks hook run.")
    parser.add_argument("--event-name", required=True, help="The GitHub Actions event that triggered the run.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    print(resolve_hook_scope(args.event_name, sys.stdin.read().splitlines()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
