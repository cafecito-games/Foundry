"""Shared helpers for the validator test suite."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "protocol" / "v1"
FIXTURES = PROTOCOL / "fixtures"

if str(ROOT / "src") not in sys.path:
    sys.path.insert(0, str(ROOT / "src"))


class ScratchTestCase(unittest.TestCase):
    """Writes every generated artifact into a per-test temporary directory."""

    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory(prefix="foundry-test-adapter-")
        self.addCleanup(self._directory.cleanup)
        self.scratch = Path(self._directory.name)

    def write(self, name: str, content) -> str:
        path = self.scratch / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            path.write_bytes(content)
        else:
            path.write_bytes(content.encode("utf-8"))
        return str(path)

    def missing(self, name: str) -> str:
        return str(self.scratch / name)
