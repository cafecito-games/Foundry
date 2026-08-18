#!/usr/bin/env python3
"""Decide which files the static-checks workflow hands to `prek`.

A pull request is checked over its own diff so the gate stays fast. Every other
trigger sweeps the whole tree, because a finding in a file no pull request touches
is otherwise never reported and survives on `develop` indefinitely.
"""

from __future__ import annotations

import argparse
import sys

ALL_FILES = "--all-files"
CHANGED_FILES_EVENT = "pull_request"
# `prek-action` splits `extra-args` with `string-argv`, which understands quoted
# spans but has no escape syntax at all, so a path carrying a quote or a backslash
# has no faithful representation in that argument string.
UNREPRESENTABLE_CHARACTERS = "'\"\\"


def resolve_hook_scope(event_name: str, changed_files: list[str]) -> str:
    """Return the `prek` arguments for `event_name` over `changed_files`.

    Falls back to the full sweep whenever the changed-file list cannot be expressed
    faithfully: an empty diff (`--files` with no operand is an error) or a path the
    action's argument splitter would mangle. Checking a superset is always correct,
    and silently dropping a file from the gate never is.
    """

    if event_name != CHANGED_FILES_EVENT:
        return ALL_FILES

    paths = [path for path in (raw.strip() for raw in changed_files) if path]
    if not paths:
        return ALL_FILES
    if any(character in path for path in paths for character in UNREPRESENTABLE_CHARACTERS):
        return ALL_FILES
    # Single quotes survive `string-argv` intact and keep a path containing spaces
    # as one argument.
    return "--files " + " ".join(f"'./{path}'" for path in paths)


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
