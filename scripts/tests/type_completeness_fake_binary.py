#!/usr/bin/env python3
"""Stand-in for the `foundry` binary used by the type-completeness presubmit tests.

It answers exactly the two commands the wrapper invokes, `test completeness select` and
`test completeness run`, from documents captured from the real binary. It never fabricates a document:
every payload it serves comes from a file the test points it at, so a test can drive the wrapper
through a fail-closed path without owning a compiled engine.

Configuration is by environment variable so the wrapper's command line stays exactly what it sends to
the real binary:

* `FOUNDRY_FAKE_SELECTION`   file whose contents are written to stdout for `select`.
* `FOUNDRY_FAKE_SELECT_EXIT` exit code for `select` (default 0).
* `FOUNDRY_FAKE_REPORT_DIR`  directory holding `<family>.json` reports copied to `--report`.
* `FOUNDRY_FAKE_RUN_EXIT`    exit code for `run` (default 0).
* `FOUNDRY_FAKE_RUN_SLEEP`   seconds to sleep before `run` answers, for deadline coverage.
* `FOUNDRY_FAKE_STDOUT_NOISE` text printed before the selection document.
"""

from __future__ import annotations

import os
import shutil
import sys
import time
from pathlib import Path
from typing import Optional


def _option(argv: list[str], name: str) -> Optional[str]:
    for index, argument in enumerate(argv):
        if argument == name and index + 1 < len(argv):
            return argv[index + 1]
    return None


def _select(argv: list[str]) -> int:
    noise = os.environ.get("FOUNDRY_FAKE_STDOUT_NOISE", "")
    if noise:
        sys.stdout.write(noise)
    selection = os.environ.get("FOUNDRY_FAKE_SELECTION", "")
    if selection:
        sys.stdout.write(Path(selection).read_text(encoding="utf-8"))
    return int(os.environ.get("FOUNDRY_FAKE_SELECT_EXIT", "0"))


def _run(argv: list[str]) -> int:
    sleep_seconds = float(os.environ.get("FOUNDRY_FAKE_RUN_SLEEP", "0"))
    if sleep_seconds:
        time.sleep(sleep_seconds)
    family = _option(argv, "--family")
    report_path = _option(argv, "--report")
    report_dir = os.environ.get("FOUNDRY_FAKE_REPORT_DIR", "")
    if family and report_path and report_dir:
        source = Path(report_dir) / f"{family}.json"
        if source.exists():
            destination = Path(report_path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
    return int(os.environ.get("FOUNDRY_FAKE_RUN_EXIT", "0"))


def main(argv: list[str]) -> int:
    if "select" in argv:
        return _select(argv)
    if "run" in argv:
        return _run(argv)
    sys.stderr.write(f"unsupported fake invocation: {argv}\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
