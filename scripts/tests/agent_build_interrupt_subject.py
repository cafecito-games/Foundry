#!/usr/bin/env python3
"""Run agent_build.py against a stand-in SCons in its own process.

Signal handling is a property of a whole process, so the wrapper's response to a supervisor's stop
signal can only be observed from outside it. This is that outside process' payload.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from typing import Any
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "agent_build.py"
_spec = importlib.util.spec_from_file_location("agent_build", MODULE_PATH)
assert _spec is not None and _spec.loader is not None
agent_build: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = agent_build
_spec.loader.exec_module(agent_build)


def main(argv: list[str]) -> int:
    scons, log_path, progress_path, binary_path = (Path(argument) for argument in argv[:4])
    target = agent_build.BuildTarget("macos", binary_path, None)
    with mock.patch.object(agent_build, "scons_prefix", return_value=[sys.executable, str(scons)]):
        with mock.patch.object(agent_build, "resolve_build_target", return_value=target):
            exit_code = agent_build.run(["--jobs", "1", "--log", str(log_path), "--progress-file", str(progress_path)])
    return int(exit_code)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
